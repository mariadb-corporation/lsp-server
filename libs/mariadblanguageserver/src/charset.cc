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

#include "mdbls/charset.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

#include "mdbls/statement_index.h"
#include "text_util.h"

namespace mdbls {

namespace {

void append_utf8(uint32_t cp, std::string *out) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// 0x80..0x9F of strings/ctype-latin1.c's cs_to_uni[]; the rest is identity.
constexpr uint16_t k_latin1_80_9f[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

bool is_word(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// The words of `s`, skipping blanks, quotes and a leading /*!NNNNN.
std::string next_word(std::string_view s, size_t *i) {
  while (*i < s.size() && !is_word(s[*i])) ++*i;
  const size_t start = *i;
  while (*i < s.size() && is_word(s[*i])) ++*i;
  return lower(s.substr(start, *i - start));
}

}  // namespace

std::optional<File_charset> parse_charset_name(std::string_view name) {
  const std::string n = lower(name);
  if (n == "utf8mb4" || n == "utf8" || n == "utf8mb3") return File_charset::utf8mb4;
  if (n == "latin1" || n == "cp1252") return File_charset::latin1;
  return std::nullopt;
}

std::optional<size_t> first_invalid_utf8(std::string_view s) {
  size_t i = 0;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      ++i;
      continue;
    }
    size_t n;
    uint32_t cp;
    if (c >= 0xC2 && c <= 0xDF) {
      n = 1;
      cp = c & 0x1F;
    } else if (c >= 0xE0 && c <= 0xEF) {
      n = 2;
      cp = c & 0x0F;
    } else if (c >= 0xF0 && c <= 0xF4) {
      n = 3;
      cp = c & 0x07;
    } else {
      return i;
    }
    if (i + n >= s.size()) return i;
    for (size_t k = 1; k <= n; ++k) {
      const auto cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) return i;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if ((n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
        (n == 3 && (cp < 0x10000 || cp > 0x10FFFF))) {
      return i;
    }
    i += n + 1;
  }
  return std::nullopt;
}

std::string latin1_to_utf8(std::string_view latin1) {
  std::string out;
  out.reserve(latin1.size() + latin1.size() / 16);
  for (const char ch : latin1) {
    const auto b = static_cast<unsigned char>(ch);
    if (b < 0x80) {
      out.push_back(ch);
    } else if (b < 0xA0) {
      append_utf8(k_latin1_80_9f[b - 0x80], &out);
    } else {
      append_utf8(b, &out);
    }
  }
  return out;
}

std::string utf8_to_latin1(std::string_view utf8) {
  std::string out;
  out.reserve(utf8.size());
  for (size_t i = 0; i < utf8.size();) {
    const auto b = static_cast<unsigned char>(utf8[i]);
    if (b < 0x80) {
      out.push_back(static_cast<char>(b));
      ++i;
      continue;
    }
    const size_t n = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
    uint32_t cp = b & (0x7F >> n);
    for (size_t k = 1; k < n && i + k < utf8.size(); ++k) {
      cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
    }
    i += n;
    if (cp >= 0xA0 && cp <= 0xFF) {
      out.push_back(static_cast<char>(cp));
      continue;
    }
    const auto *hit = std::find(std::begin(k_latin1_80_9f), std::end(k_latin1_80_9f), cp);
    out.push_back(hit != std::end(k_latin1_80_9f) ? static_cast<char>(0x80 + (hit - std::begin(k_latin1_80_9f)))
                                                  : '?');
  }
  return out;
}

std::vector<Charset_declaration> charset_declarations(std::string_view text, size_t before) {
  // The index splits byte-wise and every character set considered here is
  // ASCII-compatible, so it can run on text that is not UTF-8.
  const auto index = Statement_index::build(text.substr(0, std::min(before, text.size())));
  std::vector<Charset_declaration> found;
  for (const auto &st : index.statements()) {
    if (st.kind != Statement_kind::sql) continue;
    const std::string_view s = index.text_of(text, st);
    size_t i = 0;
    std::string w = next_word(s, &i);
    // /*!40101 SET NAMES latin1 */: the version number is a word of digits.
    if (!w.empty() && std::isdigit(static_cast<unsigned char>(w[0]))) w = next_word(s, &i);
    if (w != "set") continue;
    w = next_word(s, &i);
    if (w == "character") {
      if (next_word(s, &i) != "set") continue;
    } else if (w != "names" && w != "charset") {
      continue;
    }
    std::string name = next_word(s, &i);
    if (!name.empty() && name != "default") found.push_back({st.begin, name});
  }
  return found;
}

std::optional<std::string> declared_charset(std::string_view text, size_t before) {
  const auto all = charset_declarations(text, before);
  if (all.empty()) return std::nullopt;
  return all.back().name;
}

}  // namespace mdbls
