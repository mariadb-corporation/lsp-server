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
  unsigned long version_id = 130100;  // release id for versioned comments
  int priority = 0;                   // priority of the jobs in the pool
  size_t chunk_bytes = 1u << 20;      // statements per job: about this much text
};

// Parses statement `i` of `index` with a parser from `cache`.  A comment
// statement is left unchecked.
Statement_check check_statement(std::string_view text, const Statement_index &index, size_t i,
                                Parser_cache &cache, unsigned long version_id);

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

// Parses every SQL statement of `index` on `pool` and returns one entry per
// statement of the index, comments included (left unchecked).  `text` must
// outlive the call; the call waits for its own jobs.
std::vector<Statement_check> check_statements(std::string_view text,
                                              const Statement_index &index,
                                              Parser_pool &pool,
                                              const Check_options &options = {});

}  // namespace mdbls

#endif  // MDBLS_SYNTAX_CHECK_H_
