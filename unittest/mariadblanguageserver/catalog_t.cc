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

#include <string>

#include "mdbls/outline.h"
#include "test_util.h"

namespace mdbls {

namespace {

Outline outline_of(const std::string &text) {
  static Parser_pool pool(2);
  const auto index = Statement_index::build(text);
  return build_outline(text, index, pool);
}

const Schema *schema(const Outline &o, std::string_view name) {
  for (const auto &s : o.catalog.schemas()) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

const Table *table(const Outline &o, std::string_view s, std::string_view t) {
  const Schema *sc = schema(o, s);
  if (sc == nullptr) return nullptr;
  for (const auto &tb : sc->tables) {
    if (tb.name == t) return &tb;
  }
  return nullptr;
}

std::vector<std::string> column_names(const Table &t) {
  std::vector<std::string> out;
  for (const auto &c : t.columns) out.push_back(c.name);
  return out;
}

}  // namespace

TEST(Catalog, use_and_qualified_names) {
  const auto o = outline_of(
      "CREATE TABLE loose (a INT);\n"
      "CREATE DATABASE shop CHARACTER SET latin1;\n"
      "USE shop;\n"
      "CREATE TABLE `order` (id INT PRIMARY KEY, `note``s` TEXT COMMENT 'it''s');\n"
      "CREATE TABLE other.t (x INT);\n");
  ASSERT_NE(nullptr, table(o, "", "loose"));
  const Schema *shop = schema(o, "shop");
  ASSERT_NE(nullptr, shop);
  EXPECT_TRUE(shop->declared);
  ASSERT_EQ(1u, shop->options.size());
  EXPECT_EQ("latin1", shop->options[0].second);
  const Table *order = table(o, "shop", "order");
  ASSERT_NE(nullptr, order);
  EXPECT_EQ((std::vector<std::string>{"id", "note`s"}), column_names(*order));
  EXPECT_EQ("it's", order->columns[1].comment);
  EXPECT_FALSE(order->columns[0].nullable);  // PRIMARY KEY implies NOT NULL
  EXPECT_NE(nullptr, table(o, "other", "t"));
  EXPECT_EQ(o.ddl_statements, o.applied);
}

TEST(Catalog, alter_table) {
  const auto o = outline_of(
      "CREATE TABLE t (a INT, b INT, c INT, KEY kb (b));\n"
      "ALTER TABLE t ADD COLUMN d DATE AFTER a, DROP COLUMN c;\n"
      "ALTER TABLE t MODIFY b BIGINT NOT NULL DEFAULT 0;\n"
      "ALTER TABLE t CHANGE a a2 VARCHAR(10);\n"
      "ALTER TABLE t RENAME COLUMN d TO d2;\n"
      "ALTER TABLE t ADD PRIMARY KEY (a2), ADD UNIQUE KEY ub (b), DROP INDEX kb;\n"
      "ALTER TABLE t ADD CONSTRAINT fk FOREIGN KEY (b) REFERENCES p (id) ON DELETE CASCADE;\n"
      "ALTER TABLE t ALTER COLUMN d2 SET DEFAULT '2026-01-01';\n"
      "ALTER TABLE t ENGINE=MyISAM, COMMENT='x';\n"
      "ALTER TABLE t RENAME TO t2;\n");
  EXPECT_EQ(nullptr, table(o, "", "t"));
  const Table *t = table(o, "", "t2");
  ASSERT_NE(nullptr, t);
  EXPECT_EQ((std::vector<std::string>{"a2", "d2", "b"}), column_names(*t));
  EXPECT_EQ("BIGINT", t->columns[2].type);
  EXPECT_FALSE(t->columns[2].nullable);
  EXPECT_EQ("0", t->columns[2].default_value);
  EXPECT_EQ("VARCHAR(10)", t->columns[0].type);
  EXPECT_EQ("'2026-01-01'", t->columns[1].default_value);
  ASSERT_EQ(2u, t->indexes.size());
  EXPECT_EQ("primary", t->indexes[0].kind);
  EXPECT_EQ("ub", t->indexes[1].name);
  ASSERT_EQ(1u, t->foreign_keys.size());
  EXPECT_EQ("fk", t->foreign_keys[0].name);
  EXPECT_EQ("p", t->foreign_keys[0].ref_table);
  EXPECT_EQ("CASCADE", t->foreign_keys[0].on_delete);
  bool engine = false;
  for (const auto &[k, v] : t->options) engine = engine || (k == "engine" && v == "MyISAM");
  EXPECT_TRUE(engine);
}

TEST(Catalog, drop_rename_like_and_if_not_exists) {
  const auto o = outline_of(
      "CREATE TABLE a (x INT);\n"
      "CREATE TABLE IF NOT EXISTS a (y INT);\n"
      "CREATE TABLE b LIKE a;\n"
      "CREATE TABLE c (z INT);\n"
      "RENAME TABLE c TO c2, b TO s2.b;\n"
      "DROP TABLE IF EXISTS a, nope;\n"
      "CREATE INDEX ix ON c2 (z);\n"
      "CREATE SEQUENCE seq;\n"
      "CREATE VIEW v AS SELECT 1;\n"
      "DROP VIEW v;\n");
  EXPECT_EQ(nullptr, table(o, "", "a"));
  const Table *b = table(o, "s2", "b");
  ASSERT_NE(nullptr, b);
  EXPECT_EQ((std::vector<std::string>{"x"}), column_names(*b));  // LIKE copied a
  const Table *c2 = table(o, "", "c2");
  ASSERT_NE(nullptr, c2);
  ASSERT_EQ(1u, c2->indexes.size());
  EXPECT_EQ("ix", c2->indexes[0].name);
  const Schema *dflt = schema(o, "");
  ASSERT_NE(nullptr, dflt);
  EXPECT_EQ(1u, dflt->sequences.size());
  EXPECT_TRUE(dflt->views.empty());
}

TEST(Catalog, routines_triggers_events) {
  const auto o = outline_of(
      "DELIMITER //\n"
      "CREATE PROCEDURE db.p(IN a INT, OUT b VARCHAR(10), c DATE) BEGIN SELECT 1; END//\n"
      "CREATE FUNCTION f(x INT) RETURNS DECIMAL(10,2) DETERMINISTIC RETURN x//\n"
      "CREATE TRIGGER trg BEFORE INSERT ON t FOR EACH ROW SET NEW.a = 1//\n"
      "CREATE EVENT ev ON SCHEDULE EVERY 1 DAY DO SELECT 1//\n"
      "DELIMITER ;\n"
      "DROP FUNCTION f;\n");
  const Schema *db = schema(o, "db");
  ASSERT_NE(nullptr, db);
  ASSERT_EQ(1u, db->routines.size());
  const Routine &p = db->routines[0];
  EXPECT_EQ("procedure", p.kind);
  ASSERT_EQ(3u, p.parameters.size());
  EXPECT_EQ("OUT", p.parameters[1].mode);
  EXPECT_EQ("b", p.parameters[1].name);
  EXPECT_EQ("VARCHAR(10)", p.parameters[1].type);
  EXPECT_EQ("", p.parameters[2].mode);
  const Schema *dflt = schema(o, "");
  ASSERT_NE(nullptr, dflt);
  EXPECT_TRUE(dflt->routines.empty());  // f was dropped
  ASSERT_EQ(1u, dflt->triggers.size());
  EXPECT_EQ("BEFORE", dflt->triggers[0].timing);
  EXPECT_EQ("INSERT", dflt->triggers[0].events);
  EXPECT_EQ("t", dflt->triggers[0].table);
  EXPECT_EQ(1u, dflt->events.size());
}

TEST(Catalog, rest_objects) {
  const auto o = outline_of(
      "CREATE REST SERVICE /svc;\n"
      "USE REST SERVICE /svc;\n"
      "CREATE REST SCHEMA /sch FROM sakila;\n"
      "CREATE REST VIEW /actor ON SCHEMA /sch AS sakila.actor;\n"
      "CREATE REST PROCEDURE /p ON SERVICE /svc SCHEMA /sch AS sakila.film_in_stock;\n"
      "CREATE REST CONTENT SET /cs;\n"
      "CREATE REST AUTH APP 'app' VENDOR MariaDB;\n"
      "CREATE REST ROLE 'admin';\n"
      "CREATE REST USER 'jane'@'app';\n"
      "DROP REST VIEW /actor;\n");
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(1u, svcs.size());
  EXPECT_EQ("/svc", svcs[0].path);
  ASSERT_EQ(1u, svcs[0].schemas.size());
  EXPECT_EQ("/sch", svcs[0].schemas[0].path);
  EXPECT_EQ("sakila", svcs[0].schemas[0].schema);
  ASSERT_EQ(1u, svcs[0].schemas[0].objects.size());
  EXPECT_EQ("procedure", svcs[0].schemas[0].objects[0].kind);
  EXPECT_EQ("sakila.film_in_stock", svcs[0].schemas[0].objects[0].object);
  EXPECT_EQ(1u, svcs[0].content_sets.size());
  ASSERT_EQ(1u, o.catalog.rest_auth_apps().size());
  EXPECT_EQ("app", o.catalog.rest_auth_apps()[0].name);
  ASSERT_EQ(1u, o.catalog.rest_users().size());
  EXPECT_EQ("jane@app", o.catalog.rest_users()[0].name);
}

// DROP REST takes the object's kind, its FROM selector and the current
// service into account; it never creates a service.
TEST(Catalog, rest_drop_is_scoped) {
  const auto o = outline_of(
      "CREATE REST SERVICE /a;\n"
      "CREATE REST SERVICE /b;\n"
      "CREATE REST SCHEMA /s ON SERVICE /a FROM sakila;\n"
      "CREATE REST SCHEMA /s ON SERVICE /b FROM sakila;\n"
      "CREATE REST VIEW /x ON SERVICE /a SCHEMA /s AS sakila.actor;\n"
      "CREATE REST PROCEDURE /x ON SERVICE /a SCHEMA /s AS sakila.film_in_stock;\n"
      "CREATE REST VIEW /x ON SERVICE /b SCHEMA /s AS sakila.actor;\n"
      "CREATE REST CONTENT SET /cs ON SERVICE /a;\n"
      "CREATE REST CONTENT SET /cs ON SERVICE /b;\n"
      "USE REST SERVICE /a;\n"
      "DROP REST VIEW /x;\n"
      "DROP REST CONTENT SET /cs;\n"
      "DROP REST PROCEDURE /x FROM SERVICE /nowhere DATABASE /s;\n");
  const auto &svcs = o.catalog.rest_services();
  ASSERT_EQ(2u, svcs.size());  // /nowhere was not created by the DROP
  const auto &a = svcs[0], &b = svcs[1];
  ASSERT_EQ(1u, a.schemas[0].objects.size());
  EXPECT_EQ("procedure", a.schemas[0].objects[0].kind);  // the view went, not the procedure
  EXPECT_TRUE(a.content_sets.empty());
  ASSERT_EQ(1u, b.schemas[0].objects.size());  // service /b was not searched
  EXPECT_EQ("view", b.schemas[0].objects[0].kind);
  EXPECT_EQ(1u, b.content_sets.size());
}

TEST(Catalog, named_key_replaces_its_namesake) {
  const auto o = outline_of(
      "CREATE TABLE t (a INT, b INT, KEY k (a), KEY k (b), UNIQUE (a), UNIQUE (b));\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(3u, t->indexes.size());  // k once, the two unnamed keys both kept
  EXPECT_EQ("k", t->indexes[0].name);
  EXPECT_EQ(std::vector<std::string>{"b"}, t->indexes[0].columns);
}

TEST(Catalog, notes_for_what_cannot_be_applied) {
  const auto o = outline_of(
      "ALTER TABLE missing ADD COLUMN x INT;\n"
      "CREATE TABLE broken (a INT,);\n");
  ASSERT_EQ(2u, o.notes.size());
  EXPECT_NE(std::string::npos, o.notes[0].second.find("does not create this table"));
  EXPECT_NE(std::string::npos, o.notes[1].second.find("syntax error"));
}

// The grammar rule names the fold looks for must exist; a MariaDB update
// that renames one fails here instead of silently dropping objects.
TEST(Catalog, grammar_rules_exist) {
  EXPECT_EQ(std::vector<std::string>{}, catalog_missing_grammar_symbols());
}

// The whole outline of the sakila schema, against the reviewed copy in
// tests/fixtures/outline/.  Regenerate with:
//   mariadb-lsp outline tests/fixtures/sql/sakila-schema.sql > tests/fixtures/outline/sakila-schema.json
// (then drop the "file" member) and review the diff.
TEST(Catalog, sakila_outline_matches_golden_file) {
  const std::string text = test::read_fixture("sakila-schema.sql");
  std::string golden = test::read_fixture("../outline/sakila-schema.json");
  while (!golden.empty() && (golden.back() == '\n' || golden.back() == '\r')) golden.pop_back();
  ASSERT_FALSE(golden.empty());
  static Parser_pool pool(2);
  const auto index = Statement_index::build(text);
  const auto outline = build_outline(text, index, pool);
  EXPECT_EQ(golden, outline_json(outline, text, Line_map(text)));
}

TEST(Catalog, may_change_catalog) {
  EXPECT_TRUE(may_change_catalog("CREATE TABLE t (a INT)"));
  EXPECT_TRUE(may_change_catalog("  /*!40101 drop table t */"));
  EXPECT_TRUE(may_change_catalog("-- c\nUSE db"));
  EXPECT_FALSE(may_change_catalog("INSERT INTO t VALUES (1)"));
  EXPECT_FALSE(may_change_catalog("SELECT 1"));
  EXPECT_FALSE(may_change_catalog("CREATED"));
}

}  // namespace mdbls
