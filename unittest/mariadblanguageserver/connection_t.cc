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

// Connection URIs (the cases of the MCP plugin's test_config.py), the
// shell's secret store through its plaintext helper, and the workspace
// catalog.

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

#include "mdbls/catalog_view.h"
#include "mdbls/connection_uri.h"
#include "mdbls/live_catalog.h"
#include "mdbls/secret_store.h"
#include "mdbls/workspace_catalog.h"

namespace mdbls {

namespace fs = std::filesystem;

namespace {

void set_env(const char *name, const std::string &value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  if (value.empty()) {
    ::unsetenv(name);
  } else {
    ::setenv(name, value.c_str(), 1);
  }
#endif
}

fs::path temp_dir(const std::string &name) {
  const fs::path p = fs::temp_directory_path() / ("mdbls-" + name + "-" + std::to_string(::getpid()));
  fs::remove_all(p);
  fs::create_directories(p);
  return p;
}

}  // namespace

TEST(Connection_uri, every_spelling_of_one_connection_normalizes_alike) {
  const std::string canonical = "mariadb://root@127.0.0.1:3306";
  EXPECT_EQ(normalize_connection_uri("root@127.0.0.1:3306"), canonical);
  EXPECT_EQ(normalize_connection_uri(canonical), canonical);
  EXPECT_EQ(normalize_connection_uri("MariaDB://root@127.0.0.1"), canonical);
  EXPECT_EQ(normalize_connection_uri("  root:ignored@127.0.0.1:3306/  "), canonical);
  EXPECT_EQ(normalize_connection_uri("root@LOCALHOST"), "mariadb://root@localhost:3306");

  // Another server, user, protocol, schema or option is another connection.
  for (const char *other : {"root@127.0.0.1:3307", "admin@127.0.0.1:3306",
                            "mysql://root@127.0.0.1:3306", "mysqlx://root@127.0.0.1:3306",
                            "root@127.0.0.1:3306/mysql", "root@127.0.0.1:3306?ssl-mode=REQUIRED"}) {
    EXPECT_NE(normalize_connection_uri(other), canonical) << other;
  }
  EXPECT_EQ(normalize_connection_uri("mariadb+ssh://root@127.0.0.1"),
            "mariadb+ssh://root@127.0.0.1:3306");
  EXPECT_NE(normalize_connection_uri("mariadb+ssh://root@127.0.0.1:3306"), canonical);
  // A socket is not a host and gets no port.
  EXPECT_EQ(normalize_connection_uri("root@%2Ftmp%2Fmysql.sock"),
            "mariadb://root@%2Ftmp%2Fmysql.sock");
  EXPECT_EQ(normalize_connection_uri("root@(/tmp/mysql.sock)"),
            "mariadb://root@%2Ftmp%2Fmysql.sock");

  // A fixed point.
  for (const char *uri : {"mariadb://root@127.0.0.1:3306", "mariadb+ssh://root@127.0.0.1:3306",
                          "mysqlx://root@127.0.0.1", "root@%2Ftmp%2Fmysql.sock",
                          "u%40x@[::1]:3307/my%20db?ssl-mode=REQUIRED&a=b"}) {
    const auto once = normalize_connection_uri(uri);
    ASSERT_TRUE(once.has_value()) << uri;
    EXPECT_EQ(normalize_connection_uri(*once), once) << uri;
  }
  for (const char *bad : {"", "   ", "mariadb://", "not a uri", "root@host:99999", "root@host:x"}) {
    EXPECT_FALSE(normalize_connection_uri(bad).has_value()) << bad;
  }
}

TEST(Connection_uri, components) {
  const auto u = parse_connection_uri("mariadb://dba:s%40cret@db.example.com:3307/sakila?ssl-mode=REQUIRED");
  ASSERT_TRUE(u.has_value());
  EXPECT_EQ(u->user, "dba");
  EXPECT_EQ(u->password, "s@cret");
  EXPECT_EQ(u->host, "db.example.com");
  EXPECT_EQ(u->port, 3307u);
  EXPECT_EQ(u->schema, "sakila");
  ASSERT_NE(u->option("SSL-MODE"), nullptr);
  EXPECT_EQ(*u->option("ssl-mode"), "REQUIRED");
  EXPECT_EQ(format_connection_uri(*u), "mariadb://dba@db.example.com:3307/sakila?ssl-mode=REQUIRED");
  const auto v6 = parse_connection_uri("root@[::1]:3310");
  ASSERT_TRUE(v6.has_value());
  EXPECT_TRUE(v6->ipv6);
  EXPECT_EQ(v6->host, "::1");
  EXPECT_EQ(shell_password_secret_ids(*parse_connection_uri("root@localhost")),
            (std::vector<std::string>{"root@localhost", "root@localhost:3306"}));
}

TEST(Secret_store, decodes_the_shells_markers) {
  EXPECT_EQ(decode_shell_secret("rpass word"), "pass word");
  EXPECT_EQ(decode_shell_secret("bcGFzcyJ3b3Jk"), "pass\"word");
  EXPECT_EQ(decode_shell_secret("plain"), "plain");
}

TEST(Secret_store, runs_programs) {
#ifndef _WIN32
  int code = -1;
  const auto out = run_program("/bin/cat", {}, "hello", &code);
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(*out, "hello");
  EXPECT_EQ(code, 0);
  const auto fail = run_program("/bin/sh", {"-c", "exit 3"}, "", &code);
  ASSERT_TRUE(fail.has_value());
  EXPECT_EQ(code, 3);
  EXPECT_FALSE(run_program("/nonexistent/program", {}, "", &code).has_value());
  // A program that does not finish is killed.
  const auto t0 = std::chrono::steady_clock::now();
  EXPECT_FALSE(run_program("/bin/sleep", {"30"}, "", &code, std::chrono::milliseconds(200))
                   .has_value());
  EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::seconds(5));
#endif
}

// Against MariaDB Shell's mariadb-secret-store-plaintext, in a config
// directory of its own.
TEST(Secret_store, plaintext_helper) {
  const std::string bin = MDBLS_SHELL_BIN_DIR;
  if (!fs::exists(fs::path(bin) / "mariadb-secret-store-plaintext")) {
    GTEST_SKIP() << "no mariadb-secret-store-plaintext in " << bin;
  }
  const auto dir = temp_dir("secrets");
  set_env("MARIADB_SHELL_USER_CONFIG_HOME", dir.string());
  set_env("MARIADB_SHELL_CREDENTIAL_STORE_HELPER", "plaintext");
  const Secret_store store({bin, ""});
  ASSERT_EQ(store.error(), "");
  EXPECT_EQ(store.helper_name(), "plaintext");

  auto put = [&](const std::string &type, const std::string &id, const std::string &secret) {
    int code = -1;
    const std::string json =
        "{\"SecretType\":\"" + type + "\",\"SecretID\":\"" + id + "\",\"Secret\":\"" + secret + "\"}";
    ASSERT_TRUE(run_program(store.helper_path(), {"store"}, json, &code).has_value());
    ASSERT_EQ(code, 0);
  };
  std::string error;
  EXPECT_FALSE(store.password_for("root@127.0.0.1", nullptr, &error).has_value());
  EXPECT_NE(error.find("no password"), std::string::npos) << error;

  put("generic", "MCP:CONN:root@127.0.0.1:3306", "rmcp-pw");
  put("generic", "GUI:CONN:mariadb://app@db:3307/shop", "bZ3VpLXB3");  // base64 "gui-pw"
  put("generic", "MCP:Connection:/Sandboxes:old@host", "rold-pw");
  put("password", "shelluser@localhost:3306", "shell-pw");

  std::string source;
  EXPECT_EQ(store.password_for("mariadb://root@127.0.0.1", &source, &error), "mcp-pw");
  EXPECT_EQ(source, "mcp connection root@127.0.0.1:3306");
  EXPECT_EQ(store.password_for("app@DB:3307/shop", nullptr, &error), "gui-pw");
  EXPECT_EQ(store.password_for("old@host:3306", nullptr, &error), "old-pw");
  EXPECT_EQ(store.password_for("shelluser@localhost", &source, &error), "shell-pw");
  EXPECT_EQ(source, "shell credential shelluser@localhost:3306");
  EXPECT_FALSE(store.password_for("app@db:3307", nullptr, &error).has_value());  // no schema

  const auto conns = store.connections(&error);
  ASSERT_EQ(conns.size(), 3u);
  EXPECT_EQ(conns[0].kind, "gui");
  EXPECT_EQ(conns[0].uri, "mariadb://app@db:3307/shop");
  EXPECT_EQ(conns[1].uri, "root@127.0.0.1:3306");
  EXPECT_EQ(conns[2].uri, "old@host");

  set_env("MARIADB_SHELL_CREDENTIAL_STORE_HELPER", "<disabled>");
  EXPECT_NE(Secret_store({bin, ""}).error(), "");
  set_env("MARIADB_SHELL_CREDENTIAL_STORE_HELPER", "");
  set_env("MARIADB_SHELL_USER_CONFIG_HOME", "");
  fs::remove_all(dir);
}

TEST(Live_catalog, reports_a_connection_that_is_not_configured) {
  const auto dir = temp_dir("live");
  set_env("MARIADB_SHELL_USER_CONFIG_HOME", dir.string());
  set_env("MARIADB_SHELL_CREDENTIAL_STORE_HELPER", "plaintext");
  {
    std::mutex m;
    std::condition_variable cv;
    bool updated = false;
    Live_catalog_options o;
    o.secrets.shell_bin_dir = MDBLS_SHELL_BIN_DIR;
    Live_catalog live("nobody@127.0.0.1:1", o, [&] {
      std::lock_guard lock(m);
      updated = true;
      cv.notify_all();
    });
    std::unique_lock lock(m);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(20), [&] { return updated; }));
    EXPECT_EQ(live.snapshot(), nullptr);
    EXPECT_NE(live.error(), "");
    EXPECT_EQ(live.normalized_uri(), "mariadb://nobody@127.0.0.1:1");
  }
  {
    // A password in the URI is refused, never sent.
    std::mutex m;
    std::condition_variable cv;
    bool updated = false;
    Live_catalog live("nobody:pw@127.0.0.1:1", {}, [&] {
      std::lock_guard lock(m);
      updated = true;
      cv.notify_all();
    });
    std::unique_lock lock(m);
    ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(20), [&] { return updated; }));
    EXPECT_NE(live.error().find("must not hold a password"), std::string::npos) << live.error();
    EXPECT_EQ(live.error().find("pw@"), std::string::npos);
  }
  {
    Live_catalog ssh("mariadb+ssh://root@host", {});
    for (int k = 0; k < 200 && ssh.attempts() == 0; ++k) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_NE(ssh.error().find("SSH tunnels"), std::string::npos);
  }
  set_env("MARIADB_SHELL_CREDENTIAL_STORE_HELPER", "");
  set_env("MARIADB_SHELL_USER_CONFIG_HOME", "");
  fs::remove_all(dir);
}

TEST(Workspace_catalog, folds_files_in_order) {
  const auto dir = temp_dir("ws");
  fs::create_directories(dir / "schema" / "b");
  std::ofstream(dir / "schema" / "01_db.sql") << "CREATE DATABASE shop;\nUSE shop;\n";
  std::ofstream(dir / "schema" / "b" / "02_tables.sql")
      << "CREATE TABLE item (id INT, name TEXT);\nCREATE TABLE gone (x INT);\n";
  std::ofstream(dir / "schema" / "03_alter.sql")
      << "ALTER TABLE item ADD COLUMN price DECIMAL(10,2);\nDROP TABLE gone;\n";
  std::ofstream(dir / "notes.txt") << "not sql";
  std::vector<std::string> errors;
  // A directory gives its .sql files sorted by path: 01, 03, b/02 -- so the
  // ALTER comes first here, which is why the order is configurable.
  const auto ordered = expand_catalog_paths(
      {"schema/01_db.sql", "schema/b/02_tables.sql", "schema", "missing.sql"}, dir.string(),
      &errors);
  ASSERT_EQ(ordered.size(), 3u);
  EXPECT_NE(ordered[0].find("01_db.sql"), std::string::npos);
  EXPECT_NE(ordered[1].find("02_tables.sql"), std::string::npos);
  EXPECT_NE(ordered[2].find("03_alter.sql"), std::string::npos);
  ASSERT_EQ(errors.size(), 1u);

  Parser_pool pool(2);
  const auto ws = build_workspace_catalog(ordered, pool, {});
  ASSERT_EQ(ws.files.size(), 3u);
  const Schema *shop = ws.outline.catalog.find("shop");
  ASSERT_NE(shop, nullptr);
  const Table *item = shop->table("item");
  ASSERT_NE(item, nullptr);
  ASSERT_EQ(item->columns.size(), 3u);
  EXPECT_EQ(item->origin.source, 1u);
  EXPECT_EQ(item->columns[2].origin.source, 2u);
  EXPECT_EQ(shop->table("gone"), nullptr);
  const auto &f = ws.files[item->origin.source];
  EXPECT_EQ(f.text.substr(item->origin.name.begin, 4), "item");
  EXPECT_EQ(ws.file(f.uri), &f);
  EXPECT_EQ(file_uri_to_path(f.uri), f.path);
  fs::remove_all(dir);
}

TEST(Workspace_catalog, file_uris) {
  EXPECT_EQ(file_uri_to_path("file:///home/a/b%20c.sql"), "/home/a/b c.sql");
  EXPECT_EQ(file_uri_to_path("file:///c%3A/x/y.sql"), "c:/x/y.sql");
  EXPECT_EQ(file_uri_to_path("untitled:Untitled-1"), "");
  EXPECT_EQ(path_to_file_uri("/home/a/b c.sql"), "file:///home/a/b%20c.sql");
  EXPECT_EQ(path_to_file_uri("c:\\x\\y.sql"), "file:///c%3A/x/y.sql");
}

TEST(Catalog_view, layers_hide_later_ones) {
  Schema doc_schema;
  doc_schema.name = "";
  Table t;
  t.name = "film";
  Column dc;
  dc.name = "doc_col";
  t.columns.push_back(dc);
  doc_schema.tables.push_back(t);
  Schema live_schema;
  live_schema.name = "sakila";
  Table lt;
  lt.name = "film";
  Column lc;
  lc.name = "live_col";
  lt.columns.push_back(lc);
  live_schema.tables.push_back(lt);
  Table la;
  la.name = "actor";
  live_schema.tables.push_back(la);
  Catalog_view v;
  v.add({std::make_shared<Catalog>(Catalog::from_schemas({doc_schema})), Layer_kind::document, {}});
  v.add({std::make_shared<Catalog>(Catalog::from_schemas({live_schema})), Layer_kind::live, "sakila"});
  EXPECT_EQ(v.current_schema(), "sakila");
  auto hit = v.find_table("", "film");
  ASSERT_NE(hit.table, nullptr);
  EXPECT_EQ(hit.table->columns[0].name, "doc_col");
  EXPECT_EQ(hit.layer->kind, Layer_kind::document);
  hit = v.find_table("", "actor");
  ASSERT_NE(hit.table, nullptr);
  EXPECT_EQ(hit.layer->kind, Layer_kind::live);
  std::vector<std::string> names;
  v.for_each_table("", [&](const Catalog_view::Table_hit &h) { names.push_back(h.table->name); });
  EXPECT_EQ(names, (std::vector<std::string>{"film", "actor"}));
  EXPECT_EQ(v.schema_names(), (std::vector<std::string>{"sakila"}));
}

}  // namespace mdbls
