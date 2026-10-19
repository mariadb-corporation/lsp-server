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

#include "mdbls/source_include.h"

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

}  // namespace mdbls
