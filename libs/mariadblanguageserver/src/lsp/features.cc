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

#include "lsp/features.h"

#include <algorithm>
#include <functional>
#include <string>

#include "mdbls/semantic_tokens.h"
#include "mdbls/syntax_tree.h"
#include "mdbparse.h"
#include "text_util.h"

namespace mdbls::lsp {

namespace {

// LSP SymbolKind values.
enum Symbol_kind : int {
  k_module = 2,
  k_namespace = 3,
  k_package = 4,
  k_class = 5,
  k_method = 6,
  k_property = 7,
  k_field = 8,
  k_interface = 11,
  k_function = 12,
  k_constant = 14,
  k_object = 19,
  k_key = 20,
  k_event = 24,
};

struct Symbols {
  Json_writer &w;
  const Snapshot &s;

  // One DocumentSymbol; `children` writes the children's array content.
  void symbol(const std::string &name, const std::string &detail, int kind, const Origin &o,
              const std::function<void()> &children = {}) {
    w.StartObject();
    w.Key("name");
    w.String(name.empty() ? "(unnamed)" : name.c_str());
    if (!detail.empty()) {
      w.Key("detail");
      w.String(detail.c_str());
    }
    w.Key("kind");
    w.Int(kind);
    Range range = o.statement;
    Range sel = o.name;
    if (sel.end <= sel.begin || sel.begin < range.begin || sel.end > range.end) {
      sel = {range.begin, range.begin};
    }
    w.Key("range");
    write_range(w, s, range.begin, range.end);
    w.Key("selectionRange");
    write_range(w, s, sel.begin, sel.end);
    if (children) {
      w.Key("children");
      w.StartArray();
      children();
      w.EndArray();
    }
    w.EndObject();
  }
};

// The range covering a schema's own statement and everything in it.
Origin schema_origin(const Schema &schema) {
  uint32_t lo = UINT32_MAX, hi = 0;
  auto take = [&](const Origin &o) {
    if (o.statement.end <= o.statement.begin) return;
    lo = std::min(lo, o.statement.begin);
    hi = std::max(hi, o.statement.end);
  };
  if (schema.declared) take(schema.origin);
  for (const auto &x : schema.tables) take(x.origin);
  for (const auto &x : schema.views) take(x.origin);
  for (const auto &x : schema.routines) take(x.origin);
  for (const auto &x : schema.triggers) take(x.origin);
  for (const auto &x : schema.events) take(x.origin);
  for (const auto &x : schema.sequences) take(x.origin);
  if (lo == UINT32_MAX) return {};
  Origin o;
  o.statement = {lo, hi};
  o.name = schema.declared ? schema.origin.name : Range{lo, lo};
  return o;
}

}  // namespace

void write_position(Json_writer &w, Position p) {
  w.StartObject();
  w.Key("line");
  w.Uint(p.line);
  w.Key("character");
  w.Uint(p.character);
  w.EndObject();
}

void write_range(Json_writer &w, std::string_view text, const Line_map &lines, uint32_t begin,
                 uint32_t end) {
  const Position a = lines.position(text, begin);
  const Position b = end <= begin ? a : lines.position_after(text, begin, a, end);
  w.StartObject();
  w.Key("start");
  write_position(w, a);
  w.Key("end");
  write_position(w, b);
  w.EndObject();
}

void write_range(Json_writer &w, const Snapshot &s, uint32_t begin, uint32_t end) {
  write_range(w, *s.text, *s.lines, begin, end);
}

void document_symbols(Json_writer &w, const Snapshot &s, const Outline &outline) {
  Symbols out{w, s};
  const Catalog &c = outline.catalog;
  w.StartArray();
  for (const auto &schema : c.schemas()) {
    if (schema.empty() && !schema.declared) continue;
    out.symbol(schema.name.empty() ? "(no schema)" : schema.name, "schema", k_namespace,
               schema_origin(schema), [&] {
                 for (const auto &t : schema.tables) {
                   out.symbol(t.name, t.temporary ? "temporary table" : "table", k_class, t.origin,
                              [&] {
                                for (const auto &col : t.columns)
                                  out.symbol(col.name, col.type, k_field, col.origin);
                                for (const auto &ix : t.indexes)
                                  out.symbol(ix.name.empty() ? ix.kind + " (" + join(ix.columns) + ")"
                                                             : ix.name,
                                             ix.kind + " (" + join(ix.columns) + ")", k_key,
                                             ix.origin);
                                for (const auto &fk : t.foreign_keys)
                                  out.symbol(fk.name.empty() ? "foreign key (" + join(fk.columns) + ")"
                                                             : fk.name,
                                             "references " + fk.ref_table + " (" +
                                                 join(fk.ref_columns) + ")",
                                             k_key, fk.origin);
                                for (const auto &ck : t.checks)
                                  out.symbol(ck.name.empty() ? "check" : ck.name, ck.expression,
                                             k_property, ck.origin);
                              });
                 }
                 for (const auto &v : schema.views) out.symbol(v.name, "view", k_interface, v.origin);
                 for (const auto &r : schema.routines) {
                   std::string params;
                   for (const auto &p : r.parameters)
                     params += (params.empty() ? "" : ", ") + (p.mode.empty() ? "" : p.mode + " ") +
                               p.name + " " + p.type;
                   out.symbol(r.name,
                              r.kind + "(" + params + ")" +
                                  (r.returns.empty() ? "" : " returns " + r.returns),
                              r.kind == "function" ? k_function : k_method, r.origin);
                 }
                 for (const auto &t : schema.triggers)
                   out.symbol(t.name, t.timing + " " + t.events + " on " + t.table, k_event,
                              t.origin);
                 for (const auto &e : schema.events) out.symbol(e.name, "event", k_event, e.origin);
                 for (const auto &q : schema.sequences)
                   out.symbol(q.name, "sequence", k_constant, q.origin);
               });
  }
  for (const auto &svc : c.rest_services()) {
    out.symbol(svc.path, "REST service", k_module, svc.origin, [&] {
      for (const auto &rs : svc.schemas) {
        out.symbol(rs.path, "REST schema " + rs.schema, k_package, rs.origin, [&] {
          for (const auto &o : rs.objects)
            out.symbol(o.path, "REST " + o.kind + " " + o.object, k_object, o.origin);
        });
      }
      for (const auto &cs : svc.content_sets)
        out.symbol(cs.name, "REST content set", k_package, cs.origin);
    });
  }
  for (const auto &a : c.rest_auth_apps()) out.symbol(a.name, "REST auth app", k_object, a.origin);
  for (const auto &r : c.rest_roles()) out.symbol(r.name, "REST role", k_object, r.origin);
  for (const auto &u : c.rest_users()) out.symbol(u.name, "REST user", k_object, u.origin);
  w.EndArray();
}

void folding_ranges(Json_writer &w, const Snapshot &s, Parser_cache &cache) {
  const auto &text = *s.text;
  const auto &lines = *s.lines;
  const auto &stmts = s.index->statements();
  auto fold = [&](uint32_t first, uint32_t last, const char *kind) {
    if (last <= first) return;
    w.StartObject();
    w.Key("startLine");
    w.Uint(first);
    w.Key("endLine");
    w.Uint(last);
    if (kind != nullptr) {
      w.Key("kind");
      w.String(kind);
    }
    w.EndObject();
  };
  w.StartArray();
  for (size_t i = 0; i < stmts.size(); ++i) {
    const auto &st = stmts[i];
    const uint32_t first = lines.line_of(st.begin);
    if (st.kind == Statement_kind::comment) {
      // A run of comment statements on consecutive lines folds as one.
      uint32_t last = lines.line_of(st.body_end > st.begin ? st.body_end - 1 : st.begin);
      while (i + 1 < stmts.size() && stmts[i + 1].kind == Statement_kind::comment &&
             lines.line_of(stmts[i + 1].begin) <= last + 1) {
        ++i;
        last = lines.line_of(stmts[i].body_end > stmts[i].begin ? stmts[i].body_end - 1
                                                                : stmts[i].begin);
      }
      fold(first, last, "comment");
      continue;
    }
    const uint32_t last = lines.line_of(st.end > st.begin ? st.end - 1 : st.begin);
    fold(first, last, nullptr);
    const std::string_view body = s.index->text_of(text, st);
    if (last > first && body.size() < (1u << 20) &&
        (starts_with_word(body, "CREATE") || starts_with_word(body, "BEGIN"))) {
      for (auto [open, close] : statement_blocks(text, *s.index, i, cache, s.version_id)) {
        const uint32_t a = lines.line_of(open), b = lines.line_of(close);
        if (b > a + 1 || (b > a && a != first)) fold(a, b - 1, nullptr);
      }
    }
  }
  w.EndArray();
}

void selection_ranges(Json_writer &w, const Snapshot &s, std::span<const Position> positions,
                      Parser_cache &cache) {
  const auto &text = *s.text;
  const auto &index = *s.index;
  w.StartArray();
  for (const auto &pos : positions) {
    const uint32_t offset = s.lines->offset(text, pos);
    std::vector<std::pair<uint32_t, uint32_t>> chain;  // innermost first
    auto add = [&](uint32_t b, uint32_t e) {
      if (e < b) return;
      if (!chain.empty() && chain.back().first == b && chain.back().second == e) return;
      if (!chain.empty() && (b > chain.back().first || e < chain.back().second)) return;
      chain.emplace_back(b, e);
    };
    const size_t i = index.find(offset);
    if (i < index.statements().size() && index.statements()[i].begin <= offset) {
      const auto &st = index.statements()[i];
      if (st.kind == Statement_kind::sql) {
        mdbp_parser *p = cache.get(index.modes()[st.mode].mode, s.version_id, true);
        const std::string_view sql = index.text_of(text, st);
        mdbp_result r;
        if (p != nullptr && mdbp_parse_sql(p, sql.data(), sql.size(), &r) != MDBP_INTERNAL) {
          const Syntax_tree tree = Syntax_tree::build(p, r, st.begin);
          // The token at the offset, or the one just before it (a cursor
          // right after a word selects the word).
          uint32_t leaf = Syntax_tree::npos;
          for (uint32_t n = 0; n < tree.size(); ++n) {
            if (!tree.is_token(n)) continue;
            const uint32_t b = tree.begin(n), e = tree.end(n);
            if (b <= offset && offset < e) {
              leaf = n;
              break;
            }
            if (e == offset && e > b) leaf = n;
          }
          for (uint32_t n = leaf; n != Syntax_tree::npos; n = tree.node(n).parent) {
            if (tree.end(n) > tree.begin(n)) add(tree.begin(n), tree.end(n));
          }
        }
      }
      add(st.begin, st.body_end);
      if (st.end > st.body_end) add(st.begin, st.end);
    }
    if (chain.empty()) add(offset, offset);
    add(0, static_cast<uint32_t>(text.size()));
    // SelectionRange {range, parent: {...}}: innermost outside.
    for (size_t k = 0; k < chain.size(); ++k) {
      w.StartObject();
      w.Key("range");
      write_range(w, s, chain[k].first, chain[k].second);
      if (k + 1 < chain.size()) w.Key("parent");
    }
    for (size_t k = 0; k < chain.size(); ++k) w.EndObject();
  }
  w.EndArray();
}

std::vector<uint32_t> semantic_tokens(const Snapshot &s, uint32_t begin, uint32_t end,
                                      Parser_pool &pool, int priority) {
  const auto &index = *s.index;
  const auto &stmts = index.statements();
  size_t last = index.find(begin);
  const size_t first = last;
  while (last < stmts.size() && (stmts[last].begin < end || end <= begin)) ++last;

  // Groups of statements of about 64 KiB, one job each.
  std::vector<std::pair<size_t, size_t>> groups;
  for (size_t i = first, bytes = 0; i < last; ++i) {
    if (groups.empty() || bytes >= (64u << 10)) {
      groups.emplace_back(i, i);
      bytes = 0;
    }
    groups.back().second = i + 1;
    bytes += stmts[i].end - stmts[i].begin;
  }
  std::vector<std::vector<Semantic_token>> parts(groups.size());
  pool.run_all(priority, groups.size(), [&](size_t g, Parser_cache &cache) {
    for (size_t i = groups[g].first; i < groups[g].second; ++i) {
      statement_semantic_tokens(*s.text, index, i, cache, s.version_id, &parts[g]);
    }
  });
  std::vector<uint32_t> data;
  Position at;
  for (const auto &part : parts) encode_semantic_tokens(*s.text, *s.lines, part, &data, &at);
  return data;
}

}  // namespace mdbls::lsp
