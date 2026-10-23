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

#ifndef MDBLS_COMPLETION_H_
#define MDBLS_COMPLETION_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mdbls/catalog_view.h"
#include "mdbls/parser_pool.h"
#include "mdbls/sql_names.h"
#include "mdbls/statement_index.h"

namespace mdbls {

enum class Completion_kind : uint8_t {
  keyword,
  schema,
  table,
  view,
  column,
  alias,     // a table alias or a table name used as a qualifier
  function,  // built-in
  routine,   // stored function or procedure
  variable,
};

const char *completion_kind_name(Completion_kind kind);

// A change elsewhere in the document that an item brings with it: the
// table a column comes from, added to the FROM clause.
struct Text_edit {
  uint32_t begin = 0;
  uint32_t end = 0;
  std::string text;
};

// `text` with `edits` (in order, not overlapping) applied.
std::string apply_edits(std::string_view text, const std::vector<Text_edit> &edits);

struct Completion_item {
  std::string label;
  Completion_kind kind = Completion_kind::keyword;
  std::string detail;         // "table", "int(11) NOT NULL", a call form
  std::string documentation;  // Markdown, may be empty
  std::string insert_text;    // replaces the word being typed
  bool snippet = false;       // insert_text is an LSP snippet
  std::string sort_text;
  // Applied with the item, outside the word it replaces; none overlaps it.
  std::vector<Text_edit> additional_edits;
  // The item ends in a '.': the next names are asked for at once.
  bool retrigger = false;
};

struct Completion_list {
  // The word being typed, which an item replaces: [replace_begin,
  // replace_end) in the document, a backtick it opens included.
  uint32_t replace_begin = 0;
  uint32_t replace_end = 0;
  std::string prefix;  // that word, unquoted
  std::vector<Completion_item> items;
  bool incomplete = false;  // more items matched than were returned
  // What was found at the cursor, for tests and logs: "table", "column", ...
  Name_kind context = Name_kind::none;
  bool quoted = false;  // the word is in backticks: items bring their own
};

struct Completion_options {
  unsigned long version_id = k_default_version_id;
  bool upper_case_keywords = true;
  bool snippets = false;        // the client takes snippets: functions get ($0)
  // Lines a completion lays out (the FROM or JOIN a column brings) stay
  // within this many characters; 0: no limit.
  size_t line_width = 80;
  // One level of indentation in those lines ("\t", "  ", "    "); empty:
  // a tab when the line's indentation has tabs, else two spaces.
  std::string indent;
  // The FROM a column brings goes one level deeper than the first line of
  // its query's statement, as the formatter puts clauses (Format_options).
  bool indent_clauses = true;
  bool indent_routine_clauses = true;
  // A column of a table not in scope adds the table: FROM, or a JOIN; and
  // the tables to join are offered.
  bool add_tables = true;
  size_t max_items = 500;
};

// The statement the cursor at `offset` is in: the index of the statement
// containing it, or of the one whose text ends right at it (typing at the
// end of a statement with no delimiter yet).  nullopt between statements.
std::optional<size_t> statement_at(const Statement_index &index, uint32_t offset);

// The schema of the last `USE x` statement before statement `i`, or "".
std::string schema_in_use(std::string_view text, const Statement_index &index, size_t i);

// Completion at `offset` of `text`: keywords the parser can take there, and
// names from `catalogs` that fit what the cursor stands in (a table after
// FROM, a column of the tables in scope after SELECT, a.| the columns of a).
// Parses the cursor's statement on the calling thread with `cache`.
Completion_list complete(std::string_view text, const Statement_index &index, uint32_t offset,
                         const Catalog_view &catalogs, Parser_cache &cache,
                         const Completion_options &options = {});

// The terminals the parser could take after `sql` (a statement prefix), by
// grammar symbol; nullopt when the prefix itself has a syntax error before
// its end.  `parser` must not record derivations.
std::optional<std::vector<int>> expected_after(mdbp_parser *parser, std::string_view sql);

}  // namespace mdbls

#endif  // MDBLS_COMPLETION_H_
