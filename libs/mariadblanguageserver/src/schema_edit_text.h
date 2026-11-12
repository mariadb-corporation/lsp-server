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

#ifndef MDBLS_SCHEMA_EDIT_TEXT_H_
#define MDBLS_SCHEMA_EDIT_TEXT_H_

// The SQL text schema edits write (schema_edit.cc): definitions made from
// the outline's structs, in the style of the file they go into.

#include <string>
#include <string_view>
#include <vector>

#include "mdbls/catalog.h"

namespace mdbls::edit_text {

// A character of a name or keyword: letters, digits, _, $ and non-ASCII.
bool is_word_char(char c);

// A word of a statement as the subject scanner sees it.
struct Word {
  std::string_view text;
  bool quoted = false;  // a quoted name
  bool punct = false;   // one punctuation character
};

// The first `max` words of `sql`: names, keywords, quoted names, and single
// punctuation characters.  Comments and strings are skipped; a versioned
// comment's text is read as if it were not commented.
std::vector<Word> words_of(std::string_view sql, size_t max);

// How the file being edited writes SQL.
struct Style {
  bool backticks = true;  // names in backticks, else quoted only when needed
  bool upper = true;      // keywords in upper case
  std::string eol = "\n";
  bool backslash_escapes = true;  // false under NO_BACKSLASH_ESCAPES

  std::string id(std::string_view name) const;
  // `keyword` (given in upper case) in the file's case.
  std::string kw(std::string_view keyword) const;
};

// Whether `name` is a type plugin MariaDB ships: the spatial types, INET4,
// INET6, UUID, VECTOR, XMLTYPE.  The grammar reads any other name in their
// place too (udt_name), so a misspelt type parses.
bool plugin_type(std::string_view name);

// A string literal: in single quotes, quotes doubled, backslashes (and a
// NUL) escaped unless `backslash_escapes` is false (NO_BACKSLASH_ESCAPES).
std::string sql_string(std::string_view value, bool backslash_escapes = true);

// The column-level CHECKs of column `column` of `t`.
std::vector<Check> column_checks(const Table &t, std::string_view column);

// `name type ...`: the column's whole definition, its column-level CHECKs
// (`checks`, those whose `column` is this column) at the end.
std::string column_sql(const Column &c, const std::vector<Check> &checks, const Style &st);
// `a`, `b`: the names quoted, comma-separated.
std::string ids(const std::vector<std::string> &names, const Style &st);
// PRIMARY KEY (...), [UNIQUE|FULLTEXT|SPATIAL|VECTOR] INDEX name (...) ...
std::string index_sql(const Index &ix, const Style &st);
// [CONSTRAINT name] FOREIGN KEY [index] (...) REFERENCES t (...) ...
std::string foreign_key_sql(const Foreign_key &fk, const Style &st);
// [CONSTRAINT name] CHECK (expr)
std::string check_sql(const Check &ck, const Style &st);
// PERIOD FOR SYSTEM_TIME (s, e) / PERIOD FOR p (s, e)
std::string period_sql(const Period &p, const Style &st);
// One table option, under its outline name: ENGINE=InnoDB, COMMENT='x'.
std::string table_option_sql(std::string_view name, std::string_view value, const Style &st);

// The statements, on one line, without a delimiter.
std::string create_table_sql(const Table &t, const Style &st);
// `or_replace`: CREATE OR REPLACE.
std::string create_view_sql(const View &v, const Style &st, bool or_replace = false);
std::string create_routine_sql(const Routine &r, const Style &st, bool or_replace = false);
std::string create_trigger_sql(const Trigger &t, const Style &st, bool or_replace = false);
std::string create_event_sql(const Event &e, const Style &st, bool or_replace = false);
std::string create_sequence_sql(const Sequence &q, const Style &st);

}  // namespace mdbls::edit_text

#endif  // MDBLS_SCHEMA_EDIT_TEXT_H_
