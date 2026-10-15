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

#include "mdbls/line_map.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace mdbls {

namespace {

// Width of the UTF-8 sequence a lead byte starts, in UTF-16 code units, or 0
// for a continuation byte.
inline uint32_t utf16_units(unsigned char b) {
  if ((b & 0xC0) == 0x80) return 0;  // continuation
  return b >= 0xF0 && b <= 0xF4 ? 2 : 1;
}

}  // namespace

uint32_t utf16_length(std::string_view utf8) {
  uint32_t n = 0;
  for (const char c : utf8) n += utf16_units(static_cast<unsigned char>(c));
  return n;
}

Line_map::Line_map(std::string_view text) {
  if (text.size() >= std::numeric_limits<uint32_t>::max()) {
    throw std::length_error("text of 4 GiB or more");
  }
  m_size = static_cast<uint32_t>(text.size());
  m_starts.push_back(0);
  const char *const begin = text.data();
  const char *const end = begin + text.size();

  if (std::memchr(begin, '\r', text.size()) == nullptr) {
    // Fast path: "\n" only.
    for (const char *p = begin;
         (p = static_cast<const char *>(std::memchr(p, '\n', end - p))) != nullptr;) {
      ++p;
      m_starts.push_back(static_cast<uint32_t>(p - begin));
    }
    return;
  }
  for (const char *p = begin; p < end; ++p) {
    if (*p == '\n') {
      m_starts.push_back(static_cast<uint32_t>(p + 1 - begin));
    } else if (*p == '\r') {
      if (p + 1 < end && p[1] == '\n') ++p;
      m_starts.push_back(static_cast<uint32_t>(p + 1 - begin));
    }
  }
}

uint32_t Line_map::line_start(uint32_t line) const {
  return line < m_starts.size() ? m_starts[line] : m_size;
}

uint32_t Line_map::line_of(uint32_t offset) const {
  const auto it = std::upper_bound(m_starts.begin(), m_starts.end(), offset);
  return static_cast<uint32_t>(it - m_starts.begin()) - 1;
}

uint32_t Line_map::content_end(std::string_view text, uint32_t line) const {
  uint32_t end = line + 1 < m_starts.size() ? m_starts[line + 1] : m_size;
  const uint32_t start = line_start(line);
  if (end > start && text[end - 1] == '\n') --end;
  if (end > start && text[end - 1] == '\r') --end;
  return end;
}

Position Line_map::position(std::string_view text, uint32_t offset) const {
  offset = std::min(offset, m_size);
  // Step back to the start of the character `offset` is in.
  while (offset > 0 && offset < m_size &&
         (static_cast<unsigned char>(text[offset]) & 0xC0) == 0x80) {
    --offset;
  }
  const uint32_t line = line_of(offset);
  const uint32_t start = m_starts[line];
  return {line, utf16_length(text.substr(start, offset - start))};
}

uint32_t Line_map::offset(std::string_view text, Position pos) const {
  if (pos.line >= m_starts.size()) return m_size;
  const uint32_t start = m_starts[pos.line];
  const uint32_t end = content_end(text, pos.line);
  uint32_t units = 0;
  uint32_t i = start;
  while (i < end) {
    const uint32_t w = utf16_units(static_cast<unsigned char>(text[i]));
    if (w != 0) {
      if (units + w > pos.character) break;  // inside this character
      units += w;
    }
    ++i;
    // Skip the rest of the character so `i` stays on a character start.
    while (i < end && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) ++i;
    if (units == pos.character) break;
  }
  return i;
}

}  // namespace mdbls
