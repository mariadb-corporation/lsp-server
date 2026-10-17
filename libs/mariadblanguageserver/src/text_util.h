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

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

inline bool istarts_with(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && iequals(s.substr(0, prefix.size()), prefix);
}

// Whether `needle` occurs in `hay`, ignoring case.  Searches with memchr for
// the first character's two cases, so a long text costs little.
inline bool icontains(std::string_view hay, std::string_view needle) {
  if (needle.empty()) return true;
  if (hay.size() < needle.size()) return false;
  const int lo = std::tolower(static_cast<unsigned char>(needle[0]));
  const int up = std::toupper(static_cast<unsigned char>(needle[0]));
  const char *p = hay.data();
  const char *end = hay.data() + hay.size() - needle.size() + 1;
  while (p < end) {
    const void *a = std::memchr(p, lo, static_cast<size_t>(end - p));
    const void *b = lo == up ? nullptr : std::memchr(p, up, static_cast<size_t>(end - p));
    const char *hit = static_cast<const char *>(a == nullptr ? b : b == nullptr ? a : std::min(a, b));
    if (hit == nullptr) return false;
    if (iequals(std::string_view(hit, needle.size()), needle)) return true;
    p = hit + 1;
  }
  return false;
}

// Runs of blanks become one space; leading and trailing blanks go.
inline std::string collapse(std::string_view s) {
  std::string out;
  bool space = false;
  for (const char c : s) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      space = !out.empty();
    } else {
      if (space) out.push_back(' ');
      space = false;
      out.push_back(c);
    }
  }
  return out;
}

// `s` without blanks at either end.
inline std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

// `name`, "name" (ANSI_QUOTES) and plain names, without their quotes:
// `a``b` -> a`b.
inline std::string unquote_identifier(std::string_view s) {
  if (s.size() >= 2 && (s.front() == '`' || s.front() == '"') && s.back() == s.front()) {
    const char q = s.front();
    std::string out;
    for (size_t i = 1; i + 1 < s.size(); ++i) {
      out.push_back(s[i]);
      if (s[i] == q && i + 2 < s.size() && s[i + 1] == q) ++i;  // doubled quote
    }
    return out;
  }
  return std::string(s);
}

// `v` joined with ", ".
inline std::string join(const std::vector<std::string> &v, std::string_view sep = ", ") {
  std::string out;
  for (const auto &x : v) {
    if (!out.empty()) out += sep;
    out += x;
  }
  return out;
}

// %XX decoding; nullopt for a '%' not followed by two hex digits.
inline std::optional<std::string> percent_decode(std::string_view s) {
  auto hex = [](char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '%') {
      out.push_back(s[i]);
      continue;
    }
    if (i + 2 >= s.size() || hex(s[i + 1]) < 0 || hex(s[i + 2]) < 0) return std::nullopt;
    out.push_back(static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2])));
    i += 2;
  }
  return out;
}

// Percent-encodes everything but RFC 3986's unreserved characters and `keep`.
inline std::string percent_encode(std::string_view s, std::string_view keep = {}) {
  static const char *hex = "0123456789ABCDEF";
  std::string out;
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' ||
        keep.find(ch) != std::string_view::npos) {
      out.push_back(ch);
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 15]);
    }
  }
  return out;
}

// `text` starts with `word`, ignoring case, and no letter or digit follows.
inline bool starts_with_word(std::string_view text, std::string_view word) {
  if (text.size() < word.size() || !iequals(text.substr(0, word.size()), word)) return false;
  return text.size() == word.size() || !std::isalnum(static_cast<unsigned char>(text[word.size()]));
}

}  // namespace mdbls

#endif  // MDBLS_TEXT_UTIL_H_
