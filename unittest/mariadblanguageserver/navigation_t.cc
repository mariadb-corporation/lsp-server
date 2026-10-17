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

// Name resolution, hover, references and signature help.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "mdbls/completion.h"
#include "mdbls/navigation.h"
#include "mdbls/outline.h"

namespace mdbls {

namespace {

const char *const k_schema = R"(CREATE DATABASE sakila;
USE sakila;
CREATE TABLE actor (actor_id INT NOT NULL, first_name VARCHAR(45) COMMENT 'given name',
  PRIMARY KEY (actor_id));
CREATE TABLE film (film_id INT NOT NULL, title VARCHAR(100), actor_id INT,
  FOREIGN KEY (actor_id) REFERENCES actor (actor_id));
CREATE PROCEDURE refresh_all(IN since DATE, OUT n INT) SELECT 1;
CREATE FUNCTION film_count(a INT) RETURNS INT RETURN 1;
)";

Parser_pool &pool() {
  static Parser_pool p(2);
  return p;
}

struct Doc {
  std::string text;
  uint32_t cursor = 0;
  Statement_index index;
  std::shared_ptr<Outline> outline;
  Catalog_view view;
  Parser_cache cache;

  explicit Doc(const std::string &sql, bool with_schema = true) {
    text = (with_schema ? std::string(k_schema) : std::string()) + sql;
    const size_t bar = text.find('|');
    if (bar != std::string::npos) {
      text.erase(bar, 1);
      cursor = static_cast<uint32_t>(bar);
    }
    index = Statement_index::build(text);
    outline = std::make_shared<Outline>(build_outline(text, index, pool()));
    view.add({std::shared_ptr<const Catalog>(outline, &outline->catalog), Layer_kind::document, {}});
    const auto at = statement_at(index, cursor);
    view.set_current_schema(schema_in_use(text, index, at ? *at : index.find(cursor)));
  }

  size_t statement() const { return *statement_at(index, cursor); }

  Resolution resolve() {
    const Statement &st = index.statements()[statement()];
    names = std::make_unique<Statement_names>(cache.get("", k_default_version_id, true),
                                              index.text_of(text, st), st.begin);
    return Name_resolver(view, *names).resolve_at(cursor);
  }

  std::string hover() { return hover_markdown(resolve()); }

  // The texts of the references to what the cursor names.
  std::vector<std::string> references() {
    const auto r = resolve();
    std::vector<std::string> out;
    for (const auto &ref : find_references(text, index, r.id, statement(), view, pool())) {
      out.push_back(text.substr(ref.range.begin, ref.range.end - ref.range.begin) +
                    (ref.defining ? "*" : ""));
    }
    return out;
  }

  std::unique_ptr<Statement_names> names;
};

}  // namespace

TEST(Navigation, hover_table) {
  Doc d("SELECT * FROM fi|lm;");
  const auto r = d.resolve();
  EXPECT_EQ(r.id.kind, Target_kind::table);
  EXPECT_EQ(r.id.schema, "sakila");
  const auto h = hover_markdown(r);
  EXPECT_NE(h.find("**table** `sakila`.`film`"), std::string::npos) << h;
  EXPECT_NE(h.find("title VARCHAR(100)"), std::string::npos) << h;
  EXPECT_NE(h.find("FOREIGN KEY (actor_id) REFERENCES actor (actor_id)"), std::string::npos) << h;
  EXPECT_NE(h.find("Defined in this document."), std::string::npos);
}

TEST(Navigation, hover_column_through_alias) {
  Doc d("SELECT a.first_n|ame FROM actor a;");
  const auto r = d.resolve();
  EXPECT_EQ(r.id.kind, Target_kind::column);
  EXPECT_EQ(r.id.table, "actor");
  ASSERT_NE(r.column, nullptr);
  const auto h = hover_markdown(r);
  EXPECT_NE(h.find("first_name VARCHAR(45)"), std::string::npos) << h;
  EXPECT_NE(h.find("given name"), std::string::npos) << h;
}

TEST(Navigation, unqualified_column_found_in_the_right_table) {
  Doc d("SELECT tit|le FROM actor JOIN film USING (actor_id);");
  const auto r = d.resolve();
  EXPECT_EQ(r.id.table, "film");
}

TEST(Navigation, hover_alias) {
  Doc d("SELECT a|.actor_id FROM actor a;");
  const auto r = d.resolve();
  EXPECT_EQ(r.id.kind, Target_kind::table_alias);
  ASSERT_TRUE(r.local_definition.has_value());
  EXPECT_EQ(d.text.substr(r.local_definition->begin, 1), "a");
  EXPECT_NE(hover_markdown(r).find("**alias** `a` for table `actor`"), std::string::npos);
}

TEST(Navigation, hover_builtin_functions) {
  Doc d("SELECT CONC|AT('a', 'b');");
  EXPECT_NE(d.hover().find("CONCAT(str1,str2,...)"), std::string::npos);
  Doc k("SELECT COU|NT(*) FROM film;");
  EXPECT_NE(k.hover().find("**function** `COUNT`"), std::string::npos);
}

TEST(Navigation, hover_routines) {
  Doc d("CALL refresh_a|ll(NOW(), @n);");
  const auto h = d.hover();
  EXPECT_NE(h.find("PROCEDURE refresh_all(IN since DATE, OUT n INT)"), std::string::npos) << h;
  Doc f("SELECT film_co|unt(1);");
  EXPECT_NE(f.hover().find("RETURNS INT"), std::string::npos);
}

TEST(Navigation, definitions_resolve_to_themselves) {
  Doc d("SELECT 1;");
  const size_t name = d.text.find("TABLE film") + 6;
  Doc c(d.text.substr(0, name + 1) + "|" + d.text.substr(name + 1), false);
  const auto r = c.resolve();
  EXPECT_EQ(r.id.kind, Target_kind::table);
  EXPECT_TRUE(r.ref.defining);
  ASSERT_NE(r.table, nullptr);
  EXPECT_EQ(r.table->origin.name.begin, name);
}

TEST(Navigation, references_to_a_table) {
  Doc d(
      "SELECT * FROM fi|lm;\n"
      "INSERT INTO film (title) VALUES ('x');\n"
      "SELECT film.title FROM sakila.film;\n"
      "SELECT 'film' FROM actor;\n"
      "CREATE DATABASE other;\n"
      "USE other;\n"
      "SELECT * FROM film;\n");
  // Not the string, and not other.film after USE other.
  EXPECT_EQ(d.references(), (std::vector<std::string>{"film*", "film", "film", "film", "film"}));
}

TEST(Navigation, references_to_a_column) {
  Doc d(
      "SELECT ti|tle FROM film;\n"
      "SELECT f.title FROM film f WHERE f.title > '';\n"
      "UPDATE film SET title = NULL;\n"
      "SELECT first_name AS title FROM actor ORDER BY title;\n");
  // The definition, then each use; the select alias and its use in ORDER BY
  // name something else.
  EXPECT_EQ(d.references(),
            (std::vector<std::string>{"title*", "title", "title", "title", "title"}));
}

TEST(Navigation, references_to_an_alias_stay_in_the_statement) {
  Doc d("SELECT a.actor_id FROM actor a| WHERE a.first_name = 'x';\nSELECT a.film_id FROM film a;");
  EXPECT_EQ(d.references(), (std::vector<std::string>{"a", "a*", "a"}));
}

TEST(Navigation, signature_help_for_builtins) {
  Doc d("SELECT CONCAT('a', |");
  const auto h = signature_help(d.text, d.index, d.cursor, d.view, d.cache, k_default_version_id);
  ASSERT_TRUE(h.has_value());
  ASSERT_EQ(h->signatures.size(), 1u);
  EXPECT_EQ(h->signatures[0].label, "CONCAT(str1,str2,...)");
  EXPECT_EQ(h->active_parameter, 1u);
  ASSERT_EQ(h->signatures[0].params.size(), 3u);

  Doc s("SELECT SUBSTRING(title, 2, |) FROM film;");
  const auto hs = signature_help(s.text, s.index, s.cursor, s.view, s.cache, k_default_version_id);
  ASSERT_TRUE(hs.has_value());
  EXPECT_EQ(hs->active_parameter, 2u);
  EXPECT_EQ(hs->signatures[hs->active_signature].label, "SUBSTRING(str,pos,len)");
}

TEST(Navigation, signature_help_for_routines) {
  Doc d("CALL refresh_all(NOW(), |");
  const auto h = signature_help(d.text, d.index, d.cursor, d.view, d.cache, k_default_version_id);
  ASSERT_TRUE(h.has_value());
  EXPECT_EQ(h->signatures[0].label, "refresh_all(IN since DATE, OUT n INT)");
  EXPECT_EQ(h->active_parameter, 1u);
  ASSERT_EQ(h->signatures[0].params.size(), 2u);
  // Inside a nested call, the inner one counts.
  Doc n("SELECT film_count(CONCAT(|");
  const auto hn = signature_help(n.text, n.index, n.cursor, n.view, n.cache, k_default_version_id);
  ASSERT_TRUE(hn.has_value());
  EXPECT_EQ(hn->signatures[0].label, "CONCAT(str1,str2,...)");
  Doc none("SELECT film_id FROM film WHERE film_id IN (|");
  EXPECT_FALSE(signature_help(none.text, none.index, none.cursor, none.view, none.cache,
                              k_default_version_id)
                   .has_value());
}

}  // namespace mdbls
