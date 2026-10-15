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

#ifndef MDBLS_STATEMENT_INDEX_H_
#define MDBLS_STATEMENT_INDEX_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mdbls {

enum class Statement_kind : uint8_t {
  sql,      // SQL text (possibly inside versioned comments)
  comment,  // only comments: a "-- ..." or "# ..." line, or a /* */ block
};

// One statement of a document, as byte offsets into its text.
//
//   begin      first byte (leading blanks are not part of it)
//   body_end   end of the statement text, where its delimiter starts
//   end        end of the delimiter; equal to body_end when there is none
//
// No line or column is stored: Line_map converts offsets when needed, so an
// edit that shifts text only shifts offsets.
struct Statement {
  uint32_t begin = 0;
  uint32_t body_end = 0;
  uint32_t end = 0;
  uint16_t mode = 0;       // index into Statement_index::modes(): the sql_mode
                           // in force when the statement runs
  uint8_t delimiter = 0;   // index into Statement_index::delimiters(); 0 is ""
  Statement_kind kind = Statement_kind::sql;
  uint8_t active_delimiter = 0;  // the delimiter in force while it was split,
                                 // also after it (DELIMITER commands come
                                 // between statements)
  bool mentions_sql_mode = false;  // may change the mode (Sql_mode_tracker)
  bool clean_after = true;  // the splitter was in no string or comment after
                            // it: a delimiter inside an unclosed /*! ... does
                            // end a statement, but the comment stays open
};

// How an edit changed the statement list: statements [0, first) are the
// same, the `removed` old statements from `first` on were replaced by
// `added` new ones, and every statement after them is the same, its offsets
// moved by `delta`.
struct Index_splice {
  size_t first = 0;
  size_t removed = 0;
  size_t added = 0;
  int64_t delta = 0;
};

// The sql_mode a statement is lexed with.  `known` is false after a SET whose
// value could not be followed (see Sql_mode_tracker); `mode` then still holds
// the last mode that was known.
struct Statement_mode {
  std::string mode;
  uint32_t bits = 0;
  bool known = true;

  bool operator==(const Statement_mode &) const = default;
};

struct Index_options {
  std::string sql_mode;           // the connection's sql_mode at the start
  unsigned long version_id = 130100;  // release id for versioned comments
  std::string delimiter = ";";    // the delimiter at the start
};

// What the splitter left open at the end of the text.
enum class Open_context : uint8_t {
  none,
  single_quote_string,
  double_quote_string,
  backtick_identifier,
  double_quote_identifier,
  comment,
  other,
};

// The statements of one document, in text order, built in one pass by MariaDB
// Shell's Sql_splitter.  DELIMITER commands are applied while splitting and
// are not statements themselves; SET sql_mode statements change the mode of
// the statements after them (Sql_mode_tracker).  `\g` and `\G` end a statement
// like a delimiter; any other backslash outside a string is ordinary text.
class Statement_index {
 public:
  static Statement_index build(std::string_view text, const Index_options &options = {});

  // The index of `text`, the document after replacing `removed` bytes at
  // `edit_begin` of the text `old` was built from with `inserted` bytes.
  // Equal to build(text, options), but only the region around the edit is
  // split again: from the end of the statement before the edited one until
  // a statement starts where an old one did, shifted by the edit, in the
  // same delimiter and sql_mode.  `splice` describes the change.
  static Statement_index update(const Statement_index &old, std::string_view text,
                                uint32_t edit_begin, uint32_t removed, uint32_t inserted,
                                const Index_options &options = {},
                                Index_splice *splice = nullptr);

  const std::vector<Statement> &statements() const { return m_statements; }
  const std::vector<Statement_mode> &modes() const { return m_modes; }
  const std::vector<std::string> &delimiters() const { return m_delimiters; }

  // The delimiter and sql_mode in force after the last statement.
  const std::string &final_delimiter() const { return m_final_delimiter; }
  // What a statement at the very end left open (an unterminated string or
  // comment); none when the text ends cleanly.
  Open_context open_at_end() const { return m_open_at_end; }
  // Messages the splitter reported, such as an invalid DELIMITER.
  const std::vector<std::string> &errors() const { return m_errors; }

  // Index of the statement containing byte `offset` (begin <= offset < end),
  // or of the first statement after it; statements().size() if none.
  size_t find(uint32_t offset) const;

  std::string_view text_of(std::string_view text, const Statement &s) const {
    return text.substr(s.begin, s.body_end - s.begin);
  }

 private:
  uint16_t intern_mode(const Statement_mode &mode);
  // Splits `text` from `from` on, with the delimiter and tracker state there,
  // appending to m_statements; `stop` may end the scan early (it gets the
  // statement just added) and returns true to stop.
  template <typename Stop>
  void scan(std::string_view text, uint32_t from, class Sql_mode_tracker &tracker,
            const std::string &delimiter, Stop &&stop);
  uint8_t intern_delimiter(const std::string &delimiter);

  std::vector<Statement> m_statements;
  std::vector<Statement_mode> m_modes;
  std::vector<std::string> m_delimiters{""};
  std::string m_final_delimiter;
  Open_context m_open_at_end = Open_context::none;
  std::vector<std::string> m_errors;
};

}  // namespace mdbls

#endif  // MDBLS_STATEMENT_INDEX_H_
