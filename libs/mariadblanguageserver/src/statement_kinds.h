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

// Kinds of statements, by their parse tree, that the formatter and
// completion both go by.

#ifndef MDBLS_STATEMENT_KINDS_H_
#define MDBLS_STATEMENT_KINDS_H_

#include <cstdint>

#include "mdbls/syntax_tree.h"

namespace mdbls {

// Whether node `n`, a verb_clause's child, is a statement whose clauses
// (FROM, WHERE, SET, ...) start lines of their own one level deeper than
// its first line: a query, INSERT, REPLACE, UPDATE, DELETE, EXPLAIN or
// ANALYZE of one, CREATE or ALTER VIEW or USER, CREATE TABLE ... AS SELECT
// without columns, GRANT, REVOKE.
bool has_clauses(const Syntax_tree &tree, uint32_t n);

}  // namespace mdbls

#endif  // MDBLS_STATEMENT_KINDS_H_
