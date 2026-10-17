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

#ifndef MDBLS_LSP_JSON_POSITION_H_
#define MDBLS_LSP_JSON_POSITION_H_

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cstdint>
#include <string_view>

#include "mdbls/line_map.h"

// LSP Position and Range objects, written as JSON.  Needs RapidJSON on the
// include path of whoever includes it.
namespace mdbls::lsp {

using Json_writer = rapidjson::Writer<rapidjson::StringBuffer>;

// {"line": ..., "character": ...}
void write_position(Json_writer &w, Position p);

// {"start": ..., "end": ...} of the bytes [begin, end) of `text`, which
// `lines` was built from.  end <= begin gives an empty range at begin.
void write_range(Json_writer &w, std::string_view text, const Line_map &lines, uint32_t begin,
                 uint32_t end);

}  // namespace mdbls::lsp

#endif  // MDBLS_LSP_JSON_POSITION_H_
