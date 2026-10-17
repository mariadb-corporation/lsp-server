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

#include "mdbls/navigation.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "mdbls/completion.h"
#include "mdbls/syntax_tree.h"
#include "mdbparse.h"
#include "text_util.h"

namespace mdbls {

namespace {

// Statements larger than this are parsed only up to a little after the last
// token that spells the name looked for.
constexpr size_t k_truncate_above = 64u << 10;
constexpr size_t k_truncate_slack = 1024;

std::string qualified(const std::string &schema, const std::string &name) {
  return (schema.empty() ? std::string() : "`" + schema + "`.") + "`" + name + "`";
}

std::string column_line(const Column &c) {
  std::string s = quote_identifier(c.name) + " " + c.attributes();
  if (c.generated) s += " AS (" + *c.generated + ")";
  return s;
}

const char *layer_text(const Catalog_layer *l) {
  if (l == nullptr) return nullptr;
  switch (l->kind) {
    case Layer_kind::document: return "Defined in this document.";
    case Layer_kind::workspace: return "Defined in the workspace.";
    case Layer_kind::live: return "From the connected server.";
  }
  return nullptr;
}

// Copies what a catalog lookup found into `r`.
void set_hit(Resolution &r, const Catalog_view::Table_hit &hit) {
  r.table = hit.table;
  r.view = hit.view;
  r.schema = hit.schema;
  r.layer = hit.layer;
}

// Runs `fn(i, view, cache, out)` for each statement index of `todo` on
// `pool`, `per_job` statements a job, each job with its own copy of
// `catalogs` whose current schema is the one in use at the statement; the
// outputs in `todo` order.
template <typename T, typename Fn>
std::vector<T> on_statements(const std::vector<size_t> &todo, size_t per_job,
                             const std::vector<std::string> &schemas, const Catalog_view &catalogs,
                             Parser_pool &pool, int priority, Fn &&fn) {
  const size_t jobs = (todo.size() + per_job - 1) / per_job;
  std::vector<std::vector<T>> parts(jobs);
  pool.run_all(priority, jobs, [&](size_t job, Parser_cache &cache) {
    Catalog_view view = catalogs;
    const size_t from = job * per_job, to = std::min(todo.size(), from + per_job);
    for (size_t k = from; k < to; ++k) {
      view.set_current_schema(schemas[todo[k]]);
      fn(todo[k], view, cache, parts[job]);
    }
  });
  std::vector<T> out;
  for (auto &p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

}  // namespace

bool Object_id::operator==(const Object_id &o) const {
  return kind == o.kind && iequals(schema, o.schema) && iequals(table, o.table) &&
         iequals(name, o.name);
}

void Name_resolver::resolve_table(Resolution &r, const std::string &schema,
                                  const std::string &name) const {
  const auto hit = m_catalogs.find_table(schema, name);
  r.id.name = name;
  if (hit.table != nullptr || hit.view != nullptr) {
    set_hit(r, hit);
    r.id.kind = hit.view != nullptr ? Target_kind::view : Target_kind::table;
    r.id.schema = hit.schema->name.empty() ? m_catalogs.current_schema() : hit.schema->name;
    r.id.name = hit.table != nullptr ? hit.table->name : hit.view->name;
    return;
  }
  r.id.kind = Target_kind::table;
  r.id.schema = schema.empty() ? m_catalogs.current_schema() : schema;
}

void Name_resolver::as_alias(Resolution &r, const Table_ref &t) const {
  r.id = {Target_kind::table_alias, "", "", t.alias};
  r.local_definition = t.alias_range;
  r.table_ref = t;
  if (!t.derived) set_hit(r, m_catalogs.find_table(t.schema, t.name));
}

bool Name_resolver::as_cte(Resolution &r, std::string_view name) const {
  for (const auto &c : m_names.ctes()) {
    if (iequals(c.name, name)) {
      r.id = {Target_kind::cte, "", "", c.name};
      r.local_definition = c.name_range;
      r.table_ref = c;
      return true;
    }
  }
  return false;
}

void Name_resolver::resolve_column(Resolution &r) const {
  const Name_ref &ref = r.ref;
  r.id.kind = Target_kind::column;
  r.id.name = ref.name;
  // The table found, and the column in it.
  auto in_table = [&](const std::string &schema, const std::string &table) {
    Resolution t;
    resolve_table(t, schema, table);
    set_hit(r, {t.table, t.view, t.schema, t.layer});
    r.id.schema = t.id.schema;
    r.id.table = t.id.name;
    if (t.table != nullptr) r.column = t.table->column(ref.name);
  };
  auto in_ref = [&](const Table_ref &t) {
    r.table_ref = t;
    if (t.derived) {
      r.id.schema.clear();
      r.id.table = t.visible_name();
      return;
    }
    in_table(t.schema, t.name);
  };

  if (ref.has_owner) {
    for (const auto &c : m_names.ctes()) {
      if (ref.owner_schema.empty() && iequals(c.name, ref.owner)) {
        in_ref(c);
        return;
      }
    }
    in_table(ref.owner_schema, ref.owner);
    return;
  }
  if (ref.qualifiers.size() >= 2) {
    in_table(ref.qualifiers[ref.qualifiers.size() - 2], ref.qualifiers.back());
    return;
  }
  const auto tables = m_names.tables_at(ref.range.begin);
  if (ref.qualifiers.size() == 1) {
    const std::string &q = ref.qualifiers[0];
    if (const Table_ref *t = table_for_qualifier(tables, m_names.ctes(), q)) return in_ref(*t);
    in_table("", q);
    return;
  }
  // Unqualified: the first table in scope that has it.
  for (const auto &t : tables) {
    if (t.derived) {
      for (const auto &c : t.columns) {
        if (iequals(c, ref.name)) return in_ref(t);
      }
      continue;
    }
    const auto hit = m_catalogs.find_table(t.schema, t.name);
    const bool has = hit.table != nullptr
                         ? hit.table->column(ref.name) != nullptr
                         : hit.view != nullptr &&
                               std::any_of(hit.view->columns.begin(), hit.view->columns.end(),
                                           [&](const std::string &c) { return iequals(c, ref.name); });
    if (has) return in_ref(t);
  }
  // A routine variable, or a select alias in ORDER BY / GROUP BY / HAVING.
  for (const auto &v : m_names.variables()) {
    if (iequals(v.name, ref.name)) {
      r.id = {Target_kind::variable, "", "", v.name};
      r.variable = v;
      r.local_definition = v.range;
      return;
    }
  }
  for (const auto &a : m_names.select_aliases_at(ref.range.begin)) {
    if (iequals(a, ref.name)) {
      r.id = {Target_kind::select_alias, "", "", a};
      return;
    }
  }
  if (tables.size() == 1) return in_ref(tables[0]);
}

Resolution Name_resolver::resolve(const Name_ref &ref) const {
  Resolution r;
  r.ref = ref;
  switch (ref.kind) {
    case Name_kind::none:
    case Name_kind::index:
    case Name_kind::other:
      break;
    case Name_kind::schema: {
      r.id = {Target_kind::schema, ref.name, "", ref.name};
      for (const auto &l : m_catalogs.layers()) {
        if (l.catalog == nullptr) continue;
        if (const Schema *s = l.catalog->find(ref.name)) {
          r.schema = s;
          r.layer = &l;
          break;
        }
      }
      break;
    }
    case Name_kind::table: {
      if (ref.qualifiers.empty() && as_cte(r, ref.name)) return r;
      resolve_table(r, ref.qualifiers.empty() ? std::string() : ref.qualifiers.back(), ref.name);
      break;
    }
    case Name_kind::qualifier: {
      for (const auto &t : m_names.tables_at(ref.range.begin)) {
        if (!t.alias.empty() && iequals(t.alias, ref.name)) {
          as_alias(r, t);
          return r;
        }
      }
      if (as_cte(r, ref.name)) return r;
      resolve_table(r, "", ref.name);
      break;
    }
    case Name_kind::column:
      if (ref.defining && !ref.has_owner) {
        // CREATE VIEW v (a, b): a column of the view being defined.
        r.id = {Target_kind::column, m_catalogs.current_schema(), "", ref.name};
        break;
      }
      resolve_column(r);
      break;
    case Name_kind::routine:
    case Name_kind::function: {
      const std::string schema = ref.qualifiers.empty() ? std::string() : ref.qualifiers.back();
      const auto hit = m_catalogs.find_routine(schema, ref.name);
      if (hit.routine != nullptr) {
        r.routine = hit.routine;
        r.schema = hit.schema;
        r.layer = hit.layer;
        r.id = {Target_kind::routine,
                hit.schema->name.empty() ? m_catalogs.current_schema() : hit.schema->name, "",
                hit.routine->name};
        break;
      }
      if (ref.kind == Name_kind::function && ref.qualifiers.empty()) {
        if (const auto *f = builtin_function(ref.name)) {
          r.builtin = f;
          r.id = {Target_kind::builtin_function, "", "", std::string(f->name)};
          break;
        }
      }
      r.id = {Target_kind::routine, schema.empty() ? m_catalogs.current_schema() : schema, "",
              ref.name};
      break;
    }
    case Name_kind::alias: {
      for (const auto &t : m_names.tables_at(ref.range.begin)) {
        if (t.alias_range.begin == ref.range.begin && t.alias_range.end == ref.range.end) {
          as_alias(r, t);
          return r;
        }
      }
      r.id = {Target_kind::select_alias, "", "", ref.name};
      r.local_definition = ref.range;
      break;
    }
    case Name_kind::variable: {
      r.id = {Target_kind::variable, "", "", ref.name};
      for (const auto &v : m_names.variables()) {
        if (iequals(v.name, ref.name)) {
          r.variable = v;
          r.local_definition = v.range;
          break;
        }
      }
      break;
    }
  }
  return r;
}

Resolution Name_resolver::resolve_at(uint32_t offset) const {
  const uint32_t tok = m_names.token_at(offset);
  if (tok == Syntax_tree::npos) return {};
  Name_ref ref = m_names.classify(tok);
  if (ref.kind == Name_kind::none) {
    // COUNT(, LEFT(, SUBSTRING(: built-in functions spelled by keywords.
    const auto &tokens = m_names.tree().tokens();
    if (tok + 1 < tokens.size() && is_left_paren(tokens[tok + 1].symbol)) {
      if (const auto *f = builtin_function(ref.name)) {
        Resolution r;
        r.ref = ref;
        r.ref.kind = Name_kind::function;
        r.builtin = f;
        r.id = {Target_kind::builtin_function, "", "", std::string(f->name)};
        return r;
      }
    }
    return {};
  }
  return resolve(ref);
}

std::string hover_markdown(const Resolution &r) {
  std::string out;
  auto source = [&] {
    if (const char *t = layer_text(r.layer)) out += std::string("\n\n") + t;
  };
  auto table_body = [&](const Table &t) {
    out += "\n```sql\n";
    for (const auto &c : t.columns) out += column_line(c) + "\n";
    for (const auto &ix : t.indexes) {
      const std::string cols = join(ix.columns);
      out += (ix.kind == "primary" ? std::string("PRIMARY KEY")
                                   : upper(ix.kind == "index" ? "key" : ix.kind + " key") +
                                         (ix.name.empty() ? "" : " " + quote_identifier(ix.name))) +
             " (" + cols + ")\n";
    }
    for (const auto &fk : t.foreign_keys) {
      out += "FOREIGN KEY (" + join(fk.columns) + ") REFERENCES " + quote_identifier(fk.ref_table) +
             " (" + join(fk.ref_columns) + ")\n";
    }
    out += "```";
  };
  const std::string schema_name = r.schema != nullptr ? r.schema->name : r.id.schema;
  switch (r.id.kind) {
    case Target_kind::none:
      return {};
    case Target_kind::schema:
      if (r.schema == nullptr) return {};
      out = "**schema** `" + r.schema->name + "`";
      out += "\n\n" + std::to_string(r.schema->tables.size()) + " tables, " +
             std::to_string(r.schema->views.size()) + " views, " +
             std::to_string(r.schema->routines.size()) + " routines";
      source();
      return out;
    case Target_kind::table:
      if (r.table == nullptr) return {};
      out = std::string(r.table->temporary ? "**temporary table** " : "**table** ") +
            qualified(schema_name, r.table->name);
      table_body(*r.table);
      for (const auto &[k, v] : r.table->options) {
        if (k == "comment") out += "\n\n" + v;
      }
      source();
      return out;
    case Target_kind::view:
      if (r.view == nullptr) return {};
      out = "**view** " + qualified(schema_name, r.view->name);
      if (!r.view->columns.empty()) out += " (" + join(r.view->columns) + ")";
      if (!r.view->definition.empty()) out += "\n```sql\n" + r.view->definition + "\n```";
      source();
      return out;
    case Target_kind::column: {
      if (r.column != nullptr) {
        out = "**column** " + qualified("", r.id.table) + ".`" + r.column->name + "`";
        out += "\n```sql\n" + column_line(*r.column) + "\n```";
        if (r.column->comment) out += "\n\n" + *r.column->comment;
        source();
        return out;
      }
      if (r.table_ref.derived && !r.id.table.empty()) {
        return "**column** `" + r.id.name + "` of `" + r.id.table + "`";
      }
      if (r.view != nullptr) {
        return "**column** `" + r.id.name + "` of view " + qualified(schema_name, r.view->name);
      }
      return {};
    }
    case Target_kind::routine: {
      if (r.routine == nullptr) return {};
      out = "**" + r.routine->kind + "** " + qualified(schema_name, r.routine->name);
      out += "\n```sql\n" + upper(r.routine->kind) + " " + quote_identifier(r.routine->name) + "(" +
             r.routine->parameter_list() + ")" + (r.routine->returns.empty() ? "" : " RETURNS " + r.routine->returns) +
             "\n```";
      source();
      return out;
    }
    case Target_kind::builtin_function: {
      if (r.builtin == nullptr) return {};
      return "**function** `" + std::string(r.builtin->name) + "`\n" + builtin_markdown(*r.builtin);
    }
    case Target_kind::table_alias: {
      const auto &t = r.table_ref;
      out = "**alias** `" + r.id.name + "` for " +
            (t.derived ? std::string("a subquery") : "table " + qualified(t.schema, t.name));
      if (r.table != nullptr) table_body(*r.table);
      if (t.derived && !t.columns.empty()) out += "\n\nColumns: " + join(t.columns);
      return out;
    }
    case Target_kind::select_alias:
      return "**select alias** `" + r.id.name + "`";
    case Target_kind::cte: {
      out = "**common table expression** `" + r.id.name + "`";
      if (!r.table_ref.columns.empty()) out += "\n\nColumns: " + join(r.table_ref.columns);
      return out;
    }
    case Target_kind::variable:
      return "**variable** `" + r.id.name + "`" +
             (r.variable.type.empty() ? std::string() : " " + r.variable.type);
  }
  return out;
}

std::vector<std::string> schemas_in_use(std::string_view text, const Statement_index &index,
                                        const std::string &initial) {
  const auto &st = index.statements();
  std::vector<std::string> out(st.size());
  std::string current = initial;
  for (size_t i = 0; i < st.size(); ++i) {
    out[i] = current;
    if (st[i].kind != Statement_kind::sql) continue;
    if (starts_with_word(index.text_of(text, st[i]), "USE")) {
      const auto s = schema_in_use(text, index, i + 1);
      if (!s.empty()) current = s;
    }
  }
  return out;
}

namespace {

// The references to `id` in statement `i`, parsed with `cache`.
void references_in(std::string_view text, const Statement_index &index, size_t i,
                   const Object_id &id, const Catalog_view &catalogs, Parser_cache &cache,
                   unsigned long version_id, bool truncate, std::vector<Reference> *out) {
  const Statement &st = index.statements()[i];
  if (st.kind != Statement_kind::sql) return;
  const std::string &mode = index.modes()[st.mode].mode;
  std::string_view sql = index.text_of(text, st);
  if (truncate && sql.size() > k_truncate_above) {
    // Only as far as the last token that spells the name.
    mdbp_parser *plain = cache.get(mode, version_id, false);
    if (plain == nullptr) return;
    mdbp_result lr;
    if (mdbp_lex_sql(plain, sql.data(), sql.size(), &lr) != MDBP_OK || lr.tokens == nullptr) return;
    size_t last = 0;
    bool any = false;
    for (size_t k = 0; k < lr.ntokens; ++k) {
      const auto &t = lr.tokens[k];
      if (t.byte_end <= t.byte_start) continue;
      const int sym = mdbp_grammar_token_symbol(t.token_number);
      if (!is_name_symbol(sym)) continue;
      const auto word = sql.substr(static_cast<size_t>(t.byte_start),
                                   static_cast<size_t>(t.byte_end - t.byte_start));
      if (iequals(unquote_name(word), id.name)) {
        any = true;
        last = static_cast<size_t>(t.byte_end);
      }
    }
    if (!any) return;
    sql = sql.substr(0, std::min(sql.size(), last + k_truncate_slack));
  }
  mdbp_parser *tp = cache.get(mode, version_id, true);
  if (tp == nullptr) return;
  const Statement_names names(tp, sql, st.begin);
  if (!names.parsed()) return;
  const Name_resolver resolver(catalogs, names);
  for (const auto &ref : names.names(id.name)) {
    const auto r = resolver.resolve(ref);
    if (r.id == id) out->push_back({ref.range, ref.defining});
  }
}

}  // namespace

std::vector<Reference> statement_references(std::string_view text, const Statement_index &index,
                                            size_t statement, const Object_id &id,
                                            const Catalog_view &catalogs, Parser_cache &cache,
                                            unsigned long version_id) {
  std::vector<Reference> out;
  if (statement < index.statements().size()) {
    references_in(text, index, statement, id, catalogs, cache, version_id, false, &out);
  }
  return out;
}

std::vector<Reference> find_references(std::string_view text, const Statement_index &index,
                                       const Object_id &id, size_t statement,
                                       const Catalog_view &catalogs, Parser_pool &pool,
                                       const Reference_options &options) {
  if (id.kind == Target_kind::none || id.kind == Target_kind::builtin_function) return {};
  const auto &st = index.statements();
  std::vector<size_t> todo;
  if (id.statement_local()) {
    if (statement < st.size()) todo.push_back(statement);
  } else {
    for (size_t i = 0; i < st.size(); ++i) {
      if (st[i].kind == Statement_kind::sql && icontains(index.text_of(text, st[i]), id.name)) {
        todo.push_back(i);
      }
    }
  }
  if (todo.empty()) return {};
  return on_statements<Reference>(
      todo, 32, schemas_in_use(text, index, options.initial_schema), catalogs, pool,
      options.priority,
      [&](size_t i, const Catalog_view &view, Parser_cache &cache, std::vector<Reference> &out) {
        references_in(text, index, i, id, view, cache, options.version_id, true, &out);
      });
}

std::vector<Name_issue> unknown_names(std::string_view text, const Statement_index &index,
                                      const Catalog_view &catalogs, Parser_pool &pool,
                                      const Reference_options &options) {
  const auto &st = index.statements();
  std::vector<size_t> todo;
  for (size_t i = 0; i < st.size(); ++i) {
    if (st[i].kind == Statement_kind::sql) todo.push_back(i);
  }
  return on_statements<Name_issue>(
      todo, 64, schemas_in_use(text, index, options.initial_schema), catalogs, pool,
      options.priority,
      [&](size_t i, const Catalog_view &view, Parser_cache &cache, std::vector<Name_issue> &out) {
        mdbp_parser *p = cache.get(index.modes()[st[i].mode].mode, options.version_id, true);
        if (p == nullptr) return;
        const Statement_names names(p, index.text_of(text, st[i]), st[i].begin);
        if (!names.accepted()) return;  // a syntax error is reported already
        const Name_resolver resolver(view, names);
        for (const auto &ref : names.names()) {
          if (ref.defining || (ref.kind != Name_kind::table && ref.kind != Name_kind::column)) {
            continue;
          }
          const auto r = resolver.resolve(ref);
          if (r.id.kind == Target_kind::table && r.table == nullptr && r.view == nullptr) {
            // Only where the schema is known: a table of a schema nobody
            // described may well exist.
            if (r.id.schema.empty() || !view.has_schema(r.id.schema)) continue;
            const std::string schema =
                ref.qualifiers.empty() ? std::string() : ref.qualifiers.back() + "`.`";
            out.push_back({i, ref.range, "unknown table `" + schema + ref.name + "`"});
          } else if (r.id.kind == Target_kind::column && r.column == nullptr &&
                     r.table != nullptr && !r.table_ref.derived) {
            out.push_back({i, ref.range,
                           "unknown column `" + ref.name + "` in table `" + r.table->name + "`"});
          }
        }
      });
}

std::optional<Signature_help> signature_help(std::string_view text, const Statement_index &index,
                                             uint32_t offset, const Catalog_view &catalogs,
                                             Parser_cache &cache, unsigned long version_id) {
  const auto at = statement_at(index, offset);
  if (!at) return std::nullopt;
  const Statement &st = index.statements()[*at];
  if (st.kind != Statement_kind::sql) return std::nullopt;
  mdbp_parser *p = cache.get(index.modes()[st.mode].mode, version_id, false);
  if (p == nullptr) return std::nullopt;
  const std::string_view sql = text.substr(st.begin, offset - st.begin);
  mdbp_result r;
  if (mdbp_lex_sql(p, sql.data(), sql.size(), &r) != MDBP_OK || r.tokens == nullptr) {
    return std::nullopt;
  }
  struct Open {
    size_t paren;
    size_t commas;
  };
  std::vector<Open> stack;
  for (size_t k = 0; k < r.ntokens; ++k) {
    const char *name = r.tokens[k].name != nullptr ? r.tokens[k].name : "";
    if (is_left_paren(mdbp_grammar_token_symbol(r.tokens[k].token_number))) {
      stack.push_back({k, 0});
    } else if (std::strcmp(name, "')'") == 0) {
      if (!stack.empty()) stack.pop_back();
    } else if (std::strcmp(name, "','") == 0) {
      if (!stack.empty()) ++stack.back().commas;
    }
  }
  if (stack.empty() || stack.back().paren == 0) return std::nullopt;
  const size_t paren = stack.back().paren;
  const auto tok_text = [&](size_t k) {
    return sql.substr(static_cast<size_t>(r.tokens[k].byte_start),
                      static_cast<size_t>(r.tokens[k].byte_end - r.tokens[k].byte_start));
  };
  const size_t name_tok = paren - 1;
  const std::string name = unquote_name(tok_text(name_tok));
  std::string schema;
  bool call = false;
  size_t before = name_tok;
  if (before >= 2 && std::strcmp(r.tokens[before - 1].name, "'.'") == 0) {
    schema = unquote_name(tok_text(before - 2));
    before -= 2;
  }
  if (before >= 1 && iequals(tok_text(before - 1), "CALL")) call = true;

  Signature_help help;
  help.active_parameter = stack.back().commas;
  const auto hit = catalogs.find_routine(schema, name);
  if (hit.routine != nullptr && (call || hit.routine->kind == "function")) {
    Signature sig;
    sig.label = hit.routine->name + "(";
    for (size_t k = 0; k < hit.routine->parameters.size(); ++k) {
      const auto &prm = hit.routine->parameters[k];
      if (k > 0) sig.label += ", ";
      const size_t b = sig.label.size();
      sig.label += (prm.mode.empty() ? "" : prm.mode + " ") + prm.name + " " + prm.type;
      sig.params.emplace_back(b, sig.label.size());
    }
    sig.label += ")";
    if (!hit.routine->returns.empty()) sig.label += " RETURNS " + hit.routine->returns;
    help.signatures.push_back(std::move(sig));
    return help;
  }
  if (call || !schema.empty()) return std::nullopt;
  const Builtin_function *f = builtin_function(name);
  if (f == nullptr || f->forms.empty()) return std::nullopt;
  const std::string doc = builtin_markdown(*f, false);
  size_t from = 0;
  while (from <= f->forms.size()) {
    const size_t nl = f->forms.find('\n', from);
    std::string_view form = f->forms.substr(from, nl == std::string_view::npos ? std::string_view::npos : nl - from);
    while (!form.empty() && (form.back() == ',' || form.back() == ' ')) form.remove_suffix(1);
    Signature sig;
    sig.label = std::string(form);
    sig.params = call_form_parameters(sig.label);
    sig.documentation = doc;
    help.signatures.push_back(std::move(sig));
    if (nl == std::string_view::npos) break;
    from = nl + 1;
  }
  help.active_signature = 0;
  for (size_t k = 0; k < help.signatures.size(); ++k) {
    if (help.signatures[k].params.size() > help.active_parameter) {
      help.active_signature = k;
      break;
    }
  }
  return help;
}

}  // namespace mdbls
