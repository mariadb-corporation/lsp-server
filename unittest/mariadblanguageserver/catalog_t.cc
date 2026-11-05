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
  EXPECT_EQ(std::vector<std::string>{"b"}, t->indexes[0].columns());
}

TEST(Catalog, notes_for_what_cannot_be_applied) {
  const auto o = outline_of(
      "ALTER TABLE missing ADD COLUMN x INT;\n"
      "CREATE TABLE broken (a INT,);\n");
  ASSERT_EQ(2u, o.notes.size());
  EXPECT_NE(std::string::npos, o.notes[0].second.find("does not create this table"));
  EXPECT_NE(std::string::npos, o.notes[1].second.find("syntax error"));
}

// Column attributes the outline keeps (they make the schema snapshot of an
// MSM project).  CHARACTER SET and COLLATE are not part of the type.
TEST(Catalog, column_attributes) {
  const auto o = outline_of(
      "CREATE TABLE t (\n"
      "  a VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL,\n"
      "  ts TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6) INVISIBLE,\n"
      "  g INT AS (a + 1) PERSISTENT, g2 INT GENERATED ALWAYS AS (2) STORED, g3 INT AS (3),\n"
      "  v INT WITHOUT SYSTEM VERSIONING, w INT WITH SYSTEM VERSIONING,\n"
      "  b CHAR(3) BINARY, x ENUM('a','b') CHARSET latin1, y TEXT ASCII, z CHAR(2) UNICODE,\n"
      "  u INT UNSIGNED ZEROFILL, e INT ENCRYPTED=YES,\n"
      "  rs TIMESTAMP(6) AS ROW START, re TIMESTAMP(6) AS ROW END\n"
      ");\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(15u, t->columns.size());
  const auto &c = t->columns;
  EXPECT_EQ("VARCHAR(255)", c[0].type);
  EXPECT_EQ("utf8mb4", c[0].charset);
  EXPECT_EQ("utf8mb4_bin", c[0].collation);
  EXPECT_EQ("CURRENT_TIMESTAMP(6)", c[1].on_update);
  EXPECT_TRUE(c[1].invisible);
  EXPECT_FALSE(c[0].invisible);
  EXPECT_EQ(std::nullopt, c[0].on_update);
  EXPECT_EQ("PERSISTENT", c[2].generated_storage);
  EXPECT_EQ("STORED", c[3].generated_storage);
  EXPECT_EQ(std::nullopt, c[4].generated_storage);  // not written
  EXPECT_EQ("(3)", c[4].generated);
  EXPECT_EQ(std::nullopt, c[0].generated_storage);
  EXPECT_EQ(false, c[5].system_versioning);
  EXPECT_EQ(true, c[6].system_versioning);
  EXPECT_EQ(std::nullopt, c[0].system_versioning);
  EXPECT_EQ("CHAR(3) BINARY", c[7].type);
  EXPECT_EQ(std::nullopt, c[7].charset);
  EXPECT_EQ("ENUM('a','b')", c[8].type);
  EXPECT_EQ("latin1", c[8].charset);
  EXPECT_EQ("TEXT", c[9].type);
  EXPECT_EQ("latin1", c[9].charset);  // ASCII
  EXPECT_EQ("CHAR(2)", c[10].type);
  EXPECT_EQ("ucs2", c[10].charset);  // UNICODE
  EXPECT_EQ("INT UNSIGNED ZEROFILL", c[11].type);
  ASSERT_EQ(1u, c[12].options.size());
  EXPECT_EQ("encrypted", c[12].options[0].first);
  EXPECT_EQ("YES", c[12].options[0].second);
  EXPECT_EQ("START", c[13].generated_row);
  EXPECT_EQ("END", c[14].generated_row);
  EXPECT_EQ(std::nullopt, c[13].generated);
  // Completion shows attributes(), hover definition(): both keep CHARACTER SET and COLLATE.
  EXPECT_EQ("VARCHAR(255) CHARACTER SET utf8mb4 COLLATE utf8mb4_bin NOT NULL", c[0].attributes());
  EXPECT_EQ("TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6) ON UPDATE CURRENT_TIMESTAMP(6) INVISIBLE",
            c[1].definition());
  EXPECT_EQ("INT AS (a + 1) PERSISTENT", c[2].definition());
}

TEST(Catalog, quoted_collation_reads_alike_everywhere) {
  const auto o = outline_of("CREATE TABLE t (a CHAR(1) COLLATE 'utf8mb4_bin', b CHAR(1) NOT NULL COLLATE 'utf8mb4_bin');\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(2u, t->columns.size());
  EXPECT_EQ("utf8mb4_bin", t->columns[0].collation);
  EXPECT_EQ("utf8mb4_bin", t->columns[1].collation);
}

TEST(Catalog, column_check_is_a_table_check) {
  const auto o = outline_of(
      "CREATE TABLE t (j JSON CHECK (JSON_VALID(j)), k INT, CONSTRAINT ck CHECK (k > 0));\n"
      "ALTER TABLE t MODIFY k INT CHECK (k < 9);\n"
      "ALTER TABLE t ADD CONSTRAINT IF NOT EXISTS ck2 CHECK (k <> 5);\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(4u, t->checks.size());
  EXPECT_EQ("", t->checks[0].name);
  EXPECT_EQ("j", t->checks[0].column);
  EXPECT_EQ("JSON_VALID(j)", t->checks[0].expression);
  EXPECT_EQ("ck", t->checks[1].name);
  EXPECT_EQ("", t->checks[1].column);
  EXPECT_EQ("k", t->checks[2].column);
  EXPECT_EQ("k < 9", t->checks[2].expression);
  EXPECT_EQ("ck2", t->checks[3].name);

  // MODIFY replaces the column's own check; DROP COLUMN drops it.
  const auto o2 = outline_of(
      "CREATE TABLE t (j JSON CHECK (JSON_VALID(j)), k INT);\n"
      "ALTER TABLE t MODIFY j JSON;\n"
      "ALTER TABLE t MODIFY k INT CHECK (k > 1);\n"
      "ALTER TABLE t DROP COLUMN k;\n");
  const Table *t2 = table(o2, "", "t");
  ASSERT_NE(nullptr, t2);
  EXPECT_TRUE(t2->checks.empty());
}

// DROP COLUMN takes the column out of its indexes; an index left without
// key parts goes with it, as on the server.
TEST(Catalog, drop_column_leaves_its_indexes) {
  const auto o = outline_of(
      "CREATE TABLE t (a INT, b INT, KEY k (a, b), KEY ka (a), UNIQUE u (b));\n"
      "ALTER TABLE t DROP COLUMN a;\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(2u, t->indexes.size());
  EXPECT_EQ("k", t->indexes[0].name);
  EXPECT_EQ(std::vector<std::string>{"b"}, t->indexes[0].columns());
  EXPECT_EQ("u", t->indexes[1].name);
}

TEST(Catalog, index_details) {
  const auto o = outline_of(
      "CREATE TABLE t (a VARCHAR(20), b INT, g GEOMETRY NOT NULL, s DATE, e DATE, txt TEXT,\n"
      "  PERIOD FOR p (s, e),\n"
      "  KEY k1 (a(10) DESC, b ASC) USING HASH COMMENT 'it''s' IGNORED,\n"
      "  KEY k2 USING BTREE (b) KEY_BLOCK_SIZE=8 NOT IGNORED,\n"
      "  SPATIAL KEY sg (g),\n"
      "  UNIQUE KEY u (b, p WITHOUT OVERLAPS),\n"
      "  FULLTEXT KEY ft (txt) WITH PARSER ngram);\n"
      "ALTER TABLE t ADD INDEX ai TYPE RTREE (b), ALTER INDEX k2 IGNORED;\n"
      "CREATE UNIQUE INDEX ci USING HASH ON t (a(4) DESC) COMMENT 'c' IGNORED;\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(7u, t->indexes.size());
  const Index &k1 = t->indexes[0];
  EXPECT_EQ((std::vector<std::string>{"a", "b"}), k1.columns());
  ASSERT_EQ(2u, k1.key_parts.size());
  EXPECT_EQ("a", k1.key_parts[0].column);
  EXPECT_EQ(10u, k1.key_parts[0].length);
  EXPECT_EQ("DESC", k1.key_parts[0].order);
  EXPECT_EQ(std::nullopt, k1.key_parts[1].length);
  EXPECT_EQ("ASC", k1.key_parts[1].order);
  EXPECT_EQ(std::nullopt, k1.key_parts[0].expression);
  EXPECT_EQ("HASH", k1.using_);
  EXPECT_EQ("it's", k1.comment);
  EXPECT_TRUE(k1.ignored);
  const Index &k2 = t->indexes[1];
  EXPECT_EQ("BTREE", k2.using_);
  EXPECT_TRUE(k2.ignored);  // ALTER INDEX k2 IGNORED
  ASSERT_EQ(1u, k2.options.size());
  EXPECT_EQ("key_block_size", k2.options[0].first);
  EXPECT_EQ("8", k2.options[0].second);
  const Index &sg = t->indexes[2];
  EXPECT_EQ("spatial", sg.kind);
  EXPECT_EQ(std::vector<std::string>{"g"}, sg.columns());
  ASSERT_EQ(1u, sg.key_parts.size());
  EXPECT_EQ("p", t->indexes[3].without_overlaps);
  EXPECT_EQ(std::vector<std::string>{"b"}, t->indexes[3].columns());
  ASSERT_EQ(1u, t->indexes[4].options.size());
  EXPECT_EQ("parser", t->indexes[4].options[0].first);
  EXPECT_EQ("ngram", t->indexes[4].options[0].second);
  EXPECT_EQ("RTREE", t->indexes[5].using_);
  EXPECT_FALSE(t->indexes[5].ignored);
  const Index &ci = t->indexes[6];
  EXPECT_EQ("ci", ci.name);
  EXPECT_EQ("unique", ci.kind);
  EXPECT_EQ("HASH", ci.using_);
  EXPECT_EQ("c", ci.comment);
  EXPECT_TRUE(ci.ignored);
  ASSERT_EQ(1u, ci.key_parts.size());
  EXPECT_EQ(4u, ci.key_parts[0].length);
  EXPECT_EQ("DESC", ci.key_parts[0].order);
}

TEST(Catalog, foreign_key_names_and_match) {
  const auto o = outline_of(
      "CREATE TABLE c (pid INT, pid2 INT, pid3 INT REFERENCES p (id) MATCH FULL,\n"
      "  CONSTRAINT fk_c1 FOREIGN KEY (pid) REFERENCES p (id) MATCH SIMPLE,\n"
      "  CONSTRAINT fk_c2 FOREIGN KEY fkidx (pid2) REFERENCES p (id2),\n"
      "  FOREIGN KEY fk3 (pid) REFERENCES p (id));\n"
      "ALTER TABLE c ADD CONSTRAINT fk4 FOREIGN KEY ix4 (pid2) REFERENCES p (id2) MATCH PARTIAL;\n"
      "ALTER TABLE c DROP FOREIGN KEY fk_c2;\n");
  const Table *c = table(o, "", "c");
  ASSERT_NE(nullptr, c);
  ASSERT_EQ(4u, c->foreign_keys.size());
  const auto &f = c->foreign_keys;
  EXPECT_EQ("", f[0].name);
  EXPECT_EQ("FULL", f[0].match);
  EXPECT_EQ("fk_c1", f[1].name);
  EXPECT_EQ("", f[1].index_name);
  EXPECT_EQ("SIMPLE", f[1].match);
  EXPECT_EQ("fk3", f[2].name);  // fk_c2 (index fkidx) was dropped by its constraint name
  EXPECT_EQ("fk3", f[2].index_name);
  EXPECT_EQ("", f[2].match);
  EXPECT_EQ("fk4", f[3].name);
  EXPECT_EQ("ix4", f[3].index_name);
  EXPECT_EQ("PARTIAL", f[3].match);
}

TEST(Catalog, table_partitioning_versioning_periods) {
  const auto o = outline_of(
      "CREATE TABLE t (a INT, s DATE, e DATE, rs TIMESTAMP(6) AS ROW START,\n"
      "  re TIMESTAMP(6) AS ROW END, PERIOD FOR SYSTEM_TIME (rs, re), PERIOD FOR app (s, e))\n"
      "  ENGINE=InnoDB WITH SYSTEM VERSIONING PAGE_COMPRESSED=1 UNION=(x, y)\n"
      "  PARTITION BY HASH (a)\n  PARTITIONS 4;\n"
      "CREATE TABLE u (a INT) PARTITION BY RANGE (a) (PARTITION p0 VALUES LESS THAN (10));\n"
      "ALTER TABLE u REMOVE PARTITIONING;\n"
      "CREATE TABLE w (a INT);\n"
      "ALTER TABLE w ADD SYSTEM VERSIONING, ADD COLUMN b INT PARTITION BY KEY (a) PARTITIONS 2;\n"
      "ALTER TABLE t DROP PERIOD FOR app;\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  EXPECT_EQ("PARTITION BY HASH (a) PARTITIONS 4", t->partitioning);
  EXPECT_TRUE(t->system_versioning);
  ASSERT_EQ(1u, t->periods.size());
  EXPECT_EQ("SYSTEM_TIME", t->periods[0].name);
  EXPECT_EQ("rs", t->periods[0].start);
  EXPECT_EQ("re", t->periods[0].end);
  auto option = [](const Table &x, const std::string &k) {
    for (const auto &[key, v] : x.options)
      if (key == k) return v;
    return std::string("<none>");
  };
  EXPECT_EQ("1", option(*t, "page_compressed"));
  EXPECT_EQ("(x, y)", option(*t, "union"));
  const Table *u = table(o, "", "u");
  ASSERT_NE(nullptr, u);
  EXPECT_EQ("", u->partitioning);
  EXPECT_FALSE(u->system_versioning);
  const Table *w = table(o, "", "w");
  ASSERT_NE(nullptr, w);
  EXPECT_TRUE(w->system_versioning);
  EXPECT_EQ("PARTITION BY KEY (a) PARTITIONS 2", w->partitioning);
}

TEST(Catalog, alter_keeps_column_members) {
  const auto o = outline_of(
      "CREATE TABLE t (a INT, b INT, KEY k (a), CHECK (a > 0));\n"
      "ALTER TABLE t MODIFY a VARCHAR(5) CHARACTER SET latin1 COLLATE latin1_bin NOT NULL\n"
      "  INVISIBLE COMMENT 'x';\n"
      "ALTER TABLE t CHANGE b b2 TIMESTAMP ON UPDATE CURRENT_TIMESTAMP WITHOUT SYSTEM VERSIONING;\n"
      "ALTER TABLE t ADD COLUMN g INT AS (1) VIRTUAL INVISIBLE;\n"
      "ALTER TABLE t ALTER COLUMN a SET DEFAULT 'q';\n"
      "ALTER TABLE t RENAME COLUMN a TO a2;\n");
  const Table *t = table(o, "", "t");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(3u, t->columns.size());
  const auto &a = t->columns[0];
  EXPECT_EQ("a2", a.name);
  EXPECT_EQ("VARCHAR(5)", a.type);
  EXPECT_EQ("latin1", a.charset);
  EXPECT_EQ("latin1_bin", a.collation);
  EXPECT_TRUE(a.invisible);
  EXPECT_EQ("'q'", a.default_value);
  const auto &b = t->columns[1];
  EXPECT_EQ("b2", b.name);
  EXPECT_EQ("CURRENT_TIMESTAMP", b.on_update);
  EXPECT_EQ(false, b.system_versioning);
  EXPECT_EQ(std::nullopt, b.charset);
  const auto &g = t->columns[2];
  EXPECT_EQ("VIRTUAL", g.generated_storage);
  EXPECT_TRUE(g.invisible);
  ASSERT_EQ(1u, t->indexes.size());
  EXPECT_EQ(std::vector<std::string>{"a2"}, t->indexes[0].columns());  // follows the rename
  EXPECT_EQ("a2", t->indexes[0].key_parts[0].column);
}

TEST(Catalog, view_routine_trigger_event_sequence_details) {
  const auto o = outline_of(
      "CREATE TABLE t (a INT, b INT);\n"
      "CREATE OR REPLACE ALGORITHM=MERGE DEFINER=`root`@`localhost` SQL SECURITY INVOKER\n"
      "  VIEW v AS SELECT a FROM t WITH LOCAL CHECK OPTION;\n"
      "CREATE VIEW v2 AS SELECT a FROM t WITH CHECK OPTION;\n"
      "CREATE VIEW v3 AS SELECT 1;\n"
      "DELIMITER //\n"
      "CREATE DEFINER=root@'%' FUNCTION f(x INT) RETURNS INT DETERMINISTIC READS SQL DATA\n"
      "  SQL SECURITY INVOKER COMMENT 'it''s' RETURN x + 1//\n"
      "CREATE PROCEDURE p() NOT DETERMINISTIC MODIFIES SQL DATA\n"
      "BEGIN\n  SELECT   1;  -- kept\nEND//\n"
      "CREATE TRIGGER tr1 BEFORE INSERT ON t FOR EACH ROW SET NEW.a = 1//\n"
      "CREATE DEFINER=CURRENT_USER TRIGGER tr2 BEFORE UPDATE ON t FOR EACH ROW PRECEDES tr1\n"
      "BEGIN SET NEW.b = 2; END//\n"
      "CREATE EVENT e1 ON SCHEDULE EVERY 1 HOUR STARTS '2030-01-01' ON COMPLETION NOT PRESERVE\n"
      "  DISABLE ON SLAVE COMMENT 'ev' DO DELETE FROM t//\n"
      "CREATE EVENT e2 ON SCHEDULE AT '2030-01-01 00:00:00' DO BEGIN SELECT 1; END//\n"
      "DELIMITER ;\n"
      "ALTER PROCEDURE p COMMENT 'changed' SQL SECURITY INVOKER;\n"
      "ALTER EVENT e2 ON COMPLETION PRESERVE ENABLE;\n"
      "CREATE SEQUENCE s1 START WITH 10 INCREMENT BY -2 MINVALUE=1 MAXVALUE 100 CACHE 5 CYCLE;\n"
      "CREATE SEQUENCE s2 NOCACHE NOCYCLE;\n"
      "ALTER SEQUENCE s2 INCREMENT 3;\n");
  const Schema *sc = schema(o, "");
  ASSERT_NE(nullptr, sc);
  ASSERT_EQ(3u, sc->views.size());
  EXPECT_EQ("MERGE", sc->views[0].algorithm);
  EXPECT_EQ("INVOKER", sc->views[0].security);
  EXPECT_EQ("root@localhost", sc->views[0].definer);
  EXPECT_EQ("LOCAL", sc->views[0].check_option);
  EXPECT_EQ("CASCADED", sc->views[1].check_option);
  EXPECT_EQ("", sc->views[2].algorithm);
  EXPECT_EQ("", sc->views[2].check_option);

  ASSERT_EQ(2u, sc->routines.size());
  const Routine &f = sc->routines[0];
  EXPECT_EQ("RETURN x + 1", f.body);
  EXPECT_EQ(true, f.deterministic);
  EXPECT_EQ("READS SQL DATA", f.data_access);
  EXPECT_EQ("INVOKER", f.security);
  EXPECT_EQ("root@%", f.definer);
  EXPECT_EQ("it's", f.comment);
  const Routine &p = sc->routines[1];
  EXPECT_EQ("BEGIN\n  SELECT   1;  -- kept\nEND", p.body);  // as written
  EXPECT_EQ(false, p.deterministic);
  EXPECT_EQ("MODIFIES SQL DATA", p.data_access);
  EXPECT_EQ("INVOKER", p.security);  // ALTER PROCEDURE
  EXPECT_EQ("changed", p.comment);
  EXPECT_EQ("", p.definer);

  ASSERT_EQ(2u, sc->triggers.size());
  EXPECT_EQ("SET NEW.a = 1", sc->triggers[0].body);
  EXPECT_EQ("", sc->triggers[0].order_kind);
  EXPECT_EQ("PRECEDES", sc->triggers[1].order_kind);
  EXPECT_EQ("tr1", sc->triggers[1].order_trigger);
  EXPECT_EQ("CURRENT_USER", sc->triggers[1].definer);
  EXPECT_EQ("BEGIN SET NEW.b = 2; END", sc->triggers[1].body);

  ASSERT_EQ(2u, sc->events.size());
  const Event &e1 = sc->events[0];
  EXPECT_EQ("EVERY 1 HOUR STARTS '2030-01-01'", e1.schedule);
  EXPECT_EQ(false, e1.on_completion_preserve);
  EXPECT_EQ("DISABLE ON SLAVE", e1.status);
  EXPECT_EQ("ev", e1.comment);
  EXPECT_EQ("DELETE FROM t", e1.body);
  const Event &e2 = sc->events[1];
  EXPECT_EQ("AT '2030-01-01 00:00:00'", e2.schedule);  // kept by ALTER EVENT
  EXPECT_EQ(true, e2.on_completion_preserve);
  EXPECT_EQ("ENABLE", e2.status);
  EXPECT_EQ(std::nullopt, e2.comment);
  EXPECT_EQ("BEGIN SELECT 1; END", e2.body);

  ASSERT_EQ(2u, sc->sequences.size());
  const Sequence &s1 = sc->sequences[0];
  EXPECT_EQ("10", s1.start);
  EXPECT_EQ("-2", s1.increment);
  EXPECT_EQ("1", s1.minvalue);
  EXPECT_EQ("100", s1.maxvalue);
  EXPECT_EQ("5", s1.cache);
  EXPECT_EQ(true, s1.cycle);
  const Sequence &s2 = sc->sequences[1];
  EXPECT_EQ("0", s2.cache);
  EXPECT_EQ(false, s2.cycle);
  EXPECT_EQ("3", s2.increment);
  EXPECT_EQ("", s2.start);
}

// Every member of format 1 is written, null when the SQL does not state it.
TEST(Catalog, outline_json_has_the_new_members) {
  const std::string text =
      "CREATE TABLE t (a VARCHAR(9) CHARACTER SET utf8mb4, KEY k (a(3)));\n"
      "CREATE TABLE c (x INT, CONSTRAINT f FOREIGN KEY fi (x) REFERENCES t (a));\n"
      "CREATE VIEW v AS SELECT 1;\n"
      "CREATE PROCEDURE p() SELECT 1;\n"
      "CREATE TRIGGER tr BEFORE INSERT ON t FOR EACH ROW SET @a = 1;\n"
      "CREATE EVENT e ON SCHEDULE AT '2030-01-01' DO SELECT 1;\n"
      "CREATE SEQUENCE s;\n";
  static Parser_pool pool(2);
  const auto index = Statement_index::build(text);
  const auto json = outline_json(build_outline(text, index, pool), text, Line_map(text));
  for (const char *member :
       {"\"type\":\"VARCHAR(9)\"", "\"charset\":\"utf8mb4\"", "\"on_update\":null",
        "\"invisible\":false", "\"generated_storage\":null", "\"generated_row\":null",
        "\"system_versioning\":null",
        "\"key_parts\":[{\"column\":\"a\",\"length\":3,\"order\":null,\"expression\":null}]",
        "\"using\":null", "\"ignored\":false", "\"without_overlaps\":null",
        "\"name\":\"f\",", "\"index_name\":\"fi\"", "\"match\":null", "\"partitioning\":null",
        "\"system_versioning\":false", "\"periods\":[]", "\"algorithm\":null",
        "\"check_option\":null", "\"body\":\"SELECT 1\"", "\"deterministic\":null",
        "\"data_access\":null", "\"order\":null", "\"update_columns\":[]",
        "\"schedule\":\"AT '2030-01-01'\"", "\"on_completion_preserve\":null", "\"status\":null",
        "\"start\":null", "\"cycle\":null"}) {
    EXPECT_NE(std::string::npos, json.find(member)) << member;
  }
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
