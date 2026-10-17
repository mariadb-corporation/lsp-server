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

#ifndef MDBLS_DOCUMENT_H_
#define MDBLS_DOCUMENT_H_

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mdbls/line_map.h"
#include "mdbls/statement_index.h"
#include "mdbls/syntax_check.h"

namespace mdbls {

// One state of a document, immutable, shared with the jobs working on it.
struct Snapshot {
  int64_t version = 0;      // the client's version
  uint64_t generation = 0;  // ours: one more per change
  std::shared_ptr<const std::string> text;
  std::shared_ptr<const Statement_index> index;
  std::shared_ptr<const Line_map> lines;
  unsigned long version_id = k_default_version_id;  // for versioned comments and the parser
};

// The result of checking one statement of the snapshot of `generation`;
// offsets in `check` are relative to the statement's begin.
struct Check_result {
  size_t statement;
  Statement_check check;
};

// An open document: its text, statement index and line map, kept current
// edit by edit, and the syntax check verdict of each statement.
//
// Not thread-safe: one thread (the server's) owns it.  Other threads see
// Snapshots.  The text and the line map are changed in place when no
// snapshot shares them, so an edit in the middle of a large file costs a
// memmove, not a copy; while a job holds a snapshot they are copied first.
class Document {
 public:
  Document(std::string text, int64_t version, Index_options options);

  // One change of a didChange notification: replaces `range` (LSP positions
  // in the current text) by `text`, or the whole text when there is no
  // range.  Statements outside the edited region keep their verdicts.
  // Returns the byte offset where the change begins (0 for the whole text).
  uint32_t change(std::optional<std::pair<Position, Position>> range, std::string_view text);
  void set_version(int64_t version) { m_version = version; m_snapshot.reset(); }

  // New sql_mode, version id or delimiter: indexes the text again and drops
  // every verdict.
  void set_options(const Index_options &options);
  const Index_options &options() const { return m_options; }

  std::shared_ptr<const Snapshot> snapshot();

  int64_t version() const { return m_version; }
  const std::string &text() const { return *m_text; }
  const Statement_index &index() const { return *m_index; }
  const Line_map &lines() const { return *m_lines; }
  const std::vector<Statement_check> &checks() const { return m_checks; }

  // The SQL statements with no verdict that no job is checking: marks them
  // as being checked and returns them in text order.
  std::vector<size_t> take_unchecked();

  // Verdicts computed on the snapshot of `generation`.  Statements an edit
  // since then replaced are dropped (they are unchecked again and will be
  // taken anew); the rest are moved to where the edits put them.
  void apply_checks(uint64_t generation, std::span<const Check_result> results);

  // Statements given out by take_unchecked() whose verdict has not arrived.
  size_t checks_in_flight() const;
  size_t sql_statements() const;
  size_t checked_statements() const;

  // The byte offset of `position` in the current text.
  uint32_t offset(Position position) const { return m_lines->offset(*m_text, position); }
  Position position(uint32_t offset) const { return m_lines->position(*m_text, offset); }

 private:
  void rebuild();
  void splice(const Index_splice &splice);
  // The statement of generation `from` at index `i` now, or nullopt when an
  // edit since then replaced it.
  std::optional<size_t> translate(uint64_t from, size_t i) const;

  Index_options m_options;
  int64_t m_version = 0;
  uint64_t m_generation = 0;
  std::shared_ptr<std::string> m_text;
  std::shared_ptr<const Statement_index> m_index;
  std::shared_ptr<Line_map> m_lines;
  std::shared_ptr<const Snapshot> m_snapshot;  // of the current state, made on demand

  std::vector<Statement_check> m_checks;  // one per statement
  std::vector<uint8_t> m_queued;          // given out, verdict not in yet
  // The splice of every generation after m_history_base, oldest first.
  std::deque<Index_splice> m_history;
  uint64_t m_history_base = 0;
};

}  // namespace mdbls

#endif  // MDBLS_DOCUMENT_H_
