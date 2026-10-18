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

#include <algorithm>
#include <string>
#include <vector>

#include "mdbls/formatter.h"

namespace mdbls {

namespace {

// `sql` with its edits applied.
std::string layout(const std::string &sql, size_t width = 80, bool upper = true, std::string indent = "  ") {
  const auto index = Statement_index::build(sql);
  Parser_cache cache;
  Format_options o;
  o.line_width = width;
  o.upper_case_keywords = upper;
  o.indent = std::move(indent);
  auto edits = format_sql(sql, index, 0, static_cast<uint32_t>(sql.size()), cache, o);
  std::sort(edits.begin(), edits.end(), [](const Text_edit &a, const Text_edit &b) { return a.begin > b.begin; });
  std::string out = sql;
  for (const auto &e : edits) out.replace(e.begin, e.end - e.begin, e.text);
  return out;
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
  EXPECT_EQ(layout("SELECT * FROM t WHERE x IN ( -- why\nSELECT 1);"),
            "SELECT *\nFROM t\nWHERE x IN ( -- why\n  SELECT 1\n);");
}

TEST(Formatter, line_breaks_inside_other_constructs_are_kept) {
  EXPECT_EQ(layout("SELECT CASE\nWHEN a = 1 THEN 'one'\nELSE 'other' END AS x\nFROM t;"),
            "SELECT CASE\n         WHEN a = 1 THEN 'one'\n         ELSE 'other' END AS x\nFROM t;");
}

TEST(Formatter, other_statements_are_left_alone) {
  const std::string ddl = "CREATE TABLE t (\n  id INT,\n  name TEXT\n);\nINSERT INTO t VALUES (1,'a');\n";
  EXPECT_EQ(layout(ddl), ddl);
  const std::string versioned = "/*!40101 SELECT a FROM t */;";
  EXPECT_EQ(layout(versioned), versioned);
  const std::string broken = "SELECT a FROM WHERE;";
  EXPECT_EQ(layout(broken), broken);
}

TEST(Formatter, the_indentation_and_line_endings_of_the_statement_are_kept) {
  EXPECT_EQ(layout("  SELECT a FROM t\r\n  WHERE b = 1;"), "  SELECT a\r\n  FROM t\r\n  WHERE b = 1;");
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
  };
  for (const auto &q : queries) {
    for (const size_t width : {0, 20, 40, 80, 120}) {
      const std::string once = layout(q + ";", width);
      EXPECT_EQ(layout(once, width), once) << "width " << width << "\n" << once;
      // The same, every line indented: it stays so.
      std::string indented = "  " + once;
      for (size_t at = indented.find('\n'); at != std::string::npos; at = indented.find('\n', at + 1)) {
        indented.insert(at + 1, "  ");
      }
      EXPECT_EQ(layout(indented, width), indented) << "width " << width << ", indented\n" << indented;
    }
  }
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
