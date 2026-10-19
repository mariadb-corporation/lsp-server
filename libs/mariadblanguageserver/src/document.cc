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

#include "mdbls/document.h"

#include <algorithm>
#include <stdexcept>

namespace mdbls {

namespace {

// Generations whose splices are kept; verdicts computed on an older snapshot
// are dropped, and their statements checked again.
constexpr size_t k_history = 4096;

}  // namespace

Document::Document(std::string text, int64_t version, Index_options options)
    : m_options(std::move(options)),
      m_version(version),
      m_text(std::make_shared<std::string>(std::move(text))) {
  rebuild();
}

void Document::rebuild() {
  m_snapshot.reset();
  m_lines = std::make_shared<Line_map>(*m_text);
  const size_t old_count = m_index ? m_index->statements().size() : 0;
  m_index = std::make_shared<const Statement_index>(Statement_index::build_parallel(
      *m_text, m_options, 0));
  ++m_generation;
  // Everything is replaced: queued jobs' verdicts will all be dropped.
  m_history.push_back({0, old_count, m_index->statements().size(), 0});
  m_checks.assign(m_index->statements().size(), {});
  m_queued.assign(m_index->statements().size(), 0);
  while (m_history.size() > k_history) {
    m_history.pop_front();
    ++m_history_base;
  }
}

void Document::set_options(const Index_options &options) {
  m_options = options;
  rebuild();
}

uint32_t Document::change(std::optional<std::pair<Position, Position>> range, std::string_view text) {
  if (!range) {
    // Replace the buffer's content, reusing its memory when it is ours.
    m_snapshot.reset();
    if (m_text.use_count() == 1) {
      m_text->assign(text);
    } else {
      m_text = std::make_shared<std::string>(text);
    }
    rebuild();
    return 0;
  }
  uint32_t begin = offset(range->first);
  uint32_t end = offset(range->second);
  if (end < begin) std::swap(begin, end);
  const uint32_t removed = end - begin;
  if (m_text->size() - removed + text.size() >= UINT32_MAX) {
    throw std::length_error("text of 4 GiB or more");
  }
  const uint32_t inserted = static_cast<uint32_t>(text.size());

  m_snapshot.reset();
  if (m_text.use_count() == 1) {
    m_text->replace(begin, removed, text);
  } else {
    auto copy = std::make_shared<std::string>();
    copy->reserve(m_text->size() - removed + inserted);
    copy->append(*m_text, 0, begin).append(text).append(*m_text, end);
    m_text = std::move(copy);
  }
  if (m_lines.use_count() != 1) m_lines = std::make_shared<Line_map>(*m_lines);
  m_lines->update(*m_text, begin, removed, inserted);

  Index_splice sp;
  m_index = std::make_shared<const Statement_index>(
      Statement_index::update(*m_index, *m_text, begin, removed, inserted, m_options, &sp));
  ++m_generation;
  splice(sp);
  return begin;
}

void Document::splice(const Index_splice &sp) {
  // Verdicts of the replaced statements go; those after them move.
  auto checks_first = m_checks.begin() + static_cast<ptrdiff_t>(sp.first);
  auto queued_first = m_queued.begin() + static_cast<ptrdiff_t>(sp.first);
  if (sp.added >= sp.removed) {
    m_checks.insert(checks_first + static_cast<ptrdiff_t>(sp.removed), sp.added - sp.removed, {});
    m_queued.insert(queued_first + static_cast<ptrdiff_t>(sp.removed), sp.added - sp.removed, 0);
  } else {
    m_checks.erase(checks_first + static_cast<ptrdiff_t>(sp.added),
                   checks_first + static_cast<ptrdiff_t>(sp.removed));
    m_queued.erase(queued_first + static_cast<ptrdiff_t>(sp.added),
                   queued_first + static_cast<ptrdiff_t>(sp.removed));
  }
  for (size_t i = sp.first; i < sp.first + sp.added; ++i) {
    m_checks[i] = {};
    m_queued[i] = 0;
  }
  if (sp.delta != 0) {
    for (size_t i = sp.first + sp.added; i < m_checks.size(); ++i) {
      if (m_checks[i].status == Check_status::syntax_error) {
        m_checks[i].error_offset = static_cast<uint32_t>(m_checks[i].error_offset + sp.delta);
      }
    }
  }
  m_history.push_back(sp);
  while (m_history.size() > k_history) {
    m_history.pop_front();
    ++m_history_base;
  }
}

std::shared_ptr<const Snapshot> Document::snapshot() {
  if (!m_snapshot) {
    auto s = std::make_shared<Snapshot>();
    s->version = m_version;
    s->generation = m_generation;
    s->text = m_text;
    s->index = m_index;
    s->lines = m_lines;
    s->version_id = m_options.version_id;
    s->index_options = m_options;
    m_snapshot = std::move(s);
  }
  return m_snapshot;
}

std::vector<size_t> Document::take_unchecked() {
  std::vector<size_t> out;
  const auto &stmts = m_index->statements();
  for (size_t i = 0; i < stmts.size(); ++i) {
    if (stmts[i].kind == Statement_kind::sql && m_checks[i].status == Check_status::unchecked &&
        !m_queued[i]) {
      m_queued[i] = 1;
      out.push_back(i);
    }
  }
  return out;
}

std::optional<size_t> Document::translate(uint64_t from, size_t i) const {
  // m_history[k] took generation m_history_base + k to the next one.
  if (from < m_history_base) return std::nullopt;
  for (size_t k = static_cast<size_t>(from - m_history_base); k < m_history.size(); ++k) {
    const auto &sp = m_history[k];
    if (i < sp.first) continue;
    if (i < sp.first + sp.removed) return std::nullopt;
    i = i - sp.removed + sp.added;
  }
  return i;
}

void Document::apply_checks(uint64_t generation, std::span<const Check_result> results) {
  const auto &stmts = m_index->statements();
  for (const auto &r : results) {
    const auto i = translate(generation, r.statement);
    if (!i || *i >= stmts.size() || !m_queued[*i]) continue;
    m_queued[*i] = 0;
    m_checks[*i] = r.check;
    if (r.check.status == Check_status::syntax_error) {
      m_checks[*i].error_offset += stmts[*i].begin;
    }
  }
}

size_t Document::checks_in_flight() const {
  return static_cast<size_t>(std::count(m_queued.begin(), m_queued.end(), uint8_t{1}));
}

size_t Document::sql_statements() const {
  return static_cast<size_t>(std::count_if(
      m_index->statements().begin(), m_index->statements().end(),
      [](const Statement &s) { return s.kind == Statement_kind::sql; }));
}

size_t Document::checked_statements() const {
  return static_cast<size_t>(std::count_if(m_checks.begin(), m_checks.end(), [](const auto &c) {
    return c.status != Check_status::unchecked;
  }));
}

}  // namespace mdbls
