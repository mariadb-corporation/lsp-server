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
  // Lines stay within this many characters, counted from the start of the
  // line (the statement's indentation included), where a break can keep
  // them there; 0: no limit.
  size_t line_width = 80;
  std::string indent = "  ";  // one level of indentation
  // A query's or DML statement's lines after its first (its clauses, and
  // what they hold) one level deeper, at the top level of the text and in
  // routine bodies.
  bool indent_clauses = true;
  bool indent_routine_clauses = true;
};

// The edits laying out the statements of `text` that overlap [begin, end)
// (whole statements), one per statement whose text changes, in order.  A
// statement is laid out when it is a query (SELECT, WITH, a parenthesized
// query), one built around a query or clauses (INSERT or REPLACE ...
// SELECT, UPDATE, DELETE, CREATE or ALTER VIEW), CREATE TABLE with its
// columns, ALTER TABLE, a routine (CREATE PROCEDURE, FUNCTION, TRIGGER,
// EVENT) or a compound statement (BEGIN NOT ATOMIC, IF, CASE, a loop); it
// is left as it is when it is none of those,
// does not parse whole, holds a versioned comment, or the layout would not
// keep its tokens and comments.
//
// A query is laid out like this:
//  - Its clauses (FROM, WHERE, GROUP BY, HAVING, ORDER BY, LIMIT, ...) stay
//    on one line when the query fits there and the statement is on one line;
//    otherwise each starts a line of its own, one level deeper than the
//    statement's first line (`indent_clauses`, `indent_routine_clauses`),
//    or at its level.
//  - The other statements' clauses do the same: the query after INSERT INTO
//    t (...) and CREATE VIEW v AS, SET, ON DUPLICATE KEY UPDATE, RETURNING,
//    WITH CHECK OPTION, an UPDATE's or DELETE's WHERE, ORDER BY, LIMIT.
//  - CREATE TABLE puts each column, key and constraint on a line of its
//    own, one level deeper, and the closing parenthesis on the next; its
//    partitions and subpartitions likewise; its options fill the line, then
//    continue one level deeper.  ALTER TABLE puts several changes one per
//    line.
//  - A routine's header stays on its line, its parameters too when they
//    fit (else one per line, one level deeper); its characteristics, or an
//    event's clauses, go one level deeper on lines of their own when the
//    statement breaks; a BEGIN ... END body starts a line at the header's
//    level, another statement one level deeper, an event's after DO.
//  - In a block, IF, CASE or loop each statement goes on a line of its own
//    one level deeper, laid out by these rules on its own; END, ELSE,
//    ELSEIF and UNTIL back at the block's level, a CASE's WHEN arms one
//    level deeper; THEN or DO after a condition that breaks starts a line.
//    An empty line between statements stays.
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
//  - The lines end as the document's do: "\r\n" when it has one, else "\n".
std::vector<Text_edit> format_sql(std::string_view text, const Statement_index &index, uint32_t begin,
                                  uint32_t end, Parser_cache &cache, const Format_options &options = {});

}  // namespace mdbls

#endif  // MDBLS_FORMATTER_H_
