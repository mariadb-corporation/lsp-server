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

#include "mdbls/sql_mode.h"

#include <cctype>
#include <utility>
#include <vector>

namespace mdbls {

namespace {

std::string to_upper(std::string_view s) {
  std::string out(s);
  for (auto &c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::toupper(static_cast<unsigned char>(a[i])) !=
        std::toupper(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

uint32_t element_bits(std::string_view e) {
  constexpr uint32_t k_ansi_like = k_ansi_quotes | k_pipes_as_concat | k_ignore_space;
  if (e == "ANSI_QUOTES") return k_ansi_quotes;
  if (e == "NO_BACKSLASH_ESCAPES") return k_no_backslash_escapes;
  if (e == "PIPES_AS_CONCAT") return k_pipes_as_concat;
  if (e == "IGNORE_SPACE") return k_ignore_space;
  if (e == "HIGH_NOT_PRECEDENCE") return k_high_not_precedence;
  if (e == "ANSI" || e == "POSTGRESQL" || e == "DB2" || e == "MAXDB") return k_ansi_like;
  if (e == "MYSQL40" || e == "MYSQL323") return k_high_not_precedence;
  if (e == "ORACLE") return k_ansi_like | k_oracle;
  if (e == "MSSQL") return k_ansi_like | k_mssql;
  return 0;
}

// --- a small tokenizer for SET statements -------------------------------

enum class Tok { word, string, user_var, sys_var, op };

struct Token {
  Tok kind;
  std::string text;  // word as written; string contents; variable name
                     // without '@'/'@@'; operator spelling
};

bool is_word_char(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' ||
         static_cast<unsigned char>(c) >= 0x80;
}

// Reads a quoted string or identifier starting at s[i] (the quote).
std::string read_quoted(std::string_view s, size_t *i, bool backslash_escapes) {
  const char q = s[*i];
  std::string out;
  size_t j = *i + 1;
  while (j < s.size()) {
    const char c = s[j];
    if (c == '\\' && backslash_escapes && q != '`' && j + 1 < s.size()) {
      out.push_back(s[j + 1]);
      j += 2;
    } else if (c == q) {
      if (j + 1 < s.size() && s[j + 1] == q) {
        out.push_back(q);
        j += 2;
      } else {
        ++j;
        break;
      }
    } else {
      out.push_back(c);
      ++j;
    }
  }
  *i = j;
  return out;
}

// Appends the tokens of `s` to `out`.  Ordinary comments are skipped;
// versioned comments the release id admits are read as SQL.
void tokenize(std::string_view s, unsigned long version_id, bool backslash_escapes,
              std::vector<Token> *out) {
  size_t i = 0;
  while (i < s.size()) {
    const char c = s[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
    } else if (c == '#' || (c == '-' && s.substr(i, 2) == "--" &&
                            (i + 2 == s.size() ||
                             std::isspace(static_cast<unsigned char>(s[i + 2]))))) {
      const auto eol = s.find('\n', i);
      i = eol == std::string_view::npos ? s.size() : eol + 1;
    } else if (s.substr(i, 2) == "/*") {
      const auto close = s.find("*/", i + 2);
      const size_t end = close == std::string_view::npos ? s.size() : close;
      size_t j = i + 2;
      bool executable = false;
      if (j < end && s[j] == '!') {
        executable = true;
        ++j;
      } else if (s.substr(j, 2) == "M!") {
        executable = true;
        j += 2;
      }
      if (executable) {
        const bool mariadb_only = s[i + 2] == 'M';
        size_t k = j;
        while (k < end && k - j < 6 && std::isdigit(static_cast<unsigned char>(s[k]))) ++k;
        const size_t ndigits = k - j;
        bool read = true;
        if (ndigits == 5 || ndigits == 6) {
          const unsigned long v = std::stoul(std::string(s.substr(j, ndigits)));
          // MariaDB never reads MySQL's /*!50700 .. 99999 comments.
          read = v <= version_id &&
                 (mariadb_only || ndigits == 6 || v < 50700 || v > 99999);
          j = k;
        }
        if (read) tokenize(s.substr(j, end - j), version_id, backslash_escapes, out);
      }
      i = close == std::string_view::npos ? s.size() : close + 2;
    } else if (c == '\'' || c == '"') {
      out->push_back({Tok::string, read_quoted(s, &i, backslash_escapes)});
    } else if (c == '`') {
      out->push_back({Tok::word, read_quoted(s, &i, false)});
    } else if (c == '@') {
      const bool sys = s.substr(i, 2) == "@@";
      size_t j = i + (sys ? 2 : 1);
      std::string name;
      if (!sys && j < s.size() && (s[j] == '\'' || s[j] == '"' || s[j] == '`')) {
        name = read_quoted(s, &j, backslash_escapes);
      } else {
        while (j < s.size() && (is_word_char(s[j]) || (sys && s[j] == '.'))) {
          name.push_back(s[j]);
          ++j;
        }
      }
      out->push_back({sys ? Tok::sys_var : Tok::user_var, name});
      i = j;
    } else if (is_word_char(c)) {
      size_t j = i;
      while (j < s.size() && is_word_char(s[j])) ++j;
      out->push_back({Tok::word, std::string(s.substr(i, j - i))});
      i = j;
    } else if (s.substr(i, 2) == ":=") {
      out->push_back({Tok::op, "="});
      i += 2;
    } else {
      out->push_back({Tok::op, std::string(1, c)});
      ++i;
    }
  }
}

// "@@sql_mode", "@@session.sql_mode", "@@local.sql_mode" name the session
// sql_mode; "@@global.sql_mode" does not.
bool is_session_sql_mode(const Token &t) {
  if (t.kind != Tok::sys_var) return false;
  return iequals(t.text, "sql_mode") || iequals(t.text, "session.sql_mode") ||
         iequals(t.text, "local.sql_mode");
}

}  // namespace

std::string normalize_sql_mode(std::string_view list) {
  std::string out;
  size_t start = 0;
  while (start <= list.size()) {
    auto comma = list.find(',', start);
    if (comma == std::string_view::npos) comma = list.size();
    auto e = list.substr(start, comma - start);
    while (!e.empty() && std::isspace(static_cast<unsigned char>(e.front()))) e.remove_prefix(1);
    while (!e.empty() && std::isspace(static_cast<unsigned char>(e.back()))) e.remove_suffix(1);
    if (!e.empty()) {
      if (!out.empty()) out.push_back(',');
      out += to_upper(e);
    }
    start = comma + 1;
  }
  return out;
}

uint32_t sql_mode_bits(std::string_view list) {
  const std::string norm = normalize_sql_mode(list);
  uint32_t bits = 0;
  size_t start = 0;
  while (start <= norm.size()) {
    auto comma = norm.find(',', start);
    if (comma == std::string::npos) comma = norm.size();
    bits |= element_bits(std::string_view(norm).substr(start, comma - start));
    start = comma + 1;
  }
  return bits;
}

Sql_mode_tracker::Sql_mode_tracker(std::string_view initial, unsigned long version_id)
    : m_initial(normalize_sql_mode(initial)),
      m_mode(m_initial),
      m_bits(sql_mode_bits(m_initial)),
      m_version_id(version_id) {}

void Sql_mode_tracker::set(std::string mode, bool known) {
  m_mode = std::move(mode);
  m_bits = sql_mode_bits(m_mode);
  m_known = known;
}

bool mentions_sql_mode(std::string_view statement) {
  for (size_t i = 0; i + 8 <= statement.size(); ++i) {
    const char c = statement[i];
    if ((c == 's' || c == 'S') && iequals(statement.substr(i, 8), "sql_mode")) return true;
  }
  return false;
}

bool Sql_mode_tracker::observe(std::string_view statement) {
  // Cheap rejection: only statements containing "sql_mode" can matter.
  if (!mentions_sql_mode(statement)) return false;

  std::vector<Token> t;
  tokenize(statement, m_version_id, (m_bits & k_no_backslash_escapes) == 0, &t);
  if (t.empty() || t[0].kind != Tok::word || !iequals(t[0].text, "SET")) return false;
  if (t.size() > 1 && t[1].kind == Tok::word &&
      (iequals(t[1].text, "NAMES") || iequals(t[1].text, "CHARACTER") ||
       iequals(t[1].text, "CHARSET") || iequals(t[1].text, "PASSWORD") ||
       iequals(t[1].text, "TRANSACTION") || iequals(t[1].text, "ROLE") ||
       iequals(t[1].text, "DEFAULT") || iequals(t[1].text, "STATEMENT"))) {
    return false;
  }

  const std::string before = m_mode;
  const bool before_known = m_known;

  size_t i = 1;
  while (i < t.size()) {
    // One assignment: up to the next comma outside parentheses.
    size_t end = i;
    int depth = 0;
    while (end < t.size()) {
      if (t[end].kind == Tok::op) {
        if (t[end].text == "(") ++depth;
        if (t[end].text == ")") --depth;
        if (t[end].text == "," && depth == 0) break;
      }
      ++end;
    }
    size_t k = i;
    bool global = false;
    if (k < end && t[k].kind == Tok::word) {
      if (iequals(t[k].text, "GLOBAL")) {
        global = true;
        ++k;
      } else if (iequals(t[k].text, "SESSION") || iequals(t[k].text, "LOCAL")) {
        ++k;
      }
    }
    if (k + 1 < end && t[k + 1].kind == Tok::op && t[k + 1].text == "=") {
      const Token &target = t[k];
      const size_t v = k + 2;
      const size_t nvalue = end - v;
      const bool session_target =
          !global && ((target.kind == Tok::word && iequals(target.text, "sql_mode")) ||
                      is_session_sql_mode(target));
      if (session_target) {
        if (nvalue == 1 && t[v].kind == Tok::string) {
          set(normalize_sql_mode(t[v].text), true);
        } else if (nvalue == 1 && t[v].kind == Tok::word && iequals(t[v].text, "DEFAULT")) {
          set(m_initial, true);
        } else if (nvalue == 1 && t[v].kind == Tok::word &&
                   !std::isdigit(static_cast<unsigned char>(t[v].text[0]))) {
          set(normalize_sql_mode(t[v].text), true);  // SET sql_mode = ansi
        } else if (nvalue == 1 && t[v].kind == Tok::user_var) {
          const auto it = m_saved.find(to_upper(t[v].text));
          if (it != m_saved.end()) {
            set(it->second, true);
          } else {
            set(m_mode, false);
          }
        } else if (nvalue == 1 && is_session_sql_mode(t[v])) {
          // sql_mode = @@sql_mode: no change
        } else {
          set(m_mode, false);
        }
      } else if (target.kind == Tok::user_var) {
        const std::string key = to_upper(target.text);
        if (nvalue == 1 && is_session_sql_mode(t[v]) && m_known) {
          m_saved[key] = m_mode;
        } else if (nvalue == 1 && t[v].kind == Tok::string) {
          m_saved[key] = normalize_sql_mode(t[v].text);
        } else {
          m_saved.erase(key);
        }
      }
    }
    i = end + 1;
  }
  return m_mode != before || m_known != before_known;
}

}  // namespace mdbls
