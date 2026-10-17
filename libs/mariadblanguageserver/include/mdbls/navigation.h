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

#ifndef MDBLS_NAVIGATION_H_
#define MDBLS_NAVIGATION_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mdbls/catalog_view.h"
#include "mdbls/keywords.h"
#include "mdbls/parser_pool.h"
#include "mdbls/sql_names.h"
#include "mdbls/statement_index.h"

namespace mdbls {

// What a name stands for, once looked up.
enum class Target_kind : uint8_t {
  none,
  schema,
  table,
  view,
  column,
  routine,           // stored procedure or function
  builtin_function,
  table_alias,       // FROM t AS a: `a`, and every a.x
  select_alias,      // SELECT x AS y: `y`
  cte,               // WITH c AS (...)
  variable,          // routine parameter or local variable
};

// The object a name refers to, independent of where it is written: two
// names refer to the same object when their identities are equal.  Names
// compare without regard to case.
struct Object_id {
  Target_kind kind = Target_kind::none;
  std::string schema;  // the schema it was looked up in ("" when unknown)
  std::string table;   // for a column: its table
  std::string name;

  bool operator==(const Object_id &o) const;
  // Defined in the statement that uses it (alias, CTE, variable): its
  // references and renames stay inside it.
  bool statement_local() const {
    return kind == Target_kind::table_alias || kind == Target_kind::select_alias ||
           kind == Target_kind::cte || kind == Target_kind::variable;
  }
};

struct Resolution {
  Name_ref ref;  // the name as written
  Object_id id;
  // What the catalogs know about it; each may be null.
  const Catalog_layer *layer = nullptr;
  const Schema *schema = nullptr;
  const Table *table = nullptr;
  const View *view = nullptr;
  const Column *column = nullptr;
  const Routine *routine = nullptr;
  const Builtin_function *builtin = nullptr;
  // For an alias, a CTE or a variable: where the statement defines it.
  std::optional<Range> local_definition;
  Table_ref table_ref;  // an alias's table, or the table a column was found in
  Variable variable;

  bool found() const { return id.kind != Target_kind::none; }
  bool statement_local() const { return id.statement_local(); }
};

// Looks names of one statement up in the catalogs.  The statement's
// current schema is the view's.
class Name_resolver {
 public:
  Name_resolver(const Catalog_view &catalogs, const Statement_names &names)
      : m_catalogs(catalogs), m_names(names) {}

  Resolution resolve(const Name_ref &ref) const;
  // The name at `offset`; a built-in function called with a keyword name
  // (COUNT, LEFT, SUBSTRING) too.
  Resolution resolve_at(uint32_t offset) const;

 private:
  void resolve_table(Resolution &r, const std::string &schema, const std::string &name) const;
  void resolve_column(Resolution &r) const;
  // r names `t` by its alias (its table's details too).
  void as_alias(Resolution &r, const Table_ref &t) const;
  // r names the statement's CTE `name`, if it has one.
  bool as_cte(Resolution &r, std::string_view name) const;

  const Catalog_view &m_catalogs;
  const Statement_names &m_names;
};

// Markdown describing a resolution, for a hover; empty when there is
// nothing to say.
std::string hover_markdown(const Resolution &r);

// The schema in use before each statement of `index`: the last USE before
// it, else `initial`.
std::vector<std::string> schemas_in_use(std::string_view text, const Statement_index &index,
                                        const std::string &initial);

struct Reference {
  Range range;
  bool defining = false;
};

struct Reference_options {
  unsigned long version_id = k_default_version_id;
  int priority = 0;
  std::string initial_schema;  // the schema in use at the start of the text
};

// Every name in `text` that refers to `id`, in text order: the whole text
// for a schema object, statement `statement` alone for a statement-local
// one.  Statements whose text does not contain the name are not parsed;
// the others are parsed on `pool`, which the call waits for (so it must not
// run on `pool` itself).  `catalogs` supplies the layers; its current
// schema is set per statement.
std::vector<Reference> find_references(std::string_view text, const Statement_index &index,
                                       const Object_id &id, size_t statement,
                                       const Catalog_view &catalogs, Parser_pool &pool,
                                       const Reference_options &options = {});

// The same, within statement `statement` only, on the calling thread.
std::vector<Reference> statement_references(std::string_view text, const Statement_index &index,
                                            size_t statement, const Object_id &id,
                                            const Catalog_view &catalogs, Parser_cache &cache,
                                            unsigned long version_id);

// A name the catalogs do not know.
struct Name_issue {
  size_t statement = 0;
  Range range;
  std::string message;  // "unknown table `x`", "unknown column `c` in `t`"
};

// The tables and columns the statements of `text` use that `catalogs` do
// not have: a table (not a CTE) in a schema the catalogs know, and a column
// of a table they know.  Names a statement defines are not checked.  Parsed
// on `pool`; the call waits for it.
std::vector<Name_issue> unknown_names(std::string_view text, const Statement_index &index,
                                      const Catalog_view &catalogs, Parser_pool &pool,
                                      const Reference_options &options = {});

// signatureHelp: the call the cursor is in, by its tokens.
struct Signature {
  std::string label;                               // "CONCAT(str1,str2,...)"
  std::vector<std::pair<size_t, size_t>> params;   // byte ranges in label
  std::string documentation;                       // Markdown
};

struct Signature_help {
  std::vector<Signature> signatures;
  size_t active_signature = 0;
  size_t active_parameter = 0;
};

// The signatures of the innermost call around `offset`: a built-in
// function, or a stored function or procedure (CALL p(...)) from
// `catalogs`.  nullopt when the cursor is in no call or the callee is not
// known.
std::optional<Signature_help> signature_help(std::string_view text, const Statement_index &index,
                                             uint32_t offset, const Catalog_view &catalogs,
                                             Parser_cache &cache, unsigned long version_id);

}  // namespace mdbls

#endif  // MDBLS_NAVIGATION_H_
