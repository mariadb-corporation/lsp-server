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

#include <map>
#include <string>
#include <string_view>

#include "mdbls/outline.h"
#include "mdbls/parser_pool.h"
#include "mdbls/source_include.h"
#include "mdbls/statement_index.h"
#include "mdbls/syntax_check.h"
#include "mdbls/workspace_catalog.h"
#include "test_util.h"

namespace mdbls {

namespace {

std::string_view cut(std::string_view text, Range r) { return text.substr(r.begin, r.end - r.begin); }

// The text a slice selects, or "error: ..." for a malformed one.
std::string sliced(std::string_view text, std::string_view start, std::string_view end) {
  const auto s = slice_source(text, start, end);
  if (!s.error.empty()) return "error: " + s.error;
  return std::string(cut(text, s.range));
}

// A reader over files kept in memory.
Source_reader files(std::map<std::string, std::string> f) {
  return [f = std::move(f)](const std::string &path, std::string *text) {
    const auto it = f.find(path);
    if (it == f.end()) return false;
    *text = it->second;
    return true;
  };
}

const std::string k_msm_dir = std::string(MDBLS_SQL_FIXTURE_DIR) + "/msm/development";

Parser_pool &pool() {
  static Parser_pool p(2);
  return p;
}

// The line (1-based) an origin starts on, in the text it points into.
uint32_t line_of(std::string_view text, const Origin &o) {
  return Line_map(text).position(text, o.statement.begin).line + 1;
}

}  // namespace

TEST(Source_include, directive_parts) {
  const std::string text =
      "USE s;\n"
      "  SOURCE './a.sql'[ln4:-115]; -- header and footer\n"
      "SELECT 1;\n";
  const auto d = find_source_directives(text);
  ASSERT_EQ(1u, d.size());
  EXPECT_EQ("  SOURCE './a.sql'[ln4:-115]; -- header and footer\n", cut(text, d[0].line));
  EXPECT_EQ("SOURCE './a.sql'[ln4:-115];", cut(text, d[0].statement));
  EXPECT_EQ("./a.sql", cut(text, d[0].path));
  EXPECT_EQ("[ln4:-115]", cut(text, d[0].slice));
  EXPECT_EQ("./a.sql", d[0].file);
  EXPECT_EQ("ln4", d[0].start);
  EXPECT_EQ("-115", d[0].end);
}

TEST(Source_include, directive_forms) {
  // What msm's regular expression takes, and what it does not.
  const std::map<std::string, std::string> taken = {
      {"SOURCE 'a.sql';", "a.sql"},
      {"SOURCE \"a.sql\";", "a.sql"},
      {"SOURCE 'a.sql'[:];", "a.sql"},
      {"SOURCE 'a.sql' [ 3 : ln-2 ] ;", "a.sql"},
      {"\t SOURCE 'a b.sql'[89:]; -- c", "a b.sql"},
      {"SOURCE 'it\"s.sql';", "it\"s.sql"},
      // The shortest path the rest matches after.
      {"SOURCE 'a' x 'b';", "a' x 'b"},
      // White space between the parts may span lines.
      {"SOURCE\n'a.sql'\n;", "a.sql"},
  };
  for (const auto &[line, file] : taken) {
    const auto d = find_source_directives("SELECT 1;\n" + line + "\nSELECT 2;\n");
    ASSERT_EQ(1u, d.size()) << line;
    EXPECT_EQ(file, d[0].file) << line;
  }
  for (const std::string line :
       {"source 'a.sql';", "SOURCE a.sql;", "SOURCE 'a.sql\";", "SOURCE 'a.sql'", "SOURCE 'a.sql'[1];",
        "SOURCE 'a.sql'[x:];", "SOURCE 'a.sql'[1:2;", "-- SOURCE 'a.sql';", "x SOURCE 'a.sql';",
        "SOURCE'a.sql';", "SOURCEX 'a.sql';", "SOURCE 'a\n.sql';"}) {
    EXPECT_TRUE(find_source_directives("SELECT 1;\n" + line + "\nSELECT 2;\n").empty()) << line;
  }
}

TEST(Source_include, directive_line_ends) {
  // The line break goes with the directive: "\n", "\r\n", a lone "\r", or
  // none at the end of the text.
  for (const std::string brk : {"\n", "\r\n", "\r"}) {
    const std::string text = "A;" + brk + "SOURCE 'a';" + brk + "SOURCE 'b'; -- c" + brk + "B;";
    const auto d = find_source_directives(text);
    ASSERT_EQ(2u, d.size());
    EXPECT_EQ("SOURCE 'a';" + brk, cut(text, d[0].line));
    EXPECT_EQ("SOURCE 'b'; -- c" + brk, cut(text, d[1].line));
  }
  const std::string last = "A;\nSOURCE 'a';";
  const auto d = find_source_directives(last);
  ASSERT_EQ(1u, d.size());
  EXPECT_EQ(last.size(), d[0].line.end);
  EXPECT_EQ(1u, find_source_directives("SOURCE 'a';").size());
}

TEST(Source_include, slice_by_characters) {
  const std::string text = "-- header\nSELECT 1;\n-- footer\n";  // 30 characters
  EXPECT_EQ(text, sliced(text, "", ""));
  EXPECT_EQ("SELECT 1;\n-- footer\n", sliced(text, "10", ""));
  EXPECT_EQ("SELECT 1;\n", sliced(text, "10", "-10"));
  EXPECT_EQ("-- header\n", sliced(text, "", "10"));
  EXPECT_EQ("-- footer\n", sliced(text, "-10", ""));
  EXPECT_EQ(text, sliced(text, "-100", "100"));
  EXPECT_EQ("", sliced(text, "20", "10"));
  EXPECT_EQ("", sliced(text, "99999999999999999999", ""));

  // Code points, not bytes; "\r\n" is one character, as msm reads in text mode.
  EXPECT_EQ("ü€x", sliced("aü€x", "1", ""));
  EXPECT_EQ("€", sliced("aü€x", "-2", "-1"));
  EXPECT_EQ("b\r\nc", sliced("a\r\nb\r\nc", "2", ""));
  EXPECT_EQ("a\r\n", sliced("a\r\nb\r\nc", "", "2"));
}

TEST(Source_include, slice_by_lines) {
  const std::string text = "-- header\nSELECT 1;\nSELECT 2;\n-- footer\n";
  EXPECT_EQ("SELECT 1;\nSELECT 2;\n-- footer\n", sliced(text, "ln2", ""));
  EXPECT_EQ("SELECT 1;\nSELECT 2;\n", sliced(text, "ln2", "ln3"));
  EXPECT_EQ("SELECT 1;\nSELECT 2;\n", sliced(text, "ln2", "ln-2"));
  EXPECT_EQ("-- header\n", sliced(text, "", "ln1"));
  EXPECT_EQ("-- footer\n", sliced(text, "ln-1", ""));
  EXPECT_EQ("SELECT 2;\n-- footer\n", sliced(text, "ln3", "ln99"));
  EXPECT_EQ(text, sliced(text, "ln-99", ""));
  EXPECT_EQ("", sliced(text, "ln3", "ln2"));
  // Mixed with a character offset
  EXPECT_EQ("SELECT 1;\nSELECT 2;\n", sliced(text, "ln2", "-10"));
  EXPECT_EQ("SELECT 1;\n", sliced(text, "10", "ln2"));

  // A last line without a break counts; "\r\n" and a lone "\r" end lines,
  // a form feed does not.
  EXPECT_EQ("b", sliced("a\nb", "ln2", ""));
  EXPECT_EQ("a\n", sliced("a\nb", "", "ln-2"));
  EXPECT_EQ("b\r\n", sliced("a\r\nb\r\nc", "ln2", "ln2"));
  EXPECT_EQ("b\r", sliced("a\rb\rc", "ln2", "ln2"));
  EXPECT_EQ("x\fy\n", sliced("x\fy\nz", "", "ln1"));
  EXPECT_EQ("", sliced("", "ln1", "ln-1"));
}

TEST(Source_include, slice_errors) {
  for (const auto &[start, end] : std::map<std::string, std::string>{
           {"ln0", ""}, {"", "ln0"}, {"ln", ""}, {"ln-", ""}, {"-", ""}, {"", "ln-0"}}) {
    EXPECT_EQ(0u, sliced("a\n", start, end).find("error: the slice bound")) << start << ":" << end;
  }
}

TEST(Source_include, expansion_and_mapping) {
  const std::string text =
      "USE s;\n"                                       // 0..7
      "SOURCE './sections/t.sql'[ln2:]; -- tables\n"   // 7..
      "CREATE PROCEDURE p() BEGIN\n"
      "  SOURCE 'sections/r.sql';\n"
      "END;\n";
  const std::string t = "-- (c)\nCREATE TABLE a (id INT);\n";
  const std::string r = "GRANT SELECT ON s.a TO x;\n";
  const auto e = expand_sources(text, "/p/dev", files({{"/p/dev/sections/t.sql", t},
                                                       {"/p/dev/sections/r.sql", r}}));
  ASSERT_EQ(2u, e.includes.size());
  EXPECT_EQ(
      "USE s;\n"
      "CREATE TABLE a (id INT);\n"
      "CREATE PROCEDURE p() BEGIN\n"
      "GRANT SELECT ON s.a TO x;\n"
      "END;\n",
      e.text);
  EXPECT_EQ("/p/dev/sections/t.sql", e.includes[0].path);
  EXPECT_EQ("/p/dev/sections/r.sql", e.includes[1].path);
  EXPECT_TRUE(e.includes[0].error.empty());

  // `a` of the CREATE TABLE comes from the first include.
  const auto a = static_cast<uint32_t>(e.text.find("a (id"));
  const auto loc = e.map(a);
  EXPECT_EQ(1u, loc.source);
  EXPECT_EQ(t.find("a (id"), loc.offset);
  // The statement maps whole into that file.
  const auto create = static_cast<uint32_t>(e.text.find("CREATE TABLE"));
  const auto stmt = e.map(Range{create, create + 25});
  EXPECT_EQ(1u, stmt.source);
  EXPECT_EQ("CREATE TABLE a (id INT);\n", cut(t, stmt.range));

  // The procedure starts and ends in the document: its range there spans
  // the directive in its body.
  const auto proc = static_cast<uint32_t>(e.text.find("CREATE PROCEDURE"));
  const auto proc_range = e.map(Range{proc, static_cast<uint32_t>(e.text.size())});
  EXPECT_EQ(0u, proc_range.source);
  EXPECT_EQ(text.substr(text.find("CREATE PROCEDURE")), cut(text, proc_range.range));

  // A range from the document into an include ends at the directive.
  const auto into = e.map(Range{proc, static_cast<uint32_t>(e.text.find("TO x"))});
  EXPECT_EQ(0u, into.source);
  EXPECT_EQ("CREATE PROCEDURE p() BEGIN\n  SOURCE 'sections/r.sql';", cut(text, into.range));
  // A range from an include into the document ends with the include.
  const auto out_of = e.map(Range{create, proc + 3});
  EXPECT_EQ(1u, out_of.source);
  EXPECT_EQ("CREATE TABLE a (id INT);\n", cut(t, out_of.range));

  // The end of the text maps to the end of the document.
  EXPECT_EQ(text.size(), e.map(static_cast<uint32_t>(e.text.size())).offset);
}

TEST(Source_include, expansion_errors) {
  const std::string text =
      "SOURCE 'missing.sql';\n"
      "SOURCE 'a.sql'[ln0:];\n"
      "SOURCE 'a.sql'[ln2:];\n"
      "SOURCE '/abs/n.sql';\n"
      "SELECT 1;\n";
  const auto e = expand_sources(text, "/d", files({{"/d/a.sql", "x;\ny;\n"},
                                                   {"/abs/n.sql", "-- n\n  SOURCE 'deeper.sql';\n"}}));
  ASSERT_EQ(4u, e.includes.size());
  EXPECT_EQ("cannot read /d/missing.sql", e.includes[0].error);
  EXPECT_NE(std::string::npos, e.includes[1].error.find("`ln0`"));
  EXPECT_TRUE(e.includes[2].error.empty());
  EXPECT_TRUE(e.includes[3].error.empty());
  // Failed directives include nothing; a nested one is listed, not expanded.
  EXPECT_EQ("y;\n-- n\n  SOURCE 'deeper.sql';\nSELECT 1;\n", e.text);
  ASSERT_EQ(1u, e.includes[3].nested.size());
  const auto &n = e.includes[3].nested[0];
  EXPECT_EQ("deeper.sql", n.file);
  EXPECT_EQ("SOURCE 'deeper.sql';", cut(e.includes[3].text, n.statement));

  // A relative path needs a folder.
  const auto untitled = expand_sources("SOURCE 'a.sql';\n", "", files({}));
  ASSERT_EQ(1u, untitled.includes.size());
  EXPECT_EQ("the path is relative, and the document is not in a folder", untitled.includes[0].error);
  EXPECT_EQ("", untitled.text);

  // No directives: nothing to expand.
  const auto none = expand_sources("SELECT 1;\n", "/d", files({}));
  EXPECT_TRUE(none.empty());
  EXPECT_TRUE(none.text.empty());
  EXPECT_EQ(5u, none.map(5).offset);
}

TEST(Source_include, outline_of_an_msm_script) {
  const std::string text = test::read_fixture("msm/development/shop_next.sql");
  ASSERT_FALSE(text.empty());
  const auto outline = build_outline(expand_sources(text, k_msm_dir), pool(), Index_options{});
  ASSERT_EQ(4u, outline.includes.size());
  EXPECT_EQ(k_msm_dir + "/sections/140_tables.sql", outline.includes[0].path);

  // The sections have no USE: their objects are in the script's schema.
  const Schema *shop = outline.catalog.find("shop");
  ASSERT_NE(nullptr, shop);
  ASSERT_EQ(3u, shop->tables.size());
  const Table *customer = shop->table("customer");
  ASSERT_NE(nullptr, customer);
  ASSERT_EQ(3u, customer->columns.size());  // the ALTER TABLE's email too

  // Origins point into the file each object is defined in.
  const auto &tables = outline.includes[0];
  EXPECT_EQ(1u, customer->origin.source);
  EXPECT_EQ(3u, line_of(tables.text, customer->origin));
  EXPECT_EQ("customer", cut(tables.text, customer->origin.name));
  EXPECT_EQ(1u, customer->columns[2].origin.source);
  EXPECT_EQ(7u, line_of(tables.text, customer->columns[2].origin));
  const Table *note = shop->table("note");
  ASSERT_NE(nullptr, note);
  EXPECT_EQ(4u, note->origin.source);

  // A function in DELIMITER %% of an include, a view after it.
  const Routine *count = shop->routine("order_count");
  ASSERT_NE(nullptr, count);
  EXPECT_EQ(2u, count->origin.source);
  EXPECT_EQ("order_count", cut(outline.includes[1].text, count->origin.name));
  const View *view = shop->view("customer_orders");
  ASSERT_NE(nullptr, view);
  EXPECT_EQ(2u, view->origin.source);

  // The procedure whose body has a directive is the script's, its range
  // spanning that directive.
  const Routine *restore = shop->routine("restore_roles");
  ASSERT_NE(nullptr, restore);
  EXPECT_EQ(0u, restore->origin.source);
  const auto proc = cut(text, restore->origin.statement);
  EXPECT_EQ(0u, proc.find("CREATE PROCEDURE restore_roles()"));
  EXPECT_NE(std::string_view::npos, proc.find("SOURCE './sections/170_roles.sql';\nEND"));

  // Notes: an ALTER of an unknown table in an include sits on its directive
  // and names the file and line; so does a directive msm does not expand.
  ASSERT_EQ(2u, outline.notes.size());
  const auto at_directive = [&](size_t k) { return outline.includes[k].directive.statement.begin; };
  EXPECT_EQ(at_directive(0), outline.notes[0].first);
  EXPECT_EQ(0u, outline.notes[0].second.find("140_tables.sql:13: ")) << outline.notes[0].second;
  EXPECT_EQ(at_directive(3), outline.notes[1].first);
  EXPECT_EQ("180_extra.sql:2: SOURCE './deeper.sql' not expanded: msm expands one level only",
            outline.notes[1].second);
}

TEST(Source_include, outline_notes_unreadable_include) {
  const std::string text = "USE s;\nSOURCE 'missing.sql';\nCREATE TABLE t (a INT);\n";
  const auto outline = build_outline(expand_sources(text, "/nowhere"), pool(), Index_options{});
  ASSERT_EQ(1u, outline.notes.size());
  EXPECT_EQ(text.find("SOURCE"), outline.notes[0].first);
  EXPECT_EQ("not read: cannot read /nowhere/missing.sql", outline.notes[0].second);
  const Schema *s = outline.catalog.find("s");
  ASSERT_NE(nullptr, s);
  ASSERT_EQ(1u, s->tables.size());
  EXPECT_EQ("t", cut(text, s->tables[0].origin.name));
}

TEST(Source_include, workspace_folds_an_included_file_once) {
  // The script and its sections folder listed: the sections come in only
  // through the script's directives, so in its schema and only once.
  std::vector<std::string> errors;
  const auto paths = expand_catalog_paths({"shop_next.sql", "sections"}, k_msm_dir, &errors);
  ASSERT_TRUE(errors.empty());
  ASSERT_EQ(5u, paths.size());
  const auto ws = build_workspace_catalog(paths, pool(), Index_options{});
  const Schema *shop = ws.outline.catalog.find("shop");
  ASSERT_NE(nullptr, shop);
  EXPECT_EQ(3u, shop->tables.size());
  EXPECT_EQ(nullptr, ws.outline.catalog.find(""));
  const Table *customer = shop->table("customer");
  ASSERT_NE(nullptr, customer);
  const auto &file = ws.files.at(customer->origin.source);
  EXPECT_NE(std::string::npos, file.path.find("/sections/140_tables.sql")) << file.path;
  EXPECT_EQ("customer", cut(file.text, customer->origin.name));
  // No file entry was added: every include is listed.
  EXPECT_EQ(5u, ws.files.size());

  // The script alone: the included files are added as sources.
  const auto alone = build_workspace_catalog({paths[0]}, pool(), Index_options{});
  ASSERT_EQ(5u, alone.files.size());
  const Table *c = alone.outline.catalog.find("shop")->table("customer");
  ASSERT_NE(nullptr, c);
  EXPECT_EQ("customer", cut(alone.files.at(c->origin.source).text, c->origin.name));
}

TEST(Source_include, directives_in_one_statement) {
  const std::string text = "SELECT 1;\n  SOURCE 'a.sql';\nSELECT 2;\nSOURCE 'b.sql';";
  // Only those starting in the range, the text around deciding line starts.
  const auto in_middle = find_source_directives(text, text.find("SOURCE"), text.find("SELECT 2"));
  ASSERT_EQ(1u, in_middle.size());
  EXPECT_EQ("a.sql", in_middle[0].file);
  EXPECT_EQ(text.find("  SOURCE"), in_middle[0].line.begin);
  EXPECT_TRUE(find_source_directives(text, 0, text.find("SOURCE")).empty());
  const auto last = find_source_directives(text, text.find("SELECT 2"), text.size());
  ASSERT_EQ(1u, last.size());
  EXPECT_EQ("b.sql", last[0].file);
}

TEST(Source_include, syntax_check) {
  const std::string text =
      "USE s;\n"
      "SOURCE './a.sql'[ln2:]; -- tables\n"
      "DELIMITER %%\n"
      "CREATE PROCEDURE p()\n"
      "BEGIN\n"
      "  SOURCE './roles.sql'; -- in the body\n"
      "END%%\n"
      "CREATE PROCEDURE q()\n"
      "BEGIN\n"
      "  SOURCE './roles.sql';\n"
      "  SELECT FROM;\n"
      "END%%\n"
      "DELIMITER ;\n";
  const auto index = Statement_index::build(text);
  const auto checks = check_statements(text, index, pool());
  std::vector<std::pair<std::string, Check_status>> sql;
  for (size_t i = 0; i < checks.size(); ++i) {
    const auto &st = index.statements()[i];
    if (st.kind != Statement_kind::sql) continue;
    sql.push_back({std::string(text.substr(st.begin, 8)), checks[i].status});
  }
  ASSERT_EQ(4u, sql.size());
  EXPECT_EQ(Check_status::ok, sql[0].second);
  // The directive is not parsed; the procedure is, without its directive.
  EXPECT_EQ(Check_status::source_directive, sql[1].second);
  EXPECT_EQ(Check_status::ok, sql[2].second);
  // An error after a directive is found where it is.
  ASSERT_EQ(Check_status::syntax_error, sql[3].second);
  size_t q = 0;
  while (index.statements()[q].begin != text.find("CREATE PROCEDURE q")) ++q;
  EXPECT_EQ(text.find("FROM;"), checks[q].error_offset);

  // As plain SQL, the directives are errors.
  Check_options plain;
  plain.source_directives = false;
  const auto as_sql = check_statements(text, index, pool(), plain);
  size_t errors = 0;
  for (const auto &c : as_sql) errors += c.status == Check_status::syntax_error ? 1 : 0;
  EXPECT_EQ(3u, errors);
}

TEST(Source_include, problems) {
  const std::string text =
      "SOURCE 'missing.sql'; -- gone\n"
      "SOURCE 'a.sql'[ln0:];\n"
      "SOURCE 'a.sql'[5:];\n"
      "SOURCE 'empty.sql';\n"
      "SOURCE 'nests.sql'[:];\n"
      "SOURCE 'a.sql';\n";
  const auto e = expand_sources(text, "/d", files({{"/d/a.sql", "x;\n"},
                                                   {"/d/empty.sql", ""},
                                                   {"/d/nests.sql", "y;\nSOURCE 'z.sql';\n"}}));
  const auto problems = source_problems(e);
  ASSERT_EQ(5u, problems.size());
  EXPECT_TRUE(problems[0].error);
  EXPECT_EQ("missing.sql", cut(text, problems[0].range));
  EXPECT_EQ("cannot read /d/missing.sql", problems[0].message);
  EXPECT_TRUE(problems[1].error);
  EXPECT_EQ("[ln0:]", cut(text, problems[1].range));
  EXPECT_FALSE(problems[2].error);
  EXPECT_EQ("[5:]", cut(text, problems[2].range));
  EXPECT_EQ("the slice selects nothing of a.sql", problems[2].message);
  EXPECT_FALSE(problems[3].error);
  EXPECT_EQ("SOURCE 'empty.sql';", cut(text, problems[3].range));
  EXPECT_EQ("empty.sql is empty", problems[3].message);
  EXPECT_FALSE(problems[4].error);
  EXPECT_EQ("SOURCE 'nests.sql'[:];", cut(text, problems[4].range));
  EXPECT_EQ("nests.sql line 2 has SOURCE 'z.sql', which msm does not expand: it expands one "
            "level only",
            problems[4].message);
}

TEST(Source_include, path_being_typed) {
  const std::string text = "USE s;\n  SOURCE './sections/14";
  const auto at = source_path_at(text, static_cast<uint32_t>(text.size()));
  ASSERT_TRUE(at);
  EXPECT_EQ("./sections/14", at->typed);
  EXPECT_EQ(text.find("./"), at->path);
  EXPECT_EQ(text.find("14"), at->segment);
  // Not after the closing quote, not before the opening one, not elsewhere.
  const std::string closed = "SOURCE 'a.sql' ";
  EXPECT_FALSE(source_path_at(closed, static_cast<uint32_t>(closed.size())));
  EXPECT_FALSE(source_path_at("SOURCE ", 7));
  EXPECT_FALSE(source_path_at("SELECT './x", 11));
  EXPECT_FALSE(source_path_at("source './x", 11));

  const auto files = source_path_candidates(k_msm_dir, "./sections/1");
  std::vector<std::string> names;
  for (const auto &c : files) names.push_back(c.name);
  EXPECT_EQ((std::vector<std::string>{"140_tables.sql", "150_routines.sql", "170_roles.sql",
                                      "180_extra.sql"}),
            names);
  const auto top = source_path_candidates(k_msm_dir, "");
  ASSERT_FALSE(top.empty());
  EXPECT_EQ("sections", top[0].name);  // folders first
  EXPECT_TRUE(top[0].folder);
  EXPECT_TRUE(source_path_candidates("", "sections/").empty());  // relative, no folder
}

TEST(Source_include, contexts_where_includes_begin) {
  const std::string text = test::read_fixture("msm/development/shop_next.sql");
  Outline_options oo;
  oo.schema = "start";
  const auto outline = build_outline(expand_sources(text, k_msm_dir), pool(), Index_options{}, oo);
  ASSERT_EQ(4u, outline.includes.size());
  const auto &tables = outline.includes[0].context;
  EXPECT_TRUE(tables.known);
  EXPECT_EQ("shop", tables.schema);
  EXPECT_EQ(";", tables.delimiter);
  // In the procedure body, under DELIMITER %%: its statements end with ';'.
  const auto &roles = outline.includes[2].context;
  EXPECT_TRUE(roles.known);
  EXPECT_EQ(";", roles.delimiter);
  EXPECT_EQ("shop", roles.schema);
  // The expanded text is kept, and maps back.
  ASSERT_TRUE(outline.expanded);
  const auto at = static_cast<uint32_t>(outline.expanded->text.find("CREATE TABLE customer"));
  const auto m = outline.map(Range{at, at + 12});
  EXPECT_EQ(1u, m.source);
  EXPECT_EQ("CREATE TABLE", cut(outline.includes[0].text, m.range));

  // Without a USE, the schema given is the one in use.
  const auto plain = build_outline(expand_sources("SOURCE 'a.sql';\n", "/d",
                                                  files({{"/d/a.sql", "CREATE TABLE t (a INT);\n"}})),
                                   pool(), Index_options{}, oo);
  EXPECT_EQ("start", plain.includes[0].context.schema);
  ASSERT_NE(nullptr, plain.catalog.find("start"));
  EXPECT_EQ(1u, plain.catalog.find("start")->tables.size());
}

}  // namespace mdbls
