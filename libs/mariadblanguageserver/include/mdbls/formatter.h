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

#ifndef MDBLS_FORMATTER_H_
#define MDBLS_FORMATTER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mdbls/completion.h"
#include "mdbls/parser_pool.h"
#include "mdbls/statement_index.h"

namespace mdbls {

struct Format_options {
  unsigned long version_id = k_default_version_id;
  bool upper_case_keywords = true;
  // Lines stay within this many characters where a break can keep them
  // there; 0: no limit.
  size_t line_width = 80;
  std::string indent = "  ";  // one level of indentation
};

// The layout of one statement, parsed with `parser` (one that records
// derivations): the text that replaces [*begin, *end) of `text`, the
// statement's first token to its last, or nullopt when the statement is left
// as it is - it is not a query (SELECT, WITH, a parenthesized query), does
// not parse whole, holds a versioned comment, or the layout would not keep
// its tokens and comments.  [st_begin, st_end) is the statement's text.
//
// A query is laid out like this:
//  - Its clauses (FROM, WHERE, GROUP BY, HAVING, ORDER BY, LIMIT, ...) stay
//    on one line when the query fits there and the statement is on one line;
//    otherwise each starts a line of its own.
//  - Select, GROUP BY and ORDER BY lists continue after the comma on the
//    same line up to the width, then on the next, aligned with the first
//    item.
//  - A JOIN goes on a line of its own one level deeper than FROM (when the
//    FROM clause is too long, or the statement spans lines); ON stays on the
//    JOIN's line, or goes one level deeper on the next when it does not fit.
//  - AND and OR chains that do not fit break before each operator, one
//    level deeper.
//  - A subquery stays inline when it fits, else its clauses go on lines of
//    their own one level deeper, the parentheses on lines of their own.
//  - Keywords follow `upper_case_keywords`; names, strings and the spacing
//    inside expressions are kept; a line break inside other constructs
//    (CASE, function arguments) is kept, one level deeper.
//  - Comments stay before the token after them, on a line of their own when
//    they were.
std::optional<std::string> format_statement(std::string_view text, uint32_t st_begin, uint32_t st_end,
                                            mdbp_parser *parser, const Format_options &options,
                                            uint32_t *begin, uint32_t *end);

// The edits laying out the statements of `text` that overlap [begin, end)
// (whole statements), one per statement whose text changes.
std::vector<Text_edit> format_sql(std::string_view text, const Statement_index &index, uint32_t begin,
                                  uint32_t end, Parser_cache &cache, const Format_options &options = {});

}  // namespace mdbls

#endif  // MDBLS_FORMATTER_H_
