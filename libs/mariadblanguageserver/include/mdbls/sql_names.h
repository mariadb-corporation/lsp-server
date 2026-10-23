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

#ifndef MDBLS_SQL_NAMES_H_
#define MDBLS_SQL_NAMES_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "mdbls/catalog.h"
#include "mdbls/syntax_tree.h"

struct mdbp_parser;

namespace mdbls {

// What a name in a statement stands for.
enum class Name_kind : uint8_t {
  none,       // not a name: a keyword, a literal, punctuation
  schema,
  table,      // a table, view, sequence or common table expression
  column,
  qualifier,  // the `a` of `a.b`: a table alias or a table name
  routine,    // a stored procedure (CALL p) or a routine in DDL
  function,   // the name in a function call: built-in or stored
  alias,      // a table alias or a select alias, where it is defined
  variable,   // a routine parameter or local variable, where it is declared
  index,      // an index or constraint name
  other,      // another name being defined (trigger, event, label, ...)
};

const char *name_kind_name(Name_kind kind);

// One name token and what it stands for.
//
// `qualifiers` are the names written before it, outermost first: [schema]
// for a table, [table or alias] or [schema, table] for a column.  `owner`
// is the table a column belongs to when the statement says so outright
// (CREATE TABLE, ALTER TABLE, CREATE INDEX, an INSERT column list,
// REFERENCES, NEW.x and OLD.x in a trigger); for other columns the tables
// in scope decide.
struct Name_ref {
  Name_kind kind = Name_kind::none;
  bool defining = false;   // defined here: CREATE TABLE t, a column definition, AS a
  std::string name;        // unquoted
  Range range;             // of the token, quotes included
  std::vector<std::string> qualifiers;
  std::vector<Range> qualifier_ranges;
  bool has_owner = false;
  std::string owner_schema;  // "" when not qualified
  std::string owner;
  uint32_t node = Syntax_tree::npos;  // the token's node, npos without a tree
};

// A table a query reads: FROM t AS a, JOIN, UPDATE t, INSERT INTO t, a
// derived table (SELECT ...) AS d, or a common table expression.
struct Table_ref {
  std::string schema;      // as written; "" when not qualified
  std::string name;        // "" for a derived table
  std::string alias;       // "" when none
  Range name_range;
  Range alias_range;
  bool derived = false;    // a subquery or a CTE: `columns` are its columns
  std::vector<std::string> columns;

  // The name the query refers to it by: the alias, else the table name.
  const std::string &visible_name() const { return alias.empty() ? name : alias; }
};

// A routine parameter or DECLAREd variable.
// The SELECT a position is in, for completion that adds the table a column
// comes from: where its select list ends, and the tables after its FROM.
struct Query_shape {
  bool found = false;
  Range select_list;   // the select items
  bool has_from = false;
  bool dual = false;   // FROM DUAL: nothing to join to
  Range from_tables;   // the table references after FROM, joins included
  // The start of the statement whose clauses the query's clauses are (a
  // query, INSERT, REPLACE, UPDATE, DELETE, a view, a cursor's DECLARE),
  // UINT32_MAX for a query in parentheses; whether it is in a routine body.
  uint32_t statement_begin = UINT32_MAX;
  bool in_routine = false;
};

struct Variable {
  std::string name;
  std::string type;
  Range range;
};

// The names of one statement, from its parse tree.  The statement is parsed
// once; the tree of an accepted statement is complete, that of a rejected
// one holds what the parser had built before the error (its stack), and the
// analysis falls back to the tokens where the tree has no answer.
//
// `sql` is the text that was parsed and `base` the document offset of its
// first byte, so every Range is a document offset.  `sql` must outlive the
// object.
class Statement_names {
 public:
  // Parses `sql` with `parser` (one that records derivations).  An empty
  // analysis when the parser refuses the text (invalid UTF-8).
  Statement_names(mdbp_parser *parser, std::string_view sql, uint32_t base);

  bool accepted() const { return m_accepted; }
  bool parsed() const { return m_parsed; }
  const Syntax_tree &tree() const { return m_tree; }

  // The token covering `offset`, or the one ending at it (a cursor right
  // after a word belongs to the word); npos if none.
  uint32_t token_at(uint32_t offset) const;

  // What the token at `offset` names; kind none when it names nothing.
  Name_ref name_at(uint32_t offset) const;
  Name_ref classify(uint32_t token) const;

  // Every name token of the statement, in text order; with `spelled`, only
  // those that spell it (any case, unquoted), which are all that is
  // classified then.
  std::vector<Name_ref> names(std::string_view spelled = {}) const;

  // The tables a name at `offset` can see, innermost query first, then the
  // queries around it.  From the tokens when the tree has none.
  std::vector<Table_ref> tables_at(uint32_t offset) const;
  // Every table the statement reads, by the tokens: FROM, JOIN, UPDATE,
  // INTO, and a comma in a FROM list, then a name and an optional alias.
  std::vector<Table_ref> scanned_tables() const;
  // The select aliases visible at `offset` (in ORDER BY, GROUP BY, HAVING).
  std::vector<std::string> select_aliases_at(uint32_t offset) const;
  // The innermost SELECT around `offset`; `found` false outside one.
  Query_shape query_at(uint32_t offset) const;
  // The common table expressions the statement defines.
  const std::vector<Table_ref> &ctes() const { return m_ctes; }
  // Parameters and local variables the statement declares.
  const std::vector<Variable> &variables() const { return m_variables; }

  // The unquoted name a node spells ("`a``b`" -> "a`b").
  std::string name_of(uint32_t node) const;
  std::string_view text(uint32_t node) const;

 private:
  uint32_t ancestor(uint32_t n, int symbol) const;
  uint32_t node_at(uint32_t offset) const;
  // The leaf of token `token`, npos when the parser never shifted it.
  uint32_t leaf(uint32_t token) const {
    return token < m_leaves.size() ? m_leaves[token] : Syntax_tree::npos;
  }
  void collect_tables(uint32_t scope, std::vector<Table_ref> *out) const;
  void derived_columns(uint32_t query, std::vector<std::string> *out) const;
  // A select_item's alias (AS x, AS 'x'), "" when it has none.
  std::string alias_of(uint32_t select_item) const;
  Table_ref table_ident_ref(uint32_t table_ident) const;
  // Owner of `ref`: the table `table_ident` names (nothing for npos).
  void set_owner(uint32_t table_ident, Name_ref *ref) const;
  // A table named like a CTE of the statement is that CTE.
  void apply_cte(Table_ref *ref) const;
  Name_ref classify_by_tokens(uint32_t token) const;

  std::string_view m_sql;
  uint32_t m_base;
  bool m_parsed = false;
  bool m_accepted = false;
  Syntax_tree m_tree;
  std::vector<uint32_t> m_leaves;  // token -> leaf node
  std::vector<Table_ref> m_ctes;
  std::vector<Variable> m_variables;
};

// The table `qualifier` (the `a` of `a.b`) names among `tables` (by alias,
// else by table name) or `ctes`; nullptr if none.
const Table_ref *table_for_qualifier(const std::vector<Table_ref> &tables,
                                     const std::vector<Table_ref> &ctes, std::string_view qualifier);

// The unquoted name a token spells: `a``b` -> a`b, "x" -> x, plain names
// as they are.
std::string unquote_name(std::string_view token);

}  // namespace mdbls

#endif  // MDBLS_SQL_NAMES_H_
