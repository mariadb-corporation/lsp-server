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

#include "mdbls/outline.h"

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <optional>

#include "mdbls/lsp/json_position.h"
#include "mdbls/navigation.h"
#include "mdbparse.h"

namespace mdbls {

Outline build_outline(std::string_view text, const Statement_index &index, Parser_pool &pool,
                      const Outline_options &options) {
  Outline out;
  out.catalog.set_current_schema(options.schema);
  fold_outline(out, text, index, pool, options, 0);
  return out;
}

void fold_outline(Outline &out, std::string_view text, const Statement_index &index,
                  Parser_pool &pool, const Outline_options &options, uint32_t source) {
  const auto &stmts = index.statements();
  std::vector<size_t> ddl;
  for (size_t i = 0; i < stmts.size(); ++i) {
    if (stmts[i].kind == Statement_kind::sql && may_change_catalog(index.text_of(text, stmts[i]))) {
      ddl.push_back(i);
    }
  }

  // Parse in parallel; fold in file order afterwards.
  struct Parsed {
    bool accepted = false;
    bool unsupported = false;
    Syntax_tree tree;
  };
  std::vector<Parsed> parsed(ddl.size());
  constexpr size_t k_per_job = 64;
  const size_t n_jobs = (ddl.size() + k_per_job - 1) / k_per_job;
  pool.run_all(options.priority, n_jobs, [&](size_t job, Parser_cache &cache) {
    const size_t from = job * k_per_job;
    const size_t to = std::min(ddl.size(), from + k_per_job);
    for (size_t k = from; k < to; ++k) {
      const Statement &s = stmts[ddl[k]];
      mdbp_parser *p = cache.get(index.modes()[s.mode].mode, options.version_id, true);
      if (p == nullptr) {
        parsed[k].unsupported = true;
        continue;
      }
      const auto sql = index.text_of(text, s);
      mdbp_result r;
      if (mdbp_parse_sql(p, sql.data(), sql.size(), &r) == MDBP_OK && r.accepted) {
        parsed[k].accepted = true;
        parsed[k].tree = Syntax_tree::build(p, r, s.begin);
      }
    }
  });

  out.catalog.set_source(source);
  out.ddl_statements += ddl.size();
  const size_t old_notes = out.catalog.notes().size();
  const size_t first_note = out.notes.size();
  for (size_t k = 0; k < ddl.size(); ++k) {
    const uint32_t at = stmts[ddl[k]].begin;
    if (parsed[k].unsupported) {
      out.notes.push_back({at, "not read: sql_mode " + index.modes()[stmts[ddl[k]].mode].mode +
                                   " is not supported"});
    } else if (!parsed[k].accepted) {
      out.notes.push_back({at, "not read: the statement has a syntax error"});
    } else if (out.catalog.apply(parsed[k].tree, text)) {
      ++out.applied;
    }
  }
  for (size_t k = old_notes; k < out.catalog.notes().size(); ++k) {
    out.notes.push_back(out.catalog.notes()[k]);
  }
  std::sort(out.notes.begin() + static_cast<std::ptrdiff_t>(first_note), out.notes.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
}

namespace {

// fold_outline() of an expanded text, with its index.
void fold_expanded(Outline &out, const Source_expansion &e, const Statement_index &index,
                   Parser_pool &pool, const Outline_options &options, uint32_t source,
                   const std::vector<uint32_t> &include_sources) {
  // Origins of this fold are marked, then mapped back.
  constexpr uint32_t k_pending = UINT32_MAX;
  const size_t first_note = out.notes.size();
  fold_outline(out, e.text, index, pool, options, k_pending);

  auto to_source = [&](uint32_t s) { return s == 0 ? source : include_sources[s - 1]; };
  out.catalog.for_each_origin([&](Origin &o) {
    if (o.source != k_pending) return;
    const auto statement = e.map(o.statement);
    const auto name = e.map(o.name);
    o.statement = statement.range;
    o.name = name.source == statement.source ? name.range : statement.range;
    o.source = to_source(statement.source);
  });

  for (size_t k = first_note; k < out.notes.size(); ++k) {
    auto &[at, message] = out.notes[k];
    const auto loc = e.map(at);
    if (loc.source == 0) {
      at = loc.offset;
    } else {
      const auto &inc = e.includes[loc.source - 1];
      at = inc.directive.statement.begin;
      message = source_location(inc, loc.offset) + ": " + message;
    }
  }
  for (const auto &inc : e.includes) {
    const uint32_t at = inc.directive.statement.begin;
    if (!inc.error.empty()) out.notes.push_back({at, "not read: " + inc.error});
    for (const auto &n : inc.nested) {
      out.notes.push_back({at, source_location(inc, n.statement.begin) + ": SOURCE '" + n.file +
                                   "' not expanded: msm expands one level only"});
    }
  }
  std::stable_sort(out.notes.begin() + static_cast<std::ptrdiff_t>(first_note), out.notes.end(),
                   [](const auto &a, const auto &b) { return a.first < b.first; });
}

// The including script's state where each include begins.
void set_contexts(Source_expansion &e, const Statement_index &index, const std::string &schema) {
  const auto &st = index.statements();
  if (st.empty()) return;
  const auto schemas = schemas_in_use(e.text, index, schema);
  for (const auto &seg : e.segments) {
    if (seg.source == 0) continue;
    auto &ctx = e.includes[seg.source - 1].context;
    if (ctx.known) continue;
    // The statement the included text begins in, else the first after it.
    const auto it = std::lower_bound(st.begin(), st.end(), seg.begin,
                                     [](const Statement &s, uint32_t o) { return s.end <= o; });
    const size_t i = it == st.end() ? st.size() - 1 : static_cast<size_t>(it - st.begin());
    const Statement &s = st[i];
    ctx.known = true;
    ctx.schema = schemas[i];
    ctx.sql_mode = index.modes()[s.mode].mode;
    const bool inside = s.begin < seg.begin && s.kind == Statement_kind::sql;
    const std::string &d = index.delimiters()[s.active_delimiter];
    ctx.delimiter = inside || d.empty() ? ";" : d;
  }
}

}  // namespace

void fold_outline(Outline &out, const Source_expansion &e, Parser_pool &pool,
                  const Index_options &index_options, const Outline_options &options,
                  uint32_t source, const std::vector<uint32_t> &include_sources) {
  const auto index = Statement_index::build_parallel(e.text, index_options, 0);
  fold_expanded(out, e, index, pool, options, source, include_sources);
}

Outline build_outline(Source_expansion expansion, Parser_pool &pool,
                      const Index_options &index_options, const Outline_options &options) {
  Outline out;
  out.catalog.set_current_schema(options.schema);
  std::vector<uint32_t> sources(expansion.includes.size());
  for (size_t k = 0; k < sources.size(); ++k) sources[k] = static_cast<uint32_t>(k + 1);
  auto index = std::make_shared<const Statement_index>(
      Statement_index::build_parallel(expansion.text, index_options, 0));
  fold_expanded(out, expansion, *index, pool, options, 0, sources);
  set_contexts(expansion, *index, options.schema);
  auto expanded = std::make_shared<Outline::Expanded>();
  expanded->text = std::move(expansion.text);
  expanded->segments = std::move(expansion.segments);
  expanded->index = std::move(index);
  out.expanded = std::move(expanded);
  out.includes = std::move(expansion.includes);
  return out;
}

namespace {

using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

class Json_outline {
 public:
  Json_outline(Writer &w, std::string_view text, const Line_map &lines,
               const std::vector<Source_include> &includes)
      : w_(w), text_(text), lines_(lines), includes_(includes) {}

  void str(const char *key, const std::string &v) {
    w_.Key(key);
    w_.String(v.data(), static_cast<rapidjson::SizeType>(v.size()));
  }
  void opt_str(const char *key, const std::optional<std::string> &v) {
    w_.Key(key);
    if (v) {
      w_.String(v->data(), static_cast<rapidjson::SizeType>(v->size()));
    } else {
      w_.Null();
    }
  }
  void str_or_null(const char *key, const std::string &v) {
    w_.Key(key);
    if (v.empty()) {
      w_.Null();
    } else {
      w_.String(v.data(), static_cast<rapidjson::SizeType>(v.size()));
    }
  }
  void strings(const char *key, const std::vector<std::string> &v) {
    w_.Key(key);
    w_.StartArray();
    for (const auto &s : v) w_.String(s.data(), static_cast<rapidjson::SizeType>(s.size()));
    w_.EndArray();
  }
  void position(uint32_t off) { lsp::write_position(w_, lines_.position(text_, off)); }
  void range(const char *key, Range r) {
    w_.Key(key);
    lsp::write_range(w_, text_, lines_, r.begin, r.end);
  }
  // An object from an included file: `source` is its index in `includes`,
  // its ranges are positions in that file.
  void origin(const Origin &o) {
    if (o.source > 0 && o.source <= includes_.size()) {
      const auto &inc = includes_[o.source - 1];
      w_.Key("source");
      w_.Uint(o.source - 1);
      w_.Key("range");
      lsp::write_range(w_, inc.text, inc.lines, o.statement.begin, o.statement.end);
      w_.Key("selection_range");
      lsp::write_range(w_, inc.text, inc.lines, o.name.begin, o.name.end);
      return;
    }
    range("range", o.statement);
    range("selection_range", o.name);
  }
  void options(const std::vector<std::pair<std::string, std::string>> &opts) {
    w_.Key("options");
    w_.StartObject();
    for (const auto &[k, v] : opts) str(k.c_str(), v);
    w_.EndObject();
  }
  void named(const char *key, const std::vector<Named_object> &v) {
    w_.Key(key);
    w_.StartArray();
    for (const auto &o : v) {
      w_.StartObject();
      str("name", o.name);
      origin(o.origin);
      w_.EndObject();
    }
    w_.EndArray();
  }

  void table(const Table &t) {
    w_.StartObject();
    str("name", t.name);
    w_.Key("temporary");
    w_.Bool(t.temporary);
    origin(t.origin);
    options(t.options);
    w_.Key("columns");
    w_.StartArray();
    for (const auto &c : t.columns) {
      w_.StartObject();
      str("name", c.name);
      str("type", c.type);
      w_.Key("nullable");
      w_.Bool(c.nullable);
      opt_str("default", c.default_value);
      w_.Key("auto_increment");
      w_.Bool(c.auto_increment);
      opt_str("comment", c.comment);
      opt_str("collation", c.collation);
      opt_str("generated", c.generated);
      origin(c.origin);
      w_.EndObject();
    }
    w_.EndArray();
    w_.Key("indexes");
    w_.StartArray();
    for (const auto &i : t.indexes) {
      w_.StartObject();
      str_or_null("name", i.name);
      str("kind", i.kind);
      strings("columns", i.columns);
      origin(i.origin);
      w_.EndObject();
    }
    w_.EndArray();
    w_.Key("foreign_keys");
    w_.StartArray();
    for (const auto &f : t.foreign_keys) {
      w_.StartObject();
      str_or_null("name", f.name);
      strings("columns", f.columns);
      w_.Key("references");
      w_.StartObject();
      str_or_null("schema", f.ref_schema);
      str("table", f.ref_table);
      strings("columns", f.ref_columns);
      w_.EndObject();
      str_or_null("on_delete", f.on_delete);
      str_or_null("on_update", f.on_update);
      origin(f.origin);
      w_.EndObject();
    }
    w_.EndArray();
    w_.Key("checks");
    w_.StartArray();
    for (const auto &c : t.checks) {
      w_.StartObject();
      str_or_null("name", c.name);
      str("expression", c.expression);
      origin(c.origin);
      w_.EndObject();
    }
    w_.EndArray();
    w_.EndObject();
  }

  void schema(const Schema &s) {
    w_.StartObject();
    str_or_null("name", s.name);
    w_.Key("declared");
    w_.Bool(s.declared);
    if (s.declared) origin(s.origin);
    options(s.options);
    w_.Key("tables");
    w_.StartArray();
    for (const auto &t : s.tables) table(t);
    w_.EndArray();
    w_.Key("views");
    w_.StartArray();
    for (const auto &v : s.views) {
      w_.StartObject();
      str("name", v.name);
      strings("columns", v.columns);
      str("definition", v.definition);
      origin(v.origin);
      w_.EndObject();
    }
    w_.EndArray();
    w_.Key("routines");
    w_.StartArray();
    for (const auto &r : s.routines) {
      w_.StartObject();
      str("name", r.name);
      str("kind", r.kind);
      w_.Key("parameters");
      w_.StartArray();
      for (const auto &p : r.parameters) {
        w_.StartObject();
        str_or_null("mode", p.mode);
        str("name", p.name);
        str("type", p.type);
        w_.EndObject();
      }
      w_.EndArray();
      str_or_null("returns", r.returns);
      origin(r.origin);
      w_.EndObject();
    }
    w_.EndArray();
    w_.Key("triggers");
    w_.StartArray();
    for (const auto &t : s.triggers) {
      w_.StartObject();
      str("name", t.name);
      str("timing", t.timing);
      str("events", t.events);
      str("table", t.table);
      origin(t.origin);
      w_.EndObject();
    }
    w_.EndArray();
    named("events", s.events);
    named("sequences", s.sequences);
    w_.EndObject();
  }

  void rest(const Catalog &c) {
    w_.Key("rest");
    w_.StartObject();
    w_.Key("services");
    w_.StartArray();
    for (const auto &svc : c.rest_services()) {
      w_.StartObject();
      str("path", svc.path);
      origin(svc.origin);
      w_.Key("schemas");
      w_.StartArray();
      for (const auto &sch : svc.schemas) {
        w_.StartObject();
        str("path", sch.path);
        str_or_null("schema", sch.schema);
        origin(sch.origin);
        w_.Key("objects");
        w_.StartArray();
        for (const auto &o : sch.objects) {
          w_.StartObject();
          str("kind", o.kind);
          str("path", o.path);
          str("object", o.object);
          origin(o.origin);
          w_.EndObject();
        }
        w_.EndArray();
        w_.EndObject();
      }
      w_.EndArray();
      named("content_sets", svc.content_sets);
      w_.EndObject();
    }
    w_.EndArray();
    named("auth_apps", c.rest_auth_apps());
    named("roles", c.rest_roles());
    named("users", c.rest_users());
    w_.EndObject();
  }

 private:
  Writer &w_;
  std::string_view text_;
  const Line_map &lines_;
  const std::vector<Source_include> &includes_;
};

}  // namespace

std::string outline_json(const Outline &outline, std::string_view text, const Line_map &lines,
                         std::string_view file) {
  rapidjson::StringBuffer buf;
  Writer w(buf);
  Json_outline j(w, text, lines, outline.includes);
  w.StartObject();
  w.Key("format");
  w.Uint(1);
  if (!file.empty()) j.str("file", std::string(file));
  w.Key("schemas");
  w.StartArray();
  for (const auto &s : outline.catalog.schemas()) {
    if (!s.declared && s.empty()) continue;  // a schema only named by USE
    j.schema(s);
  }
  w.EndArray();
  j.rest(outline.catalog);
  if (!outline.includes.empty()) {
    // The files SOURCE directives include, in order.
    w.Key("includes");
    w.StartArray();
    for (const auto &inc : outline.includes) {
      const auto &d = inc.directive;
      w.StartObject();
      j.str_or_null("file", inc.path);
      j.range("range", d.statement);
      j.str_or_null("slice", d.slice.end > d.slice.begin
                                 ? std::string(text.substr(d.slice.begin, d.slice.end - d.slice.begin))
                                 : std::string());
      if (!inc.error.empty()) j.str("error", inc.error);
      w.EndObject();
    }
    w.EndArray();
  }
  w.Key("statements");
  w.StartObject();
  w.Key("ddl");
  w.Uint64(outline.ddl_statements);
  w.Key("applied");
  w.Uint64(outline.applied);
  w.EndObject();
  w.Key("notes");
  w.StartArray();
  for (const auto &[at, message] : outline.notes) {
    w.StartObject();
    w.Key("start");
    j.position(at);
    j.str("message", message);
    w.EndObject();
  }
  w.EndArray();
  w.EndObject();
  return buf.GetString();
}

}  // namespace mdbls
