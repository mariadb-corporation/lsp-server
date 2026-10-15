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

#include "mdbls/statement_index.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>

#include "mdbls/sql_mode.h"
#include "mysqlshdk/libs/utils/utils_mysql_parsing.h"

namespace mdbls {

namespace {

using mysqlshdk::utils::Sql_splitter;

// sql when anything but blanks and ordinary comments is in `s`; an
// executable comment (/*! or /*M!) counts as SQL.
Statement_kind classify(std::string_view s) {
  size_t i = 0;
  while (i < s.size()) {
    const char c = s[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      ++i;
    } else if (c == '#' || (c == '-' && s.substr(i, 2) == "--" &&
                            (i + 2 == s.size() ||
                             std::isspace(static_cast<unsigned char>(s[i + 2]))))) {
      const auto eol = s.find('\n', i);
      if (eol == std::string_view::npos) return Statement_kind::comment;
      i = eol + 1;
    } else if (s.substr(i, 2) == "/*") {
      if (s.substr(i, 3) == "/*!" || s.substr(i, 4) == "/*M!") return Statement_kind::sql;
      const auto close = s.find("*/", i + 2);
      if (close == std::string_view::npos) return Statement_kind::comment;
      i = close + 2;
    } else {
      return Statement_kind::sql;
    }
  }
  return Statement_kind::comment;
}

Open_context open_context(Sql_splitter::Context c) {
  switch (c) {
    case Sql_splitter::Context::kNone:
    case Sql_splitter::Context::kStatement:
    case Sql_splitter::Context::kIdentifier:
      return Open_context::none;
    case Sql_splitter::Context::kSQuoteString:
      return Open_context::single_quote_string;
    case Sql_splitter::Context::kDQuoteString:
      return Open_context::double_quote_string;
    case Sql_splitter::Context::kBQuoteIdentifier:
      return Open_context::backtick_identifier;
    case Sql_splitter::Context::kDQuoteIdentifier:
      return Open_context::double_quote_identifier;
    case Sql_splitter::Context::kComment:
    case Sql_splitter::Context::kCommentHint:
    case Sql_splitter::Context::kCommentConditional:
      return Open_context::comment;
    default:
      return Open_context::other;
  }
}

}  // namespace

uint16_t Statement_index::intern_mode(const Statement_mode &mode) {
  // Few distinct modes occur in a document; the last one is the usual hit.
  if (!m_modes.empty() && m_modes.back() == mode) {
    return static_cast<uint16_t>(m_modes.size() - 1);
  }
  const auto it = std::find(m_modes.begin(), m_modes.end(), mode);
  if (it != m_modes.end()) return static_cast<uint16_t>(it - m_modes.begin());
  if (m_modes.size() > std::numeric_limits<uint16_t>::max()) {
    throw std::length_error("more than 65536 distinct sql_mode values in one document");
  }
  m_modes.push_back(mode);
  return static_cast<uint16_t>(m_modes.size() - 1);
}

uint8_t Statement_index::intern_delimiter(const std::string &delimiter) {
  const auto it = std::find(m_delimiters.begin(), m_delimiters.end(), delimiter);
  if (it != m_delimiters.end()) return static_cast<uint8_t>(it - m_delimiters.begin());
  if (m_delimiters.size() > std::numeric_limits<uint8_t>::max()) {
    throw std::length_error("more than 256 distinct delimiters in one document");
  }
  m_delimiters.push_back(delimiter);
  return static_cast<uint8_t>(m_delimiters.size() - 1);
}

Statement_index Statement_index::build(std::string_view text, const Index_options &options) {
  if (text.size() >= std::numeric_limits<uint32_t>::max()) {
    throw std::length_error("text of 4 GiB or more");
  }
  Statement_index index;
  Sql_mode_tracker tracker(options.sql_mode, options.version_id);
  Statement_mode current{tracker.mode(), tracker.bits(), true};
  uint16_t current_id = index.intern_mode(current);

  // \g and \G (with their format suffixes) end a statement; any other
  // backslash is an ordinary character, which also guarantees the splitter
  // never moves bytes in its buffer (libs/sqlsplitter/UPSTREAM.md).
  auto command = [](std::string_view s, bool, size_t) -> std::pair<size_t, bool> {
    const size_t n = mysqlshdk::utils::statement_delimiter_length(s);
    return n > 0 ? std::make_pair(n, true) : std::make_pair(size_t{0}, false);
  };
  auto error = [&index](std::string_view message) {
    index.m_errors.emplace_back(message);
  };

  Sql_splitter splitter(command, error, {});
  splitter.set_ansi_quotes((current.bits & k_ansi_quotes) != 0);
  splitter.set_no_backslash_escapes((current.bits & k_no_backslash_escapes) != 0);
  splitter.set_dollar_quoted_strings(false);  // MariaDB has no $tag$ strings
  splitter.set_delimiter(options.delimiter);

  if (!text.empty()) {
    // The splitter's interface takes a mutable buffer, but with the command
    // callback above it never writes to it.
    splitter.feed(const_cast<char *>(text.data()), text.size());

    Sql_splitter::Range range;
    std::string delimiter;
    while (!splitter.eof()) {
      if (!splitter.next_range(&range, &delimiter)) break;
      Statement st;
      st.begin = static_cast<uint32_t>(range.offset);
      st.body_end = static_cast<uint32_t>(range.offset + range.length);
      st.end = static_cast<uint32_t>(st.body_end + delimiter.size());
      st.delimiter = index.intern_delimiter(delimiter);
      const std::string_view body = text.substr(st.begin, st.body_end - st.begin);
      st.kind = classify(body);
      st.mode = current_id;
      index.m_statements.push_back(st);

      if (st.kind == Statement_kind::sql && tracker.observe(body)) {
        current = {tracker.mode(), tracker.bits(), tracker.known()};
        current_id = index.intern_mode(current);
        splitter.set_ansi_quotes((current.bits & k_ansi_quotes) != 0);
        splitter.set_no_backslash_escapes((current.bits & k_no_backslash_escapes) != 0);
      }
    }
    index.m_open_at_end = open_context(splitter.context());
  }
  index.m_final_delimiter = splitter.delimiter();
  return index;
}

size_t Statement_index::find(uint32_t offset) const {
  const auto it = std::upper_bound(
      m_statements.begin(), m_statements.end(), offset,
      [](uint32_t off, const Statement &s) { return off < s.end; });
  return static_cast<size_t>(it - m_statements.begin());
}

}  // namespace mdbls
