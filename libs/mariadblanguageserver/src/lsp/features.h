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

#ifndef MDBLS_LSP_FEATURES_H_
#define MDBLS_LSP_FEATURES_H_

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cstdint>
#include <span>
#include <vector>

#include "mdbls/document.h"
#include "mdbls/outline.h"
#include "mdbls/parser_pool.h"

// The LSP results computed from a snapshot, written as JSON.  Private to the
// server.
namespace mdbls::lsp {

using Json_writer = rapidjson::Writer<rapidjson::StringBuffer>;

void write_position(Json_writer &w, Position p);
void write_range(Json_writer &w, const Snapshot &s, uint32_t begin, uint32_t end);

// textDocument/documentSymbol: DocumentSymbol[] from the outline, schema >
// table > column, index, ...
void document_symbols(Json_writer &w, const Snapshot &s, const Outline &outline);

// textDocument/foldingRange: every statement and run of comment lines over
// more than one line, and the compound blocks inside routines.
void folding_ranges(Json_writer &w, const Snapshot &s, Parser_cache &cache);

// textDocument/selectionRange: for each position the chain of syntax nodes
// around it, then its statement, then the whole document.
void selection_ranges(Json_writer &w, const Snapshot &s, std::span<const Position> positions,
                      Parser_cache &cache);

// Semantic tokens of the statements overlapping [begin, end), encoded.
// The statements are lexed in parallel on `pool` at `priority`; the call
// waits for them, so it must not run on `pool` itself.
std::vector<uint32_t> semantic_tokens(const Snapshot &s, uint32_t begin, uint32_t end,
                                      Parser_pool &pool, int priority);

}  // namespace mdbls::lsp

#endif  // MDBLS_LSP_FEATURES_H_
