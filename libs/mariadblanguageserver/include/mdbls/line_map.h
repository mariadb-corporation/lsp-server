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

#ifndef MDBLS_LINE_MAP_H_
#define MDBLS_LINE_MAP_H_

#include <cstdint>
#include <string_view>
#include <vector>

namespace mdbls {

// A position as the Language Server Protocol counts it: a 0-based line and a
// 0-based column in UTF-16 code units.
struct Position {
  uint32_t line = 0;
  uint32_t character = 0;

  bool operator==(const Position &) const = default;
};

// The start offset of every line of a text, for converting between byte
// offsets and LSP positions.  This is the one place offsets become lines and
// columns; everything else in the engine keeps byte offsets.
//
// Line breaks are "\n", "\r\n" and a lone "\r", as LSP defines them.  The
// text must be smaller than 4 GiB; offsets are 32-bit.
class Line_map {
 public:
  Line_map() : m_starts{0} {}
  explicit Line_map(std::string_view text);

  uint32_t line_count() const { return static_cast<uint32_t>(m_starts.size()); }

  // Byte offset of the first character of `line`; the text size for a line
  // past the end.
  uint32_t line_start(uint32_t line) const;

  // The line containing byte `offset`.  An offset inside a "\r\n" belongs to
  // the line the break ends.
  uint32_t line_of(uint32_t offset) const;

  // Byte offset to LSP position.  `text` must be the text the map was built
  // from.  An offset inside a multi-byte character maps to that character.
  Position position(std::string_view text, uint32_t offset) const;

  // LSP position to byte offset.  A line past the end gives the text size; a
  // character past the end of its line gives the line's end (before the line
  // break); a character in the middle of a surrogate pair gives the start of
  // that character.
  uint32_t offset(std::string_view text, Position position) const;

 private:
  // End of `line`'s content, before its line break.
  uint32_t content_end(std::string_view text, uint32_t line) const;

  std::vector<uint32_t> m_starts;  // m_starts[0] == 0
  uint32_t m_size = 0;
};

// The number of UTF-16 code units `utf8` takes.  Bytes that are not valid
// UTF-8 count one unit each.
uint32_t utf16_length(std::string_view utf8);

}  // namespace mdbls

#endif  // MDBLS_LINE_MAP_H_
