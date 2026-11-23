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

#include "column_type.h"

#include <array>
#include <cstdint>
#include <utility>

#include "text_util.h"

namespace mdbls {

namespace {

// MariaDB 13.1's information_schema.CHARACTER_SETS: name, MAXLEN.
constexpr std::array<std::pair<std::string_view, int>, 40> k_charsets{{
    {"armscii8", 1}, {"ascii", 1},    {"big5", 2},    {"binary", 1},  {"cp1250", 1},
    {"cp1251", 1},   {"cp1256", 1},   {"cp1257", 1},  {"cp850", 1},   {"cp852", 1},
    {"cp866", 1},    {"cp932", 2},    {"dec8", 1},    {"eucjpms", 3}, {"euckr", 2},
    {"gb2312", 2},   {"gbk", 2},      {"geostd8", 1}, {"greek", 1},   {"hebrew", 1},
    {"hp8", 1},      {"keybcs2", 1},  {"koi8r", 1},   {"koi8u", 1},   {"latin1", 1},
    {"latin2", 1},   {"latin5", 1},   {"latin7", 1},  {"macce", 1},   {"macroman", 1},
    {"sjis", 2},     {"swe7", 1},     {"tis620", 1},  {"ucs2", 2},    {"ujis", 3},
    {"utf16", 4},    {"utf16le", 4},  {"utf32", 4},   {"utf8mb3", 3}, {"utf8mb4", 4},
}};

// utf8 is utf8mb3 (old_mode UTF8_IS_UTF8MB3, the default).
std::string canonical_charset(std::string_view name) {
  std::string n = lower(name);
  if (n == "utf8") return "utf8mb3";
  return n;
}

// A charset name, or a collation's charset; "" when neither says.
std::string charset_or_collation(std::string_view name) {
  if (name.empty() || iequals(name, "default")) return "";
  if (charset_mbmaxlen(name) > 0) return canonical_charset(name);
  return charset_of_collation(name);
}

// The digits of a length or precision, nullopt past 2^64.
std::optional<uint64_t> number(std::string_view s) {
  if (s.empty()) return std::nullopt;
  uint64_t v = 0;
  for (const char c : s) {
    if (c < '0' || c > '9') return std::nullopt;
    const uint64_t d = static_cast<uint64_t>(c - '0');
    if (v > (UINT64_MAX - d) / 10) return std::nullopt;
    v = v * 10 + d;
  }
  return v;
}

constexpr uint64_t k_max_length = 4294967295;  // UINT32_MAX, the server's limit on (N)
constexpr std::string_view k_compressed = " /*M!100301 COMPRESSED*/";

// tiny/""/medium/long by a byte length, as the server picks TEXT(n) and
// BLOB(n); "" past 4 GB.
std::string_view lob_size(uint64_t bytes) {
  if (bytes <= 255) return "tiny";
  if (bytes <= 65535) return "";
  if (bytes <= 16777215) return "medium";
  return "long";
}

// A value as the server writes it in ENUM(...) / SET(...): append_unescaped().
std::string quote_value(std::string_view v) {
  std::string out = "'";
  for (const char c : v) {
    switch (c) {
      case '\0': out += "\\0"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\\': out += "\\\\"; break;
      case '\'': out += "''"; break;
      default: out.push_back(c); break;
    }
  }
  out += '\'';
  return out;
}

bool has_non_ascii(std::string_view s) {
  for (const char c : s) {
    if (static_cast<unsigned char>(c) >= 0x80) return true;
  }
  return false;
}

// ENUM / SET values as the server keeps them, nullopt when it would refuse
// them or when that depends on a collation the script does not name.
std::optional<std::vector<std::string>> member_values(const Type_spec &t, bool binary,
                                                      bool case_sensitive) {
  if (!t.values || t.values->empty()) return std::nullopt;
  std::vector<std::string> out;
  for (std::string v : *t.values) {
    if (t.base == "set" && v.find(',') != std::string::npos) return std::nullopt;
    if (binary) {
      if (!v.empty() && v.back() == ' ') return std::nullopt;  // not checked on the server
    } else {
      while (!v.empty() && v.back() == ' ') v.pop_back();  // trailing spaces go
    }
    out.push_back(std::move(v));
  }
  // Duplicates are refused under the column's collation. Which non-ASCII
  // values a case- and accent-insensitive collation equates is not checked here.
  size_t non_ascii = 0;
  for (size_t i = 0; i < out.size(); ++i) {
    if (has_non_ascii(out[i])) ++non_ascii;
    for (size_t j = 0; j < i; ++j) {
      if (case_sensitive ? out[i] == out[j] : iequals(out[i], out[j])) return std::nullopt;
    }
  }
  if (!case_sensitive && non_ascii > 0 && out.size() > 1) return std::nullopt;
  return out;
}

bool collation_is_case_sensitive(std::string_view collation) {
  const std::string c = lower(collation);
  return c == "binary" || c.ends_with("_bin") || c.ends_with("_cs") || c.ends_with("_as_cs");
}

}  // namespace

int charset_mbmaxlen(std::string_view charset) {
  const std::string n = canonical_charset(charset);
  for (const auto &[name, len] : k_charsets) {
    if (name == n) return len;
  }
  return 0;
}

std::string charset_of_collation(std::string_view collation) {
  const std::string c = lower(collation);
  if (c == "binary") return "binary";
  if (c.starts_with("utf8_")) return "utf8mb3";
  std::string_view best;
  for (const auto &[name, len] : k_charsets) {
    if (c.size() > name.size() && c.starts_with(name) && c[name.size()] == '_' &&
        name.size() > best.size()) {
      best = name;
    }
  }
  return std::string(best);
}

std::optional<std::string> column_type(const Type_spec &t, std::string_view default_charset) {
  const std::string &b = t.base;
  if (b.empty()) return std::nullopt;

  std::vector<uint64_t> a;
  for (const auto &s : t.args) {
    const auto v = number(s);
    if (!v) return std::nullopt;
    a.push_back(*v);
  }
  if (a.size() > 2) return std::nullopt;
  const bool has_length = a.size() == 1;
  const bool has_precision = a.size() == 2;

  // A COLLATE must belong to the CHARACTER SET; one whose name has no
  // charset (uca1400_ai_ci) fits the Unicode charsets.
  if (t.charset && t.collation && !iequals(*t.collation, "default")) {
    const std::string named = charset_or_collation(*t.charset);
    const std::string of_collation = charset_of_collation(*t.collation);
    if (!of_collation.empty() ? of_collation != named
                              : named != "utf8mb3" && named != "utf8mb4" && named != "ucs2" &&
                                    named != "utf16" && named != "utf16le" && named != "utf32") {
      return std::nullopt;
    }
  }

  // The charset in force: the type's own, its collation's, else the default.
  std::string cs;
  if (t.charset) cs = charset_or_collation(*t.charset);
  if (cs.empty() && t.collation) cs = charset_of_collation(*t.collation);
  if (cs.empty()) cs = charset_or_collation(default_charset);
  const bool binary = cs == "binary";
  const int mb = binary ? 1 : charset_mbmaxlen(cs);  // 0: not known
  const bool case_sensitive =
      binary || (t.collation && collation_is_case_sensitive(*t.collation)) ||
      (!t.collation && !t.charset && collation_is_case_sensitive(default_charset));

  const std::string sign = t.zerofill ? " unsigned zerofill" : t.is_unsigned ? " unsigned" : "";
  const std::string compressed = t.compressed ? std::string(k_compressed) : "";
  auto with_n = [](std::string_view name, uint64_t n) {
    return std::string(name) + "(" + std::to_string(n) + ")";
  };

  // Integers: the display width, the server's default by type and sign.
  struct Int_width {
    std::string_view name;
    uint64_t signed_width, unsigned_width;
  };
  constexpr Int_width k_ints[] = {{"tinyint", 4, 3},  {"smallint", 6, 5}, {"mediumint", 9, 8},
                                  {"int", 11, 10},    {"bigint", 20, 20}};
  for (const auto &i : k_ints) {
    if (b != i.name) continue;
    if (has_precision) return std::nullopt;
    uint64_t w = has_length ? a[0] : 0;
    if (w == 0) w = t.is_unsigned || t.zerofill ? i.unsigned_width : i.signed_width;
    if (w > 255) return std::nullopt;
    return with_n(b, w) + sign;
  }
  if (b == "boolean") return a.empty() ? std::optional<std::string>("tinyint(1)") : std::nullopt;
  if (b == "serial") return a.empty() ? std::optional<std::string>("bigint(20) unsigned") : std::nullopt;
  if (b == "bit") {
    uint64_t w = has_length ? a[0] : 1;
    if (has_precision || w > 64) return std::nullopt;
    if (w == 0) w = 1;
    return with_n("bit", w);
  }

  if (b == "float" || b == "double") {
    std::string name = b;
    if (has_length) {  // FLOAT(p): single precision up to 24, double up to 53
      if (b != "float" || a[0] > 53) return std::nullopt;
      name = a[0] <= 24 ? "float" : "double";
      return name + sign;
    }
    if (has_precision) {
      const uint64_t m = a[0], d = a[1];
      if (m > 255 || d > 30 || m < d) return std::nullopt;
      if (m == 0) return name + sign;  // (0,0)
      return name + "(" + std::to_string(m) + "," + std::to_string(d) + ")" + sign;
    }
    return name + sign;
  }
  if (b == "decimal") {
    uint64_t m = 10, d = 0;
    if (has_length) m = a[0] == 0 ? 10 : a[0];
    if (has_precision) {
      if (a[0] == 0 && a[1] != 0) return std::nullopt;  // not checked on the server
      m = a[0] == 0 ? 10 : a[0];
      d = a[1];
    }
    if (m > 65 || d > 38 || m < d) return std::nullopt;
    return "decimal(" + std::to_string(m) + "," + std::to_string(d) + ")" + sign;
  }

  if (b == "year") {
    if (has_precision || (has_length && a[0] != 4)) return std::nullopt;
    return "year(4)";
  }
  if (b == "date") return a.empty() ? std::optional<std::string>("date") : std::nullopt;
  if (b == "time" || b == "timestamp" || b == "datetime") {
    if (has_precision) return std::nullopt;
    const uint64_t p = has_length ? a[0] : 0;
    if (p > 6) return std::nullopt;
    return p == 0 ? b : with_n(b, p);
  }

  if (b == "char" || b == "binary") {
    const uint64_t n = has_length ? a[0] : 1;
    if (has_precision || n > 255) return std::nullopt;
    return with_n(b == "binary" || binary ? "binary" : "char", n);
  }
  if (b == "varchar" || b == "varbinary") {
    if (!has_length) return std::nullopt;
    const uint64_t n = a[0];
    // At most 65532 bytes; without a known charset, what fits in any.
    const bool bytes = b == "varbinary" || binary;
    const uint64_t max = bytes ? 65532 : mb > 0 ? 65532 / static_cast<uint64_t>(mb) : 65532 / 4;
    if (n > max) return std::nullopt;
    return with_n(bytes ? "varbinary" : "varchar", n) + compressed;
  }
  if (b == "blob" || b == "text") {
    if (has_precision) return std::nullopt;
    const bool bytes = b == "blob" || binary;
    const std::string_view kind = bytes ? "blob" : "text";
    if (!has_length || a[0] == 0) return std::string(kind) + compressed;
    const uint64_t n = a[0];
    if (n > k_max_length) return std::nullopt;
    if (bytes) return std::string(lob_size(n)) + std::string(kind) + compressed;
    if (mb > 0) return std::string(lob_size(n * static_cast<uint64_t>(mb))) + "text" + compressed;
    // No charset known: only when every charset gives the same size.
    const std::string_view size = lob_size(n);
    for (const uint64_t m : {2u, 3u, 4u}) {
      if (lob_size(n * m) != size) return std::nullopt;
    }
    return std::string(size) + "text" + compressed;
  }
  for (const std::string_view size : {"tiny", "medium", "long"}) {
    if (b == std::string(size) + "blob" || b == std::string(size) + "text") {
      if (!a.empty()) return std::nullopt;
      return std::string(size) + (binary || b.ends_with("blob") ? "blob" : "text") + compressed;
    }
  }
  if (b == "json") return a.empty() ? std::optional<std::string>("longtext" + compressed) : std::nullopt;

  if (b == "enum" || b == "set") {
    const auto values = member_values(t, binary, case_sensitive);
    if (!values) return std::nullopt;
    std::string out = b + "(";
    for (size_t i = 0; i < values->size(); ++i) {
      if (i > 0) out += ',';
      out += quote_value((*values)[i]);
    }
    return out + ")";
  }

  // Type plugins MariaDB ships. They take no CHARACTER SET, COLLATE,
  // UNSIGNED or COMPRESSED.
  if (t.charset || t.collation || t.is_unsigned || t.zerofill || t.compressed) return std::nullopt;
  if (b == "vector") {
    if (!has_length || a[0] == 0 || a[0] > 16383) return std::nullopt;
    return with_n("vector", a[0]);
  }
  constexpr std::string_view k_spatial[] = {"geometry",   "point",           "linestring",
                                            "polygon",    "multipoint",      "multilinestring",
                                            "multipolygon", "geometrycollection"};
  for (const auto s : k_spatial) {
    if (b == s) return b;  // a length is ignored: POINT(5) is point
  }
  constexpr std::string_view k_plain[] = {"uuid", "inet4", "inet6", "xmltype"};
  for (const auto s : k_plain) {
    if (b == s) return a.empty() ? std::optional<std::string>(b) : std::nullopt;
  }
  return std::nullopt;
}

}  // namespace mdbls
