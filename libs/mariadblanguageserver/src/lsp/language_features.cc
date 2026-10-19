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
#include <map>
#include <optional>
#include <set>
#include <tuple>

#include "lsp/features.h"
#include "mdbls/keywords.h"
#include "mdbls/navigation.h"
#include "mdbls/source_include.h"
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

// Whether a rename of the resolved name may be done from this document.
// An object a file it includes defines is renamed in that file too.
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

// A reference in the document (source 0) or in a file it includes (source
// k: c.document->includes[k - 1]).
struct Located_reference {
  uint32_t source = 0;
  Range range;
  bool defining = false;
};

std::vector<Located_reference> references_of(const Snapshot &s, const At &at,
                                             const Catalog_sources &c, Parser_pool &pool,
                                             Parser_cache &cache, int priority,
                                             bool expanded = false) {
  std::vector<Located_reference> out;
  if (at.resolution.statement_local()) {
    for (const auto &r : statement_references(*s.text, *s.index, at.statement, at.resolution.id,
                                              at.view, cache, s.version_id)) {
      if (expanded) {
        const auto m = c.document->map(r.range);
        out.push_back({m.source, m.range, r.defining});
      } else {
        out.push_back({0, r.range, r.defining});
      }
    }
    return out;
  }
  Reference_options o;
  o.version_id = s.version_id;
  o.priority = priority;
  o.initial_schema = c.schema_hint;
  if (o.initial_schema.empty() && c.live) o.initial_schema = c.live->server.current_schema;
  const Outline *doc = c.document.get();
  if (doc == nullptr || !doc->expanded) {
    for (const auto &r : find_references(*s.text, *s.index, at.resolution.id, at.statement,
                                         at.view, pool, o)) {
      out.push_back({0, r.range, r.defining});
    }
    return out;
  }
  // A script with SOURCE directives: the text it was folded from, with the
  // files it includes, each reference mapped back to its file.  A file
  // included twice is listed once.
  const auto &e = *doc->expanded;
  std::set<std::tuple<std::string, uint32_t, uint32_t>> seen;
  for (const auto &r : find_references(e.text, *e.index, at.resolution.id, 0, at.view, pool, o)) {
    const auto m = doc->map(r.range);
    const std::string path = m.source == 0 ? std::string() : doc->includes[m.source - 1].path;
    if (seen.insert({path, m.range.begin, m.range.end}).second) {
      out.push_back({m.source, m.range, r.defining});
    }
  }
  return out;
}

// Where references of a source are: the URI, and the text and line map
// their ranges are in; no line map for a source that is not there.
struct Reference_file {
  std::string uri;
  std::string_view text;
  const Line_map *lines = nullptr;
};

// Where a references or rename request runs.  For a document: its own
// text, or its expanded text when it has SOURCE directives.  For a file an
// open script includes (`script`): the script's expanded text, the request
// moved there, so the name is resolved in the script's context and found
// in the script and every file it includes.
struct Ref_scope {
  std::shared_ptr<const Snapshot> searched;  // the text searched
  Catalog_sources catalogs;
  bool expanded = false;  // `searched` is catalogs.document->expanded
  At at;
  // Source 0's file, and each include's URI.
  std::string uri;
  std::shared_ptr<const Snapshot> snapshot;
  std::vector<std::string> include_uris;

  std::vector<Located_reference> references(Parser_pool &pool, Parser_cache &cache,
                                            int priority) const {
    return references_of(*searched, at, catalogs, pool, cache, priority, expanded);
  }
  // Where a range of the searched text is.
  Source_expansion::Located_range locate(Range r) const {
    return expanded ? catalogs.document->map(r) : Source_expansion::Located_range{0, r};
  }
  Reference_file file(uint32_t source) const {
    if (source == 0) return {uri, *snapshot->text, snapshot->lines.get()};
    const Outline *doc = catalogs.document.get();
    if (doc == nullptr || source > doc->includes.size()) return {};
    const auto &inc = doc->includes[source - 1];
    return {include_uris[source - 1], inc.text, &inc.lines};
  }
};

std::vector<std::string> include_uris(const Outline *doc, const Including_script *script) {
  std::vector<std::string> out;
  if (doc == nullptr) return out;
  for (const auto &inc : doc->includes) {
    // The requesting document under the URI its client gave.
    const bool self = script != nullptr && canonical_path(inc.path) == script->included_path;
    out.push_back(self ? script->included_uri : path_to_file_uri(inc.path));
  }
  return out;
}

std::optional<Ref_scope> ref_scope(const Snapshot &s, const std::string &uri, Position pos,
                                   const Catalog_sources &c, Parser_cache &cache,
                                   const Including_script *script) {
  if (script != nullptr && script->catalogs.document && script->catalogs.document->expanded) {
    const auto &doc = script->catalogs.document;
    const auto &e = *doc->expanded;
    const uint32_t offset = s.lines->offset(*s.text, pos);
    // The first segment of this file, as its text is now, holding `offset`.
    for (size_t k = 0; k < e.segments.size(); ++k) {
      const auto &seg = e.segments[k];
      if (seg.source == 0) continue;
      const auto &inc = doc->includes[seg.source - 1];
      const uint32_t length =
          (k + 1 < e.segments.size() ? e.segments[k + 1].begin
                                     : static_cast<uint32_t>(e.text.size())) - seg.begin;
      if (offset < seg.source_begin || offset > seg.source_begin + length) continue;
      if (inc.text != *s.text || canonical_path(inc.path) != script->included_path) continue;
      auto searched = std::make_shared<Snapshot>();
      searched->text = std::shared_ptr<const std::string>(doc, &e.text);
      searched->index = e.index;
      searched->lines = std::make_shared<Line_map>(e.text);
      searched->version_id = script->snapshot->version_id;
      searched->index_options = script->snapshot->index_options;
      const uint32_t at = seg.begin + (offset - seg.source_begin);
      auto resolved = resolve_at(*searched, searched->lines->position(e.text, at),
                                 script->catalogs, cache);
      if (!resolved) break;
      Ref_scope out;
      out.searched = std::move(searched);
      out.catalogs = script->catalogs;
      out.expanded = true;
      out.at = std::move(*resolved);
      out.uri = script->uri;
      out.snapshot = script->snapshot;
      out.include_uris = include_uris(doc.get(), script);
      return out;
    }
  }
  // The document itself, with what it includes.
  auto resolved = resolve_at(s, pos, c, cache);
  if (!resolved) return std::nullopt;
  Ref_scope out;
  out.searched = std::shared_ptr<const Snapshot>(std::shared_ptr<const Snapshot>(), &s);
  out.catalogs = c;
  out.at = std::move(*resolved);
  out.uri = uri;
  out.snapshot = out.searched;
  out.include_uris = include_uris(c.document.get(), nullptr);
  return out;
}

int symbol_kind_of_routine(const Routine &r) { return r.kind == "function" ? 12 : 6; }

// `edits` as an array of LSP TextEdits.
void write_text_edits(Json_writer &w, const Snapshot &s, const std::vector<Text_edit> &edits) {
  w.StartArray();
  for (const auto &e : edits) {
    w.StartObject();
    w.Key("range");
    write_range(w, s, e.begin, e.end);
    str(w, "newText", e.text);
    w.EndObject();
  }
  w.EndArray();
}

// True when `view` met stubs a later pass can have read: they go into
// `pass.wanted` and the request writes nothing this time.
bool wants_details(const Catalog_sources &c, const Catalog_view &view, Details_pass &pass) {
  if (pass.final || view.stubs_met().empty() || !c.live_source || !c.live_source->fetches_details()) {
    return false;
  }
  pass.wanted = view.stubs_met();
  return true;
}

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

bool source_path_completion(Json_writer &w, const Snapshot &s, Position pos,
                            const std::string &folder) {
  const uint32_t offset = s.lines->offset(*s.text, pos);
  const auto at = source_path_at(*s.text, offset);
  if (!at) return false;
  w.StartObject();
  w.Key("isIncomplete");
  w.Bool(false);
  w.Key("items");
  w.StartArray();
  for (const auto &c : source_path_candidates(folder, at->typed)) {
    w.StartObject();
    str(w, "label", c.folder ? c.name + "/" : c.name);
    w.Key("kind");
    w.Int(c.folder ? 19 : 17);  // Folder : File
    str(w, "sortText", (c.folder ? "0" : "1") + c.name);
    w.Key("textEdit");
    w.StartObject();
    w.Key("range");
    write_range(w, s, at->segment, offset);
    str(w, "newText", c.folder ? c.name + "/" : c.name);
    w.EndObject();
    if (c.folder) {
      // On into the folder.
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
  return true;
}

void completion(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                Parser_cache &cache, const Completion_options &options, Details_pass &pass) {
  const uint32_t offset = s.lines->offset(*s.text, pos);
  const Catalog_view view = catalog_view(s, c, offset);
  auto list = complete(*s.text, *s.index, offset, view, cache, options);
  if (wants_details(c, view, pass)) return;
  // Asked again as the user types: the details may be there by then.
  if (!view.stubs_met().empty() && !pass.read) list.incomplete = true;
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
      write_text_edits(w, s, it.additional_edits);
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
           Parser_cache &cache, Details_pass &pass) {
  const auto at = resolve_at(s, pos, c, cache);
  if (at && wants_details(c, at->view, pass)) return;
  std::string text = at ? hover_markdown(at->resolution) : std::string();
  // From a file a SOURCE directive includes: which one, and where.
  if (const Origin *o = at ? origin_of(at->resolution) : nullptr;
      o != nullptr && o->source > 0 && c.document && o->source <= c.document->includes.size() &&
      at->resolution.layer != nullptr && at->resolution.layer->kind == Layer_kind::document) {
    constexpr std::string_view k_here = "Defined in this document.";
    const std::string there = "Defined in `" +
                              source_location(c.document->includes[o->source - 1], o->statement.begin) +
                              "`, which this document includes.";
    if (const auto at_here = text.find(k_here); at_here != std::string::npos) {
      text.replace(at_here, k_here.size(), there);
    } else {
      text += "\n\n" + there;
    }
  }
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
  std::string include_uri;
  std::string_view where_text;
  const Line_map *where_lines = nullptr;
  Range where;
  const Origin *o = origin_of(r);
  if (r.local_definition) {
    where = *r.local_definition;
    where_uri = &uri;
  } else if (o != nullptr && r.layer != nullptr && o->name.end > o->name.begin) {
    where = o->name;
    if (r.layer->kind == Layer_kind::document && o->source > 0) {
      // In a file a SOURCE directive includes.
      if (c.document && o->source <= c.document->includes.size()) {
        const auto &inc = c.document->includes[o->source - 1];
        include_uri = path_to_file_uri(inc.path);
        where_uri = &include_uri;
        where_text = inc.text;
        where_lines = &inc.lines;
      }
    } else if (r.layer->kind == Layer_kind::document) {
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
                Parser_cache &cache, int priority, const Including_script *script) {
  const auto scope = ref_scope(s, uri, pos, c, cache, script);
  w.StartArray();
  if (scope && scope->at.resolution.found()) {
    for (const auto &ref : scope->references(pool, cache, priority)) {
      if (ref.defining && !include_declaration) continue;
      const auto f = scope->file(ref.source);
      if (f.lines != nullptr) location(w, f.uri, f.text, *f.lines, ref.range);
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

bool prepare_rename(Json_writer &w, const Snapshot &s, const std::string &uri, Position pos,
                    const Catalog_sources &c, Parser_cache &cache, std::string *error,
                    const Including_script *script) {
  const auto scope = ref_scope(s, uri, pos, c, cache, script);
  if (!scope) {
    *error = "there is no name here to rename";
    return false;
  }
  if (!renamable(scope->at.resolution, error)) return false;
  const auto &ref = scope->at.resolution.ref;
  // In the requesting document's own text.
  const auto at = scope->locate(ref.range);
  const auto f = scope->file(at.source);
  if (f.lines == nullptr || f.uri != uri) {
    *error = "there is no name here to rename";
    return false;
  }
  w.StartObject();
  w.Key("range");
  write_range(w, f.text, *f.lines, at.range.begin, at.range.end);
  str(w, "placeholder", ref.name);
  w.EndObject();
  return true;
}

bool rename(Json_writer &w, const Snapshot &s, const std::string &uri, Position pos,
            const std::string &new_name, const Catalog_sources &c, Parser_pool &pool,
            Parser_cache &cache, int priority, std::string *error,
            const Including_script *script) {
  const std::string name = unquote_name(new_name);
  if (name.empty()) {
    *error = "the new name is empty";
    return false;
  }
  const auto scope = ref_scope(s, uri, pos, c, cache, script);
  if (!scope) {
    *error = "there is no name here to rename";
    return false;
  }
  if (!renamable(scope->at.resolution, error)) return false;
  const std::string text = quote_identifier(name);
  // The edits by file: the document's, and those of the files it includes
  // or of the script that includes it.
  std::map<std::string, std::vector<Range>> by_uri;
  std::map<std::string, Reference_file> files;
  by_uri[uri];
  files.emplace(uri, Reference_file{uri, *s.text, s.lines.get()});
  for (const auto &ref : scope->references(pool, cache, priority)) {
    const auto f = scope->file(ref.source);
    if (f.lines == nullptr) continue;
    by_uri[f.uri].push_back(ref.range);
    files.insert_or_assign(f.uri, f);
  }
  w.StartObject();
  w.Key("changes");
  w.StartObject();
  for (const auto &[file_uri, ranges] : by_uri) {
    const auto &f = files.at(file_uri);
    w.Key(file_uri.c_str(), static_cast<rapidjson::SizeType>(file_uri.size()));
    w.StartArray();
    for (const auto &r : ranges) {
      w.StartObject();
      w.Key("range");
      write_range(w, f.text, *f.lines, r.begin, r.end);
      str(w, "newText", text);
      w.EndObject();
    }
    w.EndArray();
  }
  w.EndObject();
  w.EndObject();
  return true;
}

void signature_help(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                    Parser_cache &cache, Details_pass &pass) {
  const uint32_t offset = s.lines->offset(*s.text, pos);
  const Catalog_view view = catalog_view(s, c, offset);
  const auto help = mdbls::signature_help(*s.text, *s.index, offset, view, cache, s.version_id);
  if (wants_details(c, view, pass)) return;
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
    // Objects from files the document's SOURCE directives include.
    const auto &includes = d.outline->includes;
    std::vector<std::string> uris;
    for (const auto &inc : includes) uris.push_back(path_to_file_uri(inc.path));
    list(d.outline->catalog, [&](uint32_t source) {
      if (source == 0) return f;
      if (source > includes.size()) return File{};
      return File{&uris[source - 1], includes[source - 1].text, &includes[source - 1].lines};
    });
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
  write_text_edits(w, s, format_sql(text, *s.index, begin, end, cache, o));
}

}  // namespace mdbls::lsp
