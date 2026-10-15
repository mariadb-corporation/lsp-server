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

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

#include "mdbls/document.h"
#include "mdbls/parser_pool.h"

namespace mdbls {

namespace {

std::vector<Check_result> check_all(const Snapshot &s, const std::vector<size_t> &which,
                                    Parser_cache &cache) {
  std::vector<Check_result> out;
  for (size_t i : which) {
    auto c = check_statement(*s.text, *s.index, i, cache, s.version_id);
    if (c.status == Check_status::syntax_error) c.error_offset -= s.index->statements()[i].begin;
    out.push_back({i, std::move(c)});
  }
  return out;
}

}  // namespace

TEST(Document, incremental_change_and_positions) {
  Document doc("SELECT 1;\nSELECT 2;\n", 1, {});
  doc.change(std::make_pair(Position{1, 7}, Position{1, 8}), "22");
  EXPECT_EQ(doc.text(), "SELECT 1;\nSELECT 22;\n");
  doc.change(std::nullopt, "SELECT 3");
  EXPECT_EQ(doc.text(), "SELECT 3");
  EXPECT_EQ(doc.index().statements().size(), 1u);
}

TEST(Document, snapshot_keeps_its_text_while_the_document_changes) {
  Document doc("SELECT 1;", 1, {});
  const auto snap = doc.snapshot();
  doc.change(std::make_pair(Position{0, 0}, Position{0, 0}), "-- x\n");
  EXPECT_EQ(*snap->text, "SELECT 1;");
  EXPECT_EQ(doc.text(), "-- x\nSELECT 1;");
  EXPECT_EQ(snap->lines->line_count(), 1u);
  EXPECT_EQ(doc.lines().line_count(), 2u);
}

TEST(Document, late_verdicts_follow_the_edits) {
  static const std::vector<std::string> pieces = {";", "\n", " ", "x", "SELECT 1", "FROM", "'",
                                                  "(", ")", "SELECT 2;\n", "/*", "*/"};
  Parser_cache cache;
  for (unsigned seed = 1; seed <= 200; ++seed) {
    std::mt19937 rng(seed);
    Document doc("SELECT 1;\nSELECT FROM;\nSELECT 3;\nSELECT (;\nSELECT 5;\n", 1, {});
    // Jobs in flight: a snapshot and the statements taken from it.
    std::vector<std::pair<std::shared_ptr<const Snapshot>, std::vector<size_t>>> jobs;
    for (int n = 0; n < 40; ++n) {
      switch (rng() % 3) {
        case 0: {  // an edit
          const auto &text = doc.text();
          const uint32_t at = static_cast<uint32_t>(rng() % (text.size() + 1));
          const uint32_t len =
              std::min<uint32_t>(static_cast<uint32_t>(rng() % 6), static_cast<uint32_t>(text.size() - at));
          const Position a = doc.position(at), b = doc.position(at + len);
          doc.change(std::make_pair(a, b), pieces[rng() % pieces.size()]);
          break;
        }
        case 1:  // a job starts
          jobs.emplace_back(doc.snapshot(), doc.take_unchecked());
          break;
        default:  // the oldest job finishes
          if (!jobs.empty()) {
            const auto results = check_all(*jobs.front().first, jobs.front().second, cache);
            doc.apply_checks(jobs.front().first->generation, results);
            jobs.erase(jobs.begin());
          }
      }
      // Every verdict the document has is the one of its current text.
      for (size_t i = 0; i < doc.checks().size(); ++i) {
        const auto &have = doc.checks()[i];
        if (have.status == Check_status::unchecked) continue;
        const auto want = check_statement(doc.text(), doc.index(), i, cache, 130100);
        ASSERT_EQ(have.status, want.status) << "seed " << seed << " step " << n;
        ASSERT_EQ(have.error_offset, want.error_offset) << "seed " << seed << " step " << n;
        ASSERT_EQ(have.expected, want.expected) << "seed " << seed << " step " << n;
      }
    }
    // Draining the jobs and checking the rest leaves nothing unchecked.
    for (auto &[snap, which] : jobs) doc.apply_checks(snap->generation, check_all(*snap, which, cache));
    const auto snap = doc.snapshot();
    doc.apply_checks(snap->generation, check_all(*snap, doc.take_unchecked(), cache));
    EXPECT_EQ(doc.checks_in_flight(), 0u);
    EXPECT_EQ(doc.checked_statements(), doc.sql_statements()) << "seed " << seed;
  }
}

}  // namespace mdbls
