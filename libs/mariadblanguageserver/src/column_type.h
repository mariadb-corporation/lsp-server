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

#pragma once

// The data type MariaDB reports for a column, a routine parameter or a
// function's RETURNS: information_schema.COLUMNS.COLUMN_TYPE,
// PARAMETERS.DTD_IDENTIFIER. The outline's `column_type` (feature
// `column_types`).
//
// Every rule here was read off MariaDB 13.1 (CREATE TABLE / CREATE PROCEDURE,
// then information_schema); the unit test lists the cases. A type the server
// would refuse, or one whose result depends on something the script does not
// say, has no column type.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mdbls {

// A data type as the parse tree has it.
struct Type_spec {
  // The base type, aliases resolved: tinyint, smallint, mediumint, int,
  // bigint, boolean, serial, bit, float, double, decimal, year, date, time,
  // timestamp, datetime, char, varchar, binary, varbinary, tinyblob, blob,
  // mediumblob, longblob, tinytext, text, mediumtext, longtext, json, enum,
  // set; a type plugin's name, lower-cased (uuid, inet6, point, vector, ...);
  // "" when the type has no column type (anchored TYPE OF and ROW types,
  // Oracle-mode types).
  std::string base;
  // The numbers in the parentheses after the type as written: (M) or (M,D).
  std::vector<std::string> args;
  bool is_unsigned = false;
  bool zerofill = false;
  bool compressed = false;
  // With the type: CHARACTER SET (BYTE is binary, ASCII latin1, UNICODE
  // ucs2; NCHAR and NVARCHAR utf8mb3), lower-cased, and COLLATE.
  std::optional<std::string> charset;
  std::optional<std::string> collation;
  // ENUM and SET: the values, decoded. nullopt when one could not be.
  std::optional<std::vector<std::string>> values;
};

// The column type of `t`, lower-case as the server writes it, or nullopt.
// `default_charset` is the charset a type without CHARACTER SET or COLLATE
// takes (a column's table, else its schema; a parameter's schema): its name,
// or a collation's, "" when not known.
std::optional<std::string> column_type(const Type_spec &t, std::string_view default_charset);

// The bytes per character of a charset (MariaDB's CHARACTER_SETS.MAXLEN),
// 0 for a name the server does not have. utf8 is utf8mb3.
int charset_mbmaxlen(std::string_view charset);

// The charset a collation belongs to ("utf8mb4_bin" is utf8mb4, "binary"
// binary), "" when its name does not say (uca1400_ai_ci, which takes the
// charset in force).
std::string charset_of_collation(std::string_view collation);

}  // namespace mdbls
