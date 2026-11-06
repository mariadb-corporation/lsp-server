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
#include <rapidjson/document.h>

#include <algorithm>
#include <map>
#include <random>
#include <string>
#include <variant>

#include "mdbls/completion.h"
#include "mdbls/schema_edit.h"
#include "mdbls/source_include.h"

namespace mdbls {
namespace {

Parser_pool &pool() {
  static Parser_pool p(2);
  return p;
}

const std::string k_dir = "/p";

// The files of a project, by name in k_dir.
using Files = std::map<std::string, std::string>;

Source_reader reader(const Files &files) {
  return [files](const std::string &path, std::string *text) {
    for (const auto &[name, t] : files) {
      if (k_dir + "/" + name == path) {
        *text = t;
        return true;
      }
    }
    return false;
  };
}

std::variant<Edit_result, Edit_error> edit(const std::string &script, const Files &files,
                                           const std::string &request) {
  auto parsed = parse_edit_request(request);
  if (auto *e = std::get_if<Edit_error>(&parsed)) return *e;
  Edit_options o;
  o.source_dir = k_dir;
  o.read = reader(files);
  return apply_schema_edits(script, std::get<Edit_request>(parsed), pool(), o);
}

// The request of one schema and these edits (JSON objects).
std::string request(const std::string &schema, const std::string &edits) {
  return "{\"format\": 1, \"schema\": " + (schema.empty() ? std::string("null") : "\"" + schema + "\"") +
         ", \"edits\": [" + edits + "]}";
}

std::string rename_table(const std::string &from, const std::string &to) {
  return R"({"op": "rename_table", "table": ")" + from + R"(", "new_name": ")" + to + "\"}";
}

// The result, after checking that every file's edits give its text.
Edit_result ok(const std::variant<Edit_result, Edit_error> &r) {
  Edit_result none;
  if (const auto *e = std::get_if<Edit_error>(&r)) {
    ADD_FAILURE() << e->kind << ": " << e->message;
    return none;
  }
  const auto &result = std::get<Edit_result>(r);
  for (const auto &f : result.files) {
    EXPECT_EQ(f.text, apply_edits(f.original, f.edits)) << f.path;
  }
  return result;
}

Edit_error failed(const std::variant<Edit_result, Edit_error> &r) {
  Edit_error none;
  if (!std::holds_alternative<Edit_error>(r)) {
    ADD_FAILURE() << "the edit was not refused";
    return none;
  }
  return std::get<Edit_error>(r);
}

// A file of the result: "" is the script.
const Edited_file *file(const Edit_result &r, const std::string &name) {
  for (const auto &f : r.files) {
    if (name.empty() ? f.path.empty() : f.path == k_dir + "/" + name) return &f;
  }
  return nullptr;
}

std::string text(const Edit_result &r, const std::string &name) {
  const auto *f = file(r, name);
  return f ? f->text : "<no file " + name + ">";
}

size_t edit_count(const Edit_result &r, const std::string &name) {
  const auto *f = file(r, name);
  return f ? f->edits.size() : 0;
}

const Table *table(const Edit_result &r, const std::string &schema, const std::string &name) {
  const Schema *s = r.outline.catalog.find(schema);
  return s == nullptr ? nullptr : s->table(name);
}

std::vector<std::string> migration(const Edit_result &r) {
  std::vector<std::string> out;
  for (const auto &m : r.migration) out.push_back(m.sql);
  return out;
}

std::vector<std::string> note_kinds(const Edit_result &r) {
  std::vector<std::string> out;
  for (const auto &n : r.notes) out.push_back(n.kind);
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// The edit log

TEST(Edit_log, keeps_edits_in_the_original_text) {
  std::mt19937 rng(7);
  for (int round = 0; round < 500; ++round) {
    std::string original;
    const int length = static_cast<int>(rng() % 40);
    for (int k = 0; k < length; ++k) original += static_cast<char>('a' + rng() % 26);
    Edit_log log(original);
    for (int batch = 0; batch < 6; ++batch) {
      const auto size = static_cast<uint32_t>(log.text().size());
      // Up to three sorted edits that do not overlap.
      std::vector<uint32_t> cuts;
      for (int k = 0; k < 6; ++k) cuts.push_back(size == 0 ? 0 : rng() % (size + 1));
      std::sort(cuts.begin(), cuts.end());
      std::vector<Text_edit> edits;
      for (size_t k = 0; k + 1 < cuts.size(); k += 2) {
        if (rng() % 3 == 0) continue;
        std::string t;
        for (uint32_t n = rng() % 4; n > 0; --n) t += static_cast<char>('A' + rng() % 26);
        edits.push_back({cuts[k], cuts[k + 1], t});
      }
      for (size_t k = 1; k < edits.size(); ++k) {
        if (edits[k].begin < edits[k - 1].end) edits[k].begin = edits[k].end = edits[k - 1].end;
      }
      const std::string expected = apply_edits(log.text(), edits);
      log.apply(edits);
      ASSERT_EQ(expected, log.text());
      ASSERT_EQ(log.text(), apply_edits(original, log.edits()));
      for (size_t k = 1; k < log.edits().size(); ++k) {
        ASSERT_LE(log.edits()[k - 1].end, log.edits()[k].begin);
      }
    }
  }
}

TEST(Edit_log, maps_offsets) {
  Edit_log log("0123456789");
  log.apply({{2, 4, "ab"}, {6, 6, "XYZ"}, {8, 10, ""}});
  EXPECT_EQ("01ab45XYZ67", log.text());
  EXPECT_EQ(0u, log.to_current(0, false).value());
  EXPECT_EQ(2u, log.to_current(2, false).value());
  EXPECT_FALSE(log.to_current(3, false).has_value());  // replaced
  EXPECT_EQ(4u, log.to_current(4, false).value());
  EXPECT_EQ(6u, log.to_current(6, false).value());  // before the insert
  EXPECT_EQ(9u, log.to_current(6, true).value());   // after it
  EXPECT_EQ(11u, log.to_current(10, false).value());
  EXPECT_EQ(2u, log.to_original(3));  // in written text: its start
  EXPECT_EQ(6u, log.to_original(7));
  EXPECT_EQ(7u, log.to_original(10));
}

// ---------------------------------------------------------------------------
// Slice bounds

TEST(Slice_bound, round_trips) {
  const std::vector<std::string> texts = {"a\nbb\nccc\n", "a\r\nbb\r\nccc", "\xc3\xa4x\r\ny\n\n",
                                          "one line"};
  const std::vector<std::string> forms = {"", "1", "-1", "ln1", "ln-1"};
  for (const auto &t : texts) {
    for (uint32_t off = 0; off <= t.size(); ++off) {
      for (const auto &form : forms) {
        for (const bool is_end : {false, true}) {
          const auto b = slice_bound_for(t, form, is_end, off);
          if (!b) continue;
          const auto s = is_end ? slice_source(t, "", *b) : slice_source(t, *b, "");
          ASSERT_TRUE(s.error.empty()) << *b;
          EXPECT_EQ(off, is_end ? s.range.end : s.range.begin)
              << "form " << form << " -> " << *b << (is_end ? " (end)" : " (start)") << " in "
              << testing::PrintToString(t);
        }
      }
    }
  }
  // "\r\n" counts as one character; a line bound only at a line start.
  EXPECT_EQ("3", slice_bound_for("a\r\nb", "9", false, 4).value());
  EXPECT_EQ("ln2", slice_bound_for("a\nb\nc", "ln7", false, 2).value());
  EXPECT_FALSE(slice_bound_for("a\nb\nc", "ln7", false, 3).has_value());
  EXPECT_EQ("ln-2", slice_bound_for("a\nb\nc\n", "ln-1", true, 4).value());
  EXPECT_EQ("", slice_bound_for("abc", "-1", true, 3).value());
}

TEST(Slice_bound, unmap_inverts_map) {
  const std::string a = "-- head\nCREATE TABLE t (id INT);\n";
  const std::string script = "USE s;\nSOURCE 'a.sql'[ln2:];\nSELECT 1;\nSOURCE 'a.sql';\n";
  const auto e = expand_sources(script, k_dir, reader({{"a.sql", a}}));
  ASSERT_EQ(2u, e.includes.size());
  const auto size = static_cast<uint32_t>(e.text.size());
  for (uint32_t off = 0; off < size; ++off) {
    const auto at = e.map(off);
    const auto back = unmap_source(e.segments, size, at.source, at.offset);
    ASSERT_TRUE(back.has_value()) << off;
    EXPECT_EQ(off, *back);
  }
  // The header lies outside the first slice, not the second's; past the end
  // of a text nothing.
  EXPECT_EQ(e.text.find("-- head"), unmap_source(e.segments, size, 2, 0).value());
  EXPECT_FALSE(unmap_source(e.segments, size, 1, 0).has_value());
  EXPECT_FALSE(unmap_source(e.segments, size, 1, static_cast<uint32_t>(a.size())).has_value());
}

// ---------------------------------------------------------------------------
// Requests

TEST(Schema_edit, request_errors) {
  auto kind = [](const std::string &json) {
    const auto r = parse_edit_request(json);
    return std::holds_alternative<Edit_error>(r) ? std::get<Edit_error>(r).kind : "ok";
  };
  EXPECT_EQ("bad-request", kind("[]"));
  EXPECT_EQ("bad-request", kind(R"({"format": 2, "edits": []})"));
  EXPECT_EQ("bad-request", kind(R"({"format": 1})"));
  EXPECT_EQ("bad-request", kind(request("s", R"({"op": "rename"})")));
  EXPECT_EQ("bad-request", kind(request("s", R"({"op": "rename_table", "table": "t"})")));
  EXPECT_EQ("bad-request", kind(request("s", R"({"op": "rename_table", "table": "t", "new_name": ""})")));
  EXPECT_EQ("bad-request", kind(request("s", rename_table("t", std::string(65, 'x')))));
  EXPECT_EQ("bad-request", kind(request("s", rename_table("t", "x "))));
  EXPECT_EQ("bad-request",
            kind(request("s", R"({"op": "rename_routine", "routine": "r", "kind": "x", "new_name": "q"})")));
  EXPECT_EQ("ok", kind(request("", rename_table("t", std::string(64, 'x')))));
  EXPECT_EQ("ok", kind(request("s", R"({"op": "drop_table", "table": "t"})")));
}

TEST(Schema_edit, unsupported_ops) {
  const auto e = failed(edit("CREATE TABLE t (a INT);", {}, request("", R"({"op": "drop_table", "table": "t"})")));
  EXPECT_EQ("unsupported", e.kind);
  EXPECT_EQ(0u, e.edit.value());
}

// ---------------------------------------------------------------------------
// Renames

TEST(Schema_edit, rename_table_in_one_text) {
  const std::string script =
      "CREATE TABLE customer (id INT PRIMARY KEY, name TEXT);\n"
      "CREATE TABLE orders (id INT, cid INT,\n"
      "  FOREIGN KEY (cid) REFERENCES customer (id));\n"
      "CREATE VIEW v AS SELECT c.name FROM customer c JOIN orders o ON o.cid = c.id;\n"
      "INSERT INTO customer VALUES (1, 'customer');\n";
  const auto r = ok(edit(script, {}, request("", rename_table("customer", "client"))));
  EXPECT_EQ(
      "CREATE TABLE client (id INT PRIMARY KEY, name TEXT);\n"
      "CREATE TABLE orders (id INT, cid INT,\n"
      "  FOREIGN KEY (cid) REFERENCES client (id));\n"
      "CREATE VIEW v AS SELECT c.name FROM client c JOIN orders o ON o.cid = c.id;\n"
      "INSERT INTO client VALUES (1, 'customer');\n",
      text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"RENAME TABLE `customer` TO `client`"}, migration(r));
  // The string literal is data, not dynamic SQL, but nothing tells them apart.
  EXPECT_EQ(std::vector<std::string>{"name-in-string"}, note_kinds(r));
  ASSERT_NE(nullptr, table(r, "", "client"));
  EXPECT_EQ("client", table(r, "", "orders")->foreign_keys[0].ref_table);
}

TEST(Schema_edit, rename_across_sliced_includes) {
  // a.sql is included twice, b.sql with a character slice whose end must
  // move when the renamed name gets shorter.
  const std::string a =
      "-- header\n"
      "CREATE TABLE customer (id INT PRIMARY KEY);\n"
      "CREATE TABLE orders (id INT, cid INT, FOREIGN KEY (cid) REFERENCES customer (id));\n"
      "-- middle\n"
      "CREATE VIEW v AS SELECT id FROM customer;\n";
  const std::string b =
      "-- b header\n"
      "INSERT INTO customer VALUES ('customer');\n"
      "SELECT 'not this';\n";
  const auto b_begin = b.find("INSERT");
  const auto b_end = b.find("SELECT");
  const std::string script = "USE s;\n"
                             "SOURCE 'a.sql'[ln2:ln3];\n"
                             "SOURCE 'b.sql'[" + std::to_string(b_begin) + ":" + std::to_string(b_end) + "];\n"
                             "SOURCE 'a.sql'[ln5:];\n"
                             "SOURCE 'b.sql'[" + std::to_string(b_begin) + ":-19];\n";
  const auto r = ok(edit(script, {{"a.sql", a}, {"b.sql", b}}, request("s", rename_table("customer", "client"))));
  EXPECT_EQ(3u, edit_count(r, "a.sql"));  // once, for both directives
  EXPECT_EQ(1u, edit_count(r, "b.sql"));
  // The positive end moved by the two characters "customer" lost; the
  // negative one and the line bounds stay.
  EXPECT_EQ("USE s;\n"
            "SOURCE 'a.sql'[ln2:ln3];\n"
            "SOURCE 'b.sql'[" + std::to_string(b_begin) + ":" + std::to_string(b_end - 2) + "];\n"
            "SOURCE 'a.sql'[ln5:];\n"
            "SOURCE 'b.sql'[" + std::to_string(b_begin) + ":-19];\n",
            text(r, ""));
  ASSERT_NE(nullptr, table(r, "s", "client"));
  EXPECT_EQ(nullptr, table(r, "s", "customer"));
  EXPECT_TRUE(r.outline.notes.empty());
  // The literal of b.sql, included twice, is noted once.
  EXPECT_EQ(std::vector<std::string>{"name-in-string"}, note_kinds(r));
  EXPECT_EQ(b.find("'customer'"), r.notes[0].at);  // the token starts at its quote
  // The view of the second include is in a.sql, after the rename.
  const auto *v = r.outline.catalog.find("s")->view("v");
  ASSERT_NE(nullptr, v);
  EXPECT_EQ("SELECT id FROM client", v->definition);
  // In the JSON, files by their path relative to the source folder.
  rapidjson::Document d;
  d.Parse(edit_result_json(r, "-", k_dir).c_str());
  ASSERT_TRUE(d.IsObject());
  std::vector<std::string> names;
  for (const auto &f : d["files"].GetArray()) names.push_back(f["file"].GetString());
  EXPECT_EQ((std::vector<std::string>{"-", "a.sql", "b.sql"}), names);
  EXPECT_EQ(1, d["files"][1]["edits"][0]["range"]["start"]["line"].GetInt());
  EXPECT_TRUE(d["outline"].IsObject());
}

TEST(Schema_edit, file_included_twice_with_different_slices) {
  // The edit of the first line lies before the second directive's
  // selection, whose positive start moves with it.
  const std::string a = "CREATE TABLE t (id INT);\nINSERT INTO t VALUES (1);\n";
  const std::string script = "SOURCE 'a.sql'[:25];\nSOURCE 'a.sql'[25:];\n";
  const auto r = ok(edit(script, {{"a.sql", a}}, request("", rename_table("t", "tt"))));
  EXPECT_EQ(2u, edit_count(r, "a.sql"));
  EXPECT_EQ("SOURCE 'a.sql'[:26];\nSOURCE 'a.sql'[26:];\n", text(r, ""));
  EXPECT_EQ("tt", table(r, "", "tt")->name);
}

TEST(Schema_edit, rename_column_defined_by_alter_table) {
  const std::string script =
      "CREATE TABLE t (id INT);\n"
      "ALTER TABLE t ADD COLUMN email VARCHAR(80), ADD INDEX (email);\n"
      "CREATE TABLE u (e VARCHAR(80), FOREIGN KEY (e) REFERENCES t (email));\n"
      "UPDATE t SET email = NULL WHERE email = '';\n";
  const auto r = ok(edit(script, {},
                          request("", R"({"op": "rename_column", "table": "t", "column": "EMAIL", "new_name": "mail"})")));
  EXPECT_EQ(
      "CREATE TABLE t (id INT);\n"
      "ALTER TABLE t ADD COLUMN mail VARCHAR(80), ADD INDEX (mail);\n"
      "CREATE TABLE u (e VARCHAR(80), FOREIGN KEY (e) REFERENCES t (mail));\n"
      "UPDATE t SET mail = NULL WHERE mail = '';\n",
      text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"ALTER TABLE `t` RENAME COLUMN `email` TO `mail`"}, migration(r));
}

TEST(Schema_edit, quoting_follows_each_token) {
  const std::string script =
      "CREATE TABLE `a` (id INT);\n"
      "INSERT INTO a VALUES (1);\n"
      "SELECT * FROM `a`;\n";
  const auto r = ok(edit(script, {}, request("", rename_table("a", "select"))));
  EXPECT_EQ(
      "CREATE TABLE `select` (id INT);\n"
      "INSERT INTO `select` VALUES (1);\n"
      "SELECT * FROM `select`;\n",
      text(r, ""));
  const auto r2 = ok(edit(script, {}, request("", rename_table("a", "b`c"))));
  EXPECT_EQ("CREATE TABLE `b``c` (id INT);\nINSERT INTO `b``c` VALUES (1);\nSELECT * FROM `b``c`;\n",
            text(r2, ""));
  const auto r3 = ok(edit(script, {}, request("", rename_table("a", "plain"))));
  EXPECT_EQ("CREATE TABLE `plain` (id INT);\nINSERT INTO plain VALUES (1);\nSELECT * FROM `plain`;\n",
            text(r3, ""));
}

TEST(Schema_edit, case_change_is_a_rename) {
  const auto r = ok(edit("CREATE TABLE t (a INT);\nSELECT a FROM t;\n", {}, request("", rename_table("t", "T"))));
  EXPECT_EQ("CREATE TABLE T (a INT);\nSELECT a FROM T;\n", text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"RENAME TABLE `t` TO `T`"}, migration(r));
}

TEST(Schema_edit, the_same_name_again_changes_nothing) {
  // RENAME TABLE t TO t fails on the server: no edit, no migration.
  const std::string script = "CREATE TABLE t (a INT);\nCREATE PROCEDURE p() SELECT a FROM t;\n";
  const auto r = ok(edit(script, {},
                         request("", rename_table("t", "t") + "," +
                                         R"({"op": "rename_column", "table": "t", "column": "a", "new_name": "a"})" +
                                         "," + R"({"op": "rename_routine", "routine": "p", "new_name": "p"})")));
  EXPECT_EQ(script, text(r, ""));
  EXPECT_EQ(0u, edit_count(r, ""));
  EXPECT_TRUE(r.migration.empty());
  EXPECT_TRUE(r.notes.empty());
}

TEST(Schema_edit, crlf_files) {
  const std::string a = "-- head\r\nCREATE TABLE customer (id INT);\r\nINSERT INTO customer VALUES (1);\r\n-- tail\r\n";
  // A character slice over "\r\n" files: "\r\n" counts as one character.
  const std::string script = "USE s;\r\nSOURCE 'a.sql'[8:-8];\r\n";
  const auto r = ok(edit(script, {{"a.sql", a}}, request("s", rename_table("customer", "c"))));
  EXPECT_EQ("-- head\r\nCREATE TABLE c (id INT);\r\nINSERT INTO c VALUES (1);\r\n-- tail\r\n", text(r, "a.sql"));
  EXPECT_EQ(script, text(r, ""));
  rapidjson::Document d;
  d.Parse(edit_result_json(r, "-", k_dir).c_str());
  const auto &e = d["files"][0]["edits"];
  EXPECT_EQ(2u, e.Size());
  EXPECT_EQ(2, e[1]["range"]["start"]["line"].GetInt());
  EXPECT_EQ(12, e[1]["range"]["start"]["character"].GetInt());
}

TEST(Schema_edit, routines_in_delimiter_blocks) {
  const std::string script =
      "DELIMITER %%\n"
      "CREATE PROCEDURE p()\nBEGIN\n  SELECT 1;\nEND%%\n"
      "CREATE PROCEDURE q()\nBEGIN\n  CALL p();\nEND%%\n"
      "DELIMITER ;\n"
      "CALL p();\n";
  const auto r = ok(edit(script, {}, request("", R"({"op": "rename_routine", "routine": "P", "new_name": "p2"})")));
  EXPECT_EQ(
      "DELIMITER %%\n"
      "CREATE PROCEDURE p2()\nBEGIN\n  SELECT 1;\nEND%%\n"
      "CREATE PROCEDURE q()\nBEGIN\n  CALL p2();\nEND%%\n"
      "DELIMITER ;\n"
      "CALL p2();\n",
      text(r, ""));
  // No delimiter in the migration: it runs inside a stored procedure.
  EXPECT_EQ((std::vector<std::string>{"DROP PROCEDURE `p`", "CREATE PROCEDURE p2()\nBEGIN\n  SELECT 1;\nEND"}),
            migration(r));
  EXPECT_EQ(std::vector<std::string>{"routine-recreated"}, note_kinds(r));
}

TEST(Schema_edit, routine_kinds) {
  const std::string script =
      "CREATE PROCEDURE r() SELECT 1;\n"
      "CREATE FUNCTION r() RETURNS INT RETURN 1;\n"
      "SELECT r();\n";
  EXPECT_EQ("bad-request",
            failed(edit(script, {}, request("", R"({"op": "rename_routine", "routine": "r", "new_name": "x"})"))).kind);
  // References cannot tell the two apart.
  EXPECT_EQ("unsupported",
            failed(edit(script, {}, request("", R"({"op": "rename_routine", "routine": "r", "kind": "function", "new_name": "f"})")))
                .kind);
  const auto r = ok(edit("CREATE FUNCTION r() RETURNS INT RETURN 1;\nSELECT r();\n", {},
                         request("", R"({"op": "rename_routine", "routine": "r", "kind": "function", "new_name": "f"})")));
  EXPECT_EQ("CREATE FUNCTION f() RETURNS INT RETURN 1;\nSELECT f();\n", text(r, ""));
  EXPECT_EQ("DROP FUNCTION `r`", migration(r)[0]);
  EXPECT_EQ("unknown-object",
            failed(edit("CREATE FUNCTION r() RETURNS INT RETURN 1;\n", {},
                        request("", R"({"op": "rename_routine", "routine": "r", "kind": "procedure", "new_name": "f"})")))
                .kind);
}

TEST(Schema_edit, rename_view) {
  const std::string script =
      "CREATE TABLE t (a INT);\n"
      "CREATE VIEW v AS SELECT a FROM t;\n"
      "CREATE VIEW w AS SELECT a FROM v;\n";
  const auto r = ok(edit(script, {}, request("", R"({"op": "rename_view", "view": "v", "new_name": "v1"})")));
  EXPECT_EQ("CREATE TABLE t (a INT);\nCREATE VIEW v1 AS SELECT a FROM t;\nCREATE VIEW w AS SELECT a FROM v1;\n",
            text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"RENAME TABLE `v` TO `v1`"}, migration(r));
  // Tables and views share a namespace.
  EXPECT_EQ("name-taken",
            failed(edit(script, {}, request("", R"({"op": "rename_view", "view": "v", "new_name": "T"})"))).kind);
  EXPECT_EQ("unknown-object",
            failed(edit(script, {}, request("", R"({"op": "rename_view", "view": "t", "new_name": "x"})"))).kind);
}

TEST(Schema_edit, edits_apply_in_order) {
  const std::string script = "USE s;\nCREATE TABLE a (x INT);\nCREATE TABLE b (y INT, FOREIGN KEY (y) REFERENCES a (x));\n";
  const auto r = ok(edit(script, {},
                          request("s", rename_table("a", "c") + "," +
                                           R"({"op": "rename_column", "table": "c", "column": "x", "new_name": "z"})" +
                                           "," + rename_table("b", "a"))));
  EXPECT_EQ("USE s;\nCREATE TABLE c (z INT);\nCREATE TABLE a (y INT, FOREIGN KEY (y) REFERENCES c (z));\n", text(r, ""));
  EXPECT_EQ((std::vector<std::string>{"RENAME TABLE `a` TO `c`", "ALTER TABLE `c` RENAME COLUMN `x` TO `z`",
                                      "RENAME TABLE `b` TO `a`"}),
            migration(r));
  EXPECT_EQ(0u, r.migration[0].edit);
  EXPECT_EQ(2u, r.migration[2].edit);
}

TEST(Schema_edit, all_or_nothing) {
  const std::string script = "CREATE TABLE a (x INT);\nCREATE TABLE b (y INT);\n";
  const auto e = failed(edit(script, {},
                              request("", rename_table("a", "c") + "," + rename_table("a", "d") + "," +
                                              rename_table("b", "e"))));
  EXPECT_EQ("unknown-object", e.kind);
  EXPECT_EQ(1u, e.edit.value());
  EXPECT_EQ("name-taken", failed(edit(script, {}, request("", rename_table("a", "B")))).kind);
}

TEST(Schema_edit, names_in_the_request_schema_and_before_use) {
  const std::string script =
      "CREATE TABLE early (a INT);\n"
      "USE s;\n"
      "CREATE TABLE t (a INT);\n"
      "USE other;\n"
      "CREATE TABLE o (a INT);\n";
  EXPECT_EQ("x", table(ok(edit(script, {}, request("s", rename_table("early", "x")))), "", "x")->name);
  EXPECT_EQ("x", table(ok(edit(script, {}, request("s", rename_table("t", "x")))), "s", "x")->name);
  EXPECT_EQ("unknown-object", failed(edit(script, {}, request("s", rename_table("o", "x")))).kind);
}

TEST(Schema_edit, notes) {
  const std::string script =
      "USE s;\n"
      "CREATE TABLE customer (id INT COMMENT 'every customer');\n"
      "DELIMITER %%\n"
      "CREATE PROCEDURE p()\nBEGIN\n"
      "  PREPARE st FROM 'SELECT id FROM customer';\n"
      "  EXECUTE IMMEDIATE CONCAT('DELETE FROM ', 'customer');\n"
      "END%%\n"
      "DELIMITER ;\n"
      "CREATE REST SERVICE /svc;\n"
      "CREATE REST SCHEMA /sch ON SERVICE /svc FROM s;\n"
      "CREATE REST VIEW /c ON SERVICE /svc SCHEMA /sch AS s.customer;\n";
  const auto r = ok(edit(script, {}, request("s", rename_table("customer", "client"))));
  EXPECT_EQ((std::vector<std::string>{"name-in-string", "name-in-string", "rest-reference"}), note_kinds(r));
  // In the original text: the literals' lines.
  rapidjson::Document d;
  d.Parse(edit_result_json(r, "-", k_dir).c_str());
  EXPECT_EQ(5, d["notes"][0]["start"]["line"].GetInt());
  EXPECT_EQ(6, d["notes"][1]["start"]["line"].GetInt());
  EXPECT_EQ(11, d["notes"][2]["start"]["line"].GetInt());
  EXPECT_STREQ("-", d["notes"][2]["file"].GetString());
  EXPECT_FALSE(d["notes"][2].HasMember("source"));
  // A column's literal must name its table too.
  const auto c = ok(edit(script, {}, request("s", R"({"op": "rename_column", "table": "customer", "column": "id", "new_name": "k"})")));
  EXPECT_EQ(std::vector<std::string>{"name-in-string"}, note_kinds(c));
}

TEST(Schema_edit, self_check_refuses_a_lost_table) {
  // `b` is taken earlier in the script by a table that is dropped: the
  // renamed CREATE TABLE is replaced, then dropped, and the ALTER after
  // it misses its table.
  const std::string script =
      "CREATE TABLE a (x INT);\n"
      "CREATE TABLE b (y INT);\n"
      "DROP TABLE b;\n"
      "ALTER TABLE a ADD COLUMN z INT;\n";
  const auto e = failed(edit(script, {}, request("", rename_table("a", "b"))));
  EXPECT_EQ("self-check", e.kind);
  EXPECT_EQ(0u, e.edit.value());
}

TEST(Schema_edit, notes_of_the_input_are_kept) {
  // A statement the input cannot read stays unread: no refusal.
  const std::string script = "CREATE TABLE a (x INT);\nALTER TABLE missing ADD y INT;\nCREATE TABLE (;\n";
  const auto r = ok(edit(script, {}, request("", rename_table("a", "b"))));
  EXPECT_EQ(2u, r.outline.notes.size());
}

TEST(Schema_edit, error_json) {
  EXPECT_EQ(R"({"error":{"edit":2,"kind":"name-taken","message":"m"}})",
            edit_error_json({2, "name-taken", "m"}));
  EXPECT_EQ(R"({"error":{"edit":null,"kind":"bad-request","message":"m"}})",
            edit_error_json({std::nullopt, "bad-request", "m"}));
}

}  // namespace mdbls
