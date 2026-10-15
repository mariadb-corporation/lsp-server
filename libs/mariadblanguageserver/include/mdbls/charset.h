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

#ifndef MDBLS_CHARSET_H_
#define MDBLS_CHARSET_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mdbls {

// The character sets a file read from disk can be in.  The parser lexes
// UTF-8 (utf8mb4) only; a latin1 file is converted to UTF-8 first.
enum class File_charset { utf8mb4, latin1 };

// "utf8mb4", "utf8", "utf8mb3" -> utf8mb4; "latin1", "cp1252" -> latin1;
// case-insensitive.  nullopt for anything else.
std::optional<File_charset> parse_charset_name(std::string_view name);

// Offset of the first byte that is not part of valid UTF-8 (no overlong
// forms, no surrogates, nothing above U+10FFFF); nullopt when all of `text`
// is valid.
std::optional<size_t> first_invalid_utf8(std::string_view text);

// MariaDB's latin1, which is Windows cp1252 with the five bytes cp1252 leaves
// undefined (0x81, 0x8D, 0x8F, 0x90, 0x9D) mapped to the same code points, as
// strings/ctype-latin1.c does.  Every Latin-1 character becomes one Unicode
// code point and one UTF-16 unit, so line and column numbers of the result
// are those of the original file.
std::string latin1_to_utf8(std::string_view latin1);

struct Charset_declaration {
  size_t offset;     // where the declaring statement starts
  std::string name;  // as written, lower-cased
};

// Every SET NAMES, SET CHARACTER SET and SET CHARSET of a script that starts
// before byte `before`, also inside versioned comments, in text order.
// DEFAULT is skipped.
std::vector<Charset_declaration> charset_declarations(std::string_view text,
                                                      size_t before = SIZE_MAX);

// The character set a script declares for its connection before byte
// `before`: the last SET NAMES, SET CHARACTER SET or SET CHARSET before it,
// also inside a versioned comment (/*!40101 SET NAMES latin1 */, as
// mysqldump writes it).  The name as written, lower-cased; nullopt if none.
std::optional<std::string> declared_charset(std::string_view text, size_t before);

}  // namespace mdbls

#endif  // MDBLS_CHARSET_H_
