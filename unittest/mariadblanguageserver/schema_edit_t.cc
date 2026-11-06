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

// Each migration statement's tag: "action object name [(table)] [-> new]".
using Tags = std::vector<std::string>;
Tags tags(const Edit_result &r) {
  Tags out;
  for (const auto &m : r.migration) {
    std::string t = std::string(to_string(m.action)) + " " + to_string(m.object) + " " + m.name;
    if (!m.table.empty()) t += " (" + m.table + ")";
    if (!m.new_name.empty()) t += " -> " + m.new_name;
    out.push_back(t);
  }
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
  // An op this binary does not know: perhaps a newer one does.
  EXPECT_EQ("unsupported", kind(request("s", R"({"op": "rename"})")));
  EXPECT_NE(std::string::npos,
            std::get<Edit_error>(parse_edit_request(request("s", R"({"op": "add_widget"})"))).message.find("newer"));
  EXPECT_EQ("bad-request", kind(request("s", R"({"op": "rename_table", "table": "t"})")));
  EXPECT_EQ("bad-request", kind(request("s", R"({"op": "rename_table", "table": "t", "new_name": ""})")));
  EXPECT_EQ("bad-request", kind(request("s", rename_table("t", std::string(65, 'x')))));
  EXPECT_EQ("bad-request", kind(request("s", rename_table("t", "x "))));
  EXPECT_EQ("bad-request",
            kind(request("s", R"({"op": "rename_routine", "routine": "r", "kind": "x", "new_name": "q"})")));
  EXPECT_EQ("ok", kind(request("", rename_table("t", std::string(64, 'x')))));
  EXPECT_EQ("ok", kind(request("s", R"({"op": "drop_table", "table": "t"})")));
  // The ops of PR 2 on: unknown members, missing or empty structs.
  EXPECT_EQ("bad-request", kind(request("s", R"j({"op": "drop_table", "table": "t", "cascade": true})j")));
  EXPECT_EQ("bad-request", kind(request("s", R"j({"op": "add_table", "table": {"name": "t", "columns": []}})j")));
  EXPECT_EQ("bad-request", kind(request("s", R"j({"op": "add_table", "table": {"name": "t"}})j")));
  EXPECT_EQ("bad-request",
            kind(request("s", R"j({"op": "add_column", "table": "t", "column": {"name": "c", "type": "INT", "nullabel": false}})j")));
  EXPECT_EQ("bad-request",
            kind(request("s", R"j({"op": "add_column", "table": "t", "column": {"name": "c", "type": "INT"}, "after": "a", "first": true})j")));
  EXPECT_EQ("bad-request", kind(request("s", R"j({"op": "add_column", "table": "t", "column": {"name": "c"}})j")));
  EXPECT_EQ("bad-request",
            kind(request("s", R"j({"op": "add_table", "table": {"name": "t", "columns": [{"name": "a", "type": "INT"}], "indexes": [{"kind": "key", "columns": ["a"]}]}})j")));
  EXPECT_EQ("bad-request",
            kind(request("s", R"j({"op": "add_table", "table": {"name": "t", "columns": [{"name": "a", "type": "INT"}], "foreign_keys": [{"columns": ["a"], "references": {"table": "u", "columns": ["x", "y"]}}]}})j")));
  EXPECT_EQ("bad-request", kind(request("s", R"j({"op": "add_view", "view": {"name": "v", "definition": " "}})j")));
  EXPECT_EQ("bad-request",
            kind(request("s", R"j({"op": "add_routine", "routine": {"name": "f", "kind": "function", "body": "RETURN 1"}})j")));
  EXPECT_EQ("bad-request", kind(request("s", R"j({"op": "set_table_options", "table": "t", "options": {}})j")));
  EXPECT_EQ("bad-request", kind(request("s", R"j({"op": "set_table_options", "table": "t", "options": {"engine": 1}})j")));
  // Outline objects come back with their positions, which are ignored.
  EXPECT_EQ("ok",
            kind(request("s", R"j({"op": "add_column", "table": "t", "column": {"name": "c", "type": "INT", "range": {}, "selection_range": {}}})j")));
}

TEST(Schema_edit, supported_ops) {
  EXPECT_EQ(
      "rename_table rename_column rename_view rename_routine add_table drop_table add_column modify_column "
      "drop_column add_index drop_index add_foreign_key drop_foreign_key set_table_options add_view drop_view "
      "add_routine drop_routine set_primary_key add_trigger drop_trigger rename_trigger add_event drop_event rename_event",
      supported_edit_ops());
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
      "DROP PROCEDURE IF EXISTS r;\n"
      "CREATE PROCEDURE r() SELECT r();\n"
      "DROP FUNCTION IF EXISTS r;\n"
      "CREATE FUNCTION r() RETURNS INT RETURN 1;\n"
      "CALL r();\n"
      "SELECT r();\n";
  EXPECT_EQ("bad-request",
            failed(edit(script, {}, request("", R"({"op": "rename_routine", "routine": "r", "new_name": "x"})"))).kind);
  // A procedure and a function of one name: each renamed on its own.
  EXPECT_EQ("DROP PROCEDURE IF EXISTS r;\n"
            "CREATE PROCEDURE r() SELECT f();\n"
            "DROP FUNCTION IF EXISTS f;\n"
            "CREATE FUNCTION f() RETURNS INT RETURN 1;\n"
            "CALL r();\n"
            "SELECT f();\n",
            text(ok(edit(script, {}, request("", R"({"op": "rename_routine", "routine": "r", "kind": "function", "new_name": "f"})"))), ""));
  EXPECT_EQ("DROP PROCEDURE IF EXISTS p;\n"
            "CREATE PROCEDURE p() SELECT r();\n"
            "DROP FUNCTION IF EXISTS r;\n"
            "CREATE FUNCTION r() RETURNS INT RETURN 1;\n"
            "CALL p();\n"
            "SELECT r();\n",
            text(ok(edit(script, {}, request("", R"({"op": "rename_routine", "routine": "r", "kind": "procedure", "new_name": "p"})"))), ""));
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

// ---------------------------------------------------------------------------
// Tables, columns, options, views and routines (PR 2)

namespace mdbls {
namespace {

// A Workbench-style script: banners, two blank lines between tables.
const std::string k_shop =
    "CREATE DATABASE shop;\n"
    "USE shop;\n"
    "\n"
    "-- -----------------------------------------------------\n"
    "-- Table `shop`.`customer`\n"
    "-- -----------------------------------------------------\n"
    "CREATE TABLE `customer` (\n"
    "  `id` INT NOT NULL AUTO_INCREMENT,\n"
    "  `name` VARCHAR(45) NOT NULL,\n"
    "  PRIMARY KEY (`id`)\n"
    ") ENGINE=InnoDB;\n"
    "\n"
    "\n"
    "-- -----------------------------------------------------\n"
    "-- Table `shop`.`orders`\n"
    "-- -----------------------------------------------------\n"
    "CREATE TABLE `orders` (\n"
    "  `id` INT NOT NULL,\n"
    "  `customer_id` INT NOT NULL,\n"
    "  `note` TEXT,\n"
    "  PRIMARY KEY (`id`),\n"
    "  INDEX `ix_note` (`note`(10), `customer_id`),\n"
    "  CONSTRAINT `fk_orders_customer` FOREIGN KEY (`customer_id`) REFERENCES `customer` (`id`)\n"
    ") ENGINE=InnoDB COMMENT='the orders';\n"
    "\n"
    "CREATE VIEW `v_orders` AS SELECT o.id, o.note FROM orders o;\n"
    "\n"
    "DELIMITER $$\n"
    "CREATE PROCEDURE `p1`()\n"
    "BEGIN\n"
    "  SELECT note FROM orders;\n"
    "END$$\n"
    "DELIMITER ;\n";

// `text` with `from` replaced by `to` (once; it must be there).
std::string shop_with_in(std::string text, const std::string &from, const std::string &to) {
  const size_t at = text.find(from);
  EXPECT_NE(std::string::npos, at) << from;
  if (at != std::string::npos) text.replace(at, from.size(), to);
  return text;
}

// k_shop with `from` replaced by `to`.
std::string shop_with(const std::string &from, const std::string &to) { return shop_with_in(k_shop, from, to); }

}  // namespace
}  // namespace mdbls

namespace mdbls {

TEST(Schema_edit, add_table_after_the_last_table) {
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "add_table", "table": {"name": "product",
      "columns": [{"name": "id", "type": "UUID", "nullable": false}, {"name": "price", "type": "DECIMAL(10,2)"}],
      "indexes": [{"kind": "primary", "columns": ["id"]}]}})j")));
  // As far from the table before as the tables before are from each
  // other; no banner of its own.
  EXPECT_EQ(shop_with(") ENGINE=InnoDB COMMENT='the orders';\n",
                      ") ENGINE=InnoDB COMMENT='the orders';\n"
                      "\n\n"
                      "CREATE TABLE `product` (\n"
                      "  `id` UUID NOT NULL,\n"
                      "  `price` DECIMAL(10,2),\n"
                      "  PRIMARY KEY (`id`)\n"
                      ");\n"),
            text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"CREATE TABLE `product` (\n  `id` UUID NOT NULL,\n  `price` DECIMAL(10,2),\n  PRIMARY KEY (`id`)\n)"},
            migration(r));
  const Table *t = table(r, "shop", "product");
  ASSERT_NE(nullptr, t);
  EXPECT_EQ(2u, t->columns.size());
  EXPECT_FALSE(t->columns[0].nullable);
}

TEST(Schema_edit, add_table_with_foreign_keys) {
  // An n:m table: both keys go into the CREATE TABLE.
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "add_table", "table": {"name": "customer_has_orders",
      "columns": [{"name": "customer_id", "type": "INT", "nullable": false}, {"name": "orders_id", "type": "INT", "nullable": false}],
      "indexes": [{"kind": "primary", "columns": ["customer_id", "orders_id"]}],
      "foreign_keys": [
        {"name": "fk_c", "columns": ["customer_id"], "references": {"table": "customer", "columns": ["id"]}},
        {"name": "fk_o", "columns": ["orders_id"], "references": {"table": "orders", "columns": ["id"]}, "on_delete": "cascade"}]}})j")));
  const Table *t = table(r, "shop", "customer_has_orders");
  ASSERT_NE(nullptr, t);
  ASSERT_EQ(2u, t->foreign_keys.size());
  EXPECT_EQ("orders", t->foreign_keys[1].ref_table);
  EXPECT_EQ("CASCADE", t->foreign_keys[1].on_delete);
  EXPECT_NE(std::string::npos, text(r, "").find("CONSTRAINT `fk_o` FOREIGN KEY (`orders_id`) REFERENCES `orders` (`id`) ON DELETE CASCADE"));
}

TEST(Schema_edit, add_table_errors) {
  auto kind = [](const std::string &table) {
    const auto r = edit(k_shop, {}, request("shop", R"j({"op": "add_table", "table": )j" + table + "}"));
    return std::holds_alternative<Edit_error>(r) ? std::get<Edit_error>(r).kind + ": " + std::get<Edit_error>(r).message : "ok";
  };
  EXPECT_EQ("name-taken: a table or view `Customer` exists", kind(R"j({"name": "Customer", "columns": [{"name": "a", "type": "INT"}]})j"));
  EXPECT_EQ("name-taken: a table or view `v_orders` exists", kind(R"j({"name": "v_orders", "columns": [{"name": "a", "type": "INT"}]})j"));
  // The viewer shows the message on the column's editor.
  EXPECT_EQ("bad-request: column `price`: `VARCHR(45)` is not a MariaDB data type",
            kind(R"j({"name": "x", "columns": [{"name": "id", "type": "INT"}, {"name": "price", "type": "VARCHR(45)"}]})j"));
  EXPECT_EQ("bad-request", kind(R"j({"name": "x", "columns": [{"name": "d", "type": "INT", "default": "("}]})j").substr(0, 11));
  EXPECT_EQ("bad-request: table `x` has two columns `A`",
            kind(R"j({"name": "x", "columns": [{"name": "a", "type": "INT"}, {"name": "A", "type": "INT"}]})j"));
  EXPECT_EQ("unknown-object: there is no table `nope` to reference",
            kind(R"j({"name": "x", "columns": [{"name": "a", "type": "INT"}], "foreign_keys": [{"columns": ["a"], "references": {"table": "nope", "columns": ["id"]}}]})j"));
  EXPECT_EQ("unknown-object: table `customer` has no column `nope` to reference",
            kind(R"j({"name": "x", "columns": [{"name": "a", "type": "INT"}], "foreign_keys": [{"columns": ["a"], "references": {"table": "customer", "columns": ["nope"]}}]})j"));
  EXPECT_EQ("name-taken: a foreign key `fk_orders_customer` exists on table `orders`",
            kind(R"j({"name": "x", "columns": [{"name": "a", "type": "INT"}], "foreign_keys": [{"name": "fk_orders_customer", "columns": ["a"], "references": {"table": "customer", "columns": ["id"]}}]})j"));
}

TEST(Schema_edit, add_table_in_the_style_of_the_script) {
  // Lower-case keywords (the formatter writes types as keywords), names
  // unquoted, CRLF; no blank line before, as the table before has none.
  const std::string script = "create table a (\r\n  id int\r\n);\r\n";
  const auto r = ok(edit(script, {}, request("", R"j({"op": "add_table", "table": {"name": "b",
      "columns": [{"name": "id", "type": "INT", "nullable": false}, {"name": "order", "type": "INT"}]}})j")));
  EXPECT_EQ("create table a (\r\n  id int\r\n);\r\ncreate table b (\r\n  id int not null,\r\n  `order` int\r\n);\r\n",
            text(r, ""));
}

TEST(Schema_edit, add_table_to_an_empty_script) {
  const auto r = ok(edit("", {}, request("", R"j({"op": "add_table", "table": {"name": "t", "columns": [{"name": "a", "type": "INT"}]}})j")));
  EXPECT_EQ("CREATE TABLE `t` (\n  `a` INT\n);\n", text(r, ""));
  const auto r2 = ok(edit("SET NAMES utf8mb4;", {}, request("", R"j({"op": "add_table", "table": {"name": "t", "columns": [{"name": "a", "type": "INT"}]}})j")));
  EXPECT_EQ("SET NAMES utf8mb4;\n\nCREATE TABLE `t` (\n  `a` INT\n);\n", text(r2, ""));
}

TEST(Schema_edit, drop_table) {
  const std::string script =
      "CREATE TABLE a (id INT PRIMARY KEY);\n"
      "\n"
      "-- Table b\n"
      "DROP TABLE IF EXISTS b;\n"
      "CREATE TABLE b (id INT, a_id INT, FOREIGN KEY (a_id) REFERENCES a (id));\n"
      "CREATE INDEX ix_b ON b (id);\n"
      "ALTER TABLE b ADD COLUMN c INT;\n"
      "CREATE TRIGGER tb BEFORE INSERT ON b FOR EACH ROW SET NEW.c = 1;\n"
      "\n"
      "CREATE VIEW v AS SELECT id FROM b;\n"
      "INSERT INTO b VALUES (1, 1, 1);\n";
  const auto r = ok(edit(script, {}, request("", R"j({"op": "drop_table", "table": "b"})j")));
  EXPECT_EQ(
      "CREATE TABLE a (id INT PRIMARY KEY);\n"
      "\n"
      "CREATE VIEW v AS SELECT id FROM b;\n"
      "INSERT INTO b VALUES (1, 1, 1);\n",
      text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"DROP TABLE `b`"}, migration(r));
  EXPECT_EQ((std::vector<std::string>{"still-referenced", "still-referenced"}), note_kinds(r));
  EXPECT_EQ(nullptr, table(r, "", "b"));
  // A table another one references stays.
  const auto e = failed(edit(script, {}, request("", R"j({"op": "drop_table", "table": "a"})j")));
  EXPECT_EQ("referenced", e.kind);
  // Unless an earlier edit of the request dropped that one.
  const auto r2 = ok(edit(script, {}, request("", R"j({"op": "drop_table", "table": "b"}, {"op": "drop_table", "table": "a"})j")));
  EXPECT_EQ("CREATE VIEW v AS SELECT id FROM b;\nINSERT INTO b VALUES (1, 1, 1);\n", text(r2, ""));
}

TEST(Schema_edit, add_column) {
  // After the last column, before the keys.
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "add_column", "table": "orders", "column": {"name": "total", "type": "DECIMAL(10,2)", "nullable": false}})j")));
  EXPECT_EQ(shop_with("  `note` TEXT,\n", "  `note` TEXT,\n  `total` DECIMAL(10,2) NOT NULL,\n"), text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"ALTER TABLE `orders` ADD COLUMN `total` DECIMAL(10,2) NOT NULL"}, migration(r));
  // After a column, first.
  const auto r2 = ok(edit(k_shop, {}, request("shop", R"j({"op": "add_column", "table": "orders", "column": {"name": "x", "type": "INT"}, "after": "id"},
      {"op": "add_column", "table": "orders", "column": {"name": "y", "type": "INT"}, "first": true})j")));
  EXPECT_EQ(shop_with("  `id` INT NOT NULL,\n  `customer_id`", "  `y` INT,\n  `id` INT NOT NULL,\n  `x` INT,\n  `customer_id`"), text(r2, ""));
  EXPECT_EQ((std::vector<std::string>{"ALTER TABLE `orders` ADD COLUMN `x` INT AFTER `id`", "ALTER TABLE `orders` ADD COLUMN `y` INT FIRST"}),
            migration(r2));
  // A table on one line, the last element a column.
  const auto r3 = ok(edit("CREATE TABLE t (a INT, b INT);\n", {}, request("", R"j({"op": "add_column", "table": "t", "column": {"name": "c", "type": "INT"}},
      {"op": "add_column", "table": "t", "column": {"name": "d", "type": "INT"}, "after": "a"})j")));
  EXPECT_EQ("CREATE TABLE t (a INT, d INT, b INT, c INT);\n", text(r3, ""));
  // The last element followed by a comment, the parenthesis on its line.
  const auto r4 = ok(edit("CREATE TABLE t (\n  a INT -- the a\n);\nCREATE TABLE u (\n  a INT);\n", {},
                         request("", R"j({"op": "add_column", "table": "t", "column": {"name": "b", "type": "INT"}},
      {"op": "add_column", "table": "u", "column": {"name": "b", "type": "INT"}})j")));
  EXPECT_EQ("CREATE TABLE t (\n  a INT, -- the a\n  b INT\n);\nCREATE TABLE u (\n  a INT,\n  b INT);\n", text(r4, ""));
}

TEST(Schema_edit, add_column_after_one_an_alter_table_added) {
  const std::string script =
      "CREATE TABLE t (a INT, b INT);\n"
      "ALTER TABLE t ADD COLUMN c INT;\n"
      "SELECT 1;\n";
  // No `after`: after the table's last column, which the ALTER TABLE adds.
  const auto r = ok(edit(script, {}, request("", R"j({"op": "add_column", "table": "t", "column": {"name": "d", "type": "INT"}})j")));
  EXPECT_EQ("CREATE TABLE t (a INT, b INT);\nALTER TABLE t ADD COLUMN c INT;\nALTER TABLE t ADD COLUMN d INT AFTER c;\nSELECT 1;\n",
            text(r, ""));
  const Table *t = table(r, "", "t");
  ASSERT_NE(nullptr, t);
  EXPECT_EQ("d", t->columns.back().name);
}

TEST(Schema_edit, add_column_errors) {
  auto kind = [](const std::string &edit_json) {
    const auto r = edit(k_shop, {}, request("shop", edit_json));
    return std::holds_alternative<Edit_error>(r) ? std::get<Edit_error>(r).kind : "ok";
  };
  EXPECT_EQ("unknown-object", kind(R"j({"op": "add_column", "table": "nope", "column": {"name": "x", "type": "INT"}})j"));
  EXPECT_EQ("name-taken", kind(R"j({"op": "add_column", "table": "orders", "column": {"name": "NOTE", "type": "INT"}})j"));
  EXPECT_EQ("unknown-object", kind(R"j({"op": "add_column", "table": "orders", "column": {"name": "x", "type": "INT"}, "after": "nope"})j"));
  EXPECT_EQ("bad-request", kind(R"j({"op": "add_column", "table": "orders", "column": {"name": "x", "type": "INTEGRAL"}})j"));
}

TEST(Schema_edit, add_column_to_a_sliced_include) {
  // The slice's end moves past the longer text.
  const std::string tables = "-- tables\nCREATE TABLE t (\n  a INT\n);\n-- end\n";
  const std::string script = "SOURCE 'tables.sql'[ln2:ln4];\n";
  const auto r = ok(edit(script, {{"tables.sql", tables}},
                         request("", R"j({"op": "add_column", "table": "t", "column": {"name": "b", "type": "INT"}})j")));
  EXPECT_EQ("-- tables\nCREATE TABLE t (\n  a INT,\n  b INT\n);\n-- end\n", text(r, "tables.sql"));
  EXPECT_EQ("SOURCE 'tables.sql'[ln2:ln5];\n", text(r, ""));
}

TEST(Schema_edit, modify_column) {
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "modify_column", "table": "orders", "column": "note",
      "new_column": {"name": "note", "type": "VARCHAR(200)", "comment": "free text"}})j")));
  EXPECT_EQ(shop_with("  `note` TEXT,\n", "  `note` VARCHAR(200) COMMENT 'free text',\n"), text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"ALTER TABLE `orders` MODIFY COLUMN `note` VARCHAR(200) COMMENT 'free text'"}, migration(r));
  // A new name: also a rename, references included.
  const auto r2 = ok(edit(k_shop, {}, request("shop", R"j({"op": "modify_column", "table": "orders", "column": "note",
      "new_column": {"name": "remark", "type": "TEXT"}})j")));
  EXPECT_NE(std::string::npos, text(r2, "").find("  `remark` TEXT,\n"));
  EXPECT_NE(std::string::npos, text(r2, "").find("INDEX `ix_note` (`remark`(10), `customer_id`)"));
  EXPECT_NE(std::string::npos, text(r2, "").find("SELECT o.id, o.remark FROM orders o"));
  EXPECT_EQ(std::vector<std::string>{"ALTER TABLE `orders` CHANGE COLUMN `note` `remark` TEXT"}, migration(r2));
}

TEST(Schema_edit, modify_column_keeps_its_key_and_check) {
  const std::string script = "CREATE TABLE t (id INT PRIMARY KEY, j TEXT CHECK (JSON_VALID(j)));\n";
  const auto r = ok(edit(script, {}, request("", R"j({"op": "modify_column", "table": "t", "column": "id", "new_column": {"name": "id", "type": "BIGINT"}},
      {"op": "modify_column", "table": "t", "column": "j", "new_column": {"name": "j", "type": "LONGTEXT"}})j")));
  EXPECT_EQ("CREATE TABLE t (id BIGINT PRIMARY KEY, j LONGTEXT CHECK (JSON_VALID(j)));\n", text(r, ""));
}

TEST(Schema_edit, modify_column_used_by_foreign_keys) {
  // MariaDB refuses to change a column a foreign key uses (1833): the keys
  // go before and come back after.
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "modify_column", "table": "customer", "column": "id",
      "new_column": {"name": "id", "type": "BIGINT", "nullable": false, "auto_increment": true}})j")));
  EXPECT_EQ((std::vector<std::string>{
                "ALTER TABLE `orders` DROP FOREIGN KEY `fk_orders_customer`",
                "ALTER TABLE `customer` MODIFY COLUMN `id` BIGINT NOT NULL AUTO_INCREMENT",
                "ALTER TABLE `orders` ADD CONSTRAINT `fk_orders_customer` FOREIGN KEY (`customer_id`) REFERENCES `customer` (`id`)"}),
            migration(r));
}

TEST(Schema_edit, drop_column) {
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "drop_column", "table": "orders", "column": "note"})j")));
  // Its key part goes; the index stays with the other one.
  EXPECT_EQ(shop_with("  `note` TEXT,\n  PRIMARY KEY (`id`),\n  INDEX `ix_note` (`note`(10), `customer_id`),\n",
                      "  PRIMARY KEY (`id`),\n  INDEX `ix_note` (`customer_id`),\n"),
            text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"ALTER TABLE `orders` DROP COLUMN `note`"}, migration(r));
  // The view and the procedure still select it.
  EXPECT_EQ((std::vector<std::string>{"still-referenced", "still-referenced"}), note_kinds(r));
  EXPECT_EQ("referenced", failed(edit(k_shop, {}, request("shop", R"j({"op": "drop_column", "table": "orders", "column": "customer_id"})j"))).kind);
  EXPECT_EQ("referenced", failed(edit(k_shop, {}, request("shop", R"j({"op": "drop_column", "table": "customer", "column": "id"})j"))).kind);
  EXPECT_EQ("bad-request", failed(edit("CREATE TABLE t (a INT);", {}, request("", R"j({"op": "drop_column", "table": "t", "column": "a"})j"))).kind);
}

TEST(Schema_edit, drop_column_from_every_statement) {
  const std::string script =
      "CREATE TABLE t (a INT, b INT, KEY (b), KEY ab (a, b));\n"
      "ALTER TABLE t ADD COLUMN c INT, ADD INDEX ix_c (c);\n"
      "ALTER TABLE t MODIFY b BIGINT;\n"
      "CREATE INDEX ix_bc ON t (b, c);\n"
      "CREATE INDEX ix_b ON t (b);\n";
  const auto r = ok(edit(script, {}, request("", R"j({"op": "drop_column", "table": "t", "column": "b"})j")));
  EXPECT_EQ(
      "CREATE TABLE t (a INT, KEY ab (a));\n"
      "ALTER TABLE t ADD COLUMN c INT, ADD INDEX ix_c (c);\n"
      "CREATE INDEX ix_bc ON t (c);\n",
      text(r, ""));
  const Table *t = table(r, "", "t");
  ASSERT_NE(nullptr, t);
  EXPECT_EQ(2u, t->columns.size());
}

TEST(Schema_edit, set_table_options) {
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "set_table_options", "table": "orders",
      "options": {"comment": "all the orders", "engine": null, "row_format": "DYNAMIC"}})j")));
  EXPECT_EQ(shop_with(") ENGINE=InnoDB COMMENT='the orders';", ") COMMENT='all the orders' ROW_FORMAT=DYNAMIC;"), text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"ALTER TABLE `orders` COMMENT='all the orders' ROW_FORMAT=DYNAMIC"}, migration(r));
  EXPECT_EQ(std::vector<std::string>{"no-migration"}, note_kinds(r));
  // A table without options gets them after its column list.
  const auto r2 = ok(edit("create table t (a int);\n", {}, request("", R"j({"op": "set_table_options", "table": "t", "options": {"engine": "Aria", "charset": "utf8mb4"}})j")));
  EXPECT_EQ("create table t (a int) engine=Aria default charset=utf8mb4;\n", text(r2, ""));
}

TEST(Schema_edit, add_and_drop_view) {
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "add_view", "view": {"name": "view1", "definition": "SELECT 1"}})j")));
  EXPECT_EQ(shop_with("FROM orders o;\n", "FROM orders o;\n\nCREATE VIEW `view1` AS SELECT 1;\n"), text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"CREATE VIEW `view1` AS SELECT 1"}, migration(r));
  EXPECT_EQ("bad-request", failed(edit(k_shop, {}, request("shop", R"j({"op": "add_view", "view": {"name": "view1", "definition": "SELECT FROM"}})j"))).kind);
  EXPECT_EQ("name-taken", failed(edit(k_shop, {}, request("shop", R"j({"op": "add_view", "view": {"name": "orders", "definition": "SELECT 1"}})j"))).kind);

  const auto r2 = ok(edit(k_shop, {}, request("shop", R"j({"op": "drop_view", "view": "v_orders"})j")));
  EXPECT_EQ(shop_with("CREATE VIEW `v_orders` AS SELECT o.id, o.note FROM orders o;\n\n", ""), text(r2, ""));
  EXPECT_EQ(std::vector<std::string>{"DROP VIEW `v_orders`"}, migration(r2));
}

TEST(Schema_edit, add_routine_in_the_delimiter_block) {
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "add_routine", "routine": {"name": "routine1", "kind": "procedure", "body": "BEGIN\nEND"}},
      {"op": "add_routine", "routine": {"name": "f1", "kind": "function", "parameters": [{"name": "a", "type": "INT"}], "returns": "INT", "deterministic": true, "body": "RETURN a + 1"}})j")));
  EXPECT_EQ(shop_with("END$$\n", "END$$\nCREATE PROCEDURE `routine1`()\nBEGIN\nEND$$\n"
                                 "CREATE FUNCTION `f1`(`a` INT) RETURNS INT DETERMINISTIC RETURN a + 1$$\n"),
            text(r, ""));
  EXPECT_EQ((std::vector<std::string>{"CREATE PROCEDURE `routine1`()\nBEGIN\nEND",
                                      "CREATE FUNCTION `f1`(`a` INT) RETURNS INT DETERMINISTIC RETURN a + 1"}),
            migration(r));
  EXPECT_EQ((std::vector<std::string>{"routine-created", "routine-created"}), note_kinds(r));
  // Only a routine of its own kind takes the name.
  EXPECT_EQ("name-taken",
            failed(edit(k_shop, {}, request("shop", R"j({"op": "add_routine", "routine": {"name": "P1", "kind": "procedure", "body": "SELECT 1"}})j"))).kind);
  ok(edit(k_shop, {}, request("shop", R"j({"op": "add_routine", "routine": {"name": "P1", "kind": "function", "returns": "INT", "body": "RETURN 1"}})j")));
  EXPECT_EQ("bad-request",
            failed(edit(k_shop, {}, request("shop", R"j({"op": "add_routine", "routine": {"name": "r", "kind": "procedure", "body": "BEGIN SELECT FROM; END"}})j"))).kind);
}

TEST(Schema_edit, add_routine_in_a_new_delimiter_block) {
  const std::string script = "CREATE TABLE t (a INT);\n";
  const auto r = ok(edit(script, {}, request("", R"j({"op": "add_routine", "routine": {"name": "p", "kind": "procedure", "body": "BEGIN\n  SELECT a FROM t;\nEND"}})j")));
  // Names unquoted, as the script writes them.
  EXPECT_EQ("CREATE TABLE t (a INT);\nDELIMITER $$\nCREATE PROCEDURE p()\nBEGIN\n  SELECT a FROM t;\nEND$$\nDELIMITER ;\n", text(r, ""));
  // A body without a semicolon needs no block.
  const auto r2 = ok(edit(script, {}, request("", R"j({"op": "add_routine", "routine": {"name": "p", "kind": "procedure", "body": "SELECT 1"}})j")));
  EXPECT_EQ("CREATE TABLE t (a INT);\nCREATE PROCEDURE p() SELECT 1;\n", text(r2, ""));
}

TEST(Schema_edit, drop_routine) {
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "drop_routine", "routine": "p1"})j")));
  EXPECT_EQ(shop_with("CREATE PROCEDURE `p1`()\nBEGIN\n  SELECT note FROM orders;\nEND$$\n", ""), text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"DROP PROCEDURE `p1`"}, migration(r));
  EXPECT_EQ("unknown-object", failed(edit(k_shop, {}, request("shop", R"j({"op": "drop_routine", "routine": "p1", "kind": "function"})j"))).kind);
}

TEST(Schema_edit, rename_column_in_a_key_part_with_a_prefix_length) {
  const auto r = ok(edit("CREATE TABLE t (a TEXT, b INT, INDEX ix (a(10), b));\n", {},
                         request("", R"j({"op": "rename_column", "table": "t", "column": "a", "new_name": "z"})j")));
  EXPECT_EQ("CREATE TABLE t (z TEXT, b INT, INDEX ix (z(10), b));\n", text(r, ""));
}

TEST(Schema_edit, renames_reach_the_drop_before_the_create) {
  // MSM writes Section 150 to run on every deployment: drop, then create.
  // A DROP left under the old name breaks the second deployment.
  const std::string script =
      "USE shop;\n"
      "DELIMITER %%\n"
      "\n"
      "DROP PROCEDURE IF EXISTS `count_customers`%%\n"
      "DROP PROCEDURE IF EXISTS shop.count_customers%%\n"
      "DROP FUNCTION IF EXISTS `count_customers`%%\n"
      "CREATE PROCEDURE `count_customers`()\n"
      "BEGIN\n"
      "    SELECT COUNT(*) AS `customers` FROM `customer`;\n"
      "END%%\n"
      "\n"
      "DELIMITER ;\n";
  const auto r = ok(edit(script, {}, request("shop", R"j({"op": "rename_routine", "routine": "count_customers",
      "new_name": "count_clients", "kind": "procedure"})j")));
  // A DROP FUNCTION is not a reference of the procedure.
  EXPECT_EQ(
      "USE shop;\n"
      "DELIMITER %%\n"
      "\n"
      "DROP PROCEDURE IF EXISTS `count_clients`%%\n"
      "DROP PROCEDURE IF EXISTS shop.count_clients%%\n"
      "DROP FUNCTION IF EXISTS `count_customers`%%\n"
      "CREATE PROCEDURE `count_clients`()\n"
      "BEGIN\n"
      "    SELECT COUNT(*) AS `customers` FROM `customer`;\n"
      "END%%\n"
      "\n"
      "DELIMITER ;\n",
      text(r, ""));
  // msm takes the DROP of the old name from the migration.
  ASSERT_FALSE(migration(r).empty());
  EXPECT_EQ("DROP PROCEDURE `count_customers`", migration(r)[0]);

  const auto f = ok(edit("DROP FUNCTION IF EXISTS f;\nCREATE FUNCTION f() RETURNS INT RETURN 1;\nDROP PROCEDURE IF EXISTS f;\n", {},
                         request("", R"j({"op": "rename_routine", "routine": "f", "new_name": "g"})j")));
  EXPECT_EQ("DROP FUNCTION IF EXISTS g;\nCREATE FUNCTION g() RETURNS INT RETURN 1;\nDROP PROCEDURE IF EXISTS f;\n", text(f, ""));

  const auto v = ok(edit("DROP VIEW IF EXISTS `v`;\nCREATE VIEW `v` AS SELECT 1;\n", {},
                         request("", R"j({"op": "rename_view", "view": "v", "new_name": "w"})j")));
  EXPECT_EQ("DROP VIEW IF EXISTS `w`;\nCREATE VIEW `w` AS SELECT 1;\n", text(v, ""));

  // A Workbench export.
  const auto t = ok(edit("DROP TABLE IF EXISTS `mydb`.`t` ;\nCREATE TABLE IF NOT EXISTS `mydb`.`t` (`a` INT);\n", {},
                         request("mydb", R"j({"op": "rename_table", "table": "t", "new_name": "u"})j")));
  EXPECT_EQ("DROP TABLE IF EXISTS `mydb`.`u` ;\nCREATE TABLE IF NOT EXISTS `mydb`.`u` (`a` INT);\n", text(t, ""));
}

TEST(Schema_edit, a_routine_rename_leaves_triggers_and_events_of_its_name) {
  const std::string script =
      "CREATE TABLE t (a INT);\n"
      "CREATE PROCEDURE x() SELECT 1;\n"
      "DROP TRIGGER IF EXISTS x;\n"
      "CREATE TRIGGER x BEFORE INSERT ON t FOR EACH ROW SET NEW.a = 1;\n"
      "DROP EVENT IF EXISTS x;\n"
      "ALTER EVENT x DISABLE;\n";
  const auto r = ok(edit(script, {}, request("", R"j({"op": "rename_routine", "routine": "x", "new_name": "y"})j")));
  EXPECT_EQ(1u, edit_count(r, ""));
  EXPECT_NE(std::string::npos, text(r, "").find("CREATE PROCEDURE y()"));
}

TEST(Schema_edit, string_literals_follow_the_sql_mode) {
  const std::string request_json =
      R"j({"op": "add_column", "table": "t", "column": {"name": "b", "type": "INT", "comment": "C:\\dir 'x'"}})j";
  const auto r = ok(edit("CREATE TABLE t (\n  a INT\n);\n", {}, request("", request_json)));
  EXPECT_NE(std::string::npos, text(r, "").find(R"(b INT COMMENT 'C:\\dir ''x'')")) << text(r, "");
  const auto n = ok(edit("SET sql_mode = 'NO_BACKSLASH_ESCAPES';\nCREATE TABLE t (\n  a INT\n);\n", {},
                         request("", request_json)));
  EXPECT_NE(std::string::npos, text(n, "").find(R"(b INT COMMENT 'C:\dir ''x'')")) << text(n, "");
}

TEST(Schema_edit, rename_routine_to_a_name_of_the_other_kind) {
  // MariaDB keeps procedures and functions apart: the name is free.
  const std::string script = "CREATE PROCEDURE p() SELECT 1;\nCREATE FUNCTION f() RETURNS INT RETURN 1;\nCALL p();\n";
  EXPECT_EQ("CREATE PROCEDURE f() SELECT 1;\nCREATE FUNCTION f() RETURNS INT RETURN 1;\nCALL f();\n",
            text(ok(edit(script, {}, request("", R"j({"op": "rename_routine", "routine": "p", "new_name": "f"})j"))), ""));
  EXPECT_EQ("name-taken",
            failed(edit(script + "CREATE PROCEDURE q() SELECT 1;\n", {},
                        request("", R"j({"op": "rename_routine", "routine": "p", "new_name": "q"})j"))).kind);
}

TEST(Schema_edit, an_undo_request) {
  // The viewer's undo of "add a column, a foreign key on it": in reverse.
  const auto added = ok(edit(k_shop, {}, request("shop", R"j({"op": "add_column", "table": "orders", "column": {"name": "customer_id2", "type": "INT", "nullable": false}})j")));
  const std::string script = text(added, "");
  const auto r = ok(edit(script, {}, request("shop", R"j({"op": "drop_column", "table": "orders", "column": "customer_id2"})j")));
  EXPECT_EQ(k_shop, text(r, ""));
}

}  // namespace mdbls

// ---------------------------------------------------------------------------
// Indexes and foreign keys (PR 3)

namespace mdbls {

TEST(Schema_edit, add_index) {
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "add_index", "table": "orders",
      "index": {"name": "fk_x_UNIQUE", "kind": "unique", "columns": ["customer_id"]}})j")));
  // After the last index.
  EXPECT_EQ(shop_with("  INDEX `ix_note` (`note`(10), `customer_id`),\n",
                      "  INDEX `ix_note` (`note`(10), `customer_id`),\n  UNIQUE INDEX `fk_x_UNIQUE` (`customer_id`),\n"),
            text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"ALTER TABLE `orders` ADD UNIQUE INDEX `fk_x_UNIQUE` (`customer_id`)"}, migration(r));
  // A primary key after the columns; key parts with lengths and order.
  const auto r2 = ok(edit("CREATE TABLE t (a TEXT, b INT);\n", {}, request("", R"j({"op": "add_index", "table": "t",
      "index": {"kind": "primary", "columns": ["b"]}}, {"op": "add_index", "table": "t",
      "index": {"name": "ix_a", "kind": "index", "key_parts": [{"column": "a", "length": 10, "order": "desc"}]}})j")));
  EXPECT_EQ("CREATE TABLE t (a TEXT, b INT, PRIMARY KEY (b), INDEX ix_a (a(10) DESC));\n", text(r2, ""));

  auto kind = [](const std::string &edit_json) {
    const auto x = edit(k_shop, {}, request("shop", edit_json));
    return std::holds_alternative<Edit_error>(x) ? std::get<Edit_error>(x).kind : "ok";
  };
  EXPECT_EQ("name-taken", kind(R"j({"op": "add_index", "table": "orders", "index": {"name": "IX_NOTE", "kind": "index", "columns": ["id"]}})j"));
  EXPECT_EQ("name-taken", kind(R"j({"op": "add_index", "table": "orders", "index": {"kind": "primary", "columns": ["note"]}})j"));
  EXPECT_EQ("unknown-object", kind(R"j({"op": "add_index", "table": "orders", "index": {"kind": "index", "columns": ["nope"]}})j"));
  EXPECT_EQ("bad-request", kind(R"j({"op": "add_index", "table": "orders", "index": {"kind": "index"}})j"));
}

TEST(Schema_edit, drop_index_wherever_it_is_defined) {
  const std::string script =
      "CREATE TABLE t (a INT, b INT UNIQUE, c INT, KEY ix_a (a), KEY (c));\n"
      "ALTER TABLE t ADD INDEX ix_ab (a, b);\n"
      "ALTER TABLE t ADD COLUMN d INT, ADD INDEX ix_d (d);\n"
      "CREATE INDEX ix_c ON t (c);\n";
  const auto r = ok(edit(script, {}, request("", R"j({"op": "drop_index", "table": "t", "name": "ix_a"},
      {"op": "drop_index", "table": "t", "name": "ix_ab"}, {"op": "drop_index", "table": "t", "name": "ix_d"},
      {"op": "drop_index", "table": "t", "name": "ix_c"}, {"op": "drop_index", "table": "t", "columns": ["b"]},
      {"op": "drop_index", "table": "t", "columns": ["c"]})j")));
  EXPECT_EQ(
      "CREATE TABLE t (a INT, b INT, c INT);\n"
      "ALTER TABLE t ADD COLUMN d INT;\n",
      text(r, ""));
  EXPECT_EQ((std::vector<std::string>{"ALTER TABLE `t` DROP INDEX `ix_a`", "ALTER TABLE `t` DROP INDEX `ix_ab`",
                                      "ALTER TABLE `t` DROP INDEX `ix_d`", "ALTER TABLE `t` DROP INDEX `ix_c`"}),
            migration(r));
  // The unnamed ones have a name only the server knows.
  EXPECT_EQ((std::vector<std::string>{"no-migration", "no-migration"}), note_kinds(r));
  const Table *t = table(r, "", "t");
  ASSERT_NE(nullptr, t);
  EXPECT_TRUE(t->indexes.empty());
}

TEST(Schema_edit, drop_index_errors) {
  auto kind = [](const std::string &script, const std::string &edit_json) {
    const auto x = edit(script, {}, request("", edit_json));
    return std::holds_alternative<Edit_error>(x) ? std::get<Edit_error>(x).kind : "ok";
  };
  const std::string fk = "CREATE TABLE p (id INT PRIMARY KEY);\nCREATE TABLE c (pid INT, KEY ix (pid), FOREIGN KEY (pid) REFERENCES p (id));\n";
  // The foreign key needs the index, and the parent's key.
  EXPECT_EQ("referenced", kind(fk, R"j({"op": "drop_index", "table": "c", "name": "ix"})j"));
  EXPECT_EQ("referenced", kind(fk, R"j({"op": "drop_index", "table": "p", "kind": "primary"})j"));
  // Not when the key went first.
  EXPECT_EQ("ok", kind(fk, R"j({"op": "drop_foreign_key", "table": "c", "columns": ["pid"]}, {"op": "drop_index", "table": "c", "name": "ix"})j"));
  // An AUTO_INCREMENT column must lead an index.
  EXPECT_EQ("referenced", kind("CREATE TABLE t (id INT AUTO_INCREMENT PRIMARY KEY);\n",
                               R"j({"op": "drop_index", "table": "t", "kind": "primary"})j"));
  EXPECT_EQ("unknown-object", kind(fk, R"j({"op": "drop_index", "table": "c", "name": "nope"})j"));
  EXPECT_EQ("bad-request", kind(fk, R"j({"op": "drop_index", "table": "c", "name": "ix", "kind": "primary"})j"));
}

TEST(Schema_edit, set_primary_key) {
  // Written where the key is: its columns replaced.
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "set_primary_key", "table": "orders", "columns": ["id", "customer_id"]})j")));
  EXPECT_EQ(shop_with("  PRIMARY KEY (`id`),\n  INDEX", "  PRIMARY KEY (`id`, `customer_id`),\n  INDEX"), text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"ALTER TABLE `orders` DROP PRIMARY KEY, ADD PRIMARY KEY (`id`, `customer_id`)"},
            migration(r));
  // A column's PRIMARY KEY becomes an element; a table without one gets one.
  const auto r2 = ok(edit("CREATE TABLE t (a INT PRIMARY KEY, b INT);\nCREATE TABLE u (a INT, b INT);\n", {},
                          request("", R"j({"op": "set_primary_key", "table": "t", "columns": ["a", "b"]},
      {"op": "set_primary_key", "table": "u", "columns": ["b"]})j")));
  EXPECT_EQ("CREATE TABLE t (a INT, b INT, PRIMARY KEY (a, b));\nCREATE TABLE u (a INT, b INT, PRIMARY KEY (b));\n",
            text(r2, ""));
  EXPECT_EQ((std::vector<std::string>{"ALTER TABLE `t` DROP PRIMARY KEY, ADD PRIMARY KEY (`a`, `b`)",
                                      "ALTER TABLE `u` ADD PRIMARY KEY (`b`)"}),
            migration(r2));
  // A key an ALTER TABLE adds is changed there.
  const auto r3 = ok(edit("CREATE TABLE t (a INT, b INT);\nALTER TABLE t ADD PRIMARY KEY (a);\n", {},
                          request("", R"j({"op": "set_primary_key", "table": "t", "columns": ["b", "a"]})j")));
  EXPECT_EQ("CREATE TABLE t (a INT, b INT);\nALTER TABLE t ADD PRIMARY KEY (b, a);\n", text(r3, ""));
  // The same columns again change nothing.
  const auto r4 = ok(edit(k_shop, {}, request("shop", R"j({"op": "set_primary_key", "table": "orders", "columns": ["ID"]})j")));
  EXPECT_EQ(k_shop, text(r4, ""));
  EXPECT_TRUE(migration(r4).empty());
  EXPECT_EQ("unknown-object",
            failed(edit(k_shop, {}, request("shop", R"j({"op": "set_primary_key", "table": "orders", "columns": ["nope"]})j"))).kind);
  // customer.id is AUTO_INCREMENT: it must lead an index.
  EXPECT_EQ("referenced",
            failed(edit(k_shop, {}, request("shop", R"j({"op": "set_primary_key", "table": "customer", "columns": ["name"]})j"))).kind);
}

TEST(Schema_edit, add_foreign_key) {
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "add_foreign_key", "table": "orders",
      "foreign_key": {"name": "fk_2", "columns": ["id"], "references": {"table": "customer", "columns": ["id"]}, "on_delete": "NO ACTION"}})j")));
  EXPECT_EQ(shop_with("REFERENCES `customer` (`id`)\n)",
                      "REFERENCES `customer` (`id`),\n"
                      "  CONSTRAINT `fk_2` FOREIGN KEY (`id`) REFERENCES `customer` (`id`) ON DELETE NO ACTION\n)"),
            text(r, ""));
  EXPECT_EQ(std::vector<std::string>{"ALTER TABLE `orders` ADD CONSTRAINT `fk_2` FOREIGN KEY (`id`) REFERENCES `customer` (`id`) ON DELETE NO ACTION"},
            migration(r));
  // To a table the script creates later: an ALTER TABLE after both.
  const std::string script = "CREATE TABLE a (id INT PRIMARY KEY, b_id INT);\nCREATE TABLE b (id INT PRIMARY KEY);\nSELECT 1;\n";
  const auto r2 = ok(edit(script, {}, request("", R"j({"op": "add_foreign_key", "table": "a",
      "foreign_key": {"name": "fk_a_b", "columns": ["b_id"], "references": {"table": "b", "columns": ["id"]}}})j")));
  EXPECT_EQ("CREATE TABLE a (id INT PRIMARY KEY, b_id INT);\nCREATE TABLE b (id INT PRIMARY KEY);\n"
            "ALTER TABLE a ADD CONSTRAINT fk_a_b FOREIGN KEY (b_id) REFERENCES b (id);\nSELECT 1;\n",
            text(r2, ""));
  // After the ALTER TABLEs that add foreign keys already (a Workbench import).
  const auto r3 = ok(edit(script + "ALTER TABLE a ADD FOREIGN KEY (id) REFERENCES b (id);\nSELECT 2;\n", {},
                          request("", R"j({"op": "add_foreign_key", "table": "a",
      "foreign_key": {"name": "fk_a_b", "columns": ["b_id"], "references": {"table": "b", "columns": ["id"]}}})j")));
  EXPECT_NE(std::string::npos, text(r3, "").find("REFERENCES b (id);\nALTER TABLE a ADD CONSTRAINT fk_a_b"));
  EXPECT_EQ("unknown-object",
            failed(edit(k_shop, {}, request("shop", R"j({"op": "add_foreign_key", "table": "orders",
      "foreign_key": {"columns": ["id"], "references": {"table": "nope", "columns": ["id"]}}})j"))).kind);
  EXPECT_EQ("name-taken",
            failed(edit(k_shop, {}, request("shop", R"j({"op": "add_foreign_key", "table": "orders",
      "foreign_key": {"name": "fk_orders_customer", "columns": ["id"], "references": {"table": "customer", "columns": ["id"]}}})j"))).kind);
}

TEST(Schema_edit, add_foreign_key_after_its_schemas_alter_tables_only) {
  // The ALTER TABLE of another schema's foreign key is after its USE, where
  // an unqualified ALTER TABLE a would name that schema's `a`; a DROP
  // FOREIGN KEY adds none.
  const std::string script =
      "USE s;\n"
      "CREATE TABLE a (id INT PRIMARY KEY, b_id INT);\n"
      "CREATE TABLE b (id INT PRIMARY KEY, CONSTRAINT fk_0 FOREIGN KEY (id) REFERENCES a (id));\n"
      "ALTER TABLE b ADD CONSTRAINT fk_b FOREIGN KEY (id) REFERENCES a (id);\n"
      "ALTER TABLE b DROP FOREIGN KEY fk_0;\n"
      "USE o;\n"
      "CREATE TABLE c (id INT PRIMARY KEY);\n"
      "CREATE TABLE d (c_id INT);\n"
      "ALTER TABLE d ADD FOREIGN KEY (c_id) REFERENCES c (id);\n";
  const auto r = ok(edit(script, {}, request("s", R"j({"op": "add_foreign_key", "table": "a",
      "foreign_key": {"name": "fk_a_b", "columns": ["b_id"], "references": {"table": "b", "columns": ["id"]}}})j")));
  EXPECT_EQ(shop_with_in(script, "ALTER TABLE b DROP",
                         "ALTER TABLE a ADD CONSTRAINT fk_a_b FOREIGN KEY (b_id) REFERENCES b (id);\nALTER TABLE b DROP"),
            text(r, ""));
}

TEST(Schema_edit, drop_foreign_key_wherever_it_is_defined) {
  const std::string script =
      "CREATE TABLE p (id INT PRIMARY KEY);\n"
      "CREATE TABLE c (a INT REFERENCES p (id), b INT, c INT,\n"
      "  CONSTRAINT fk_b FOREIGN KEY (b) REFERENCES p (id));\n"
      "ALTER TABLE c ADD CONSTRAINT fk_c FOREIGN KEY (c) REFERENCES p (id);\n";
  const auto r = ok(edit(script, {}, request("", R"j({"op": "drop_foreign_key", "table": "c", "columns": ["a"]},
      {"op": "drop_foreign_key", "table": "c", "name": "fk_b"}, {"op": "drop_foreign_key", "table": "c", "name": "FK_C"})j")));
  EXPECT_EQ("CREATE TABLE p (id INT PRIMARY KEY);\nCREATE TABLE c (a INT, b INT, c INT);\n", text(r, ""));
  EXPECT_EQ((std::vector<std::string>{"ALTER TABLE `c` DROP FOREIGN KEY `fk_b`", "ALTER TABLE `c` DROP FOREIGN KEY `fk_c`"}),
            migration(r));
  EXPECT_EQ(std::vector<std::string>{"no-migration"}, note_kinds(r));
  EXPECT_EQ("unknown-object", failed(edit(script, {}, request("", R"j({"op": "drop_foreign_key", "table": "c", "name": "nope"})j"))).kind);
}

TEST(Schema_edit, a_relationship_and_its_undo) {
  // The viewer's identifying 1:n relationship, one request, and its undo:
  // the inverse ops in reverse order give the script back.
  const auto r = ok(edit(k_shop, {}, request("shop", R"j(
      {"op": "add_column", "table": "orders", "column": {"name": "customer_id1", "type": "INT", "nullable": false}},
      {"op": "set_primary_key", "table": "orders", "columns": ["id", "customer_id1"]},
      {"op": "add_foreign_key", "table": "orders", "foreign_key": {"name": "fk_orders_customer1",
       "columns": ["customer_id1"], "references": {"table": "customer", "columns": ["id"]}}})j")));
  const Table *t = table(r, "shop", "orders");
  ASSERT_NE(nullptr, t);
  EXPECT_EQ(2u, t->foreign_keys.size());
  const auto undo = ok(edit(text(r, ""), {}, request("shop", R"j(
      {"op": "drop_foreign_key", "table": "orders", "name": "fk_orders_customer1"},
      {"op": "set_primary_key", "table": "orders", "columns": ["id"]},
      {"op": "drop_column", "table": "orders", "column": "customer_id1"})j")));
  EXPECT_EQ(k_shop, text(undo, ""));
  // A 1:1 relationship: a new column, its unique index, the key; undone in
  // reverse (the key first: the index serves it).
  const auto one = ok(edit(k_shop, {}, request("shop", R"j(
      {"op": "add_column", "table": "orders", "column": {"name": "customer_id1", "type": "INT", "nullable": false}},
      {"op": "add_index", "table": "orders", "index": {"name": "fk_1_UNIQUE", "kind": "unique", "columns": ["customer_id1"]}},
      {"op": "add_foreign_key", "table": "orders", "foreign_key": {"name": "fk_1",
       "columns": ["customer_id1"], "references": {"table": "customer", "columns": ["id"]}}})j")));
  EXPECT_EQ("referenced", failed(edit(text(one, ""), {}, request("shop", R"j(
      {"op": "drop_index", "table": "orders", "name": "fk_1_UNIQUE"})j"))).kind);
  const auto back = ok(edit(text(one, ""), {}, request("shop", R"j(
      {"op": "drop_foreign_key", "table": "orders", "name": "fk_1"},
      {"op": "drop_index", "table": "orders", "name": "fk_1_UNIQUE"},
      {"op": "drop_column", "table": "orders", "column": "customer_id1"})j")));
  EXPECT_EQ(k_shop, text(back, ""));
}

}  // namespace mdbls

// ---------------------------------------------------------------------------
// MSM sections: Section 150 runs on every deployment

namespace mdbls {
namespace {

// An MSM script: tables in Section 140, a routine dropped and created and
// a view created or replaced in Section 150.
std::string msm_script(const std::string &section_150) {
  return "-- MSM Section 110: Database Schema Creation\n"
         "USE `shop`;\n"
         "\n"
         "-- #############################################################################\n"
         "-- MSM Section 140: Non-idempotent Schema Objects\n"
         "-- #############################################################################\n"
         "\n"
         "CREATE TABLE `customer` (\n  `id` INT NOT NULL,\n  PRIMARY KEY (`id`)\n);\n"
         "\n"
         "CREATE TABLE `orders` (\n  `id` INT NOT NULL,\n  `customer_id` INT NOT NULL,\n  PRIMARY KEY (`id`)\n);\n"
         "\n"
         "-- #############################################################################\n"
         "-- MSM Section 150: Idempotent Schema Objects\n"
         "-- #############################################################################\n" +
         section_150 +
         "\n"
         "-- #############################################################################\n"
         "-- MSM Section 170: Authorization\n"
         "-- #############################################################################\n"
         "\n"
         "SELECT 1;\n";
}

const std::string k_section_150 =
    "\n"
    "DELIMITER %%\n"
    "\n"
    "DROP PROCEDURE IF EXISTS `count_customers`%%\n"
    "CREATE PROCEDURE `count_customers`()\n"
    "BEGIN\n"
    "  SELECT COUNT(*) AS `customers` FROM `customer`;\n"
    "END%%\n"
    "\n"
    "CREATE OR REPLACE VIEW `customer_ids` AS\n"
    "    SELECT `id` FROM `customer`%%\n"
    "\n"
    "DELIMITER ;\n";

const char *const k_add_view = R"j({"op": "add_view", "view": {"name": "order_ids", "definition": "SELECT `id` FROM `orders`"}})j";
const char *const k_add_routine =
    R"j({"op": "add_routine", "routine": {"name": "count_orders", "kind": "procedure", "body": "BEGIN\n  SELECT COUNT(*) FROM `orders`;\nEND"}})j";

// `script` after `first`, then after `second` on the result.
std::string twice(const std::string &script, const std::string &first, const std::string &second) {
  const auto r = ok(edit(script, {}, request("shop", first)));
  return text(ok(edit(text(r, ""), {}, request("shop", second))), "");
}

}  // namespace

TEST(Schema_edit, msm_views_are_created_or_replaced) {
  const auto r = ok(edit(msm_script(k_section_150), {}, request("shop", k_add_view)));
  EXPECT_EQ(msm_script(shop_with_in(k_section_150, "    SELECT `id` FROM `customer`%%\n",
                                    "    SELECT `id` FROM `customer`%%\n"
                                    "\n"
                                    "CREATE OR REPLACE VIEW `order_ids` AS SELECT `id` FROM `orders`%%\n")),
            text(r, ""));
  EXPECT_EQ(msm_script(k_section_150),
            twice(msm_script(k_section_150), k_add_view, R"j({"op": "drop_view", "view": "order_ids"})j"));
}

TEST(Schema_edit, msm_routines_are_dropped_before_they_are_created) {
  const auto r = ok(edit(msm_script(k_section_150), {}, request("shop", k_add_routine)));
  // After the routine, with the blank line above its DROP.
  EXPECT_EQ(msm_script(shop_with_in(k_section_150, "END%%\n",
                                    "END%%\n"
                                    "\n"
                                    "DROP PROCEDURE IF EXISTS `count_orders`%%\n"
                                    "CREATE PROCEDURE `count_orders`()\n"
                                    "BEGIN\n"
                                    "  SELECT COUNT(*) FROM `orders`;\n"
                                    "END%%\n")),
            text(r, ""));
  // The migration has the CREATE: msm drops it and keeps the DROP of
  // Section 150.
  EXPECT_EQ(std::vector<std::string>{"CREATE PROCEDURE `count_orders`()\nBEGIN\n  SELECT COUNT(*) FROM `orders`;\nEND"},
            migration(r));
  // Dropped again, the DROP line goes too.
  EXPECT_EQ(msm_script(k_section_150),
            twice(msm_script(k_section_150), k_add_routine, R"j({"op": "drop_routine", "routine": "count_orders"})j"));
}

TEST(Schema_edit, first_views_and_routines_go_into_section_150) {
  // A schema without views: not after the last table (Section 140, which an
  // upgrade does not run), but at the end of Section 150.
  const std::string no_view =
      "\n"
      "DELIMITER %%\n"
      "\n"
      "DROP PROCEDURE IF EXISTS `count_customers`%%\n"
      "CREATE PROCEDURE `count_customers`()\n"
      "BEGIN\n"
      "  SELECT COUNT(*) AS `customers` FROM `customer`;\n"
      "END%%\n"
      "\n"
      "DELIMITER ;\n";
  const auto r = ok(edit(msm_script(no_view), {}, request("shop", k_add_view)));
  EXPECT_EQ(msm_script(shop_with_in(no_view, "END%%\n",
                                    "END%%\n\nCREATE OR REPLACE VIEW `order_ids` AS SELECT `id` FROM `orders`%%\n")),
            text(r, ""));
  // An empty Section 150: after its banner; a routine gets its own block.
  const auto e = ok(edit(msm_script(""), {}, request("shop", std::string(k_add_view) + ", " + k_add_routine)));
  EXPECT_EQ(msm_script("\n"
                       "CREATE OR REPLACE VIEW `order_ids` AS SELECT `id` FROM `orders`;\n"
                       "\n"
                       "DELIMITER $$\n"
                       "DROP PROCEDURE IF EXISTS `count_orders`$$\n"
                       "CREATE PROCEDURE `count_orders`()\n"
                       "BEGIN\n"
                       "  SELECT COUNT(*) FROM `orders`;\n"
                       "END$$\n"
                       "DELIMITER ;\n"),
            text(e, ""));
  const View *v = nullptr;
  if (const Schema *s = e.outline.catalog.find("shop")) v = s->view("order_ids");
  EXPECT_NE(nullptr, v);
}

TEST(Schema_edit, msm_tables_go_into_section_140) {
  const std::string script = msm_script(k_section_150);
  const auto r = ok(edit(script, {}, request("shop", R"j({"op": "add_table", "table": {"name": "product", "columns": [{"name": "id", "type": "INT"}]}})j")));
  const std::string t = text(r, "");
  EXPECT_LT(t.find("CREATE TABLE `product`"), t.find("MSM Section 150"));
  EXPECT_GT(t.find("CREATE TABLE `product`"), t.find("CREATE TABLE `orders`"));
}

TEST(Schema_edit, outside_msm_new_objects_follow_their_neighbours_form) {
  // A view after a view dropped first; a routine after one created or
  // replaced; a plain view after a plain one.
  const std::string script =
      "CREATE TABLE t (a INT);\n"
      "DROP VIEW IF EXISTS v;\n"
      "CREATE VIEW v AS SELECT a FROM t;\n"
      "CREATE OR REPLACE PROCEDURE p() SELECT 1;\n";
  const auto r = ok(edit(script, {}, request("", R"j({"op": "add_view", "view": {"name": "w", "definition": "SELECT 2"}},
      {"op": "add_routine", "routine": {"name": "q", "kind": "procedure", "body": "SELECT 2"}})j")));
  EXPECT_EQ(
      "CREATE TABLE t (a INT);\n"
      "DROP VIEW IF EXISTS v;\n"
      "CREATE VIEW v AS SELECT a FROM t;\n"
      "DROP VIEW IF EXISTS w;\n"
      "CREATE VIEW w AS SELECT 2;\n"
      "CREATE OR REPLACE PROCEDURE p() SELECT 1;\n"
      "CREATE OR REPLACE PROCEDURE q() SELECT 2;\n",
      text(r, ""));
  const auto plain = ok(edit("CREATE VIEW v AS SELECT 1;\n", {}, request("", R"j({"op": "add_view", "view": {"name": "w", "definition": "SELECT 2"}})j")));
  EXPECT_EQ("CREATE VIEW v AS SELECT 1;\nCREATE VIEW w AS SELECT 2;\n", text(plain, ""));
}

}  // namespace mdbls

// ---------------------------------------------------------------------------
// Migration tags and key names (msm reads the tags instead of the SQL)

namespace mdbls {
namespace {

Tags shop_tags(const std::string &edit_json) { return tags(ok(edit(k_shop, {}, request("shop", edit_json)))); }

}  // namespace

TEST(Schema_edit, migration_tags_of_objects) {
  EXPECT_EQ(Tags{"rename table customer -> client"}, shop_tags(rename_table("customer", "client")));
  EXPECT_EQ(Tags{"rename view v_orders -> v2"},
            shop_tags(R"j({"op": "rename_view", "view": "v_orders", "new_name": "v2"})j"));
  // No statement renames a routine: its DROP and its CREATE.
  EXPECT_EQ((Tags{"drop procedure p1", "create procedure p2"}),
            shop_tags(R"j({"op": "rename_routine", "routine": "p1", "new_name": "p2"})j"));
  EXPECT_EQ(Tags{"create table t2"},
            shop_tags(R"j({"op": "add_table", "table": {"name": "t2", "columns": [{"name": "a", "type": "INT"}]}})j"));
  EXPECT_EQ(Tags{"drop table orders"}, shop_tags(R"j({"op": "drop_table", "table": "orders"})j"));
  EXPECT_EQ(Tags{"create view v2"},
            shop_tags(R"j({"op": "add_view", "view": {"name": "v2", "definition": "SELECT 1"}})j"));
  EXPECT_EQ(Tags{"drop view v_orders"}, shop_tags(R"j({"op": "drop_view", "view": "v_orders"})j"));
  EXPECT_EQ(Tags{"create function f1"},
            shop_tags(R"j({"op": "add_routine", "routine": {"name": "f1", "kind": "function", "returns": "INT", "body": "RETURN 1"}})j"));
  EXPECT_EQ(Tags{"drop procedure p1"}, shop_tags(R"j({"op": "drop_routine", "routine": "p1"})j"));
}

TEST(Schema_edit, migration_tags_of_members) {
  EXPECT_EQ(Tags{"rename column note (orders) -> remark"},
            shop_tags(R"j({"op": "rename_column", "table": "orders", "column": "note", "new_name": "remark"})j"));
  EXPECT_EQ(Tags{"add column x (orders)"},
            shop_tags(R"j({"op": "add_column", "table": "orders", "column": {"name": "x", "type": "INT"}})j"));
  EXPECT_EQ(Tags{"drop column name (customer)"},
            shop_tags(R"j({"op": "drop_column", "table": "customer", "column": "name"})j"));
  // CHANGE COLUMN: renamed as it is altered.
  EXPECT_EQ(Tags{"alter column note (orders) -> remark"},
            shop_tags(R"j({"op": "modify_column", "table": "orders", "column": "note",
                "new_column": {"name": "remark", "type": "TEXT"}})j"));
  // The 1833 sequence: the foreign keys using the column go before it and
  // come back after.
  EXPECT_EQ((Tags{"drop foreign_key fk_orders_customer (orders)", "alter column id (customer)",
                  "add foreign_key fk_orders_customer (orders)"}),
            shop_tags(R"j({"op": "modify_column", "table": "customer", "column": "id",
                "new_column": {"name": "id", "type": "BIGINT", "nullable": false, "auto_increment": true}})j"));
  EXPECT_EQ(Tags{"alter table_options orders (orders)"},
            shop_tags(R"j({"op": "set_table_options", "table": "orders", "options": {"comment": "x"}})j"));
  EXPECT_EQ(Tags{"add index ix_x (orders)"},
            shop_tags(R"j({"op": "add_index", "table": "orders", "index": {"name": "ix_x", "kind": "index", "columns": ["note"]}})j"));
  EXPECT_EQ(Tags{"drop index ix_note (orders)"},
            shop_tags(R"j({"op": "drop_index", "table": "orders", "name": "ix_note"})j"));
  EXPECT_EQ(Tags{"drop primary_key PRIMARY (orders)"},
            shop_tags(R"j({"op": "drop_index", "table": "orders", "kind": "primary"})j"));
  EXPECT_EQ(Tags{"alter primary_key PRIMARY (orders)"},
            shop_tags(R"j({"op": "set_primary_key", "table": "orders", "columns": ["id", "customer_id"]})j"));
  EXPECT_EQ(Tags{"add primary_key PRIMARY (t)"},
            tags(ok(edit("CREATE TABLE t (a INT);\n", {}, request("", R"j({"op": "set_primary_key", "table": "t", "columns": ["a"]})j")))));
  EXPECT_EQ(Tags{"add foreign_key fk_x (orders)"},
            shop_tags(R"j({"op": "add_foreign_key", "table": "orders",
                "foreign_key": {"name": "fk_x", "columns": ["id"], "references": {"table": "customer", "columns": ["id"]}}})j"));
  EXPECT_EQ(Tags{"drop foreign_key fk_orders_customer (orders)"},
            shop_tags(R"j({"op": "drop_foreign_key", "table": "orders", "name": "fk_orders_customer"})j"));
}

TEST(Schema_edit, migration_tags_in_the_json) {
  const auto r = ok(edit(k_shop, {}, request("shop", R"j({"op": "rename_column", "table": "orders", "column": "note", "new_name": "remark"},
      {"op": "drop_view", "view": "v_orders"})j")));
  rapidjson::Document d;
  d.Parse(edit_result_json(r, "-", k_dir).c_str());
  ASSERT_TRUE(d.IsObject());
  const auto &m = d["migration"];
  ASSERT_EQ(2u, m.Size());
  EXPECT_STREQ("rename", m[0]["action"].GetString());
  EXPECT_STREQ("column", m[0]["object"].GetString());
  EXPECT_STREQ("note", m[0]["name"].GetString());
  EXPECT_STREQ("orders", m[0]["table"].GetString());
  EXPECT_STREQ("remark", m[0]["new_name"].GetString());
  // An object: no table, no new name.
  EXPECT_STREQ("drop", m[1]["action"].GetString());
  EXPECT_STREQ("view", m[1]["object"].GetString());
  EXPECT_FALSE(m[1].HasMember("table"));
  EXPECT_FALSE(m[1].HasMember("new_name"));
}

// Every key gets a name when it is created, so a later drop can name it.
TEST(Schema_edit, new_keys_are_named) {
  // An index: its first column's name, as the server would give it.
  const auto ix = ok(edit(k_shop, {}, request("shop", R"j({"op": "add_index", "table": "orders", "index": {"kind": "index", "columns": ["customer_id"]}},
      {"op": "add_index", "table": "orders", "index": {"kind": "unique", "columns": ["customer_id", "id"]}})j")));
  EXPECT_EQ((Tags{"add index customer_id (orders)", "add index customer_id_2 (orders)"}), tags(ix));
  EXPECT_NE(std::string::npos, text(ix, "").find("INDEX `customer_id` (`customer_id`)")) << text(ix, "");
  // An unnamed index already there has the server's name: a column's UNIQUE.
  EXPECT_EQ(Tags{"add index b_2 (t)"},
            tags(ok(edit("CREATE TABLE t (a INT, b INT UNIQUE);\n", {},
                         request("", R"j({"op": "add_index", "table": "t", "index": {"kind": "index", "columns": ["b"]}})j")))));
  // A foreign key: fk_<table>_<referenced table>, unique in the schema.
  const auto fk = ok(edit(k_shop, {}, request("shop", R"j({"op": "add_foreign_key", "table": "orders",
      "foreign_key": {"columns": ["id"], "references": {"table": "customer", "columns": ["id"]}}})j")));
  EXPECT_EQ(Tags{"add foreign_key fk_orders_customer_2 (orders)"}, tags(fk));
  // A new table's keys.
  const auto t = ok(edit(k_shop, {}, request("shop", R"j({"op": "add_table", "table": {"name": "item",
      "columns": [{"name": "id", "type": "INT"}, {"name": "order_id", "type": "INT"}],
      "indexes": [{"kind": "primary", "columns": ["id"]}, {"kind": "index", "columns": ["order_id"]}],
      "foreign_keys": [{"columns": ["order_id"], "references": {"table": "orders", "columns": ["id"]}}]}})j")));
  const auto *item = table(t, "shop", "item");
  ASSERT_NE(nullptr, item);
  ASSERT_EQ(2u, item->indexes.size());
  EXPECT_EQ("order_id", item->indexes[1].name);
  ASSERT_EQ(1u, item->foreign_keys.size());
  EXPECT_EQ("fk_item_orders", item->foreign_keys[0].name);
  // A name past 64 characters is cut to leave room for the suffix.
  const std::string long_table(60, 't');
  const std::string first = ("fk_" + long_table + "_u").substr(0, 64);  // taken
  const auto cut = ok(edit("CREATE TABLE u (id INT PRIMARY KEY);\nCREATE TABLE " + long_table + " (u_id INT, CONSTRAINT `" +
                               first + "` FOREIGN KEY (u_id) REFERENCES u (id));\n",
                           {}, request("", R"j({"op": "add_foreign_key", "table": ")j" + long_table + R"j(",
      "foreign_key": {"columns": ["u_id"], "references": {"table": "u", "columns": ["id"]}}})j")));
  ASSERT_EQ(1u, cut.migration.size());
  EXPECT_EQ("fk_" + long_table.substr(0, 59) + "_2", cut.migration[0].name);
}

TEST(Schema_edit, ops_json) {
  rapidjson::Document d;
  d.Parse(edit_ops_json().c_str());
  ASSERT_TRUE(d.IsObject());
  EXPECT_EQ(1, d["format"].GetInt());
  std::string ops;
  for (const auto &o : d["ops"].GetArray()) ops += (ops.empty() ? "" : " ") + std::string(o.GetString());
  EXPECT_EQ(supported_edit_ops(), ops);
}

}  // namespace mdbls

// ---------------------------------------------------------------------------
// Triggers and events

namespace mdbls {
namespace {

const char *const k_add_trigger =
    R"j({"op": "add_trigger", "trigger": {"name": "orders_bi", "timing": "BEFORE", "events": "INSERT", "table": "orders", "body": "BEGIN\n  SET NEW.note = 'new';\nEND"}})j";
const char *const k_add_event =
    R"j({"op": "add_event", "event": {"name": "purge", "schedule": "EVERY 1 DAY", "body": "DELETE FROM `orders`"}})j";

}  // namespace

TEST(Schema_edit, trigger_and_event_request_errors) {
  auto kind = [](const std::string &edit_json) {
    const auto r = parse_edit_request(request("s", edit_json));
    if (const auto *e = std::get_if<Edit_error>(&r)) return e->kind + ": " + e->message;
    return std::string("ok");
  };
  EXPECT_EQ("ok", kind(k_add_trigger));
  // The outline's shape: events is a string.
  EXPECT_NE("ok", kind(R"j({"op": "add_trigger", "trigger": {"name": "t", "timing": "BEFORE", "events": ["INSERT"], "table": "x", "body": "SET @a = 1"}})j"));
  EXPECT_EQ("ok", kind(R"j({"op": "add_trigger", "trigger": {"name": "t", "timing": "after", "events": "update  or delete", "table": "x", "body": "SET @a = 1", "update_columns": ["a"], "order": null, "definer": null}})j"));
  EXPECT_NE("ok", kind(R"j({"op": "add_trigger", "trigger": {"name": "t", "timing": "BEFORE", "events": "INSERT OR INSERT", "table": "x", "body": "SET @a = 1"}})j"));
  EXPECT_NE("ok", kind(R"j({"op": "add_trigger", "trigger": {"name": "t", "timing": "BEFORE", "events": "TRUNCATE", "table": "x", "body": "SET @a = 1"}})j"));
  EXPECT_NE("ok", kind(R"j({"op": "add_trigger", "trigger": {"name": "t", "timing": "DURING", "events": "INSERT", "table": "x", "body": "SET @a = 1"}})j"));
  EXPECT_NE("ok", kind(R"j({"op": "add_trigger", "trigger": {"name": "t", "timing": "BEFORE", "events": "INSERT", "table": "x", "body": "SET @a = 1", "update_columns": ["a"]}})j"));
  EXPECT_NE("ok", kind(R"j({"op": "add_trigger", "trigger": {"name": "t", "timing": "BEFORE", "events": "INSERT", "table": "x", "body": " "}})j"));
  EXPECT_NE("ok", kind(R"j({"op": "add_trigger", "trigger": {"name": "t", "timing": "BEFORE", "events": "INSERT", "table": "x", "body": "SET @a = 1", "order": {"kind": "AFTER", "trigger": "u"}}})j"));
  EXPECT_NE("ok", kind(R"j({"op": "add_trigger", "trigger": {"name": "t", "timing": "BEFORE", "events": "INSERT", "body": "SET @a = 1"}})j"));
  EXPECT_NE("ok", kind(R"j({"op": "drop_trigger", "trigger": "t", "kind": "x"})j"));
  EXPECT_EQ("ok", kind(k_add_event));
  EXPECT_EQ("ok", kind(R"j({"op": "add_event", "event": {"name": "e", "schedule": "AT CURRENT_TIMESTAMP", "body": "SELECT 1", "status": "disable on slave", "comment": "c", "on_completion_preserve": false, "definer": null}})j"));
  EXPECT_NE("ok", kind(R"j({"op": "add_event", "event": {"name": "e", "body": "SELECT 1"}})j"));
  EXPECT_NE("ok", kind(R"j({"op": "add_event", "event": {"name": "e", "schedule": "EVERY 1 DAY", "body": "SELECT 1", "status": "ON"}})j"));
  EXPECT_NE("ok", kind(R"j({"op": "rename_event", "event": "e"})j"));
  EXPECT_EQ("bad-request: the name `" + std::string(65, 'x') + "` is longer than 64 characters",
            kind(R"j({"op": "rename_trigger", "trigger": "t", "new_name": ")j" + std::string(65, 'x') + "\"}"));
}

TEST(Schema_edit, add_trigger) {
  const auto r = ok(edit(k_shop, {}, request("shop", k_add_trigger)));
  // After the last routine, in its DELIMITER block.
  EXPECT_EQ(shop_with("END$$\n", "END$$\n"
                                 "CREATE TRIGGER `orders_bi` BEFORE INSERT ON `orders` FOR EACH ROW\n"
                                 "BEGIN\n"
                                 "  SET NEW.note = 'new';\n"
                                 "END$$\n"),
            text(r, ""));
  EXPECT_EQ(Tags{"create trigger orders_bi"}, tags(r));
  EXPECT_EQ(std::vector<std::string>{"CREATE TRIGGER `orders_bi` BEFORE INSERT ON `orders` FOR EACH ROW\nBEGIN\n  SET NEW.note = 'new';\nEND"},
            migration(r));
  EXPECT_EQ(std::vector<std::string>{"trigger-created"}, note_kinds(r));
  const Schema *s = r.outline.catalog.find("shop");
  ASSERT_NE(nullptr, s);
  ASSERT_EQ(1u, s->triggers.size());
  EXPECT_EQ("INSERT", s->triggers[0].events);
  // Dropped again, the script is as it was.
  EXPECT_EQ(k_shop, twice(k_shop, k_add_trigger, R"j({"op": "drop_trigger", "trigger": "orders_bi", "table": "orders"})j"));

  auto refused = [](const std::string &edit_json) { return failed(edit(k_shop, {}, request("shop", edit_json))).kind; };
  EXPECT_EQ("unknown-object", refused(R"j({"op": "add_trigger", "trigger": {"name": "x", "timing": "BEFORE", "events": "INSERT", "table": "v_orders", "body": "SET @a = 1"}})j"));
  EXPECT_EQ("unknown-object", refused(R"j({"op": "add_trigger", "trigger": {"name": "x", "timing": "BEFORE", "events": "INSERT", "table": "nope", "body": "SET @a = 1"}})j"));
  EXPECT_EQ("unknown-object", refused(R"j({"op": "add_trigger", "trigger": {"name": "x", "timing": "BEFORE", "events": "INSERT", "table": "orders", "body": "SET @a = 1", "order": {"kind": "FOLLOWS", "trigger": "nope"}}})j"));
  EXPECT_EQ("bad-request", refused(R"j({"op": "add_trigger", "trigger": {"name": "x", "timing": "BEFORE", "events": "INSERT", "table": "orders", "body": "BEGIN SET FROM; END"}})j"));
  EXPECT_EQ("name-taken", failed(edit(k_shop, {}, request("shop", std::string(k_add_trigger) + ", " + k_add_trigger))).kind);
}

TEST(Schema_edit, add_trigger_with_all_its_members) {
  const std::string script = "CREATE TABLE t (a INT, b INT);\nCREATE TRIGGER t1 BEFORE UPDATE ON t FOR EACH ROW SET NEW.a = 1;\n";
  const auto r = ok(edit(script, {}, request("", R"j({"op": "add_trigger", "trigger": {"name": "t2", "timing": "before", "events": "update or insert", "table": "T", "update_columns": ["b"], "order": {"kind": "follows", "trigger": "T1"}, "definer": "`root`@`localhost`", "body": "SET NEW.b = 2"}})j")));
  // After the last trigger; names as the request's objects spell them.
  EXPECT_EQ(script +
                "CREATE DEFINER=`root`@`localhost` TRIGGER t2 BEFORE UPDATE OR INSERT OF b ON t FOR EACH ROW FOLLOWS t1\n"
                "  SET NEW.b = 2;\n",
            text(r, ""));
  const Schema *s = r.outline.catalog.find("");
  ASSERT_NE(nullptr, s);
  ASSERT_EQ(2u, s->triggers.size());
  const Trigger &t = s->triggers[1];
  EXPECT_EQ("UPDATE OR INSERT", t.events);
  EXPECT_EQ(std::vector<std::string>{"b"}, t.update_columns);
  EXPECT_EQ("FOLLOWS", t.order_kind);
  EXPECT_EQ("t1", t.order_trigger);
  EXPECT_EQ("t", t.table);
}

TEST(Schema_edit, a_trigger_goes_after_its_table) {
  // Routines first, tables after them: the trigger goes after its table.
  const std::string script = "CREATE PROCEDURE p() SELECT 1;\nCREATE TABLE t (a INT);\n";
  const auto r = ok(edit(script, {}, request("", R"j({"op": "add_trigger", "trigger": {"name": "x", "timing": "BEFORE", "events": "INSERT", "table": "t", "body": "SET NEW.a = 1"}})j")));
  EXPECT_EQ(script + "CREATE TRIGGER x BEFORE INSERT ON t FOR EACH ROW SET NEW.a = 1;\n", text(r, ""));
}

TEST(Schema_edit, drop_trigger) {
  const std::string script =
      "CREATE TABLE t (a INT);\n"
      "\n"
      "DROP TRIGGER IF EXISTS t1;\n"
      "CREATE TRIGGER t1 BEFORE INSERT ON t FOR EACH ROW SET NEW.a = 1;\n"
      "CREATE TRIGGER t2 BEFORE INSERT ON t FOR EACH ROW FOLLOWS t1 SET NEW.a = 2;\n";
  // t2 follows t1: its CREATE fails without t1.
  EXPECT_EQ("referenced", failed(edit(script, {}, request("", R"j({"op": "drop_trigger", "trigger": "t1"})j"))).kind);
  EXPECT_EQ("unknown-object", failed(edit(script, {}, request("", R"j({"op": "drop_trigger", "trigger": "t2", "table": "u"})j"))).kind);
  EXPECT_EQ("unknown-object", failed(edit(script, {}, request("", R"j({"op": "drop_trigger", "trigger": "t3"})j"))).kind);
  const auto r = ok(edit(script, {}, request("", R"j({"op": "drop_trigger", "trigger": "t2"}, {"op": "drop_trigger", "trigger": "T1", "table": "T"})j")));
  EXPECT_EQ("CREATE TABLE t (a INT);\n", text(r, ""));
  EXPECT_EQ((Tags{"drop trigger t2", "drop trigger t1"}), tags(r));
  EXPECT_EQ((std::vector<std::string>{"DROP TRIGGER `t2`", "DROP TRIGGER `t1`"}), migration(r));
}

TEST(Schema_edit, rename_trigger) {
  const std::string script =
      "CREATE TABLE t (a INT);\n"
      "DELIMITER ;;\n"
      "DROP TRIGGER IF EXISTS `t1`;;\n"
      "/*!50003 CREATE*/ /*!50017 DEFINER=`root`@`localhost`*/ /*!50003 TRIGGER `t1` BEFORE INSERT ON `t` FOR EACH ROW BEGIN\n"
      "  SET @msg = 't1';\n"
      "END */;;\n"
      "CREATE TRIGGER t2 BEFORE INSERT ON t FOR EACH ROW PRECEDES `t1` SET NEW.a = 2;;\n"
      "DELIMITER ;\n";
  const auto r = ok(edit(script, {}, request("", R"j({"op": "rename_trigger", "trigger": "t1", "new_name": "t one"})j")));
  EXPECT_EQ(
      "CREATE TABLE t (a INT);\n"
      "DELIMITER ;;\n"
      "DROP TRIGGER IF EXISTS `t one`;;\n"
      "/*!50003 CREATE*/ /*!50017 DEFINER=`root`@`localhost`*/ /*!50003 TRIGGER `t one` BEFORE INSERT ON `t` FOR EACH ROW BEGIN\n"
      "  SET @msg = 't1';\n"
      "END */;;\n"
      "CREATE TRIGGER t2 BEFORE INSERT ON t FOR EACH ROW PRECEDES `t one` SET NEW.a = 2;;\n"
      "DELIMITER ;\n",
      text(r, ""));
  EXPECT_EQ((Tags{"drop trigger t1", "create trigger t one"}), tags(r));
  ASSERT_EQ(2u, migration(r).size());
  EXPECT_EQ("DROP TRIGGER `t1`", migration(r)[0]);
  EXPECT_EQ(0u, migration(r)[1].find("/*!50003 CREATE*/"));
  EXPECT_EQ((std::vector<std::string>{"name-in-string", "trigger-recreated"}), note_kinds(r));
  EXPECT_EQ("name-taken", failed(edit(script, {}, request("", R"j({"op": "rename_trigger", "trigger": "t1", "new_name": "T2"})j"))).kind);
  // The same name again changes nothing to migrate.
  EXPECT_EQ(Tags{}, tags(ok(edit(script, {}, request("", R"j({"op": "rename_trigger", "trigger": "t1", "new_name": "t1"})j")))));
}

TEST(Schema_edit, add_drop_and_rename_event) {
  const auto r = ok(edit(k_shop, {}, request("shop", k_add_event)));
  EXPECT_EQ(shop_with("END$$\n", "END$$\nCREATE EVENT `purge` ON SCHEDULE EVERY 1 DAY DO DELETE FROM `orders`$$\n"), text(r, ""));
  EXPECT_EQ(Tags{"create event purge"}, tags(r));
  EXPECT_EQ(std::vector<std::string>{"event-created"}, note_kinds(r));
  EXPECT_EQ(k_shop, twice(k_shop, k_add_event, R"j({"op": "drop_event", "event": "purge"})j"));
  EXPECT_EQ("name-taken", failed(edit(k_shop, {}, request("shop", std::string(k_add_event) + ", " + k_add_event))).kind);
  EXPECT_EQ("bad-request", failed(edit(k_shop, {}, request("shop", R"j({"op": "add_event", "event": {"name": "e", "schedule": "EVERY DAY", "body": "SELECT 1"}})j"))).kind);

  const std::string script =
      "CREATE TABLE t (a INT);\n"
      "DROP EVENT IF EXISTS e1;\n"
      "CREATE EVENT e1 ON SCHEDULE EVERY 1 DAY DO DELETE FROM t;\n"
      "ALTER EVENT e1 DISABLE;\n";
  const auto rn = ok(edit(script, {}, request("", R"j({"op": "rename_event", "event": "E1", "new_name": "e2"})j")));
  EXPECT_EQ(
      "CREATE TABLE t (a INT);\n"
      "DROP EVENT IF EXISTS e2;\n"
      "CREATE EVENT e2 ON SCHEDULE EVERY 1 DAY DO DELETE FROM t;\n"
      "ALTER EVENT e2 DISABLE;\n",
      text(rn, ""));
  EXPECT_EQ(Tags{"rename event e1 -> e2"}, tags(rn));
  EXPECT_EQ(std::vector<std::string>{"ALTER EVENT `e1` RENAME TO `e2`"}, migration(rn));
  // A change of case is no rename on the server.
  EXPECT_EQ(Tags{}, tags(ok(edit(script, {}, request("", R"j({"op": "rename_event", "event": "e1", "new_name": "E1"})j")))));
  const auto d = ok(edit(script, {}, request("", R"j({"op": "drop_event", "event": "e1"})j")));
  EXPECT_EQ("CREATE TABLE t (a INT);\n", text(d, ""));
  EXPECT_EQ(Tags{"drop event e1"}, tags(d));
  EXPECT_EQ("unknown-object", failed(edit(script, {}, request("", R"j({"op": "drop_event", "event": "e9"})j"))).kind);
}

TEST(Schema_edit, msm_triggers_and_events_are_dropped_before_they_are_created) {
  const auto r = ok(edit(msm_script(k_section_150), {}, request("shop", std::string(k_add_trigger) + ", " + k_add_event)));
  // After the last routine, the event after the trigger, each with the
  // blank line above the DROP it follows.
  EXPECT_EQ(msm_script(shop_with_in(k_section_150, "END%%\n",
                                    "END%%\n"
                                    "\n"
                                    "DROP TRIGGER IF EXISTS `orders_bi`%%\n"
                                    "CREATE TRIGGER `orders_bi` BEFORE INSERT ON `orders` FOR EACH ROW\n"
                                    "BEGIN\n"
                                    "  SET NEW.note = 'new';\n"
                                    "END%%\n"
                                    "\n"
                                    "DROP EVENT IF EXISTS `purge`%%\n"
                                    "CREATE EVENT `purge` ON SCHEDULE EVERY 1 DAY DO DELETE FROM `orders`%%\n")),
            text(r, ""));
  EXPECT_EQ((Tags{"create trigger orders_bi", "create event purge"}), tags(r));
  EXPECT_EQ(msm_script(k_section_150),
            twice(msm_script(k_section_150), k_add_trigger, R"j({"op": "drop_trigger", "trigger": "orders_bi"})j"));
  EXPECT_EQ(msm_script(k_section_150),
            twice(msm_script(k_section_150), k_add_event, R"j({"op": "drop_event", "event": "purge"})j"));
  // An empty Section 150.
  const auto e = ok(edit(msm_script(""), {}, request("shop", k_add_trigger)));
  EXPECT_EQ(msm_script("\n"
                       "DELIMITER $$\n"
                       "DROP TRIGGER IF EXISTS `orders_bi`$$\n"
                       "CREATE TRIGGER `orders_bi` BEFORE INSERT ON `orders` FOR EACH ROW\n"
                       "BEGIN\n"
                       "  SET NEW.note = 'new';\n"
                       "END$$\n"
                       "DELIMITER ;\n"),
            text(e, ""));
}

TEST(Schema_edit, rename_table_keeps_its_triggers) {
  const std::string script =
      "CREATE TABLE t (a INT);\n"
      "CREATE TRIGGER t1 BEFORE INSERT ON t FOR EACH ROW SET NEW.a = 1;\n"
      "CREATE EVENT e1 ON SCHEDULE EVERY 1 DAY DO DELETE FROM t;\n";
  const auto r = ok(edit(script, {}, request("", rename_table("t", "u"))));
  EXPECT_EQ(
      "CREATE TABLE u (a INT);\n"
      "CREATE TRIGGER t1 BEFORE INSERT ON u FOR EACH ROW SET NEW.a = 1;\n"
      "CREATE EVENT e1 ON SCHEDULE EVERY 1 DAY DO DELETE FROM u;\n",
      text(r, ""));
}

}  // namespace mdbls
