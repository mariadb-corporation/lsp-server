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

#ifndef MDBLS_TEXT_UTIL_H_
#define MDBLS_TEXT_UTIL_H_

#include <cctype>
#include <string>
#include <string_view>

// Case helpers for SQL keywords and names.  They use <cctype>, so they fold
// ASCII letters only in the "C" locale, which the server never changes.
// Private to the library.
namespace mdbls {

inline bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

inline std::string lower(std::string_view s) {
  std::string out(s);
  for (auto &c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

inline std::string upper(std::string_view s) {
  std::string out(s);
  for (auto &c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

// `text` starts with `word`, ignoring case, and no letter or digit follows.
inline bool starts_with_word(std::string_view text, std::string_view word) {
  if (text.size() < word.size() || !iequals(text.substr(0, word.size()), word)) return false;
  return text.size() == word.size() || !std::isalnum(static_cast<unsigned char>(text[word.size()]));
}

}  // namespace mdbls

#endif  // MDBLS_TEXT_UTIL_H_
