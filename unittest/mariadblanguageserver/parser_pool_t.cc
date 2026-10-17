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

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "mdbls/parser_pool.h"
#include "mdbls/statement_index.h"
#include "mdbls/syntax_check.h"
#include "mdbparse.h"
#include "test_util.h"

namespace mdbls {

using namespace std::chrono_literals;

TEST(Parser_cache, one_parser_per_mode_and_refusals) {
  Parser_cache cache;
  mdbp_parser *a = cache.get("", 130100);
  ASSERT_NE(nullptr, a);
  EXPECT_EQ(a, cache.get("", 130100));
  EXPECT_NE(a, cache.get("ANSI_QUOTES", 130100));
  EXPECT_NE(a, cache.get("", 110803));
  EXPECT_EQ(nullptr, cache.get("ORACLE", 130100));
  EXPECT_EQ(nullptr, cache.get("", 5));  // a release id outside the range
}

TEST(Parser_pool, runs_higher_priority_first) {
  Parser_pool pool(1);
  std::mutex m;
  std::vector<int> order;
  // Hold the only worker until every job is queued.
  std::atomic<bool> release{false};
  pool.submit(100, [&](Parser_cache &) {
    while (!release) std::this_thread::sleep_for(1ms);
  });
  for (int p : {1, 3, 2, 3}) {
    pool.submit(p, [&, p](Parser_cache &) {
      std::lock_guard lock(m);
      order.push_back(p);
    });
  }
  release = true;
  pool.wait();
  EXPECT_EQ((std::vector<int>{3, 3, 2, 1}), order);
}

TEST(Parser_pool, idle_workers_exit_and_come_back) {
  Parser_pool pool(4, 50ms);
  std::atomic<int> ran{0};
  for (int i = 0; i < 16; ++i) {
    pool.submit(0, [&](Parser_cache &) {
      std::this_thread::sleep_for(5ms);
      ++ran;
    });
  }
  pool.wait();
  EXPECT_EQ(16, ran);
  EXPECT_GE(pool.live_threads(), 1u);
  EXPECT_LE(pool.live_threads(), 4u);

  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (pool.live_threads() > 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(10ms);
  }
  EXPECT_EQ(0u, pool.live_threads());

  pool.submit(0, [&](Parser_cache &) { ++ran; });
  pool.wait();
  EXPECT_EQ(17, ran);
}

TEST(Parser_pool, destroyed_with_queued_jobs) {
  std::atomic<int> ran{0};
  {
    Parser_pool pool(1);
    for (int i = 0; i < 100; ++i) {
      pool.submit(0, [&](Parser_cache &) {
        std::this_thread::sleep_for(1ms);
        ++ran;
      });
    }
  }  // drops what was not started
  EXPECT_LE(ran, 100);
}

TEST(Syntax_check, reports_errors_with_positions) {
  const std::string text =
      "SELECT 1;\n"
      "SELECT * FROM t WHERE;\n"
      "-- a comment\n"
      "SELECT FROM;\n"
      "CREATE REST SERVICE /svc;\n";
  const auto index = Statement_index::build(text);
  Parser_pool pool(2);
  const auto r = check_statements(text, index, pool);
  ASSERT_EQ(index.statements().size(), r.size());
  ASSERT_EQ(5u, r.size());
  EXPECT_EQ(Check_status::ok, r[0].status);
  EXPECT_EQ(Check_status::syntax_error, r[1].status);
  EXPECT_EQ(text.find("WHERE;") + 5, r[1].error_offset);  // at the statement's end
  EXPECT_EQ(0u, r[1].error_length);
  EXPECT_EQ(Check_status::unchecked, r[2].status);         // the comment
  EXPECT_EQ(Check_status::syntax_error, r[3].status);
  EXPECT_EQ(text.find("FROM;"), r[3].error_offset);
  EXPECT_EQ("FROM", r[3].error_token);
  EXPECT_FALSE(r[3].expected.empty());
  EXPECT_EQ(Check_status::ok, r[4].status);
}

TEST(Syntax_check, unsupported_mode_is_not_checked) {
  const std::string text = "SET sql_mode = ORACLE;\nSELECT 1 FROM DUAL;\nSET sql_mode = DEFAULT;\nSELECT 2;";
  const auto index = Statement_index::build(text);
  Parser_pool pool(1);
  const auto r = check_statements(text, index, pool);
  ASSERT_EQ(4u, r.size());
  EXPECT_EQ(Check_status::ok, r[0].status);
  EXPECT_EQ(Check_status::unsupported_mode, r[1].status);
  EXPECT_EQ(Check_status::unsupported_mode, r[2].status);
  EXPECT_EQ(Check_status::ok, r[3].status);
}

// The same answers with one thread and with many, on a file large enough to
// make many jobs.
TEST(Syntax_check, threads_agree) {
  const std::string one = test::read_fixture("mrs_grammar_test.sql");
  std::string text;
  for (int i = 0; i < 20; ++i) text += one + "\nSELECT FROM WHERE;\n";
  const auto index = Statement_index::build(text);

  Check_options small;
  small.chunk_bytes = 2048;
  Parser_pool single(1);
  const auto a = check_statements(text, index, single, small);
  Parser_pool many(std::max(4u, std::thread::hardware_concurrency()));
  const auto b = check_statements(text, index, many, small);

  ASSERT_EQ(a.size(), b.size());
  size_t errors = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].status, b[i].status) << i;
    EXPECT_EQ(a[i].error_offset, b[i].error_offset) << i;
    EXPECT_EQ(a[i].expected, b[i].expected) << i;
    if (a[i].status == Check_status::syntax_error) ++errors;
  }
  EXPECT_EQ(20u, errors);
}

}  // namespace mdbls
