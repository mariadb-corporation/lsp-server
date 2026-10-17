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
  const auto list = complete(*s.text, *s.index, offset, view, cache, options);
  const bool quoted = list.replace_begin < s.text->size() && (*s.text)[list.replace_begin] == '`';
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
    if (quoted) str(w, "filterText", "`" + it.label);
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
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
}

void hover(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
           Parser_cache &cache) {
  const auto at = resolve_at(s, pos, c, cache);
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
  if (r.local_definition) {
    w.StartArray();
    location(w, uri, *s.text, *s.lines, *r.local_definition);
    w.EndArray();
    return;
  }
  const Origin *o = origin_of(r);
  if (o == nullptr || r.layer == nullptr || o->name.end <= o->name.begin) {
    w.Null();
    return;
  }
  if (r.layer->kind == Layer_kind::document && o->name.end <= s.text->size()) {
    w.StartArray();
    location(w, uri, *s.text, *s.lines, o->name);
    w.EndArray();
    return;
  }
  if (r.layer->kind == Layer_kind::workspace && c.workspace &&
      o->source < c.workspace->files.size()) {
    const auto &f = c.workspace->files[o->source];
    if (o->name.end <= f.text.size()) {
      w.StartArray();
      location(w, f.uri, f.text, f.lines, o->name);
      w.EndArray();
      return;
    }
  }
  w.Null();
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
  const auto help = mdbls::signature_help(*s.text, *s.index, offset, view, cache, s.version_id);
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
  size_t n = 0;
  auto matches = [&](std::string_view name) {
    if (query.empty()) return true;
    const std::string l = lower(name), q = lower(query);
    return l.find(q) != std::string::npos;
  };
  auto symbol = [&](const std::string &name, int kind, const std::string &container,
                    const std::string &uri, std::string_view text, const Line_map &lines,
                    const Origin &o) {
    if (n >= max_symbols || !matches(name)) return;
    if (o.name.end <= o.name.begin || o.name.end > text.size()) return;
    ++n;
    w.StartObject();
    str(w, "name", name);
    w.Key("kind");
    w.Int(kind);
    if (!container.empty()) str(w, "containerName", container);
    w.Key("location");
    location(w, uri, text, lines, o.name);
    w.EndObject();
  };
  // Every object of `catalog` whose origin is in source `source`; `file`
  // gives a source's uri, text and lines.
  auto catalog_symbols = [&](const Catalog &catalog, auto &&file) {
    for (const auto &sc : catalog.schemas()) {
      if (sc.declared) {
        const auto [uri, text, lines] = file(sc.origin.source);
        if (uri != nullptr) symbol(sc.name, 3, "", *uri, text, *lines, sc.origin);
      }
      const std::string container = sc.name;
      for (const auto &t : sc.tables) {
        const auto [uri, text, lines] = file(t.origin.source);
        if (uri == nullptr) continue;
        symbol(t.name, 5, container, *uri, text, *lines, t.origin);
        for (const auto &col : t.columns) {
          const auto [curi, ctext, clines] = file(col.origin.source);
          if (curi != nullptr && !query.empty()) {
            symbol(col.name, 8, (container.empty() ? "" : container + ".") + t.name, *curi, ctext,
                   *clines, col.origin);
          }
        }
      }
      for (const auto &v : sc.views) {
        const auto [uri, text, lines] = file(v.origin.source);
        if (uri != nullptr) symbol(v.name, 11, container, *uri, text, *lines, v.origin);
      }
      for (const auto &r : sc.routines) {
        const auto [uri, text, lines] = file(r.origin.source);
        if (uri != nullptr) symbol(r.name, symbol_kind_of_routine(r), container, *uri, text, *lines, r.origin);
      }
      for (const auto &t : sc.triggers) {
        const auto [uri, text, lines] = file(t.origin.source);
        if (uri != nullptr) symbol(t.name, 24, container, *uri, text, *lines, t.origin);
      }
      for (const auto &e : sc.events) {
        const auto [uri, text, lines] = file(e.origin.source);
        if (uri != nullptr) symbol(e.name, 24, container, *uri, text, *lines, e.origin);
      }
      for (const auto &q : sc.sequences) {
        const auto [uri, text, lines] = file(q.origin.source);
        if (uri != nullptr) symbol(q.name, 14, container, *uri, text, *lines, q.origin);
      }
    }
  };
  using File = std::tuple<const std::string *, std::string_view, const Line_map *>;
  w.StartArray();
  for (const auto &d : documents) {
    if (!d.outline || !d.snapshot) continue;
    catalog_symbols(d.outline->catalog, [&](uint32_t) -> File {
      return {&d.uri, *d.snapshot->text, d.snapshot->lines.get()};
    });
  }
  if (workspace != nullptr) {
    catalog_symbols(workspace->outline.catalog, [&](uint32_t source) -> File {
      if (source >= workspace->files.size()) return {nullptr, {}, nullptr};
      const auto &f = workspace->files[source];
      // An open document's own symbols are listed already.  Compared as
      // paths: a client may spell the URI differently (case, encoding).
      for (const auto &d : documents) {
        if (d.uri == f.uri || file_uri_to_path(d.uri) == f.path) return {nullptr, {}, nullptr};
      }
      return {&f.uri, f.text, &f.lines};
    });
  }
  w.EndArray();
}

}  // namespace mdbls::lsp
