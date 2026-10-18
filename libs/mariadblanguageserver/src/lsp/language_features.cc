/*
 * Copyright (c) 2026, MariaDB plc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "lsp/language_features.h"

#include <algorithm>
#include <optional>

#include "lsp/features.h"
#include "mdbls/keywords.h"
#include "mdbls/navigation.h"
#include "mdbls/sql_names.h"
#include "text_util.h"

namespace mdbls::lsp {

namespace {

// LSP CompletionItemKind values.
int completion_item_kind(Completion_kind k) {
  switch (k) {
    case Completion_kind::keyword: return 14;
    case Completion_kind::schema: return 9;    // Module
    case Completion_kind::table: return 7;     // Class
    case Completion_kind::view: return 8;      // Interface
    case Completion_kind::column: return 5;    // Field
    case Completion_kind::alias: return 18;    // Reference
    case Completion_kind::function: return 3;  // Function
    case Completion_kind::routine: return 2;   // Method
    case Completion_kind::variable: return 6;  // Variable
  }
  return 1;
}

void markdown(Json_writer &w, const std::string &value) {
  w.StartObject();
  w.Key("kind");
  w.String("markdown");
  w.Key("value");
  w.String(value.c_str(), static_cast<rapidjson::SizeType>(value.size()));
  w.EndObject();
}

void str(Json_writer &w, const char *key, const std::string &v) {
  w.Key(key);
  w.String(v.c_str(), static_cast<rapidjson::SizeType>(v.size()));
}

void location(Json_writer &w, const std::string &uri, std::string_view text, const Line_map &lines,
              Range r) {
  w.StartObject();
  str(w, "uri", uri);
  w.Key("range");
  write_range(w, text, lines, r.begin, r.end);
  w.EndObject();
}

// The statement at `offset`, parsed with its tree, its names resolved.
struct At {
  size_t statement = 0;
  uint32_t offset = 0;
  Catalog_view view;
  std::unique_ptr<Statement_names> names;
  Resolution resolution;
};

std::optional<At> resolve_at(const Snapshot &s, Position pos, const Catalog_sources &c,
                             Parser_cache &cache) {
  At at;
  at.offset = s.lines->offset(*s.text, pos);
  const auto i = statement_at(*s.index, at.offset);
  if (!i) return std::nullopt;
  const Statement &st = s.index->statements()[*i];
  if (st.kind != Statement_kind::sql) return std::nullopt;
  mdbp_parser *p = cache.get(s.index->modes()[st.mode].mode, s.version_id, true);
  if (p == nullptr) return std::nullopt;
  at.statement = *i;
  at.view = catalog_view(s, c, at.offset);
  at.names = std::make_unique<Statement_names>(p, s.index->text_of(*s.text, st), st.begin);
  if (!at.names->parsed()) return std::nullopt;
  at.resolution = Name_resolver(at.view, *at.names).resolve_at(at.offset);
  return at;
}

// Where the catalog says the resolved object is defined.
const Origin *origin_of(const Resolution &r) {
  switch (r.id.kind) {
    case Target_kind::table: return r.table != nullptr ? &r.table->origin : nullptr;
    case Target_kind::view: return r.view != nullptr ? &r.view->origin : nullptr;
    case Target_kind::column: return r.column != nullptr ? &r.column->origin : nullptr;
    case Target_kind::routine: return r.routine != nullptr ? &r.routine->origin : nullptr;
    case Target_kind::schema:
      return r.schema != nullptr && r.schema->declared ? &r.schema->origin : nullptr;
    default: return nullptr;
  }
}

// Whether a rename of the resolved name may be done in this document.
bool renamable(const Resolution &r, std::string *error) {
  if (!r.found()) {
    *error = "there is no name here to rename";
    return false;
  }
  if (r.statement_local()) return true;
  if (r.id.kind == Target_kind::builtin_function) {
    *error = "a built-in function cannot be renamed";
    return false;
  }
  const bool object = r.id.kind == Target_kind::table || r.id.kind == Target_kind::view ||
                      r.id.kind == Target_kind::column || r.id.kind == Target_kind::routine;
  if (!object || r.layer == nullptr || r.layer->kind != Layer_kind::document) {
    *error = "only names this document defines can be renamed";
    return false;
  }
  return true;
}

std::vector<Reference> references_of(const Snapshot &s, const At &at, const Catalog_sources &c,
                                     Parser_pool &pool, Parser_cache &cache, int priority) {
  if (at.resolution.statement_local()) {
    return statement_references(*s.text, *s.index, at.statement, at.resolution.id, at.view, cache,
                                s.version_id);
  }
  Reference_options o;
  o.version_id = s.version_id;
  o.priority = priority;
  o.initial_schema = c.schema_hint;
  if (o.initial_schema.empty() && c.live) o.initial_schema = c.live->server.current_schema;
  return find_references(*s.text, *s.index, at.resolution.id, at.statement, at.view, pool, o);
}

int symbol_kind_of_routine(const Routine &r) { return r.kind == "function" ? 12 : 6; }

}  // namespace

// How long a request waits for the details of stubs it met.
constexpr std::chrono::milliseconds k_details_wait{3000};

// When `view` met stubs of the live catalog (Table::stub), has their
// details read and returns the sources with the snapshot that has them;
// nullopt when there were none, there is no one to read them, or they did
// not come in time - the request then answers with what it has.
std::optional<Catalog_sources> with_details(const Catalog_sources &c, const Catalog_view &view) {
  if (view.stubs_met().empty() || !c.live_source) return std::nullopt;
  if (!c.live_source->details(view.stubs_met(), k_details_wait)) return std::nullopt;
  Catalog_sources next = c;
  next.live = c.live_source->snapshot();
  return next;
}

Catalog_view catalog_view(const Snapshot &s, const Catalog_sources &c, uint32_t offset) {
  Catalog_view v;
  if (c.document) {
    v.add({std::shared_ptr<const Catalog>(c.document, &c.document->catalog), Layer_kind::document, {}});
  }
  if (c.workspace) {
    v.add({std::shared_ptr<const Catalog>(c.workspace, &c.workspace->outline.catalog),
           Layer_kind::workspace, {}});
  }
  if (c.live && c.live->catalog) {
    v.add({c.live->catalog, Layer_kind::live, c.live->server.current_schema});
  }
  const auto at = statement_at(*s.index, offset);
  std::string schema = schema_in_use(*s.text, *s.index, at ? *at : s.index->find(offset));
  if (schema.empty()) schema = c.schema_hint;
  v.set_current_schema(schema);
  return v;
}

void completion(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                Parser_cache &cache, const Completion_options &options) {
  const uint32_t offset = s.lines->offset(*s.text, pos);
  const Catalog_view view = catalog_view(s, c, offset);
  auto list = complete(*s.text, *s.index, offset, view, cache, options);
  if (!view.stubs_met().empty()) {
    if (const auto read = with_details(c, view)) {
      list = complete(*s.text, *s.index, offset, catalog_view(s, *read, offset), cache, options);
    } else {
      // Asked again as the user types: the details may be there by then.
      list.incomplete = true;
    }
  }
  w.StartObject();
  w.Key("isIncomplete");
  w.Bool(list.incomplete);
  w.Key("items");
  w.StartArray();
  for (const auto &it : list.items) {
    w.StartObject();
    str(w, "label", it.label);
    w.Key("kind");
    w.Int(completion_item_kind(it.kind));
    if (!it.detail.empty()) str(w, "detail", it.detail);
    if (!it.documentation.empty()) {
      w.Key("documentation");
      markdown(w, it.documentation);
    }
    str(w, "sortText", it.sort_text);
    if (list.quoted) str(w, "filterText", "`" + it.label);
    w.Key("textEdit");
    w.StartObject();
    w.Key("range");
    write_range(w, s, list.replace_begin, list.replace_end);
    str(w, "newText", it.insert_text);
    w.EndObject();
    if (it.snippet) {
      w.Key("insertTextFormat");
      w.Int(2);
    }
    // Lines the server laid out: VS Code would indent them again by the
    // current line's indentation.
    if (it.insert_text.find('\n') != std::string::npos) {
      w.Key("insertTextMode");
      w.Int(1);  // asIs
    }
    if (!it.additional_edits.empty()) {
      w.Key("additionalTextEdits");
      w.StartArray();
      for (const auto &e : it.additional_edits) {
        w.StartObject();
        w.Key("range");
        write_range(w, s, e.begin, e.end);
        str(w, "newText", e.text);
        w.EndObject();
      }
      w.EndArray();
    }
    if (it.retrigger) {
      w.Key("command");
      w.StartObject();
      str(w, "title", "");
      str(w, "command", "editor.action.triggerSuggest");
      w.EndObject();
    }
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
}

void hover(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
           Parser_cache &cache) {
  auto at = resolve_at(s, pos, c, cache);
  if (at) {
    if (const auto read = with_details(c, at->view)) at = resolve_at(s, pos, *read, cache);
  }
  const std::string text = at ? hover_markdown(at->resolution) : std::string();
  if (text.empty()) {
    w.Null();
    return;
  }
  w.StartObject();
  w.Key("contents");
  markdown(w, text);
  w.Key("range");
  write_range(w, s, at->resolution.ref.range.begin, at->resolution.ref.range.end);
  w.EndObject();
}

void definition(Json_writer &w, const Snapshot &s, const std::string &uri, Position pos,
                const Catalog_sources &c, Parser_cache &cache) {
  const auto at = resolve_at(s, pos, c, cache);
  if (!at || !at->resolution.found()) {
    w.Null();
    return;
  }
  const auto &r = at->resolution;
  // Where to point: the statement's own definition, else the catalog's, in
  // this document or a workspace file.
  const std::string *where_uri = nullptr;
  std::string_view where_text;
  const Line_map *where_lines = nullptr;
  Range where;
  const Origin *o = origin_of(r);
  if (r.local_definition) {
    where = *r.local_definition;
    where_uri = &uri;
  } else if (o != nullptr && r.layer != nullptr && o->name.end > o->name.begin) {
    where = o->name;
    if (r.layer->kind == Layer_kind::document) {
      where_uri = &uri;
    } else if (r.layer->kind == Layer_kind::workspace && c.workspace &&
               o->source < c.workspace->files.size()) {
      const auto &f = c.workspace->files[o->source];
      where_uri = &f.uri;
      where_text = f.text;
      where_lines = &f.lines;
    }
  }
  if (where_uri == &uri) {
    where_text = *s.text;
    where_lines = s.lines.get();
  }
  if (where_uri == nullptr || where.end > where_text.size()) {
    w.Null();
    return;
  }
  w.StartArray();
  location(w, *where_uri, where_text, *where_lines, where);
  w.EndArray();
}

void references(Json_writer &w, const Snapshot &s, const std::string &uri, Position pos,
                bool include_declaration, const Catalog_sources &c, Parser_pool &pool,
                Parser_cache &cache, int priority) {
  const auto at = resolve_at(s, pos, c, cache);
  w.StartArray();
  if (at && at->resolution.found()) {
    for (const auto &ref : references_of(s, *at, c, pool, cache, priority)) {
      if (ref.defining && !include_declaration) continue;
      location(w, uri, *s.text, *s.lines, ref.range);
    }
  }
  w.EndArray();
}

void document_highlight(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                        Parser_cache &cache) {
  const auto at = resolve_at(s, pos, c, cache);
  w.StartArray();
  if (at && at->resolution.found() && at->resolution.id.kind != Target_kind::builtin_function) {
    for (const auto &ref : statement_references(*s.text, *s.index, at->statement,
                                                at->resolution.id, at->view, cache, s.version_id)) {
      w.StartObject();
      w.Key("range");
      write_range(w, s, ref.range.begin, ref.range.end);
      w.Key("kind");
      w.Int(ref.defining ? 3 : 2);  // Write : Read
      w.EndObject();
    }
  }
  w.EndArray();
}

bool prepare_rename(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                    Parser_cache &cache, std::string *error) {
  const auto at = resolve_at(s, pos, c, cache);
  if (!at) {
    *error = "there is no name here to rename";
    return false;
  }
  if (!renamable(at->resolution, error)) return false;
  const auto &ref = at->resolution.ref;
  w.StartObject();
  w.Key("range");
  write_range(w, s, ref.range.begin, ref.range.end);
  str(w, "placeholder", ref.name);
  w.EndObject();
  return true;
}

bool rename(Json_writer &w, const Snapshot &s, const std::string &uri, Position pos,
            const std::string &new_name, const Catalog_sources &c, Parser_pool &pool,
            Parser_cache &cache, int priority, std::string *error) {
  const std::string name = unquote_name(new_name);
  if (name.empty()) {
    *error = "the new name is empty";
    return false;
  }
  const auto at = resolve_at(s, pos, c, cache);
  if (!at) {
    *error = "there is no name here to rename";
    return false;
  }
  if (!renamable(at->resolution, error)) return false;
  const std::string text = quote_identifier(name);
  w.StartObject();
  w.Key("changes");
  w.StartObject();
  w.Key(uri.c_str(), static_cast<rapidjson::SizeType>(uri.size()));
  w.StartArray();
  for (const auto &ref : references_of(s, *at, c, pool, cache, priority)) {
    w.StartObject();
    w.Key("range");
    write_range(w, s, ref.range.begin, ref.range.end);
    str(w, "newText", text);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  w.EndObject();
  return true;
}

void signature_help(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                    Parser_cache &cache) {
  const uint32_t offset = s.lines->offset(*s.text, pos);
  const Catalog_view view = catalog_view(s, c, offset);
  auto help = mdbls::signature_help(*s.text, *s.index, offset, view, cache, s.version_id);
  if (const auto read = with_details(c, view)) {
    help = mdbls::signature_help(*s.text, *s.index, offset, catalog_view(s, *read, offset), cache,
                                 s.version_id);
  }
  if (!help || help->signatures.empty()) {
    w.Null();
    return;
  }
  w.StartObject();
  w.Key("signatures");
  w.StartArray();
  for (const auto &sig : help->signatures) {
    w.StartObject();
    str(w, "label", sig.label);
    if (!sig.documentation.empty()) {
      w.Key("documentation");
      markdown(w, sig.documentation);
    }
    w.Key("parameters");
    w.StartArray();
    for (const auto &[b, e] : sig.params) {
      w.StartObject();
      w.Key("label");
      w.StartArray();
      // Offsets in UTF-16 units, as LSP counts them.
      w.Uint(utf16_length(std::string_view(sig.label).substr(0, b)));
      w.Uint(utf16_length(std::string_view(sig.label).substr(0, e)));
      w.EndArray();
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
  }
  w.EndArray();
  w.Key("activeSignature");
  w.Uint64(help->active_signature);
  w.Key("activeParameter");
  w.Uint64(help->active_parameter);
  w.EndObject();
}

void workspace_symbols(Json_writer &w, std::string_view query,
                       const std::vector<Symbol_document> &documents,
                       const Workspace_catalog *workspace, size_t max_symbols) {
  struct File {
    const std::string *uri = nullptr;  // nullptr: list nothing from it
    std::string_view text;
    const Line_map *lines = nullptr;
  };
  size_t n = 0;
  // One catalog's objects, `file(source)` giving where an origin points.
  auto list = [&](const Catalog &catalog, auto &&file) {
    auto symbol = [&](const std::string &name, int kind, const std::string &container,
                      const Origin &o) {
      if (n >= max_symbols || !icontains(name, query)) return;
      const File f = file(o.source);
      if (f.uri == nullptr || o.name.end <= o.name.begin || o.name.end > f.text.size()) return;
      ++n;
      w.StartObject();
      str(w, "name", name);
      w.Key("kind");
      w.Int(kind);
      if (!container.empty()) str(w, "containerName", container);
      w.Key("location");
      location(w, *f.uri, f.text, *f.lines, o.name);
      w.EndObject();
    };
    for (const auto &sc : catalog.schemas()) {
      if (n >= max_symbols) return;
      if (sc.declared) symbol(sc.name, 3, "", sc.origin);
      for (const auto &t : sc.tables) {
        symbol(t.name, 5, sc.name, t.origin);
        if (query.empty()) continue;  // columns only when asked for
        const std::string container = (sc.name.empty() ? "" : sc.name + ".") + t.name;
        for (const auto &col : t.columns) symbol(col.name, 8, container, col.origin);
      }
      for (const auto &v : sc.views) symbol(v.name, 11, sc.name, v.origin);
      for (const auto &r : sc.routines) symbol(r.name, symbol_kind_of_routine(r), sc.name, r.origin);
      for (const auto &t : sc.triggers) symbol(t.name, 24, sc.name, t.origin);
      for (const auto &e : sc.events) symbol(e.name, 24, sc.name, e.origin);
      for (const auto &q : sc.sequences) symbol(q.name, 14, sc.name, q.origin);
    }
  };
  w.StartArray();
  for (const auto &d : documents) {
    if (!d.outline || !d.snapshot) continue;
    const File f{&d.uri, *d.snapshot->text, d.snapshot->lines.get()};
    list(d.outline->catalog, [&](uint32_t) { return f; });
  }
  if (workspace != nullptr) {
    // A workspace file that is open is listed from its document already;
    // matched by path, since a client may spell the URI differently.
    std::vector<const Workspace_file *> open;
    for (const auto &d : documents) {
      if (const Workspace_file *wf = workspace->file(d.uri)) open.push_back(wf);
    }
    std::vector<File> files;
    for (const auto &wf : workspace->files) {
      const bool is_open = std::find(open.begin(), open.end(), &wf) != open.end();
      files.push_back(is_open ? File{} : File{&wf.uri, wf.text, &wf.lines});
    }
    list(workspace->outline.catalog,
         [&](uint32_t source) { return source < files.size() ? files[source] : File{}; });
  }
  w.EndArray();
}

void formatting(Json_writer &w, const Snapshot &s, bool range, Position from, Position to,
                const Format_options &o, Parser_cache &cache) {
  const std::string &text = *s.text;
  const uint32_t begin = range ? s.lines->offset(text, from) : 0;
  const uint32_t end = range ? s.lines->offset(text, to) : static_cast<uint32_t>(text.size());
  w.StartArray();
  for (const auto &e : format_sql(text, *s.index, begin, end, cache, o)) {
    w.StartObject();
    w.Key("range");
    write_range(w, s, e.begin, e.end);
    w.Key("newText");
    w.String(e.text.data(), static_cast<rapidjson::SizeType>(e.text.size()));
    w.EndObject();
  }
  w.EndArray();
}

}  // namespace mdbls::lsp
