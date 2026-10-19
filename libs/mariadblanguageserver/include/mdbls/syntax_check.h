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

#ifndef MDBLS_SYNTAX_CHECK_H_
#define MDBLS_SYNTAX_CHECK_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "mdbls/parser_pool.h"
#include "mdbls/statement_index.h"

namespace mdbls {

enum class Check_status : uint8_t {
  unchecked,         // a comment, or not checked yet
  ok,                // the parser accepts the statement
  syntax_error,      // the parser rejects it: the SQL is invalid
  invalid_utf8,      // the statement is not valid UTF-8 and was not lexed
  unsupported_mode,  // its sql_mode (ORACLE, MSSQL) is one the parser refuses
  internal_error,    // the parser could not finish
  source_directive,  // an MSM SOURCE directive (source_include.h), not SQL
};

// The verdict on one statement.  For a syntax error, `error_offset` is the
// absolute byte offset of the offending token (the statement's end when the
// error is at its end), `error_token` its terminal name ("$end" at the end)
// and `expected` the terminals that could have stood there, sorted by name.
struct Statement_check {
  Check_status status = Check_status::unchecked;
  uint32_t error_offset = 0;
  uint32_t error_length = 0;
  std::string error_token;
  std::vector<std::string> expected;
};

struct Check_options {
  unsigned long version_id = k_default_version_id;  // release id for versioned comments
  int priority = 0;                   // priority of the jobs in the pool
  size_t chunk_bytes = 1u << 20;      // statements per job: about this much text
  bool source_directives = true;      // see check_statement(); false: plain SQL
};

// Parses statement `i` of `index` with a parser from `cache`.  A comment
// statement is left unchecked.  A statement that is an MSM SOURCE directive
// is not parsed; one with directive lines inside, such as a procedure whose
// body includes a file, is parsed with those lines blanked: what a file
// brings in is checked where that file is open.
Statement_check check_statement(std::string_view text, const Statement_index &index, size_t i,
                                Parser_cache &cache, unsigned long version_id,
                                bool source_directives = true);

// A terminal name as a reader would write it: SELECT_SYM -> SELECT, '(' ->
// (, IDENT -> identifier.  Empty for terminals nobody types.
std::string readable_terminal(std::string_view name);

// The expected terminals of a check, readable, without duplicates:
// punctuation and word classes first, then keywords.
std::vector<std::string> readable_expected(const std::vector<std::string> &expected);

// "syntax error near 'FORM'", or "... near end of statement".
std::string syntax_error_near(std::string_view text, const Statement_check &check);

// "syntax error near 'FORM'; expected identifier or one of 12 keywords":
// the message for a syntax_error check of a statement in `text`.  Keywords
// are listed when there are at most `max_keywords` of them, else counted.
std::string syntax_error_message(std::string_view text, const Statement_check &check,
                                 size_t max_keywords = 8);

// "single-quoted string", "comment", ...: what an unterminated construct at
// the end of a text is called; nullptr for none.
const char *open_context_name(Open_context c);

// Says why a statement was not checked, to be fed the checks of an index in
// order.  An unsupported sql_mode is reported once, at the first statement
// of each run in that mode.
class Unchecked_notes {
 public:
  explicit Unchecked_notes(const Statement_index &index) : m_index(index) {}

  // "statement not checked: ...", or empty when statement `i` has nothing
  // to report.
  std::string operator()(size_t i, Check_status status);

 private:
  const Statement_index &m_index;
  std::string m_last_unsupported;
};

// Parses every SQL statement of `index` on `pool` and returns one entry per
// statement of the index, comments included (left unchecked).  `text` must
// outlive the call; the call waits for its own jobs.
std::vector<Statement_check> check_statements(std::string_view text,
                                              const Statement_index &index,
                                              Parser_pool &pool,
                                              const Check_options &options = {});

}  // namespace mdbls

#endif  // MDBLS_SYNTAX_CHECK_H_
