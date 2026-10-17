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

#ifndef MDBLS_OUTLINE_H_
#define MDBLS_OUTLINE_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mdbls/catalog.h"
#include "mdbls/line_map.h"
#include "mdbls/parser_pool.h"
#include "mdbls/statement_index.h"

namespace mdbls {

struct Outline {
  Catalog catalog;
  size_t ddl_statements = 0;  // statements that may change the catalog
  size_t applied = 0;         // of those, the ones that did
  // (statement offset, message): DDL that could not be applied
  std::vector<std::pair<uint32_t, std::string>> notes;
};

struct Outline_options {
  unsigned long version_id = k_default_version_id;
  int priority = 0;
};

// Parses the statements of `index` that may change the catalog (CREATE,
// ALTER, DROP, RENAME, USE) on `pool`, then folds them into a Catalog in
// file order.  Everything else, such as a dump's INSERTs, is not parsed.
Outline build_outline(std::string_view text, const Statement_index &index, Parser_pool &pool,
                      const Outline_options &options = {});

// The outline as JSON, format 1; positions are LSP positions (0-based line,
// UTF-16 column).  docs/outline-format.md describes it.
std::string outline_json(const Outline &outline, std::string_view text, const Line_map &lines,
                         std::string_view file = {});

}  // namespace mdbls

#endif  // MDBLS_OUTLINE_H_
