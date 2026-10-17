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
#include <thread>

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

template <typename Stop>
void Statement_index::scan(std::string_view text, uint32_t from, Sql_mode_tracker &tracker,
                           const std::string &delimiter, Stop &&stop) {
  Statement_mode current{tracker.mode(), tracker.bits(), tracker.known()};
  uint16_t current_id = intern_mode(current);

  // \g and \G (with their format suffixes) end a statement; any other
  // backslash is an ordinary character, which also guarantees the splitter
  // never moves bytes in its buffer (libs/sqlsplitter/UPSTREAM.md).
  auto command = [](std::string_view s, bool, size_t) -> std::pair<size_t, bool> {
    const size_t n = mysqlshdk::utils::statement_delimiter_length(s);
    return n > 0 ? std::make_pair(n, true) : std::make_pair(size_t{0}, false);
  };
  auto error = [this](std::string_view message) { m_errors.emplace_back(message); };

  Sql_splitter splitter(command, error, {});
  splitter.set_ansi_quotes((current.bits & k_ansi_quotes) != 0);
  splitter.set_no_backslash_escapes((current.bits & k_no_backslash_escapes) != 0);
  splitter.set_dollar_quoted_strings(false);  // MariaDB has no $tag$ strings
  splitter.set_delimiter(delimiter);
  m_open_at_end = Open_context::none;

  if (from < text.size()) {
    // The splitter's interface takes a mutable buffer, but with the command
    // callback above it never writes to it.
    splitter.feed(const_cast<char *>(text.data()) + from, text.size() - from);

    Sql_splitter::Range range;
    std::string ended_by;
    while (!splitter.eof()) {
      if (!splitter.next_range(&range, &ended_by)) break;
      Statement st;
      st.begin = static_cast<uint32_t>(from + range.offset);
      st.body_end = static_cast<uint32_t>(st.begin + range.length);
      st.end = static_cast<uint32_t>(st.body_end + ended_by.size());
      st.delimiter = intern_delimiter(ended_by);
      st.active_delimiter = intern_delimiter(splitter.delimiter());
      const std::string_view body = text.substr(st.begin, st.body_end - st.begin);
      st.kind = classify(body);
      st.mode = current_id;
      st.mentions_sql_mode = st.kind == Statement_kind::sql && mentions_sql_mode(body);
      st.clean_after = splitter.context() == Sql_splitter::Context::kNone;
      m_statements.push_back(st);

      if (st.mentions_sql_mode && tracker.observe(body)) {
        current = {tracker.mode(), tracker.bits(), tracker.known()};
        current_id = intern_mode(current);
        splitter.set_ansi_quotes((current.bits & k_ansi_quotes) != 0);
        splitter.set_no_backslash_escapes((current.bits & k_no_backslash_escapes) != 0);
      }
      if (stop(m_statements.back())) {
        m_final_delimiter = splitter.delimiter();
        return;
      }
    }
    m_open_at_end = open_context(splitter.context());
  }
  m_final_delimiter = splitter.delimiter();
}

Statement_index Statement_index::build(std::string_view text, const Index_options &options) {
  if (text.size() >= std::numeric_limits<uint32_t>::max()) {
    throw std::length_error("text of 4 GiB or more");
  }
  Statement_index index;
  Sql_mode_tracker tracker(options.sql_mode, options.version_id);
  index.scan(text, 0, tracker, options.delimiter, [](const Statement &) { return false; });
  return index;
}

Statement_index Statement_index::build_parallel(std::string_view text,
                                               const Index_options &options, unsigned threads,
                                               size_t min_chunk) {
  min_chunk = std::max<size_t>(min_chunk, 1);
  if (threads == 0) threads = std::max(1u, std::thread::hardware_concurrency());
  if (threads <= 1 || text.size() < 2 * min_chunk) return build(text, options);
  if (text.size() >= std::numeric_limits<uint32_t>::max()) {
    throw std::length_error("text of 4 GiB or more");
  }
  const uint32_t size = static_cast<uint32_t>(text.size());
  constexpr uint32_t k_split_bits = k_ansi_quotes | k_no_backslash_escapes;

  // Chunk k is [bounds[k], bounds[k + 1]); every bound after the first
  // follows a line break.
  const size_t n = std::min<size_t>(threads, text.size() / min_chunk);
  std::vector<uint32_t> bounds{0};
  for (size_t k = 1; k < n; ++k) {
    const size_t nl = text.find('\n', k * text.size() / n);
    if (nl == std::string_view::npos || nl + 1 >= text.size()) break;
    if (nl + 1 > bounds.back()) bounds.push_back(static_cast<uint32_t>(nl + 1));
  }
  bounds.push_back(size);
  const size_t chunks = bounds.size() - 1;
  if (chunks < 2) return build(text, options);

  // Chunks 1.. split speculatively, each up to the first statement that
  // starts in the next chunk.
  std::vector<Statement_index> parts(chunks);
  {
    std::vector<std::thread> workers;
    for (size_t k = 1; k < chunks; ++k) {
      workers.emplace_back([&, k] {
        Sql_mode_tracker t(options.sql_mode, options.version_id);
        const uint32_t limit = bounds[k + 1];
        parts[k].scan(text, bounds[k], t, options.delimiter,
                      [limit](const Statement &st) { return st.begin >= limit; });
      });
    }
    for (auto &w : workers) w.join();
  }
  // Splitter messages carry no position: they cannot be told apart from
  // ones a wrong guess produced.
  for (const auto &p : parts) {
    if (!p.m_errors.empty()) return build(text, options);
  }

  auto find_in_part = [&](size_t k, uint32_t begin) -> size_t {
    const auto &v = parts[k].m_statements;
    const auto it = std::lower_bound(v.begin(), v.end(), begin,
                                     [](const Statement &s, uint32_t off) { return s.begin < off; });
    return it != v.end() && it->begin == begin ? static_cast<size_t>(it - v.begin()) : v.size();
  };
  // Whether two splits of the same statement leave the splitter in the same
  // state: what follows them is then split alike, given the same sql_mode
  // (checked statement by statement).  The state before them does not
  // matter, nor whether they saw the DELIMITER commands before them.  Only a
  // clean state (no string or comment open) is known to be the same.
  auto same_after = [](const Statement_index &a, const Statement &x, const Statement_index &b,
                       const Statement &y) {
    return x.end == y.end && x.body_end == y.body_end && x.kind == y.kind && x.clean_after &&
           y.clean_after &&
           a.m_delimiters[x.delimiter] == b.m_delimiters[y.delimiter] &&
           a.m_delimiters[x.active_delimiter] == b.m_delimiters[y.active_delimiter];
  };

  Statement_index index;
  Sql_mode_tracker tracker(options.sql_mode, options.version_id);
  // Appends statement `j` of part `k`, if it was split in the sql_mode in
  // force there.
  auto take = [&](size_t k, size_t j) {
    const Statement &sp = parts[k].m_statements[j];
    if ((parts[k].m_modes[sp.mode].bits & k_split_bits) != (tracker.bits() & k_split_bits)) {
      return false;
    }
    Statement x = sp;
    x.delimiter = index.intern_delimiter(parts[k].m_delimiters[sp.delimiter]);
    x.active_delimiter = index.intern_delimiter(parts[k].m_delimiters[sp.active_delimiter]);
    x.mode = index.intern_mode({tracker.mode(), tracker.bits(), tracker.known()});
    index.m_statements.push_back(x);
    if (x.mentions_sql_mode) tracker.observe(index.text_of(text, x));
    return true;
  };

  uint32_t pos = 0;
  std::string delimiter = options.delimiter;
  for (;;) {
    // Split sequentially until a statement matches one of the part its
    // begin falls in.
    size_t k = 0, j = 0;
    bool aligned = false;
    index.scan(text, pos, tracker, delimiter, [&](const Statement &st) {
      k = static_cast<size_t>(std::upper_bound(bounds.begin(), bounds.end(), st.begin) -
                              bounds.begin()) - 1;
      if (k == 0 || k >= chunks) return false;
      j = find_in_part(k, st.begin);
      aligned = j < parts[k].m_statements.size() &&
                same_after(parts[k], parts[k].m_statements[j], index, st);
      return aligned;
    });
    if (!aligned) return index;  // split to the end

    // Take the part's statements while they were split in the true state.
    for (++j;; ++j) {
      if (j == parts[k].m_statements.size()) {
        if (k + 1 == chunks) {
          // The last part ends at the end of the text, in the state checked.
          index.m_open_at_end = parts[k].m_open_at_end;
          index.m_final_delimiter = parts[k].m_final_delimiter;
          return index;
        }
        break;  // a part that ended early: split on from here
      }
      if (!take(k, j)) {
        break;
      }
      const Statement &sp = parts[k].m_statements[j];
      if (k + 1 < chunks && sp.begin >= bounds[k + 1]) {
        // The first statement of the next chunk, taken from this part, which
        // also saw what came before it: go on in the next part if its split
        // of the statement leaves the same state.
        const size_t j2 = find_in_part(k + 1, sp.begin);
        if (j2 == parts[k + 1].m_statements.size() ||
            !same_after(parts[k + 1], parts[k + 1].m_statements[j2], parts[k], sp)) {
          break;
        }
        ++k;
        j = j2;
      }
    }
    // Within a part, statements follow each other in one split, so the
    // last one taken may have left a string or comment open; split from
    // the last clean point then.
    const Statement &last = index.m_statements.back();
    if (!last.clean_after) return build(text, options);
    pos = last.end;
    delimiter = index.m_delimiters[last.active_delimiter];
  }
}

Statement_index Statement_index::update(const Statement_index &old, std::string_view text,
                                       uint32_t edit_begin, uint32_t removed,
                                       uint32_t inserted, const Index_options &options,
                                       Index_splice *splice) {
  if (text.size() >= std::numeric_limits<uint32_t>::max()) {
    throw std::length_error("text of 4 GiB or more");
  }
  const auto &olds = old.m_statements;
  const int64_t delta = static_cast<int64_t>(inserted) - static_cast<int64_t>(removed);

  // The first statement the edit can touch: the first that ends at or after
  // the edit's start.  Text added right at a statement's end can extend it (a
  // statement without delimiter, or \G taking a format suffix).  Splitting
  // resumes right after the statement before it, so that a change to the
  // blanks and DELIMITER commands between the two is seen.
  // A statement without a delimiter ended because of what follows it (a
  // comment before a DELIMITER command, say), which the edit may have
  // changed, and after one that left a comment open the splitter cannot
  // start afresh: step back over those too.
  size_t k = static_cast<size_t>(
      std::lower_bound(olds.begin(), olds.end(), edit_begin,
                       [](const Statement &s, uint32_t off) { return s.end < off; }) -
      olds.begin());
  while (k > 0 && (olds[k - 1].delimiter == 0 || !olds[k - 1].clean_after)) --k;
  const uint32_t from = k == 0 ? 0 : olds[k - 1].end;

  Statement_index index;
  index.m_modes = old.m_modes;
  index.m_delimiters = old.m_delimiters;
  index.m_statements.assign(olds.begin(), olds.begin() + static_cast<ptrdiff_t>(k));

  // The state at `from`: the delimiter in force after statement k-1, and the
  // sql_mode tracker replayed over the statements before it that may change
  // the mode (their text is before the edit, so unchanged).
  Sql_mode_tracker tracker(options.sql_mode, options.version_id);
  for (size_t i = 0; i < k; ++i) {
    if (olds[i].mentions_sql_mode) tracker.observe(old.text_of(text, olds[i]));
  }
  const std::string delimiter =
      k == 0 ? options.delimiter : old.m_delimiters[olds[k - 1].active_delimiter];

  // Stop at the first new statement that starts after the inserted text and
  // equals an old one shifted by the edit, split in the same state: from
  // there on everything is as before.
  // A statement that mentions sql_mode may also save it in a variable that a
  // later one restores, state the mode id does not show: once one was split
  // again, or was removed, split to the end.
  const uint32_t edit_end_new = edit_begin + inserted;
  size_t realigned = olds.size();
  bool mode_touched = false;
  bool clean_before = true;  // the state at `from` is clean (see above)
  index.scan(text, from, tracker, delimiter, [&](const Statement &st) {
    const bool was_clean = clean_before;
    clean_before = st.clean_after;
    mode_touched = mode_touched || st.mentions_sql_mode;
    if (mode_touched || !was_clean || st.begin < edit_end_new) return false;
    const int64_t old_begin = static_cast<int64_t>(st.begin) - delta;
    if (old_begin < 0) return false;
    const auto it = std::lower_bound(
        olds.begin() + static_cast<ptrdiff_t>(k), olds.end(), static_cast<uint32_t>(old_begin),
        [](const Statement &s, uint32_t off) { return s.begin < off; });
    if (it == olds.end() || it->begin != old_begin) return false;
    const bool same = it->end - it->begin == st.end - st.begin && it->delimiter == st.delimiter &&
                      it->active_delimiter == st.active_delimiter && it->mode == st.mode &&
                      it->kind == st.kind && it->clean_after == st.clean_after &&
                      (it == olds.begin() || std::prev(it)->clean_after);
    if (!same) return false;
    for (auto o = olds.begin() + static_cast<ptrdiff_t>(k); o != it; ++o) {
      if (o->mentions_sql_mode) {
        mode_touched = true;
        return false;
      }
    }
    realigned = static_cast<size_t>(it - olds.begin());
    return true;
  });

  // Splitter messages carry no position, so they cannot be spliced: with
  // any, old or new, index the whole text again.
  if (!old.m_errors.empty() || !index.m_errors.empty()) {
    if (splice != nullptr) *splice = {0, olds.size(), 0, delta};
    Statement_index full = build(text, options);
    if (splice != nullptr) splice->added = full.m_statements.size();
    return full;
  }

  const size_t added_end = index.m_statements.size();  // includes the realigned one
  if (realigned < olds.size()) {
    // The realigned statement was split again already; keep the rest, shifted.
    index.m_statements.reserve(index.m_statements.size() + (olds.size() - realigned - 1));
    for (size_t i = realigned + 1; i < olds.size(); ++i) {
      Statement s = olds[i];
      s.begin = static_cast<uint32_t>(s.begin + delta);
      s.body_end = static_cast<uint32_t>(s.body_end + delta);
      s.end = static_cast<uint32_t>(s.end + delta);
      index.m_statements.push_back(s);
    }
    index.m_open_at_end = old.m_open_at_end;
    index.m_final_delimiter = old.m_final_delimiter;
  }
  if (splice != nullptr) {
    splice->first = k;
    splice->delta = delta;
    if (realigned < olds.size()) {
      splice->removed = realigned - k;
      splice->added = added_end - 1 - k;
    } else {
      splice->removed = olds.size() - k;
      splice->added = added_end - k;
    }
  }
  return index;
}

size_t Statement_index::find(uint32_t offset) const {
  const auto it = std::upper_bound(
      m_statements.begin(), m_statements.end(), offset,
      [](uint32_t off, const Statement &s) { return off < s.end; });
  return static_cast<size_t>(it - m_statements.begin());
}

}  // namespace mdbls
