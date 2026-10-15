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

// Parses every SQL statement of `index` on `pool` and returns one entry per
// statement of the index, comments included (left unchecked).  `text` must
// outlive the call; the call waits for its own jobs.
std::vector<Statement_check> check_statements(std::string_view text,
                                              const Statement_index &index,
                                              Parser_pool &pool,
                                              const Check_options &options = {});

}  // namespace mdbls

#endif  // MDBLS_SYNTAX_CHECK_H_
