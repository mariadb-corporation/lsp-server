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

// Details on demand: a live catalog whose reading leaves stubs, and the
// details read on its own thread when a request asks.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "mdbls/live_catalog.h"

namespace mdbls {

namespace {

using namespace std::chrono_literals;

// A reading with one schema of two stub tables.
std::optional<Live_snapshot> stub_reading(const std::string &, std::string *, bool *,
                                          const std::function<bool()> &) {
  Schema s;
  s.name = "shop";
  for (const char *name : {"customer", "orders"}) {
    Table t;
    t.name = name;
    t.stub = true;
    s.tables.push_back(t);
  }
  Live_snapshot snap;
  snap.catalog = std::make_shared<const Catalog>(Catalog::from_schemas({s}));
  return snap;
}

// The details of a table: one column named after it.
Catalog_details table_details(const std::vector<Object_key> &objects) {
  Catalog_details d;
  for (const auto &o : objects) {
    Table t;
    t.name = o.name;
    Column c;
    c.name = o.name + "_id";
    t.columns.push_back(c);
    d.tables.emplace_back(o.schema, t);
  }
  return d;
}

std::shared_ptr<const Live_snapshot> first_snapshot(const Live_catalog &lc) {
  for (int i = 0; i < 500 && !lc.snapshot(); ++i) std::this_thread::sleep_for(10ms);
  return lc.snapshot();
}

const Table *table(const std::shared_ptr<const Live_snapshot> &snap, const char *name) {
  const Schema *s = snap->catalog->find("shop");
  return s != nullptr ? s->table(name) : nullptr;
}

}  // namespace

TEST(Live_catalog_details, read_once_when_asked) {
  std::atomic<int> fetches{0};
  std::vector<Object_key> asked;
  Live_catalog_options o;
  o.fetch = stub_reading;
  o.fetch_details = [&](const std::string &, const std::vector<Object_key> &objects, std::string *,
                        const std::function<bool()> &) -> std::optional<Catalog_details> {
    ++fetches;
    asked = objects;
    return table_details(objects);
  };
  Live_catalog lc("mariadb://u@h:1", o);
  ASSERT_TRUE(first_snapshot(lc));
  EXPECT_TRUE(table(lc.snapshot(), "customer")->stub);

  EXPECT_TRUE(lc.details({{"shop", "customer", "table"}}, 5s));
  EXPECT_EQ(fetches, 1);
  EXPECT_EQ(asked, (std::vector<Object_key>{{"shop", "customer", "table"}}));
  const Table *customer = table(lc.snapshot(), "customer");
  EXPECT_FALSE(customer->stub);
  ASSERT_EQ(customer->columns.size(), 1u);
  EXPECT_EQ(customer->columns[0].name, "customer_id");
  EXPECT_TRUE(table(lc.snapshot(), "orders")->stub);

  // Read already: not asked for again.
  EXPECT_TRUE(lc.details({{"shop", "customer", "table"}}, 5s));
  EXPECT_EQ(fetches, 1);
}

TEST(Live_catalog_details, a_failure_is_not_asked_for_again) {
  std::atomic<int> fetches{0};
  Live_catalog_options o;
  o.fetch = stub_reading;
  o.fetch_details = [&](const std::string &, const std::vector<Object_key> &, std::string *error,
                        const std::function<bool()> &) -> std::optional<Catalog_details> {
    ++fetches;
    *error = "gone";
    return std::nullopt;
  };
  Live_catalog lc("mariadb://u@h:1", o);
  ASSERT_TRUE(first_snapshot(lc));
  EXPECT_TRUE(lc.details({{"shop", "orders", "table"}}, 5s));
  EXPECT_TRUE(table(lc.snapshot(), "orders")->stub);
  EXPECT_TRUE(lc.details({{"shop", "orders", "table"}}, 5s));
  EXPECT_EQ(fetches, 1);
  // A reading of the catalog forgets the failure.
  lc.refresh();
  for (int i = 0; i < 500 && lc.attempts() < 2; ++i) std::this_thread::sleep_for(10ms);
  EXPECT_TRUE(lc.details({{"shop", "orders", "table"}}, 5s));
  EXPECT_EQ(fetches, 2);
}

TEST(Live_catalog_details, slow_details_arrive_after_the_wait) {
  Live_catalog_options o;
  o.fetch = stub_reading;
  o.fetch_details = [&](const std::string &, const std::vector<Object_key> &objects, std::string *,
                        const std::function<bool()> &) -> std::optional<Catalog_details> {
    std::this_thread::sleep_for(300ms);
    return table_details(objects);
  };
  Live_catalog lc("mariadb://u@h:1", o);
  ASSERT_TRUE(first_snapshot(lc));
  EXPECT_FALSE(lc.details({{"shop", "orders", "table"}}, 20ms));
  for (int i = 0; i < 200 && table(lc.snapshot(), "orders")->stub; ++i) {
    std::this_thread::sleep_for(10ms);
  }
  EXPECT_FALSE(table(lc.snapshot(), "orders")->stub);
}

TEST(Live_catalog_details, nothing_to_wait_for_without_a_fetcher) {
  Live_catalog_options o;
  o.fetch = stub_reading;
  Live_catalog lc("mariadb://u@h:1", o);
  ASSERT_TRUE(first_snapshot(lc));
  const auto started = std::chrono::steady_clock::now();
  EXPECT_FALSE(lc.details({{"shop", "orders", "table"}}, 5s));
  EXPECT_LT(std::chrono::steady_clock::now() - started, 1s);
}

TEST(Live_catalog_details, shutting_down_lets_a_waiting_request_go) {
  std::atomic<bool> entered{false};
  Live_catalog_options o;
  o.fetch = stub_reading;
  o.fetch_details = [&](const std::string &, const std::vector<Object_key> &objects, std::string *,
                        const std::function<bool()> &cancelled) -> std::optional<Catalog_details> {
    entered = true;
    while (!cancelled()) std::this_thread::sleep_for(5ms);
    return table_details(objects);
  };
  auto lc = std::make_unique<Live_catalog>("mariadb://u@h:1", o);
  ASSERT_TRUE(first_snapshot(*lc));
  std::thread waiter([&] { lc->details({{"shop", "orders", "table"}}, 10s); });
  for (int i = 0; i < 500 && !entered; ++i) std::this_thread::sleep_for(5ms);
  const auto started = std::chrono::steady_clock::now();
  lc.reset();
  waiter.join();
  EXPECT_LT(std::chrono::steady_clock::now() - started, 5s);
}

}  // namespace mdbls
