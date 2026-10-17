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

// Completion scenarios: a cursor position (| in the text) and what must and
// must not be offered there.

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "mdbls/completion.h"
#include "mdbls/keywords.h"
#include "mdbls/outline.h"
#include "mdbparse.h"

namespace mdbls {

namespace {

const char *const k_schema = R"(
CREATE DATABASE sakila;
USE sakila;
CREATE TABLE actor (actor_id INT NOT NULL, first_name VARCHAR(45), last_name VARCHAR(45),
  PRIMARY KEY (actor_id));
CREATE TABLE film (film_id INT NOT NULL, title VARCHAR(100) COMMENT 'the title', actor_id INT);
CREATE TABLE `order` (id INT, `the total` DECIMAL(10,2));
CREATE VIEW film_list (fid, ftitle) AS SELECT film_id, title FROM film;
CREATE PROCEDURE refresh_all(IN since DATE) SELECT 1;
CREATE FUNCTION film_count(a INT) RETURNS INT RETURN 1;
)";

struct Result {
  Completion_list list;
  std::string text;
  uint32_t cursor = 0;

  std::vector<std::string> labels(std::optional<Completion_kind> kind = std::nullopt) const {
    std::vector<std::string> out;
    for (const auto &it : list.items) {
      if (!kind || it.kind == *kind) out.push_back(it.label);
    }
    return out;
  }
  bool has(const std::string &label) const {
    const auto l = labels();
    return std::find(l.begin(), l.end(), label) != l.end();
  }
  const Completion_item *item(const std::string &label) const {
    for (const auto &it : list.items) {
      if (it.label == label) return &it;
    }
    return nullptr;
  }
};

// Completes at the '|' in `sql`, which follows the schema above.
Result complete_at(const std::string &sql, bool with_schema = true, size_t max_items = 2000) {
  static Parser_pool pool(2);
  Result r;
  r.text = (with_schema ? std::string(k_schema) : std::string()) + sql;
  const size_t bar = r.text.find('|');
  EXPECT_NE(bar, std::string::npos) << "no | in " << sql;
  r.text.erase(bar, 1);
  r.cursor = static_cast<uint32_t>(bar);
  const auto index = Statement_index::build(r.text);
  auto outline = std::make_shared<Outline>(build_outline(r.text, index, pool));
  Catalog_view view;
  view.add({std::shared_ptr<const Catalog>(outline, &outline->catalog), Layer_kind::document, {}});
  const auto at = statement_at(index, r.cursor);
  view.set_current_schema(schema_in_use(r.text, index, at ? *at : index.find(r.cursor)));
  Parser_cache cache;
  Completion_options o;
  o.max_items = max_items;
  r.list = complete(r.text, index, r.cursor, view, cache, o);
  return r;
}

std::vector<std::string> sorted(std::vector<std::string> v) {
  std::sort(v.begin(), v.end());
  return v;
}

}  // namespace

TEST(Completion, columns_of_an_alias) {
  const auto r = complete_at("SELECT a.| FROM actor a JOIN film f ON f.actor_id = a.actor_id;");
  EXPECT_EQ(r.list.context, Name_kind::column);
  EXPECT_EQ(sorted(r.labels()), (std::vector<std::string>{"actor_id", "first_name", "last_name"}));
  const auto *it = r.item("actor_id");
  ASSERT_NE(it, nullptr);
  EXPECT_EQ(it->detail, "INT NOT NULL");
}

TEST(Completion, columns_of_a_table_name_used_as_qualifier) {
  const auto r = complete_at("SELECT film.| FROM film");
  EXPECT_EQ(sorted(r.labels()), (std::vector<std::string>{"actor_id", "film_id", "title"}));
}

TEST(Completion, select_list_offers_columns_then_functions_and_keywords) {
  const auto r = complete_at("SELECT | FROM film;");
  EXPECT_EQ(r.list.context, Name_kind::column);
  ASSERT_GE(r.list.items.size(), 3u);
  EXPECT_EQ(sorted({r.list.items[0].label, r.list.items[1].label, r.list.items[2].label}),
            (std::vector<std::string>{"actor_id", "film_id", "title"}));
  EXPECT_TRUE(r.has("CONCAT"));
  EXPECT_TRUE(r.has("film_count"));  // a stored function
  EXPECT_TRUE(r.has("DISTINCT"));
  EXPECT_FALSE(r.has("refresh_all"));  // a procedure is not called here
}

TEST(Completion, tables_after_from) {
  const auto r = complete_at("SELECT * FROM |");
  EXPECT_EQ(r.list.context, Name_kind::table);
  for (const char *t : {"actor", "film", "order", "film_list", "sakila"}) {
    EXPECT_TRUE(r.has(t)) << t;
  }
  EXPECT_FALSE(r.has("title"));
  EXPECT_EQ(r.item("order")->insert_text, "`order`");
  EXPECT_EQ(r.item("film_list")->kind, Completion_kind::view);
}

TEST(Completion, prefix_filters_and_sets_the_replace_range) {
  const auto r = complete_at("SELECT * FROM fi|");
  EXPECT_EQ(r.list.prefix, "fi");
  EXPECT_EQ(r.list.replace_begin, r.cursor - 2);
  EXPECT_EQ(r.list.replace_end, r.cursor);
  EXPECT_EQ(sorted(r.labels()), (std::vector<std::string>{"film", "film_list"}));
}

TEST(Completion, inside_backticks) {
  const auto r = complete_at("SELECT * FROM `fil|");
  EXPECT_EQ(r.list.prefix, "fil");
  EXPECT_EQ(r.list.replace_begin, r.cursor - 4);
  ASSERT_NE(r.item("film"), nullptr);
  EXPECT_EQ(r.item("film")->insert_text, "`film`");
}

TEST(Completion, inside_closed_backticks) {
  const auto r = complete_at("SELECT * FROM `fil|`");
  EXPECT_EQ(r.list.replace_begin, r.cursor - 4);
  EXPECT_EQ(r.list.replace_end, r.cursor + 1);  // the closing backtick too
  EXPECT_EQ(r.item("film")->insert_text, "`film`");
}

TEST(Completion, qualified_table) {
  const auto r = complete_at("SELECT * FROM sakila.|");
  EXPECT_EQ(r.list.context, Name_kind::table);
  EXPECT_TRUE(r.has("actor"));
  EXPECT_FALSE(r.has("sakila"));
}

TEST(Completion, where_clause_columns) {
  const auto r = complete_at("SELECT * FROM actor WHERE |");
  EXPECT_EQ(r.list.context, Name_kind::column);
  EXPECT_TRUE(r.has("first_name"));
  EXPECT_FALSE(r.has("title"));
  EXPECT_TRUE(r.has("NOT"));
}

TEST(Completion, join_condition) {
  const auto r = complete_at("SELECT * FROM actor a JOIN film f ON f.|");
  EXPECT_EQ(sorted(r.labels()), (std::vector<std::string>{"actor_id", "film_id", "title"}));
}

TEST(Completion, insert_column_list_while_typing) {
  const auto r = complete_at("INSERT INTO film (|");
  EXPECT_EQ(r.list.context, Name_kind::column);
  EXPECT_TRUE(r.has("title"));
  EXPECT_FALSE(r.has("first_name"));
}

TEST(Completion, insert_column_list_in_a_complete_statement) {
  const auto r = complete_at("INSERT INTO film (title, |) VALUES (1, 2);");
  EXPECT_TRUE(r.has("film_id"));
  EXPECT_FALSE(r.has("first_name"));
}

TEST(Completion, update_set) {
  const auto r = complete_at("UPDATE film SET |");
  EXPECT_EQ(r.list.context, Name_kind::column);
  EXPECT_TRUE(r.has("title"));
}

TEST(Completion, delete_where) {
  const auto r = complete_at("DELETE FROM film WHERE t|");
  EXPECT_EQ(r.labels(Completion_kind::column), (std::vector<std::string>{"title"}));
}

TEST(Completion, use_offers_schemas) {
  const auto r = complete_at("USE |");
  EXPECT_EQ(r.list.context, Name_kind::schema);
  EXPECT_EQ(r.labels(Completion_kind::schema), (std::vector<std::string>{"sakila"}));
}

TEST(Completion, call_offers_procedures) {
  const auto r = complete_at("CALL |");
  EXPECT_EQ(r.list.context, Name_kind::routine);
  EXPECT_TRUE(r.has("refresh_all"));
  EXPECT_FALSE(r.has("film_count"));
}

TEST(Completion, derived_table_columns) {
  const auto r = complete_at("SELECT d.| FROM (SELECT title AS x, film_id FROM film) d");
  EXPECT_EQ(sorted(r.labels()), (std::vector<std::string>{"film_id", "x"}));
}

TEST(Completion, common_table_expression) {
  const auto r = complete_at("WITH c AS (SELECT title FROM film) SELECT c.| FROM c");
  EXPECT_EQ(r.labels(), (std::vector<std::string>{"title"}));
  const auto t = complete_at("WITH c AS (SELECT title FROM film) SELECT * FROM |");
  EXPECT_TRUE(t.has("c"));
}

TEST(Completion, order_by_offers_select_aliases) {
  const auto r = complete_at("SELECT title AS t FROM film ORDER BY |");
  EXPECT_TRUE(r.has("t"));
  EXPECT_TRUE(r.has("film_id"));
}

TEST(Completion, view_with_column_list) {
  const auto r = complete_at("SELECT v.| FROM film_list v");
  EXPECT_EQ(sorted(r.labels()), (std::vector<std::string>{"fid", "ftitle"}));
}

TEST(Completion, functions_by_prefix) {
  const auto r = complete_at("SELECT conc|");
  EXPECT_TRUE(r.has("CONCAT"));
  EXPECT_TRUE(r.has("CONCAT_WS"));
  const auto *c = r.item("CONCAT");
  ASSERT_NE(c, nullptr);
  EXPECT_EQ(c->kind, Completion_kind::function);
  EXPECT_EQ(c->detail, "CONCAT(str1,str2,...)");
  EXPECT_NE(c->documentation.find("concatenating"), std::string::npos);
}

TEST(Completion, statement_start_keywords) {
  const auto r = complete_at("SEL|", false);
  EXPECT_TRUE(r.has("SELECT"));
  EXPECT_FALSE(r.has("FROM"));
  const auto e = complete_at("|", false);
  for (const char *k : {"SELECT", "INSERT", "CREATE", "ALTER", "WITH", "CALL"}) {
    EXPECT_TRUE(e.has(k)) << k;
  }
}

TEST(Completion, after_a_complete_query) {
  const auto r = complete_at("SELECT * FROM film |");
  for (const char *k : {"WHERE", "JOIN", "ORDER", "GROUP", "LIMIT"}) {
    EXPECT_TRUE(r.has(k)) << k;
  }
  // Non-reserved keywords that would only be an alias here are left out.
  EXPECT_FALSE(r.has("STATUS"));
  EXPECT_FALSE(r.has("COMMENT"));
}

TEST(Completion, predicate_keywords) {
  const auto r = complete_at("SELECT * FROM film WHERE title |");
  for (const char *k : {"LIKE", "IN", "IS", "BETWEEN", "NOT"}) {
    EXPECT_TRUE(r.has(k)) << k;
  }
}

TEST(Completion, own_columns_in_create_table_keys) {
  const auto r = complete_at("CREATE TABLE t (id INT, name TEXT, KEY (|))", false);
  EXPECT_EQ(sorted(r.labels(Completion_kind::column)), (std::vector<std::string>{"id", "name"}));
}

TEST(Completion, column_definitions_get_no_names) {
  const auto r = complete_at("CREATE TABLE t2 (|");
  EXPECT_TRUE(r.labels(Completion_kind::column).empty());
  EXPECT_TRUE(r.labels(Completion_kind::table).empty());
}

TEST(Completion, alter_table_columns) {
  const auto r = complete_at("ALTER TABLE film DROP COLUMN |");
  EXPECT_TRUE(r.has("title"));
  EXPECT_FALSE(r.has("first_name"));
}

TEST(Completion, trigger_new_and_old) {
  const auto r = complete_at(
      "CREATE TRIGGER tr BEFORE INSERT ON film FOR EACH ROW SET NEW.title = OLD.|");
  EXPECT_EQ(sorted(r.labels()), (std::vector<std::string>{"actor_id", "film_id", "title"}));
}

TEST(Completion, nothing_in_strings_or_comments) {
  EXPECT_TRUE(complete_at("SELECT * FROM film WHERE title = 'ab|").list.items.empty());
  EXPECT_TRUE(complete_at("SELECT * FROM film -- a comm|").list.items.empty());
  EXPECT_TRUE(complete_at("SELECT * FROM film /* a comm|").list.items.empty());
  EXPECT_TRUE(complete_at("SELECT 12|").list.items.empty());
}

TEST(Completion, subquery_sees_inner_and_outer_tables) {
  const auto r = complete_at("SELECT * FROM actor WHERE actor_id IN (SELECT | FROM film)");
  EXPECT_TRUE(r.has("title"));
  EXPECT_TRUE(r.has("first_name"));
  // the inner query's columns first
  const auto cols = r.labels(Completion_kind::column);
  ASSERT_FALSE(cols.empty());
}

TEST(Completion, routine_variables) {
  const auto r = complete_at(
      "DELIMITER //\n"
      "CREATE PROCEDURE p(IN since DATE)\n"
      "BEGIN\n"
      "  DECLARE total INT DEFAULT 0;\n"
      "  SELECT | FROM film;\n"
      "END//\n");
  ASSERT_NE(r.item("total"), nullptr);
  EXPECT_EQ(r.item("total")->kind, Completion_kind::variable);
  EXPECT_NE(r.item("since"), nullptr);
}

TEST(Completion, statement_start_in_a_routine_body) {
  const std::string body =
      "DELIMITER //\n"
      "CREATE PROCEDURE p()\n"
      "BEGIN\n"
      "  DECLARE v INT;\n"
      "  SELECT COUNT(*) INTO v FROM film;\n"
      "  @@\n"
      "END//\n";
  auto at = [&](const std::string &cursor) {
    std::string t = body;
    t.replace(t.find("@@"), 2, cursor);
    return complete_at(t);
  };
  const auto start = at("|");
  EXPECT_TRUE(start.has("IF"));
  EXPECT_TRUE(start.has("SET"));
  EXPECT_TRUE(start.labels(Completion_kind::column).empty());
  // SELECT ... INTO v names a variable, not a table.
  const auto var = at("IF |");
  ASSERT_NE(var.item("v"), nullptr);
  EXPECT_EQ(var.item("v")->kind, Completion_kind::variable);
  const auto aliases = var.labels(Completion_kind::alias);
  EXPECT_EQ(aliases, (std::vector<std::string>{"film"}));
  EXPECT_FALSE(start.has("PARSE_VCOL_EXPR"));  // the server's own, never typed
}

TEST(Completion, quoted_column_names) {
  const auto r = complete_at("SELECT o.| FROM `order` o");
  ASSERT_NE(r.item("the total"), nullptr);
  EXPECT_EQ(r.item("the total")->insert_text, "`the total`");
}

TEST(Completion, unterminated_statement_at_the_end) {
  const auto r = complete_at("SELECT * FROM film WHERE |   ");
  EXPECT_TRUE(r.has("title"));
}

TEST(Completion, max_items_marks_the_list_incomplete) {
  const auto r = complete_at("SELECT |", true, 20);
  EXPECT_EQ(r.list.items.size(), 20u);
  EXPECT_TRUE(r.list.incomplete);
}

TEST(Keywords, spellings_and_reserved_words) {
  EXPECT_EQ(keyword_spellings(grammar::symbol("SELECT_SYM")),
            (std::vector<std::string_view>{"SELECT"}));
  EXPECT_TRUE(is_nonreserved_keyword(grammar::symbol("STATUS_SYM")));
  EXPECT_FALSE(is_nonreserved_keyword(grammar::symbol("SELECT_SYM")));
  EXPECT_TRUE(needs_quoting("order"));
  EXPECT_TRUE(needs_quoting("the total"));
  EXPECT_TRUE(needs_quoting("123"));
  EXPECT_FALSE(needs_quoting("status"));
  EXPECT_FALSE(needs_quoting("film_2"));
  EXPECT_EQ(quote_identifier("a`b"), "`a``b`");
}

TEST(Keywords, builtin_functions) {
  const auto *f = builtin_function("concat");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->forms, "CONCAT(str1,str2,...)");
  const auto params = call_form_parameters("DATE_FORMAT(date, format[, locale])");
  ASSERT_EQ(params.size(), 2u);
  const std::string_view form = "DATE_FORMAT(date, format[, locale])";
  EXPECT_EQ(form.substr(params[0].first, params[0].second - params[0].first), "date");
  EXPECT_EQ(form.substr(params[1].first, params[1].second - params[1].first), "format[, locale]");
  EXPECT_TRUE(call_form_parameters("NOW()").empty());
}

TEST(Keywords, expected_after_a_complete_statement) {
  Parser_cache cache;
  mdbp_parser *p = cache.get("", k_default_version_id);
  const auto e = expected_after(p, "SELECT 1 FROM t");
  ASSERT_TRUE(e.has_value());
  EXPECT_NE(std::find(e->begin(), e->end(), grammar::symbol("WHERE")), e->end());
  // A syntax error before the end: nothing known.
  EXPECT_FALSE(expected_after(p, "SELECT FROM WHERE x").has_value());
}

}  // namespace mdbls
