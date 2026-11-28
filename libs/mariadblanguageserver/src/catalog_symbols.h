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

#ifndef MDBLS_SRC_CATALOG_SYMBOLS_H_
#define MDBLS_SRC_CATALOG_SYMBOLS_H_

#include "mdbls/syntax_tree.h"

namespace mdbls {

// The grammar symbols of catalog_symbols.inc, resolved once
// (catalog_missing_grammar_symbols() checks that the grammar has each).
struct Catalog_symbols {
#define SYM(x) int x = grammar::symbol(#x);
#include "catalog_symbols.inc"
#undef SYM
};

const Catalog_symbols &catalog_symbols();

}  // namespace mdbls

#endif  // MDBLS_SRC_CATALOG_SYMBOLS_H_
