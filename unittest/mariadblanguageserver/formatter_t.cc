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

// The layout of queries: what the formatter makes of a document.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mdbls/formatter.h"

namespace mdbls {

namespace {

// `sql` with its edits applied; the clauses at the level of their
// statement's first line unless `indent_clauses`.
std::string layout(const std::string &sql, size_t width = 80, bool upper = true, std::string indent = "  ",
                   bool indent_clauses = false) {
  const auto index = Statement_index::build(sql);
  Parser_cache cache;
  Format_options o;
  o.line_width = width;
  o.upper_case_keywords = upper;
  o.indent = std::move(indent);
  o.indent_clauses = o.indent_routine_clauses = indent_clauses;
  return apply_edits(sql, format_sql(sql, index, 0, static_cast<uint32_t>(sql.size()), cache, o));
}

}  // namespace

TEST(Formatter, a_short_query_stays_on_its_line) {
  EXPECT_EQ(layout("select  a,b   from t where x=1;"), "SELECT a, b FROM t WHERE x=1;");
}

TEST(Formatter, a_long_query_puts_each_clause_on_a_line) {
  EXPECT_EQ(layout("SELECT customer_id, first_name, last_name FROM customer WHERE store_id = 1 ORDER BY last_name;"),
            "SELECT customer_id, first_name, last_name\n"
            "FROM customer\n"
            "WHERE store_id = 1\n"
            "ORDER BY last_name;");
}

TEST(Formatter, a_statement_on_several_lines_keeps_its_clauses_apart) {
  EXPECT_EQ(layout("SELECT a FROM t\nWHERE b = 1;"), "SELECT a\nFROM t\nWHERE b = 1;");
}

TEST(Formatter, select_items_wrap_at_the_width_aligned_with_the_first) {
  EXPECT_EQ(layout("SELECT film_id, title, description, release_year, language_id, rental_duration, rental_rate, "
                   "length FROM film;"),
            "SELECT film_id, title, description, release_year, language_id, rental_duration,\n"
            "       rental_rate, length\n"
            "FROM film;");
  // Aligned with the first item, wherever the words before it end it.
  EXPECT_EQ(layout("SELECT DISTINCT film_id, title, description, release_year, language_id, rental_duration, "
                   "length FROM film;"),
            "SELECT DISTINCT film_id, title, description, release_year, language_id,\n"
            "                rental_duration, length\n"
            "FROM film;");
}

TEST(Formatter, joins_go_under_from_and_on_stays_on_the_join_line) {
  EXPECT_EQ(layout("SELECT c.name, o.total FROM customer AS c LEFT OUTER JOIN orders AS o ON o.customer_id = c.id "
                   "JOIN shop AS s ON s.id = o.shop_id;"),
            "SELECT c.name, o.total\n"
            "FROM customer AS c\n"
            "  LEFT OUTER JOIN orders AS o ON o.customer_id = c.id\n"
            "  JOIN shop AS s ON s.id = o.shop_id;");
}

TEST(Formatter, a_short_join_stays_on_the_from_line_of_a_one_line_statement) {
  EXPECT_EQ(layout("SELECT * FROM a JOIN b ON a.id = b.id;"), "SELECT * FROM a JOIN b ON a.id = b.id;");
}

TEST(Formatter, joins_in_parentheses_stay_inline_when_they_fit) {
  EXPECT_EQ(layout("SELECT *\nFROM a LEFT JOIN (b JOIN c ON b.id = c.id) ON a.id = b.id;"),
            "SELECT *\nFROM a\n  LEFT JOIN (b JOIN c ON b.id = c.id) ON a.id = b.id;");
}

TEST(Formatter, an_on_past_the_width_goes_one_level_deeper) {
  EXPECT_EQ(layout("SELECT *\nFROM customer AS c\nJOIN orders AS o ON o.customer_id = c.id;", 40),
            "SELECT *\n"
            "FROM customer AS c\n"
            "  JOIN orders AS o\n"
            "    ON o.customer_id = c.id;");
}

TEST(Formatter, and_chains_break_before_each_operator) {
  EXPECT_EQ(layout("SELECT * FROM rental WHERE rental_date >= '2005-05-01' AND return_date IS NULL AND "
                   "customer_id = 42 AND staff_id = 2;"),
            "SELECT *\n"
            "FROM rental\n"
            "WHERE rental_date >= '2005-05-01'\n"
            "  AND return_date IS NULL\n"
            "  AND customer_id = 42\n"
            "  AND staff_id = 2;");
}

TEST(Formatter, a_subquery_stays_inline_when_it_fits) {
  EXPECT_EQ(layout("SELECT * FROM customer WHERE id IN ( SELECT customer_id FROM orders );"),
            "SELECT * FROM customer WHERE id IN (SELECT customer_id FROM orders);");
}

TEST(Formatter, a_long_subquery_goes_on_lines_of_its_own) {
  EXPECT_EQ(layout("SELECT *\nFROM customer AS c\nWHERE c.id IN (SELECT o.customer_id FROM orders AS o WHERE "
                   "o.total > 100 AND o.status = 'open' AND o.created_at > '2026-01-01');"),
            "SELECT *\n"
            "FROM customer AS c\n"
            "WHERE c.id IN (\n"
            "  SELECT o.customer_id\n"
            "  FROM orders AS o\n"
            "  WHERE o.total > 100 AND o.status = 'open' AND o.created_at > '2026-01-01'\n"
            ");");
}

TEST(Formatter, group_by_and_order_by_lists_wrap_aligned) {
  EXPECT_EQ(layout("SELECT a\nFROM t\nGROUP BY first_column, second_column, third_column, fourth_column, "
                   "fifth_column, "
                   "sixth;"),
            "SELECT a\n"
            "FROM t\n"
            "GROUP BY first_column, second_column, third_column, fourth_column, fifth_column,\n"
            "         sixth;");
}

TEST(Formatter, union_parts_go_on_lines_of_their_own) {
  EXPECT_EQ(layout("SELECT a FROM t\nUNION ALL SELECT b FROM u;"), "SELECT a\nFROM t\nUNION ALL\nSELECT b\nFROM u;");
}

TEST(Formatter, keywords_follow_the_case_and_names_keep_theirs) {
  EXPECT_EQ(layout("SELECT Status, `Order` FROM Orders WHERE status = 'Open';", 80, false),
            "select Status, `Order` from Orders where status = 'Open';");
  EXPECT_EQ(layout("select name from user;"), "SELECT name FROM user;");
}

TEST(Formatter, comments_stay_before_the_token_after_them) {
  EXPECT_EQ(layout("SELECT a, -- the key\n  b\nFROM t;"), "SELECT a, -- the key\n       b\nFROM t;");
  EXPECT_EQ(layout("SELECT a /* first */, b FROM t;"), "SELECT a /* first */, b FROM t;");
  EXPECT_EQ(layout("SELECT a\n-- whole line\nFROM t;"), "SELECT a\n-- whole line\nFROM t;");
  EXPECT_EQ(layout("SELECT a -- the end of the line\nFROM t;"), "SELECT a -- the end of the line\nFROM t;");
  // A kept line break goes one level deeper, with or without a comment on it.
  EXPECT_EQ(layout("SELECT a +\n  -- note\n  b\nFROM t;"), "SELECT a +\n         -- note\n         b\nFROM t;");
  EXPECT_EQ(layout("SELECT * FROM t WHERE x IN ( -- why\nSELECT 1);"),
            "SELECT *\nFROM t\nWHERE x IN ( -- why\n  SELECT 1\n);");
  // A block comment next to a parenthesis keeps the breaks around the query.
  EXPECT_EQ(layout("SELECT * FROM t WHERE x IN (/* c */ SELECT aaaa FROM bbbb WHERE ccccc = dddd AND "
                   "eeeeeeeeeeeee = fffffffffff AND ggggggggggggg = hhhhh /* d */);"),
            "SELECT *\nFROM t\nWHERE x IN ( /* c */\n  SELECT aaaa\n  FROM bbbb\n  WHERE ccccc = dddd\n"
            "    AND eeeeeeeeeeeee = fffffffffff\n    AND ggggggggggggg = hhhhh /* d */\n);");
  EXPECT_EQ(layout("SELECT * FROM t WHERE x IN (/* c */ SELECT 1 /* d */);"),
            "SELECT * FROM t WHERE x IN ( /* c */ SELECT 1 /* d */);");
  // A comment ending a clause's keyword line: what follows goes one level
  // deeper than the keyword, and a list stays aligned with its first item.
  EXPECT_EQ(layout("SELECT -- c\na, b FROM t;"), "SELECT -- c\n  a, b\nFROM t;");
  EXPECT_EQ(layout("SELECT -- c\nfilm_id, title, description, release_year, language_id, rental_duration, "
                   "rental_rate, length FROM film;"),
            "SELECT -- c\n  film_id, title, description, release_year, language_id, rental_duration,\n"
            "  rental_rate, length\nFROM film;");
  EXPECT_EQ(layout("SELECT a FROM t WHERE -- why\nx = 1;"), "SELECT a\nFROM t\nWHERE -- why\n  x = 1;");
  EXPECT_EQ(layout("SELECT * FROM (SELECT -- inner\na FROM t) AS d;"),
            "SELECT *\nFROM (\n  SELECT -- inner\n    a\n  FROM t\n) AS d;");
  EXPECT_EQ(layout("SELECT a FROM t JOIN u ON -- c\nt.a = u.a;"),
            "SELECT a\nFROM t\n  JOIN u\n    ON -- c\n      t.a = u.a;");
  // Between the words of JOIN the line stays at their level; the table and
  // ON follow the rule.
  EXPECT_EQ(layout("SELECT a FROM t LEFT -- e\nJOIN u ON -- f\nt.id = u.id;"),
            "SELECT a\nFROM t\n  LEFT -- e\n  JOIN u\n    ON -- f\n      t.id = u.id;");
  EXPECT_EQ(layout("SELECT a FROM t LEFT OUTER JOIN -- g\nu ON t.id = u.id;"),
            "SELECT a\nFROM t\n  LEFT OUTER JOIN -- g\n    u ON t.id = u.id;");
  // Any blank after "--" makes a comment, before the query too.
  EXPECT_EQ(layout("--\tlead\nselect a,b from t;"), "--\tlead\nSELECT a, b FROM t;");
}

TEST(Formatter, line_breaks_inside_other_constructs_are_kept) {
  EXPECT_EQ(layout("SELECT CASE\nWHEN a = 1 THEN 'one'\nELSE 'other' END AS x\nFROM t;"),
            "SELECT CASE\n         WHEN a = 1 THEN 'one'\n         ELSE 'other' END AS x\nFROM t;");
}

TEST(Formatter, other_statements_are_left_alone) {
  const std::string ddl = "DROP TABLE t;\nINSERT INTO t VALUES (1,'a');\n";
  EXPECT_EQ(layout(ddl), ddl);
  const std::string versioned = "/*!40101 SELECT a FROM t */;";
  EXPECT_EQ(layout(versioned), versioned);
  const std::string broken = "SELECT a FROM WHERE;";
  EXPECT_EQ(layout(broken), broken);
  // A UDF, a transaction's BEGIN, DESCRIBE of a table are not laid out.
  for (const std::string sql :
       {"CREATE FUNCTION f RETURNS INTEGER SONAME 'f.so';", "begin work;", "describe some_table;",
        "analyze table some_table;"}) {
    EXPECT_EQ(layout(sql, 20), sql);
  }
}

TEST(Formatter, the_indentation_and_line_endings_of_the_statement_are_kept) {
  EXPECT_EQ(layout("  SELECT a FROM t\r\n  WHERE b = 1;"), "  SELECT a\r\n  FROM t\r\n  WHERE b = 1;");
  // A statement on one line breaks the way the document's lines end.
  EXPECT_EQ(layout("SELECT a FROM t WHERE b = 1;\r\nSELECT 1;\r\n", 20),
            "SELECT a\r\nFROM t\r\nWHERE b = 1;\r\nSELECT 1;\r\n");
  // A statement on several lines keeps its own, whatever the document's.
  EXPECT_EQ(layout("-- crlf\r\nSELECT a FROM t\nWHERE b = 1;"), "-- crlf\r\nSELECT a\nFROM t\nWHERE b = 1;");
  EXPECT_EQ(layout("SELECT *\nFROM a\nJOIN b ON a.id = b.id;", 80, true, "\t"),
            "SELECT *\nFROM a\n\tJOIN b ON a.id = b.id;");
}

TEST(Formatter, with_a_width_of_zero_nothing_breaks_for_length) {
  const std::string sql = "SELECT customer_id, first_name, last_name FROM customer WHERE store_id = 1 AND active = 1 "
                          "ORDER BY last_name;";
  EXPECT_EQ(layout(sql, 0), sql);
}

TEST(Formatter, a_common_table_expression_comes_before_the_query) {
  EXPECT_EQ(layout("WITH c AS (SELECT id FROM t)\nSELECT * FROM c;"),
            "WITH c AS (SELECT id FROM t)\nSELECT *\nFROM c;");
}

TEST(Formatter, formatting_twice_changes_nothing) {
  const std::string once = layout("SELECT c.name, o.total, o.created_at FROM customer AS c "
                                  "LEFT OUTER JOIN orders AS o "
                                  "ON o.customer_id = c.id WHERE o.total > 100 AND c.active = 1 ORDER BY o.total;");
  EXPECT_EQ(layout(once), once);
}

TEST(Formatter, select_into_is_a_query_too) {
  EXPECT_EQ(layout("SELECT x INTO @v FROM t WHERE id = 1 FOR UPDATE;", 20),
            "SELECT x\nINTO @v\nFROM t\nWHERE id = 1\nFOR UPDATE;");
}

TEST(Formatter, insert_select_puts_the_query_under_the_table) {
  EXPECT_EQ(layout("insert into t (a, b) select x, y from u where z = 1;"),
            "INSERT INTO t (a, b) SELECT x, y FROM u WHERE z = 1;");
  EXPECT_EQ(layout("INSERT INTO rental_archive (rental_id, rental_date, inventory_id, customer_id, return_date) "
                   "SELECT rental_id, rental_date, inventory_id, customer_id, return_date FROM rental "
                   "WHERE return_date < '2005-06-01' ON DUPLICATE KEY UPDATE return_date = NULL;"),
            "INSERT INTO rental_archive (rental_id, rental_date, inventory_id, customer_id,\n"
            "                            return_date)\n"
            "SELECT rental_id, rental_date, inventory_id, customer_id, return_date\n"
            "FROM rental\n"
            "WHERE return_date < '2005-06-01'\n"
            "ON DUPLICATE KEY UPDATE return_date = NULL;");
  EXPECT_EQ(layout("REPLACE INTO t SELECT * FROM u\nWHERE x = 1;"), "REPLACE INTO t\nSELECT *\nFROM u\nWHERE x = 1;");
}

TEST(Formatter, update_puts_set_and_its_clauses_on_lines) {
  EXPECT_EQ(layout("update t set a=1,b = 2 where c = 3;"), "UPDATE t SET a=1, b = 2 WHERE c = 3;");
  EXPECT_EQ(layout("UPDATE film SET rental_rate = rental_rate * 1.1, last_update = NOW(), special_features = "
                   "'Trailers' WHERE rating = 'PG' ORDER BY film_id LIMIT 10;"),
            "UPDATE film\n"
            "SET rental_rate = rental_rate * 1.1, last_update = NOW(),\n"
            "    special_features = 'Trailers'\n"
            "WHERE rating = 'PG'\n"
            "ORDER BY film_id\n"
            "LIMIT 10;");
  EXPECT_EQ(layout("UPDATE t JOIN u ON t.id = u.id SET t.a = u.b\nWHERE u.c = 1;"),
            "UPDATE t\n  JOIN u ON t.id = u.id\nSET t.a = u.b\nWHERE u.c = 1;");
  EXPECT_EQ(layout("WITH x AS (SELECT id FROM t) UPDATE t SET a = 1\nWHERE id IN (SELECT id FROM x);"),
            "WITH x AS (SELECT id FROM t)\nUPDATE t\nSET a = 1\nWHERE id IN (SELECT id FROM x);");
}

TEST(Formatter, delete_keeps_from_on_its_line_unless_tables_come_first) {
  EXPECT_EQ(layout("DELETE FROM t WHERE id = 1\nRETURNING id, a;"), "DELETE FROM t\nWHERE id = 1\nRETURNING id, a;");
  EXPECT_EQ(layout("DELETE t FROM t JOIN u ON t.id = u.id\nWHERE u.c = 1;"),
            "DELETE t\nFROM t\n  JOIN u ON t.id = u.id\nWHERE u.c = 1;");
  EXPECT_EQ(layout("DELETE FROM t1 USING t1 JOIN t2 ON t1.id = t2.id\nWHERE t2.x = 1;"),
            "DELETE FROM t1\nUSING t1\n  JOIN t2 ON t1.id = t2.id\nWHERE t2.x = 1;");
  // ORDER BY inside a function is no clause of the DELETE.
  EXPECT_EQ(layout("DELETE FROM t\nWHERE GROUP_CONCAT(a ORDER BY b) = 'x';"),
            "DELETE FROM t\nWHERE GROUP_CONCAT(a ORDER BY b) = 'x';");
}

TEST(Formatter, the_first_words_are_read_as_the_parser_reads_them) {
  // A qualified table name is no VALUES, a quoted one no SET.
  EXPECT_EQ(layout("INSERT INTO db.values (a) SELECT 1 FROM u\nWHERE x = 1;"),
            "INSERT INTO db.values (a)\nSELECT 1\nFROM u\nWHERE x = 1;");
  EXPECT_EQ(layout("INSERT LOW_PRIORITY INTO `set` SELECT 1 FROM u\nWHERE x = 1;"),
            "INSERT LOW_PRIORITY INTO `set`\nSELECT 1\nFROM u\nWHERE x = 1;");
  // Under NO_BACKSLASH_ESCAPES a backslash ends no string early or late.
  EXPECT_EQ(layout("SET sql_mode = 'NO_BACKSLASH_ESCAPES';\nCREATE DEFINER='a\\'@'%' VIEW v AS SELECT a FROM t\n"
                   "WHERE b = 1;"),
            "SET sql_mode = 'NO_BACKSLASH_ESCAPES';\nCREATE DEFINER='a\\'@'%' VIEW v AS\nSELECT a\nFROM t\n"
            "WHERE b = 1;");
}

TEST(Formatter, a_view_puts_its_query_under_the_header) {
  EXPECT_EQ(layout("create view v as select a from t;"), "CREATE VIEW v AS SELECT a FROM t;");
  EXPECT_EQ(layout("CREATE OR REPLACE DEFINER=`root`@`%` SQL SECURITY INVOKER VIEW v (x) AS SELECT a FROM t\n"
                   "WITH CHECK OPTION;"),
            "CREATE OR REPLACE DEFINER=`root`@`%` SQL SECURITY INVOKER VIEW v (x) AS\n"
            "SELECT a\n"
            "FROM t\n"
            "WITH CHECK OPTION;");
  EXPECT_EQ(layout("ALTER ALGORITHM=MERGE VIEW v AS SELECT a, b FROM t\nWHERE c = 1;"),
            "ALTER ALGORITHM=MERGE VIEW v AS\nSELECT a, b\nFROM t\nWHERE c = 1;");
}

TEST(Formatter, create_table_puts_each_column_and_key_on_a_line) {
  EXPECT_EQ(layout("create table t (id int primary key, name text);"),
            "CREATE TABLE t (\n  id INT PRIMARY KEY,\n  name TEXT\n);");
  EXPECT_EQ(layout("CREATE TABLE IF NOT EXISTS film (film_id SMALLINT UNSIGNED NOT NULL AUTO_INCREMENT, PRIMARY KEY "
                   "(film_id), CONSTRAINT fk FOREIGN KEY (lang) REFERENCES language (id) ON DELETE CASCADE) "
                   "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 PARTITION BY HASH(film_id) PARTITIONS 4;"),
            "CREATE TABLE IF NOT EXISTS film (\n"
            "  film_id SMALLINT UNSIGNED NOT NULL AUTO_INCREMENT,\n"
            "  PRIMARY KEY (film_id),\n"
            "  CONSTRAINT fk FOREIGN KEY (lang) REFERENCES language (id) ON DELETE CASCADE\n"
            ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4\n"
            "PARTITION BY HASH(film_id) PARTITIONS 4;");
  // Comments stay with the column after them; the last ones before the
  // parenthesis stay at the columns' level.
  EXPECT_EQ(layout("CREATE TABLE c ( -- the columns\nid INT, -- the id\n/* the name */\nname TEXT\n-- last\n);", 80, true,
                   "\t"),
            "CREATE TABLE c ( -- the columns\n\tid INT, -- the id\n\t/* the name */\n\tname TEXT\n\t-- last\n);");
  // Nothing to lay out in a copy.
  EXPECT_EQ(layout("CREATE TABLE l LIKE t;"), "CREATE TABLE l LIKE t;");
}

TEST(Formatter, partitions_go_one_per_line_like_columns) {
  EXPECT_EQ(layout("CREATE TABLE t (a INT, d DATE)\nPARTITION BY RANGE (YEAR(d)) SUBPARTITION BY HASH (a)\n"
                   "(PARTITION p0 VALUES LESS THAN (2000) (SUBPARTITION s0, SUBPARTITION s1),\n"
                   " PARTITION p1 VALUES LESS THAN MAXVALUE ENGINE = InnoDB);"),
            "CREATE TABLE t (\n"
            "  a INT,\n"
            "  d DATE\n"
            ")\n"
            "PARTITION BY RANGE (YEAR(d)) SUBPARTITION BY HASH (a) (\n"
            "  PARTITION p0 VALUES LESS THAN (2000) (\n"
            "    SUBPARTITION s0,\n"
            "    SUBPARTITION s1\n"
            "  ),\n"
            "  PARTITION p1 VALUES LESS THAN MAXVALUE ENGINE = InnoDB\n"
            ");");
  // Without definitions it stays one line.
  EXPECT_EQ(layout("CREATE TABLE t (a INT) PARTITION BY HASH (a) PARTITIONS 4;"),
            "CREATE TABLE t (\n  a INT\n)\nPARTITION BY HASH (a) PARTITIONS 4;");
}

TEST(Formatter, table_options_fill_the_line_then_go_one_level_deeper) {
  EXPECT_EQ(layout("CREATE TABLE t (a INT) ENGINE=InnoDB AUTO_INCREMENT=1000 DEFAULT CHARSET=utf8mb4 "
                   "COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC, COMMENT='a table with a long comment';"),
            "CREATE TABLE t (\n"
            "  a INT\n"
            ") ENGINE=InnoDB AUTO_INCREMENT=1000 DEFAULT CHARSET=utf8mb4\n"
            "  COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC,\n"
            "  COMMENT='a table with a long comment';");
}

TEST(Formatter, alter_table_puts_several_changes_on_lines) {
  EXPECT_EQ(layout("alter table t add column y int;"), "ALTER TABLE t ADD COLUMN y INT;");
  EXPECT_EQ(layout("ALTER TABLE film ADD COLUMN x INT AFTER title, DROP INDEX idx_title, ADD INDEX idx_x (x);"),
            "ALTER TABLE film\n"
            "  ADD COLUMN x INT AFTER title,\n"
            "  DROP INDEX idx_title,\n"
            "  ADD INDEX idx_x (x);");
  EXPECT_EQ(layout("ALTER TABLE t ADD y INT, ENGINE=InnoDB PARTITION BY HASH(id) PARTITIONS 2;"),
            "ALTER TABLE t\n  ADD y INT,\n  ENGINE=InnoDB\nPARTITION BY HASH(id) PARTITIONS 2;");
  EXPECT_EQ(layout("ALTER TABLE t ADD y INT, DROP z REMOVE PARTITIONING;"),
            "ALTER TABLE t\n  ADD y INT,\n  DROP z\nREMOVE PARTITIONING;");
}

TEST(Formatter, comments_in_other_statements_stay_where_they_are) {
  const std::string sql = "UPDATE t -- the table\nSET a = 1 /* one */, b = 2\nWHERE c = 3;";
  EXPECT_EQ(layout(sql), sql);
}

TEST(Formatter, every_layout_is_its_own_layout) {
  const std::vector<std::string> queries = {
      "select f.title, c.name as category, count(r.rental_id) as rentals, sum(p.amount) revenue from film f join "
      "film_category fc on fc.film_id = f.film_id join category c on c.category_id = fc.category_id left join "
      "inventory i on i.film_id = f.film_id where f.rating in ('PG', 'PG-13') and f.length > 90 group by f.title, "
      "c.name having count(r.rental_id) > 10 order by revenue desc limit 20",
      "SELECT customer_id, (SELECT COUNT(*) FROM rental r WHERE r.customer_id = c.customer_id AND r.return_date IS "
      "NULL) AS open_rentals, first_name FROM customer c WHERE EXISTS (SELECT 1 FROM payment p WHERE p.customer_id = "
      "c.customer_id AND p.amount > 5)",
      "WITH recent AS (SELECT * FROM rental WHERE rental_date > NOW() - INTERVAL 30 DAY), top AS (SELECT "
      "customer_id, COUNT(*) n FROM recent GROUP BY customer_id) SELECT * FROM top ORDER BY n DESC",
      "SELECT a, ROW_NUMBER() OVER (PARTITION BY b ORDER BY c DESC) AS rn, GROUP_CONCAT(d ORDER BY d SEPARATOR ',') "
      "FROM t GROUP BY a, b",
      "SELECT * FROM t1, t2, t3 WHERE t1.a = t2.a OR t1.b = t3.b AND (t1.c = 1 OR t1.d = 2)",
      "(SELECT a FROM t1 WHERE x = 1) UNION (SELECT a FROM t2 WHERE y = 2) ORDER BY a LIMIT 10",
      "SELECT CASE\n WHEN a = 1 THEN 'one' -- first\n ELSE 'other' END AS x, /* b */ b FROM t -- end\nWHERE y = 2",
      "SELECT x INTO @v FROM t WHERE id = 1 FOR UPDATE",
      "SELECT * FROM a LEFT JOIN (b JOIN c ON b.id = c.id) ON a.id = b.id WHERE a.x BETWEEN 1 AND 10",
      "SELECT * FROM (SELECT a, b FROM t WHERE c = 1 GROUP BY a, b HAVING COUNT(*) > 1) AS d JOIN e USING (a)",
      "INSERT IGNORE INTO t (a, b, c) SELECT x, y, z FROM u JOIN v ON u.id = v.id WHERE u.k > 1 ON DUPLICATE KEY "
      "UPDATE b = VALUES(b), c = c + 1",
      "INSERT INTO t (a) (SELECT x FROM u WHERE y = 1)",
      "UPDATE LOW_PRIORITY t1, t2 SET t1.a = t2.a, t1.b = 'x' WHERE t1.id = t2.id AND t2.c IS NOT NULL",
      "WITH x AS (SELECT id FROM t) DELETE FROM t WHERE id IN (SELECT id FROM x) ORDER BY id LIMIT 5",
      "DELETE t1, t2 FROM t1 JOIN t2 ON t1.id = t2.id WHERE t1.a = 1",
      "CREATE OR REPLACE VIEW v (a, b) AS SELECT x, y FROM t WHERE z = 1 UNION SELECT x, y FROM u WITH CASCADED "
      "CHECK OPTION",
      "CREATE TABLE t (id INT NOT NULL, name VARCHAR(20) DEFAULT 'x' COMMENT 'the name', KEY k (name), CHECK (id > 0 "
      "AND id < 100)) ENGINE=InnoDB",
      "ALTER TABLE t ADD COLUMN x INT AFTER y, DROP INDEX i, ADD CONSTRAINT c CHECK (x > 0)",
      "CREATE TABLE t (a INT, d DATE) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci "
      "PARTITION BY RANGE (YEAR(d)) SUBPARTITION BY HASH (a) (PARTITION p0 VALUES LESS THAN (2000) "
      "(SUBPARTITION s0, SUBPARTITION s1), PARTITION p1 VALUES LESS THAN MAXVALUE)",
  };
  for (const auto &q : queries) {
    for (const size_t width : {0, 20, 40, 80, 120}) {
      const std::string indented_clauses = layout(q + ";", width, true, "  ", true);
      EXPECT_EQ(layout(indented_clauses, width, true, "  ", true), indented_clauses) << "width " << width << "\n"
                                                                                       << indented_clauses;
      const std::string once = layout(q + ";", width);
      EXPECT_EQ(layout(once, width), once) << "width " << width << "\n" << once;
      // The same, every line indented, with the width two columns wider
      // (it counts from the start of the line): it stays so.
      std::string indented = "  " + once;
      for (size_t at = indented.find('\n'); at != std::string::npos; at = indented.find('\n', at + 1)) {
        indented.insert(at + 1, "  ");
      }
      EXPECT_EQ(layout(indented, width == 0 ? 0 : width + 2), indented) << "width " << width << ", indented\n" << indented;
    }
  }
}

namespace {

// `body` between DELIMITER $$ lines, laid out.
std::string routine(const std::string &body, size_t width = 80) {
  const std::string open = "DELIMITER $$\n";
  const std::string close = "$$\nDELIMITER ;\n";
  const std::string out = layout(open + body + close, width);
  if (!out.starts_with(open) || !out.ends_with(close)) return out;
  return out.substr(open.size(), out.size() - open.size() - close.size());
}

}  // namespace

TEST(Formatter, a_routine_puts_each_statement_on_a_line) {
  EXPECT_EQ(routine("create procedure p(in a int) begin declare n int default 0; set n = a; select n; end"),
            "CREATE PROCEDURE p(IN a INT)\n"
            "BEGIN\n"
            "  DECLARE n INT DEFAULT 0;\n"
            "  SET n = a;\n"
            "  SELECT n;\n"
            "END");
  // A body that is no block stays on the header's line when it fits, else
  // goes one level deeper.
  EXPECT_EQ(routine("create function f(x int) returns int deterministic return x * 2"),
            "CREATE FUNCTION f(x INT) RETURNS INT DETERMINISTIC RETURN x * 2");
  EXPECT_EQ(routine("create procedure p()\nselect 1"), "CREATE PROCEDURE p()\n  SELECT 1");
  EXPECT_EQ(routine("CREATE PROCEDURE p() BEGIN END"), "CREATE PROCEDURE p()\nBEGIN\nEND");
}

TEST(Formatter, characteristics_go_one_level_deeper_on_lines_of_their_own) {
  EXPECT_EQ(routine("CREATE DEFINER=`root`@`localhost` FUNCTION `f`(a INT) RETURNS INT(11) DETERMINISTIC "
                    "COMMENT 'twice' BEGIN RETURN a * 2; END"),
            "CREATE DEFINER=`root`@`localhost` FUNCTION `f`(a INT) RETURNS INT(11)\n"
            "  DETERMINISTIC\n"
            "  COMMENT 'twice'\n"
            "BEGIN\n"
            "  RETURN a * 2;\n"
            "END");
}

TEST(Formatter, parameters_that_do_not_fit_go_one_per_line) {
  EXPECT_EQ(routine("CREATE PROCEDURE rewards_report (\n IN min_monthly_purchases TINYINT UNSIGNED\n"
                    " , IN min_dollar_amount_purchased DECIMAL(10,2)\n , OUT count_rewardees INT\n)\n"
                    "READS SQL DATA\nBEGIN\nSELECT 1;\nEND"),
            "CREATE PROCEDURE rewards_report(\n"
            "  IN min_monthly_purchases TINYINT UNSIGNED,\n"
            "  IN min_dollar_amount_purchased DECIMAL(10,2),\n"
            "  OUT count_rewardees INT\n"
            ")\n"
            "  READS SQL DATA\n"
            "BEGIN\n"
            "  SELECT 1;\n"
            "END");
}

TEST(Formatter, if_case_and_loops_nest_their_statements) {
  EXPECT_EQ(routine("CREATE PROCEDURE p(a INT) BEGIN "
                    "IF a > 0 THEN SET a = 1; ELSEIF a < 0 THEN SET a = -1; ELSE SET a = 0; END IF; "
                    "CASE a WHEN 1 THEN SET a = 2; ELSE BEGIN END; END CASE; "
                    "lbl: LOOP SET a = a + 1; IF a > 9 THEN LEAVE lbl; END IF; END LOOP lbl; "
                    "WHILE a > 0 DO SET a = a - 1; END WHILE; "
                    "REPEAT SET a = a + 1; UNTIL a > 3 END REPEAT; "
                    "FOR i IN 1..3 DO SET a = a + i; END FOR; END"),
            "CREATE PROCEDURE p(a INT)\n"
            "BEGIN\n"
            "  IF a > 0 THEN\n"
            "    SET a = 1;\n"
            "  ELSEIF a < 0 THEN\n"
            "    SET a = -1;\n"
            "  ELSE\n"
            "    SET a = 0;\n"
            "  END IF;\n"
            "  CASE a\n"
            "    WHEN 1 THEN\n"
            "      SET a = 2;\n"
            "    ELSE\n"
            "      BEGIN\n"
            "      END;\n"
            "  END CASE;\n"
            "  lbl: LOOP\n"
            "    SET a = a + 1;\n"
            "    IF a > 9 THEN\n"
            "      LEAVE lbl;\n"
            "    END IF;\n"
            "  END LOOP lbl;\n"
            "  WHILE a > 0 DO\n"
            "    SET a = a - 1;\n"
            "  END WHILE;\n"
            "  REPEAT\n"
            "    SET a = a + 1;\n"
            "  UNTIL a > 3 END REPEAT;\n"
            "  FOR i IN 1..3 DO\n"
            "    SET a = a + i;\n"
            "  END FOR;\n"
            "END");
}

TEST(Formatter, then_starts_a_line_after_a_condition_that_breaks) {
  EXPECT_EQ(routine("CREATE PROCEDURE p() BEGIN IF (old.title != new.title) OR (old.description != new.description) "
                    "OR (old.film_id != new.film_id) THEN SET @a = 1; END IF; END"),
            "CREATE PROCEDURE p()\n"
            "BEGIN\n"
            "  IF (old.title != new.title)\n"
            "    OR (old.description != new.description)\n"
            "    OR (old.film_id != new.film_id)\n"
            "  THEN\n"
            "    SET @a = 1;\n"
            "  END IF;\n"
            "END");
}

TEST(Formatter, statements_of_a_body_follow_the_query_rules) {
  EXPECT_EQ(routine("CREATE PROCEDURE p() BEGIN DECLARE c CURSOR FOR SELECT id FROM t WHERE x = 1; "
                    "SELECT c.name, o.total FROM customer AS c LEFT OUTER JOIN orders AS o ON o.customer_id = c.id "
                    "WHERE o.total > 100; UPDATE t SET a = 1\nWHERE b = 2; END"),
            "CREATE PROCEDURE p()\n"
            "BEGIN\n"
            "  DECLARE c CURSOR FOR SELECT id FROM t WHERE x = 1;\n"
            "  SELECT c.name, o.total\n"
            "  FROM customer AS c\n"
            "    LEFT OUTER JOIN orders AS o ON o.customer_id = c.id\n"
            "  WHERE o.total > 100;\n"
            "  UPDATE t\n"
            "  SET a = 1\n"
            "  WHERE b = 2;\n"
            "END");
  // A handler's block starts on its line.
  EXPECT_EQ(routine("CREATE PROCEDURE p() BEGIN DECLARE EXIT HANDLER FOR SQLEXCEPTION\nBEGIN ROLLBACK; END; END"),
            "CREATE PROCEDURE p()\n"
            "BEGIN\n"
            "  DECLARE EXIT HANDLER FOR SQLEXCEPTION BEGIN\n"
            "    ROLLBACK;\n"
            "  END;\n"
            "END");
}

TEST(Formatter, comments_and_empty_lines_stay_in_a_body) {
  EXPECT_EQ(routine("CREATE PROCEDURE p() BEGIN -- start\n  DECLARE a INT; -- trailing\n\n\n"
                    "  /* why */\n  IF a THEN SET a = 1;\n  -- before else\n  ELSE SET a = 2; END IF;\n"
                    "-- before end\nEND"),
            "CREATE PROCEDURE p()\n"
            "BEGIN -- start\n"
            "  DECLARE a INT; -- trailing\n"
            "\n"
            "  /* why */\n"
            "  IF a THEN\n"
            "    SET a = 1;\n"
            "    -- before else\n"
            "  ELSE\n"
            "    SET a = 2;\n"
            "  END IF;\n"
            "  -- before end\n"
            "END");
}

TEST(Formatter, triggers_events_and_compound_statements_are_routines_too) {
  EXPECT_EQ(routine("create trigger tr before insert on t for each row set new.a = 1"),
            "CREATE TRIGGER tr BEFORE INSERT ON t FOR EACH ROW SET new.a = 1");
  EXPECT_EQ(routine("create trigger tr after update on t for each row begin insert into log (a) values (new.a); end"),
            "CREATE TRIGGER tr AFTER UPDATE ON t FOR EACH ROW\n"
            "BEGIN\n"
            "  INSERT INTO log (a) VALUES (new.a);\n"
            "END");
  EXPECT_EQ(routine("create event e on schedule every 1 day starts '2026-01-01' on completion preserve "
                    "comment 'x' do begin delete from log where ts < now(); end"),
            "CREATE EVENT e\n"
            "  ON SCHEDULE EVERY 1 DAY STARTS '2026-01-01'\n"
            "  ON COMPLETION PRESERVE\n"
            "  COMMENT 'x'\n"
            "DO BEGIN\n"
            "  DELETE FROM log WHERE ts < now();\n"
            "END");
  EXPECT_EQ(routine("create event e on schedule at '2035-12-31 20:01:23' do set @a=5"),
            "CREATE EVENT e ON SCHEDULE AT '2035-12-31 20:01:23' DO SET @a=5");
  EXPECT_EQ(routine("begin not atomic declare x int; set x = 1; end"),
            "BEGIN NOT ATOMIC\n  DECLARE x INT;\n  SET x = 1;\nEND");
  EXPECT_EQ(routine("if @a then select 1; end if"), "IF @a THEN\n  SELECT 1;\nEND IF");
  // A body of one IF is a statement other than BEGIN ... END: one level
  // deeper.
  EXPECT_EQ(routine("create trigger tr before insert on t for each row if new.a then set new.b = 1; end if"),
            "CREATE TRIGGER tr BEFORE INSERT ON t FOR EACH ROW\n"
            "  IF new.a THEN\n"
            "    SET new.b = 1;\n"
            "  END IF");
  // No empty line before END.
  EXPECT_EQ(routine("CREATE PROCEDURE p() BEGIN SELECT 1;\n\nEND"), "CREATE PROCEDURE p()\nBEGIN\n  SELECT 1;\nEND");
}

TEST(Formatter, every_routine_layout_is_its_own_layout) {
  const std::vector<std::string> routines = {
      "CREATE DEFINER=`root`@`localhost` PROCEDURE p(IN a INT, OUT b VARCHAR(20)) COMMENT 'demo' READS SQL DATA "
      "BEGIN DECLARE n INT DEFAULT 0; DECLARE c CURSOR FOR SELECT id FROM t WHERE x = 1; "
      "DECLARE CONTINUE HANDLER FOR NOT FOUND SET n = 1; "
      "IF a > 0 AND n < 10 AND a + n > 100000 THEN SELECT COUNT(*) INTO n FROM t WHERE t.id = a; "
      "ELSEIF a < 0 THEN SET n = 1; ELSE SET n = -1; END IF; "
      "CASE WHEN a = 1 THEN SET b = 'one'; ELSE SET b = 'other'; END CASE; "
      "l: WHILE n > 0 DO SET n = n - 1; END WHILE l; "
      "INSERT INTO log (a, b) SELECT x, y FROM u JOIN v ON u.id = v.id WHERE u.k > 1; END",
      "CREATE FUNCTION get_customer_balance(p_customer_id INT, p_effective_date DATETIME) RETURNS DECIMAL(5,2) "
      "DETERMINISTIC READS SQL DATA BEGIN DECLARE v INT; -- the fees\n"
      "SELECT IFNULL(SUM(film.rental_rate),0) INTO v FROM film, inventory WHERE film.film_id = inventory.film_id; "
      "RETURN v; END",
      "CREATE TRIGGER upd_film AFTER UPDATE ON film FOR EACH ROW BEGIN IF (old.title != new.title) OR "
      "(old.description != new.description) THEN UPDATE film_text SET title=new.title, description=new.description "
      "WHERE film_id=old.film_id; END IF; END",
      "CREATE EVENT e ON SCHEDULE EVERY 1 DAY ON COMPLETION PRESERVE DO DELETE FROM log WHERE ts < NOW() - INTERVAL "
      "1 DAY",
  };
  for (const auto &r : routines) {
    for (const size_t width : {0, 20, 40, 80, 120}) {
      const std::string once = routine(r, width);
      EXPECT_EQ(routine(once, width), once) << "width " << width << "\n" << once;
      std::string indented = "  " + once;
      for (size_t at = indented.find('\n'); at != std::string::npos; at = indented.find('\n', at + 1)) {
        indented.insert(at + 1, "  ");
      }
      EXPECT_EQ(routine(indented, width == 0 ? 0 : width + 2), indented) << "width " << width << ", indented\n"
                                                                          << indented;
    }
  }
}

namespace {

// `sql` laid out with the default options but these two.
std::string with_clauses(const std::string &sql, bool top, bool routines) {
  const auto index = Statement_index::build(sql);
  Parser_cache cache;
  Format_options o;
  o.indent_clauses = top;
  o.indent_routine_clauses = routines;
  return apply_edits(sql, format_sql(sql, index, 0, static_cast<uint32_t>(sql.size()), cache, o));
}

}  // namespace

TEST(Formatter, clauses_go_one_level_deeper_by_default) {
  const std::string sql =
      "SELECT c.name, o.total FROM customer AS c JOIN orders AS o ON o.customer_id = c.id WHERE o.total > 100 "
      "AND c.active = 1 AND c.name LIKE 'A%' AND o.created_at > '2026-01-01';\n"
      "CREATE VIEW v AS SELECT a FROM t\nWHERE b = 1;\n"
      "DELIMITER $$\nCREATE PROCEDURE p() BEGIN DECLARE c CURSOR FOR SELECT a FROM t\nWHERE b = 1; "
      "UPDATE film_text SET title = 'x'\nWHERE film_id = 1; SET @x =\n1; END$$\nDELIMITER ;\n";
  const std::string top =
      "SELECT c.name, o.total\n"
      "  FROM customer AS c\n"
      "    JOIN orders AS o ON o.customer_id = c.id\n"
      "  WHERE o.total > 100\n"
      "    AND c.active = 1\n"
      "    AND c.name LIKE 'A%'\n"
      "    AND o.created_at > '2026-01-01';\n"
      "CREATE VIEW v AS\n"
      "  SELECT a\n"
      "  FROM t\n"
      "  WHERE b = 1;\n";
  const std::string flat_top =
      "SELECT c.name, o.total\n"
      "FROM customer AS c\n"
      "  JOIN orders AS o ON o.customer_id = c.id\n"
      "WHERE o.total > 100\n"
      "  AND c.active = 1\n"
      "  AND c.name LIKE 'A%'\n"
      "  AND o.created_at > '2026-01-01';\n"
      "CREATE VIEW v AS\n"
      "SELECT a\n"
      "FROM t\n"
      "WHERE b = 1;\n";
  // A statement without clauses keeps its line breaks one level deeper,
  // either way.
  const std::string body =
      "DELIMITER $$\n"
      "CREATE PROCEDURE p()\n"
      "BEGIN\n"
      "  DECLARE c CURSOR FOR\n"
      "    SELECT a\n"
      "    FROM t\n"
      "    WHERE b = 1;\n"
      "  UPDATE film_text\n"
      "    SET title = 'x'\n"
      "    WHERE film_id = 1;\n"
      "  SET @x =\n"
      "    1;\n"
      "END$$\n"
      "DELIMITER ;\n";
  const std::string flat_body =
      "DELIMITER $$\n"
      "CREATE PROCEDURE p()\n"
      "BEGIN\n"
      "  DECLARE c CURSOR FOR\n"
      "  SELECT a\n"
      "  FROM t\n"
      "  WHERE b = 1;\n"
      "  UPDATE film_text\n"
      "  SET title = 'x'\n"
      "  WHERE film_id = 1;\n"
      "  SET @x =\n"
      "    1;\n"
      "END$$\n"
      "DELIMITER ;\n";
  EXPECT_EQ(with_clauses(sql, true, true), top + body);
  EXPECT_EQ(with_clauses(sql, false, true), flat_top + body);
  EXPECT_EQ(with_clauses(sql, true, false), top + flat_body);
  EXPECT_EQ(with_clauses(sql, false, false), flat_top + flat_body);
  for (const bool a : {true, false}) {
    for (const bool b : {true, false}) {
      const std::string once = with_clauses(sql, a, b);
      EXPECT_EQ(with_clauses(once, a, b), once);
    }
  }
}

TEST(Formatter, a_query_inside_explain_or_create_table_is_laid_out) {
  EXPECT_EQ(with_clauses("explain select a, b from customer where store_id = 1 and active = 1 order by last_name;",
                         true, true),
            "EXPLAIN SELECT a, b\n  FROM customer\n  WHERE store_id = 1 AND active = 1\n  ORDER BY last_name;");
  EXPECT_EQ(with_clauses("ANALYZE FORMAT=JSON SELECT a FROM t\nWHERE b = 1;", true, true),
            "ANALYZE FORMAT=JSON SELECT a\n  FROM t\n  WHERE b = 1;");
  // Like a view's: the query on a line of its own under the header.
  EXPECT_EQ(with_clauses("CREATE TABLE c AS SELECT * FROM t\nWHERE x = 1;", true, true),
            "CREATE TABLE c AS\n  SELECT *\n  FROM t\n  WHERE x = 1;");
  EXPECT_EQ(with_clauses("CREATE TABLE s (a INT) SELECT 1 AS a;", true, true),
            "CREATE TABLE s (\n  a INT\n)\nSELECT 1 AS a;");
}

TEST(Formatter, rows_fill_the_line_after_values) {
  EXPECT_EQ(with_clauses("insert into t (a, b) values (1, 2);", true, true), "INSERT INTO t (a, b) VALUES (1, 2);");
  EXPECT_EQ(with_clauses("insert into film (film_id, title, rating) values (1, 'ACADEMY DINOSAUR', 'PG'), (2, 'ACE "
                         "GOLDFINGER', 'G'), (3, 'ADAPTATION HOLES', null);",
                         true, true),
            "INSERT INTO film (film_id, title, rating)\n"
            "  VALUES (1, 'ACADEMY DINOSAUR', 'PG'), (2, 'ACE GOLDFINGER', 'G'),\n"
            "         (3, 'ADAPTATION HOLES', NULL);");
  EXPECT_EQ(with_clauses("INSERT INTO t SET a = 1, bbbbbbbbbbbbbbbbbb = 2, cccccccccccccccccccc = 3, dddddddddddddd = 4, "
                         "e = 5;",
                         true, true),
            "INSERT INTO t\n  SET a = 1, bbbbbbbbbbbbbbbbbb = 2, cccccccccccccccccccc = 3,\n"
            "      dddddddddddddd = 4, e = 5;");
}

TEST(Formatter, rows_of_a_dump_are_left_as_they_are) {
  // An INSERT over 64 KB.
  std::string big = "INSERT INTO t VALUES (0,'x')";
  for (int i = 1; big.size() <= 64 * 1024; ++i) big += ",(" + std::to_string(i) + ",'x')";
  big += ";";
  EXPECT_EQ(with_clauses(big, true, true), big);
  // Short INSERTs in a text over 4 MB: left as they are when the whole
  // text is formatted, laid out when a range of it is.
  std::string text;
  const std::string row = "insert into t (a, b) values (1, 'a'), (2, 'b'), (3, 'c'), (4, 'd'), (5, 'e'), (6, 'f');\n";
  while (text.size() <= 4 * 1024 * 1024) text += row;
  const auto index = Statement_index::build(text);
  Parser_cache cache;
  EXPECT_TRUE(format_sql(text, index, 0, static_cast<uint32_t>(text.size()), cache).empty());
  const auto edits = format_sql(text, index, 0, 1, cache);
  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(edits[0].text, "INSERT INTO t (a, b)\n  VALUES (1, 'a'), (2, 'b'), (3, 'c'), (4, 'd'), (5, 'e'), (6, 'f')");
}

TEST(Formatter, grant_revoke_and_users_put_their_clauses_on_lines) {
  EXPECT_EQ(with_clauses("grant select on db.t to u;", true, true), "GRANT SELECT ON db.t TO u;");
  EXPECT_EQ(with_clauses("grant select, insert, update, delete, create, drop, references, index, alter on db.* to "
                         "'app'@'%', 'other'@'localhost' with grant option;",
                         true, true),
            "GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, REFERENCES, INDEX, ALTER\n"
            "  ON db.*\n"
            "  TO 'app'@'%', 'other'@'localhost'\n"
            "  WITH GRANT OPTION;");
  EXPECT_EQ(with_clauses("REVOKE SELECT, INSERT ON db.t\nFROM 'app'@'%';", true, true),
            "REVOKE SELECT, INSERT\n  ON db.t\n  FROM 'app'@'%';");
  EXPECT_EQ(with_clauses("create user 'u'@'%' identified by 'p' require ssl with max_queries_per_hour 10 password "
                         "expire account lock;",
                         true, true),
            "CREATE USER 'u'@'%' IDENTIFIED BY 'p'\n"
            "  REQUIRE SSL\n"
            "  WITH MAX_QUERIES_PER_HOUR 10\n"
            "  PASSWORD EXPIRE ACCOUNT LOCK;");
}

TEST(Formatter, alter_event_and_procedure_are_laid_out_like_routines) {
  EXPECT_EQ(with_clauses("alter event e on schedule every 1 day on completion preserve rename to f enable comment "
                         "'nightly cleanup' do delete from log where ts < now() - interval 1 day;",
                         true, true),
            "ALTER EVENT e\n"
            "  ON SCHEDULE EVERY 1 DAY\n"
            "  ON COMPLETION PRESERVE\n"
            "  RENAME TO f\n"
            "  ENABLE\n"
            "  COMMENT 'nightly cleanup'\n"
            "DO DELETE FROM log WHERE ts < now() - INTERVAL 1 DAY;");
  EXPECT_EQ(with_clauses("alter procedure p comment 'a procedure with a long comment that does not fit' sql security "
                         "invoker;",
                         true, true),
            "ALTER PROCEDURE p\n  COMMENT 'a procedure with a long comment that does not fit'\n  SQL SECURITY INVOKER;");
  EXPECT_EQ(with_clauses("alter function f comment 'x';", true, true), "ALTER FUNCTION f COMMENT 'x';");
}

TEST(Formatter, rest_sql_is_laid_out_as_show_create_writes_it) {
  EXPECT_EQ(with_clauses("CREATE OR REPLACE REST VIEW /city ON SERVICE /svc SCHEMA /sakila AS sakila.city CLASS MyCity "
                         "@INSERT { cityId: city_id @KEY @SORTABLE, country: sakila.country @UNNEST { countryId: "
                         "country_id } } AUTHENTICATION REQUIRED COMMENT \"x\" OPTIONS { \"a\": { \"b\": true }, \"c\": "
                         "[1, null] };",
                         true, true),
            "CREATE OR REPLACE REST VIEW /city\n"
            "  ON SERVICE /svc SCHEMA /sakila\n"
            "  AS sakila.city CLASS MyCity @INSERT {\n"
            "    cityId: city_id @KEY @SORTABLE,\n"
            "    country: sakila.country @UNNEST {\n"
            "      countryId: country_id\n"
            "    }\n"
            "  }\n"
            "  AUTHENTICATION REQUIRED\n"
            "  COMMENT \"x\"\n"
            "  OPTIONS {\n"
            "    \"a\": {\n"
            "      \"b\": true\n"
            "    },\n"
            "    \"c\": [\n"
            "      1,\n"
            "      null\n"
            "    ]\n"
            "  };");
  // Short ones stay on their line; paths and vendors keep their case.
  EXPECT_EQ(with_clauses("create rest content set /scripts on service /svc;", true, true),
            "CREATE REST CONTENT SET /scripts ON SERVICE /svc;");
  EXPECT_EQ(with_clauses("CREATE REST AUTH APP \"MariaDB\" VENDOR MariaDB ALLOW NEW USERS TO REGISTER DEFAULT ROLE "
                         "\"Full Access\";",
                         true, true),
            "CREATE REST AUTH APP \"MariaDB\"\n"
            "  VENDOR MariaDB\n"
            "  ALLOW NEW USERS TO REGISTER\n"
            "  DEFAULT ROLE \"Full Access\";");
  EXPECT_EQ(with_clauses("CREATE OR REPLACE REST SERVICE /svc ENABLED COMMENT \"A simple REST service\" AUTHENTICATION "
                         "PATH \"/authentication\" REDIRECTION DEFAULT VALIDATION DEFAULT;",
                         true, true),
            "CREATE OR REPLACE REST SERVICE /svc\n"
            "  ENABLED\n"
            "  COMMENT \"A simple REST service\"\n"
            "  AUTHENTICATION\n"
            "    PATH \"/authentication\"\n"
            "    REDIRECTION DEFAULT\n"
            "    VALIDATION DEFAULT;");
}

TEST(Formatter, every_new_statement_layout_is_its_own_layout) {
  const std::vector<std::string> statements = {
      "EXPLAIN SELECT c.name, o.total FROM customer AS c JOIN orders AS o ON o.customer_id = c.id WHERE o.total > 100",
      "CREATE TABLE c AS SELECT a, b, count(*) AS n FROM t WHERE x = 1 GROUP BY a, b HAVING count(*) > 1",
      "INSERT INTO film (film_id, title, rating) VALUES (1, 'ACADEMY DINOSAUR', 'PG'), (2, 'ACE GOLDFINGER', 'G'), "
      "(3, 'ADAPTATION HOLES', 'NC-17') ON DUPLICATE KEY UPDATE title = VALUES(title)",
      "GRANT SELECT, INSERT, UPDATE ON db.* TO 'app'@'%', 'other'@'localhost' WITH GRANT OPTION",
      "CREATE USER 'u'@'%' IDENTIFIED BY 'p' REQUIRE SSL WITH MAX_QUERIES_PER_HOUR 10 PASSWORD EXPIRE",
      "ALTER EVENT e ON SCHEDULE EVERY 1 DAY COMMENT 'x' DO DELETE FROM log WHERE ts < NOW()",
      "CREATE OR REPLACE REST VIEW /city ON SERVICE /svc SCHEMA /sakila AS sakila.city CLASS MyCity { cityId: city_id "
      "@KEY, country: sakila.country { countryId: country_id } } AUTHENTICATION REQUIRED OPTIONS { \"a\": [1, 2] }",
  };
  for (const auto &st : statements) {
    for (const bool indented : {true, false}) {
      for (const size_t width : {0, 20, 40, 80, 120}) {
        const std::string once = layout(st + ";", width, true, "  ", indented);
        EXPECT_EQ(layout(once, width, true, "  ", indented), once) << "width " << width << "\n" << once;
      }
    }
  }
}

TEST(Formatter, an_events_body_indents_its_clauses_once) {
  const std::string sql = "DELIMITER $$\nCREATE EVENT e ON SCHEDULE AT '2035-12-31' DO SELECT a FROM t\nWHERE b = 1$$\n"
                          "DELIMITER ;\n";
  EXPECT_EQ(with_clauses(sql, true, true),
            "DELIMITER $$\nCREATE EVENT e\n  ON SCHEDULE AT '2035-12-31'\nDO SELECT a\n  FROM t\n  WHERE b = 1$$\n"
            "DELIMITER ;\n");
}

TEST(Formatter, a_range_formats_the_statements_it_touches) {
  const std::string sql = "select a from t;\nselect b from u;\n";
  const auto index = Statement_index::build(sql);
  Parser_cache cache;
  const auto edits = format_sql(sql, index, 20, 21, cache);
  ASSERT_EQ(edits.size(), 1u);
  EXPECT_EQ(edits[0].text, "SELECT b FROM u");
  EXPECT_EQ(edits[0].begin, 17u);
}

}  // namespace mdbls
