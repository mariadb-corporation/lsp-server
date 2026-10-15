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

#ifndef MDBLS_SQL_MODE_H_
#define MDBLS_SQL_MODE_H_

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace mdbls {

// The sql_mode flags that change how SQL is split or lexed.  The composite
// names are expanded as the server's expand_sql_mode() does.
enum Sql_mode_bit : uint32_t {
  k_ansi_quotes = 1u << 0,
  k_no_backslash_escapes = 1u << 1,
  k_pipes_as_concat = 1u << 2,
  k_ignore_space = 1u << 3,
  k_high_not_precedence = 1u << 4,
  k_oracle = 1u << 5,  // the parser refuses this mode
  k_mssql = 1u << 6,   // the parser refuses this mode
};

// Upper-cases a comma-separated sql_mode list and removes blanks and empty
// elements: " ansi_quotes, ,traditional" becomes "ANSI_QUOTES,TRADITIONAL".
std::string normalize_sql_mode(std::string_view list);

// The Sql_mode_bit flags a (normalized or not) sql_mode list sets.
uint32_t sql_mode_bits(std::string_view list);

// Follows the session sql_mode through a script, one statement at a time,
// by reading SET statements textually:
//
//   SET [SESSION | LOCAL] sql_mode = '<list>' | <name> | DEFAULT | @var | @@sql_mode
//   SET @@[session. | local.]sql_mode = ...
//   SET @var = @@[session.]sql_mode          (saved for a later restore)
//
// including several assignments in one SET and SET statements wrapped in
// versioned comments (/*!40101 SET SQL_MODE=@OLD_SQL_MODE */), which is how
// mysqldump writes them.  Any other value expression (CONCAT(), REPLACE(), a
// variable not seen before) makes the mode unknown until the next plain
// assignment; the previous text is kept.
class Sql_mode_tracker {
 public:
  // `initial` is the connection's mode, also what DEFAULT restores.
  // `version_id` decides which versioned comments are read as SQL.
  explicit Sql_mode_tracker(std::string_view initial = {},
                            unsigned long version_id = 130100);

  // Reads one statement (without its delimiter).  Returns true when the
  // session mode or its known-ness changed.
  bool observe(std::string_view statement);

  const std::string &mode() const { return m_mode; }
  uint32_t bits() const { return m_bits; }
  bool known() const { return m_known; }

 private:
  void set(std::string mode, bool known);

  std::string m_initial;
  std::string m_mode;
  uint32_t m_bits = 0;
  bool m_known = true;
  unsigned long m_version_id;
  std::map<std::string, std::string> m_saved;  // user variable -> mode
};

}  // namespace mdbls

#endif  // MDBLS_SQL_MODE_H_
