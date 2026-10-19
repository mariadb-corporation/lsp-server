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
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mdbls/catalog.h"
#include "mdbls/line_map.h"
#include "mdbls/parser_pool.h"
#include "mdbls/source_include.h"
#include "mdbls/statement_index.h"

namespace mdbls {

struct Outline {
  Catalog catalog;
  size_t ddl_statements = 0;  // statements that may change the catalog
  size_t applied = 0;         // of those, the ones that did
  // (statement offset, message): DDL that could not be applied
  std::vector<std::pair<uint32_t, std::string>> notes;
  // The SOURCE directives of a document's outline: an origin with source k
  // points into the text of includes[k - 1].
  std::vector<Source_include> includes;
  // The expanded text it was folded from, with the way back (map()), when
  // there were directives: what references across the includes search.
  struct Expanded {
    std::string text;
    std::vector<Source_expansion::Segment> segments;
    std::shared_ptr<const Statement_index> index;
  };
  std::shared_ptr<const Expanded> expanded;

  Source_expansion::Located_range map(Range range) const {
    return expanded ? map_source(expanded->segments, includes, range)
                    : Source_expansion::Located_range{0, range};
  }
};

struct Outline_options {
  unsigned long version_id = k_default_version_id;
  int priority = 0;
  std::string schema;  // in use before the first statement (build_outline)
};

// Parses the statements of `index` that may change the catalog (CREATE,
// ALTER, DROP, RENAME, USE) on `pool`, then folds them into a Catalog in
// file order.  Everything else, such as a dump's INSERTs, is not parsed.
Outline build_outline(std::string_view text, const Statement_index &index, Parser_pool &pool,
                      const Outline_options &options = {});

// The same, folding into `outline` after what it holds already: one more
// file of a script spread over several.  `source` is what the new origins
// record (Origin::source).
void fold_outline(Outline &outline, std::string_view text, const Statement_index &index,
                  Parser_pool &pool, const Outline_options &options = {}, uint32_t source = 0);

// build_outline for a text with SOURCE directives: `expansion` is
// expand_sources() of it, `index_options` the options its index was built
// with.  The expanded text is indexed and folded, every origin and note is
// mapped back to where it came from, and the includes go to
// Outline::includes.
Outline build_outline(Source_expansion expansion, Parser_pool &pool,
                      const Index_options &index_options, const Outline_options &options = {});

// fold_outline for an expanded text: origins in the including text get
// `source`, those in include k get include_sources[k - 1].  A note on an
// included statement moves to its directive and names the file and line;
// an include that could not be read, and a directive msm does not expand
// because it is in an included text, get a note at their directive too.
void fold_outline(Outline &outline, const Source_expansion &expansion, Parser_pool &pool,
                  const Index_options &index_options, const Outline_options &options,
                  uint32_t source, const std::vector<uint32_t> &include_sources);

// The outline as JSON, format 1; positions are LSP positions (0-based line,
// UTF-16 column).  docs/outline-format.md describes it.
std::string outline_json(const Outline &outline, std::string_view text, const Line_map &lines,
                         std::string_view file = {});

}  // namespace mdbls

#endif  // MDBLS_OUTLINE_H_
