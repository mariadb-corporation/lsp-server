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

// mariadb-lsp: the MariaDB language server and its command-line tools.
//
//   mariadb-lsp lsp              the language server, on stdin and stdout
//   mariadb-lsp check   FILE...  syntax errors, or success
//   mariadb-lsp outline FILE     the schema objects a file defines, as JSON
//   mariadb-lsp split   FILE     the statement index of a SQL file
//   mariadb-lsp tokens  FILE     the tokens of every statement
//   mariadb-lsp tree    FILE     the parse tree of every statement
//   mariadb-lsp complete --at L:C FILE   completion candidates at a position
//   mariadb-lsp connections      the connections the shell's secret store holds

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mdbls/catalog_view.h"
#include "mdbls/charset.h"
#include "mdbls/completion.h"
#include "mdbls/connection_uri.h"
#include "mdbls/live_catalog.h"
#include "mdbls/navigation.h"
#include "mdbls/secret_store.h"
#include "mdbls/line_map.h"
#include "mdbls/lsp/json_position.h"
#include "mdbls/lsp/server.h"
#include "mdbls/lsp/transport.h"
#include "mdbls/outline.h"
#include "mdbls/parser_pool.h"
#include "mdbls/sql_mode.h"
#include "mdbls/statement_index.h"
#include "mdbls/syntax_check.h"
#include "mdbls/syntax_tree.h"
#include "mdbparse.h"

namespace {

constexpr int k_exit_ok = 0;
constexpr int k_exit_errors = 1;
constexpr int k_exit_usage = 2;

const char *const k_usage =
    "usage: mariadb-lsp <command> [options] FILE|-...\n"
    "       mariadb-lsp lsp [--stdio] [--mcp] [--threads N] [--log FILE] [--shell-bin-dir DIR]\n"
    "\n"
    "commands:\n"
    "  lsp      run the language server: LSP over stdin and stdout\n"
    "  check    check the syntax of SQL files: prints the errors, or nothing\n"
    "           (with --format json, a summary); exit 0 when there are none,\n"
    "           1 when there are\n"
    "  outline  the schema objects a SQL file defines, as JSON\n"
    "  split    list the statements of a SQL file\n"
    "  tokens   list the tokens of every statement of a SQL file\n"
    "  tree     print the parse tree of every statement of a SQL file\n"
    "  complete list the completion candidates at --at LINE:COL of a SQL file,\n"
    "           from the file's own schema objects (and --connection's)\n"
    "  connections\n"
    "           list the connections MariaDB Shell's secret store holds for the\n"
    "           MCP server and the GUI, by URI\n"
    "\n"
    "options:\n"
    "  --sql-mode LIST    the sql_mode at the start of the file; SET sql_mode in\n"
    "                     the file is followed from there\n"
    "  --delimiter D      the delimiter at the start of the file (default ;)\n"
    "  --charset NAME     the file's character set: utf8mb4 (default) or latin1;\n"
    "                     a file that is not valid UTF-8 is read as latin1 when it\n"
    "                     declares SET NAMES latin1 before its first non-UTF-8 byte\n"
    "  --version-id N     release id versioned comments are judged against\n"
    "                     (major*10000 + minor*100 + patch; default 130100)\n"
    "  --format tsv|json  output format (default tsv)\n"
    "  --text             split: include each statement's text in the output\n"
    "  --at LINE:COL      complete: the position, 1-based (COL in characters)\n"
    "  --connection URI   check, outline, complete: read the sql_mode, the version\n"
    "                     and the catalog from this server (its password from the\n"
    "                     shell's secret store); check then also reports tables and\n"
    "                     columns the server does not have, as warnings\n"
    "  --shell-bin-dir DIR\n"
    "                     where mariadb-shell and its secret store helpers are\n"
    "                     (default: mariadb-shell on PATH)\n"
    "  --threads N        check: parser threads (default: one per CPU core)\n"
    "  --max-errors N     check: stop listing errors after N per file (default 100)\n"
    "  --log FILE         lsp: write a log to FILE\n"
    "  --stdio            lsp: talk on stdin and stdout (the only transport)\n"
    "  --mcp              lsp: read connections' catalogs through the client, which\n"
    "                     has MariaDB Shell's MCP server (mariadb/dbCall requests),\n"
    "                     instead of connecting itself\n"
    "  --stats            print sizes and timings to stderr\n"
    "  --version          print \"mariadb-lsp <version>\" and exit\n"
    "  --help             this text\n"
    "\n"
    "Lines and columns are 1-based in text output and 0-based (LSP, UTF-16\n"
    "columns) in json output.\n"
    "\n"
    "exit: 0 done (check: no errors), 1 check found errors, 2 usage or\n"
    "      unreadable input\n";

struct Options {
  std::string command;
  std::vector<std::string> files;
  std::optional<mdbls::File_charset> charset;
  unsigned threads = 0;
  size_t max_errors = 100;
  mdbls::Index_options index;
  bool json = false;
  bool text = false;
  bool stats = false;
  std::string log;
  std::optional<mdbls::Position> at;  // 0-based
  std::string connection;
  std::string shell_bin_dir;
  bool mcp = false;
  bool sql_mode_given = false;
  bool version_given = false;
};

// The --connection server's catalog, read once.
std::shared_ptr<const mdbls::Live_snapshot> g_live;

using Clock = std::chrono::steady_clock;
using mdbls::lsp::write_position;

double ms_since(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

bool read_input(const std::string &path, std::string *out) {
  if (path == "-") {
    out->assign(std::istreambuf_iterator<char>(std::cin), {});
    return !std::cin.bad();
  }
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return false;
  const auto size = in.tellg();
  if (size < 0) return false;
  out->resize(static_cast<size_t>(size));
  in.seekg(0);
  return static_cast<bool>(in.read(out->data(), static_cast<std::streamsize>(out->size())));
}

const char *kind_name(mdbls::Statement_kind k) {
  return k == mdbls::Statement_kind::comment ? "comment" : "sql";
}

// "line:column", 1-based, as text output shows a position.
std::string lc(mdbls::Position p) {
  return std::to_string(p.line + 1) + ":" + std::to_string(p.character + 1);
}

// Writes `out` to stdout once it holds a megabyte, so output streams.
void flush_if_large(std::string &out) {
  if (out.size() > (1u << 20)) {
    std::cout << out;
    out.clear();
  }
}

// One line of tsv: control characters shown as \n, \t, \r.
std::string escape_tsv(std::string_view s, size_t max) {
  std::string out;
  for (const char c : s.substr(0, max)) {
    if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out.push_back(c);
    }
  }
  if (s.size() > max) out += "...";
  return out;
}

void run_split(const Options &o, const std::string &text, const mdbls::Line_map &lines,
               const mdbls::Statement_index &index) {
  const auto &stmts = index.statements();
  if (o.json) {
    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> w(buf);
    w.StartObject();
    w.Key("statements");
    w.StartArray();
    for (const auto &s : stmts) {
      w.StartObject();
      w.Key("begin");
      w.Uint(s.begin);
      w.Key("body_end");
      w.Uint(s.body_end);
      w.Key("end");
      w.Uint(s.end);
      w.Key("start");
      write_position(w, lines.position(text, s.begin));
      w.Key("stop");
      write_position(w, lines.position(text, s.end));
      w.Key("kind");
      w.String(kind_name(s.kind));
      w.Key("delimiter");
      w.String(index.delimiters()[s.delimiter].c_str());
      w.Key("sql_mode");
      w.String(index.modes()[s.mode].mode.c_str());
      w.Key("sql_mode_known");
      w.Bool(index.modes()[s.mode].known);
      if (o.text) {
        const auto t = index.text_of(text, s);
        w.Key("text");
        w.String(t.data(), static_cast<rapidjson::SizeType>(t.size()));
      }
      w.EndObject();
    }
    w.EndArray();
    w.Key("open_at_end");
    if (const char *open = mdbls::open_context_name(index.open_at_end())) {
      w.String(open);
    } else {
      w.Null();
    }
    w.Key("errors");
    w.StartArray();
    for (const auto &e : index.errors()) w.String(e.c_str());
    w.EndArray();
    w.EndObject();
    std::cout << buf.GetString() << '\n';
    return;
  }

  std::string out;
  for (const auto &s : stmts) {
    const auto &mode = index.modes()[s.mode];
    out += lc(lines.position(text, s.begin)) + "-" + lc(lines.position(text, s.end)) + "\t" +
           kind_name(s.kind) + "\t" + escape_tsv(index.delimiters()[s.delimiter], 16) + "\t" +
           (mode.mode.empty() ? std::string("-") : mode.mode) + (mode.known ? "" : "?") + "\t" +
           escape_tsv(index.text_of(text, s), o.text ? std::string::npos : 60) + "\n";
    flush_if_large(out);
  }
  std::cout << out;
  if (const char *open = mdbls::open_context_name(index.open_at_end())) {
    std::cerr << "mariadb-lsp: the file ends inside an unterminated " << open << "\n";
  }
  for (const auto &e : index.errors()) std::cerr << "mariadb-lsp: " << e << "\n";
}

void run_tokens(const Options &o, const std::string &text, const mdbls::Line_map &lines,
                const mdbls::Statement_index &index) {
  mdbls::Parser_cache cache;
  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> w(buf);
  std::string out;
  if (o.json) w.StartArray();
  for (size_t i = 0; i < index.statements().size(); ++i) {
    const auto &s = index.statements()[i];
    if (s.kind != mdbls::Statement_kind::sql) continue;
    const auto stmt = index.text_of(text, s);
    // nullptr when the parser refuses the sql_mode.
    mdbp_parser *p = cache.get(index.modes()[s.mode].mode, o.index.version_id);
    mdbp_result r;
    const mdbp_status st =
        p ? mdbp_lex_sql(p, stmt.data(), stmt.size(), &r) : MDBP_UNSUPPORTED;
    if (o.json) {
      w.StartObject();
      w.Key("statement");
      w.Uint64(i);
      w.Key("status");
      w.String(st == MDBP_OK ? "ok" : st == MDBP_UNSUPPORTED ? "unsupported" : "internal");
      w.Key("tokens");
      w.StartArray();
    } else if (st != MDBP_OK) {
      out += lc(lines.position(text, s.begin)) + "\t(not lexed: " +
             (p ? std::string("the statement is not valid UTF-8")
                : "sql_mode " + index.modes()[s.mode].mode + " is not supported") +
             ")\n";
      continue;
    }
    uint32_t last_offset = s.begin;
    mdbls::Position last = lines.position(text, s.begin);
    for (size_t k = 0; st == MDBP_OK && k < r.ntokens; ++k) {
      const auto &t = r.tokens[k];
      const auto begin = static_cast<uint32_t>(s.begin + t.byte_start);
      last = lines.position_after(text, last_offset, last, begin);
      last_offset = begin;
      if (o.json) {
        w.StartObject();
        w.Key("name");
        w.String(t.name);
        w.Key("begin");
        w.Uint(begin);
        w.Key("end");
        w.Uint(static_cast<uint32_t>(s.begin + t.byte_end));
        w.Key("start");
        write_position(w, last);
        w.EndObject();
        continue;
      }
      std::string_view tt;
      if (t.text != nullptr && t.byte_end > t.byte_start) {
        tt = std::string_view(t.text, static_cast<size_t>(t.byte_end - t.byte_start));
      }
      out += lc(last) + "\t" + t.name + "\t" + escape_tsv(tt, 80) + "\n";
    }
    if (o.json) {
      w.EndArray();
      w.EndObject();
    }
    flush_if_large(out);
  }
  if (o.json) {
    w.EndArray();
    std::cout << buf.GetString() << '\n';
  } else {
    std::cout << out;
  }
}

struct Check_summary {
  size_t sql_statements = 0;
  size_t errors = 0;
};

// Checks one file; writes text lines to `out` or one object to `w`.
Check_summary run_check(const Options &o, const std::string &file, const std::string &text,
                        const mdbls::Line_map &lines, const mdbls::Statement_index &index,
                        mdbls::Parser_pool &pool, std::string *out,
                        rapidjson::Writer<rapidjson::StringBuffer> *w) {
  mdbls::Check_options copts;
  copts.version_id = o.index.version_id;
  const auto results = mdbls::check_statements(text, index, pool, copts);

  Check_summary sum;
  struct Note {
    uint32_t offset;
    std::string message;
  };
  std::vector<Note> notes;
  mdbls::Unchecked_notes unchecked(index);
  for (size_t i = 0; i < results.size(); ++i) {
    const auto &st = index.statements()[i];
    if (st.kind == mdbls::Statement_kind::sql) ++sum.sql_statements;
    if (results[i].status == mdbls::Check_status::syntax_error) ++sum.errors;
    if (auto note = unchecked(i, results[i].status); !note.empty()) {
      notes.push_back({st.begin, std::move(note)});
    }
  }
  if (index.open_at_end() != mdbls::Open_context::none) ++sum.errors;
  for (const auto &e : index.errors()) notes.push_back({0, e});

  // With --connection: the tables and columns the server does not have.
  std::vector<mdbls::Name_issue> issues;
  if (g_live) {
    mdbls::Outline_options oo;
    oo.version_id = o.index.version_id;
    auto outline = std::make_shared<mdbls::Outline>(mdbls::build_outline(text, index, pool, oo));
    mdbls::Catalog_view view;
    view.add({std::shared_ptr<const mdbls::Catalog>(outline, &outline->catalog),
              mdbls::Layer_kind::document, {}});
    view.add({g_live->catalog, mdbls::Layer_kind::live, g_live->server.current_schema});
    mdbls::Reference_options ro;
    ro.version_id = o.index.version_id;
    ro.initial_schema = g_live->server.current_schema;
    issues = mdbls::unknown_names(text, index, view, pool, ro);
  }

  auto pos = [&](uint32_t off) { return lines.position(text, off); };
  if (w != nullptr) {
    w->StartObject();
    w->Key("file");
    w->String(file.c_str());
    w->Key("statements");
    w->Uint64(sum.sql_statements);
    w->Key("errors");
    w->StartArray();
    size_t listed = 0;
    for (size_t i = 0; i < results.size() && listed < o.max_errors; ++i) {
      const auto &c = results[i];
      if (c.status != mdbls::Check_status::syntax_error) continue;
      ++listed;
      w->StartObject();
      w->Key("start");
      write_position(*w, pos(c.error_offset));
      w->Key("offset");
      w->Uint(c.error_offset);
      w->Key("length");
      w->Uint(c.error_length);
      w->Key("token");
      w->String(c.error_token.c_str());
      w->Key("message");
      w->String(mdbls::syntax_error_near(text, c).c_str());
      w->Key("expected");
      w->StartArray();
      for (const auto &e : mdbls::readable_expected(c.expected)) w->String(e.c_str());
      w->EndArray();
      w->EndObject();
    }
    if (const char *open = mdbls::open_context_name(index.open_at_end())) {
      w->StartObject();
      w->Key("start");
      write_position(*w, pos(static_cast<uint32_t>(text.size())));
      w->Key("offset");
      w->Uint(static_cast<uint32_t>(text.size()));
      w->Key("length");
      w->Uint(0);
      w->Key("token");
      w->String("$end");
      w->Key("message");
      w->String((std::string("the file ends inside an unterminated ") + open).c_str());
      w->Key("expected");
      w->StartArray();
      w->EndArray();
      w->EndObject();
    }
    w->EndArray();
    if (g_live) {
      w->Key("warnings");
      w->StartArray();
      for (const auto &is : issues) {
        w->StartObject();
        w->Key("start");
        write_position(*w, pos(is.range.begin));
        w->Key("offset");
        w->Uint(is.range.begin);
        w->Key("length");
        w->Uint(is.range.end - is.range.begin);
        w->Key("message");
        w->String(is.message.c_str());
        w->EndObject();
      }
      w->EndArray();
    }
    w->Key("notes");
    w->StartArray();
    for (const auto &n : notes) {
      w->StartObject();
      w->Key("start");
      write_position(*w, pos(n.offset));
      w->Key("message");
      w->String(n.message.c_str());
      w->EndObject();
    }
    w->EndArray();
    w->EndObject();
    return sum;
  }

  auto where = [&](uint32_t off) { return file + ":" + lc(pos(off)); };
  size_t listed = 0;
  for (size_t i = 0; i < results.size(); ++i) {
    const auto &c = results[i];
    if (c.status != mdbls::Check_status::syntax_error) continue;
    if (listed++ == o.max_errors) {
      *out += file + ": more errors not listed\n";
      break;
    }
    *out += where(c.error_offset) + ": error: " + mdbls::syntax_error_message(text, c);
    *out += "\n";
  }
  if (const char *open = mdbls::open_context_name(index.open_at_end())) {
    *out += where(static_cast<uint32_t>(text.size())) +
            ": error: the file ends inside an unterminated " + open + "\n";
  }
  for (const auto &is : issues) *out += where(is.range.begin) + ": warning: " + is.message + "\n";
  for (const auto &n : notes) *out += where(n.offset) + ": note: " + n.message + "\n";
  return sum;
}

// A file read from disk, as UTF-8.  Returns false, after printing why, when
// the file is not valid UTF-8 and nothing says it is latin1.
bool to_utf8(const Options &o, const std::string &file, std::string *text,
             std::string *charset, std::vector<std::string> *notes) {
  // Line and column of `off`; the text before it must be valid UTF-8 (it is
  // the first invalid byte, or a position in converted text).
  auto where = [&](const std::string &t, size_t off) {
    const mdbls::Line_map lines(t);
    const uint32_t line = lines.line_of(static_cast<uint32_t>(off));
    const uint32_t start = lines.line_start(line);
    const uint32_t col = mdbls::utf16_length(std::string_view(t).substr(start, off - start));
    return file + ":" + lc({line, col});
  };
  bool latin1 = o.charset == mdbls::File_charset::latin1;
  const auto bad = latin1 ? std::nullopt : mdbls::first_invalid_utf8(*text);
  if (bad && !o.charset) {
    const auto declared = mdbls::declared_charset(*text, *bad);
    if (declared && mdbls::parse_charset_name(*declared) == mdbls::File_charset::latin1) {
      latin1 = true;
      notes->push_back("read as latin1, as its SET NAMES " + *declared + " declares");
    }
  }
  if (bad && !latin1) {
    char byte[8];
    std::snprintf(byte, sizeof byte, "0x%02X", static_cast<unsigned char>((*text)[*bad]));
    std::cerr << where(*text, *bad) << ": error: the file is not valid UTF-8 (byte " << byte
              << "); if it is Latin-1, use --charset latin1\n";
    return false;
  }
  if (!latin1) {
    *charset = "utf8mb4";
    return true;
  }
  *text = mdbls::latin1_to_utf8(*text);
  *charset = "latin1";
  // The whole file is read as latin1; say where it declares something else.
  for (const auto &d : mdbls::charset_declarations(*text)) {
    if (mdbls::parse_charset_name(d.name) != mdbls::File_charset::latin1) {
      notes->push_back(where(*text, d.offset) + ": note: the file declares SET NAMES " + d.name +
                       " here; it is read as latin1 throughout");
    }
  }
  return true;
}

// The parse tree of every statement, indented, tokens with their text.
void run_tree(const Options &o, const std::string &text, const mdbls::Line_map &lines,
              const mdbls::Statement_index &index) {
  mdbls::Parser_cache cache;
  std::string out;
  for (const auto &s : index.statements()) {
    if (s.kind != mdbls::Statement_kind::sql) continue;
    out += "-- statement at " + lc(lines.position(text, s.begin)) + "\n";
    mdbp_parser *p = cache.get(index.modes()[s.mode].mode, o.index.version_id, true);
    if (p == nullptr) {
      out += "(not parsed: sql_mode " + index.modes()[s.mode].mode + " is not supported)\n";
      continue;
    }
    const auto sql = index.text_of(text, s);
    mdbp_result r;
    mdbp_parse_sql(p, sql.data(), sql.size(), &r);
    const auto tree = mdbls::Syntax_tree::build(p, r, s.begin);
    if (!r.accepted) out += "(rejected; the trees built before the error follow)\n";
    std::vector<std::pair<uint32_t, int>> todo;
    for (auto it = tree.roots().rbegin(); it != tree.roots().rend(); ++it) todo.push_back({*it, 0});
    while (!todo.empty()) {
      const auto [n, depth] = todo.back();
      todo.pop_back();
      out.append(static_cast<size_t>(depth) * 2, ' ');
      out += tree.name(n);
      if (tree.is_token(n)) out += "  " + escape_tsv(tree.text(text, n), 60);
      out += "\n";
      const auto ch = tree.children(n);
      for (auto it = ch.rbegin(); it != ch.rend(); ++it) todo.push_back({*it, depth + 1});
    }
    flush_if_large(out);
  }
  std::cout << out;
}

void run_complete(const Options &o, const std::string &text, const mdbls::Line_map &lines,
                  const mdbls::Statement_index &index, mdbls::Parser_pool &pool) {
  mdbls::Outline_options oo;
  oo.version_id = o.index.version_id;
  auto outline = std::make_shared<mdbls::Outline>(mdbls::build_outline(text, index, pool, oo));
  const uint32_t offset = lines.offset(text, *o.at);
  mdbls::Catalog_view view;
  view.add({std::shared_ptr<const mdbls::Catalog>(outline, &outline->catalog),
            mdbls::Layer_kind::document, {}});
  if (g_live) view.add({g_live->catalog, mdbls::Layer_kind::live, g_live->server.current_schema});
  const auto at = mdbls::statement_at(index, offset);
  view.set_current_schema(mdbls::schema_in_use(text, index, at ? *at : index.find(offset)));
  mdbls::Parser_cache cache;
  mdbls::Completion_options co;
  co.version_id = o.index.version_id;
  auto t0 = Clock::now();
  const auto list = mdbls::complete(text, index, offset, view, cache, co);
  const double first_ms = ms_since(t0);
  // With --stats, the time of a warm call too: the first one also builds
  // the keyword tables and the parsers.
  std::vector<double> warm;
  for (int k = 0; o.stats && k < 20; ++k) {
    t0 = Clock::now();
    (void)mdbls::complete(text, index, offset, view, cache, co);
    warm.push_back(ms_since(t0));
  }
  std::sort(warm.begin(), warm.end());
  if (o.json) {
    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> w(buf);
    w.StartObject();
    w.Key("context");
    w.String(mdbls::name_kind_name(list.context));
    w.Key("prefix");
    w.String(list.prefix.c_str());
    w.Key("replace");
    mdbls::lsp::write_range(w, text, lines, list.replace_begin, list.replace_end);
    w.Key("incomplete");
    w.Bool(list.incomplete);
    w.Key("items");
    w.StartArray();
    for (const auto &it : list.items) {
      w.StartObject();
      w.Key("label");
      w.String(it.label.c_str());
      w.Key("kind");
      w.String(mdbls::completion_kind_name(it.kind));
      w.Key("detail");
      w.String(it.detail.c_str());
      w.Key("insert");
      w.String(it.insert_text.c_str());
      w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    std::cout << buf.GetString() << '\n';
  } else {
    std::cout << "# context " << mdbls::name_kind_name(list.context) << ", prefix '"
              << list.prefix << "', " << list.items.size() << " items"
              << (list.incomplete ? " (more)" : "") << "\n";
    for (const auto &it : list.items) {
      std::cout << mdbls::completion_kind_name(it.kind) << '\t' << it.label << '\t'
                << escape_tsv(it.detail, 80) << '\n';
    }
  }
  if (o.stats) {
    std::fprintf(stderr, "complete: first %.2f ms, warm median %.2f ms, max %.2f ms\n", first_ms,
                 warm[warm.size() / 2], warm.back());
  }
}

std::optional<Options> parse_args(int argc, char **argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&]() -> std::optional<std::string> {
      if (i + 1 >= argc) {
        std::cerr << "mariadb-lsp: " << arg << " needs a value\n";
        return std::nullopt;
      }
      return std::string(argv[++i]);
    };
    // The value as a decimal number; nullopt, after saying why, when it is not.
    auto number = [&]() -> std::optional<unsigned long> {
      const auto v = value();
      if (!v) return std::nullopt;
      char *end = nullptr;
      const unsigned long n = std::strtoul(v->c_str(), &end, 10);
      if (v->empty() || *end != '\0') {
        std::cerr << "mariadb-lsp: " << arg << " needs a number\n";
        return std::nullopt;
      }
      return n;
    };
    if (arg == "--help" || arg == "-h") {
      std::cout << k_usage;
      std::exit(k_exit_ok);
    } else if (arg == "--version") {
      std::cout << "mariadb-lsp " MDBLS_VERSION "\n";
      std::exit(k_exit_ok);
    } else if (arg == "--sql-mode") {
      const auto v = value();
      if (!v) return std::nullopt;
      o.index.sql_mode = *v;
      o.sql_mode_given = true;
    } else if (arg == "--charset") {
      const auto v = value();
      if (!v) return std::nullopt;
      o.charset = mdbls::parse_charset_name(*v);
      if (!o.charset) {
        std::cerr << "mariadb-lsp: --charset is utf8mb4 or latin1\n";
        return std::nullopt;
      }
    } else if (arg == "--delimiter") {
      const auto v = value();
      if (!v || v->empty()) return std::nullopt;
      o.index.delimiter = *v;
    } else if (arg == "--version-id") {
      const auto n = number();
      if (!n) return std::nullopt;
      if (!mdbls::version_id_supported(*n)) {
        std::cerr << "mariadb-lsp: --version-id " << *n
                  << " is not a release id the parser accepts (major*10000 + minor*100 + patch)\n";
        return std::nullopt;
      }
      o.index.version_id = *n;
      o.version_given = true;
    } else if (arg == "--connection") {
      const auto v = value();
      if (!v) return std::nullopt;
      if (!mdbls::parse_connection_uri(*v)) {
        std::cerr << "mariadb-lsp: --connection is not a connection URI\n";
        return std::nullopt;
      }
      o.connection = *v;
    } else if (arg == "--shell-bin-dir") {
      const auto v = value();
      if (!v) return std::nullopt;
      o.shell_bin_dir = *v;
    } else if (arg == "--threads") {
      const auto n = number();
      if (!n) return std::nullopt;
      o.threads = static_cast<unsigned>(*n);
    } else if (arg == "--max-errors") {
      const auto n = number();
      if (!n) return std::nullopt;
      o.max_errors = *n;
    } else if (arg == "--format") {
      auto v = value();
      if (!v || (*v != "tsv" && *v != "json")) {
        std::cerr << "mariadb-lsp: --format is tsv or json\n";
        return std::nullopt;
      }
      o.json = *v == "json";
    } else if (arg == "--log") {
      const auto v = value();
      if (!v) return std::nullopt;
      o.log = *v;
    } else if (arg == "--at") {
      const auto v = value();
      if (!v) return std::nullopt;
      unsigned line = 0, col = 0;
      if (std::sscanf(v->c_str(), "%u:%u", &line, &col) != 2 || line == 0 || col == 0) {
        std::cerr << "mariadb-lsp: --at is LINE:COL, both from 1\n";
        return std::nullopt;
      }
      o.at = mdbls::Position{line - 1, col - 1};
    } else if (arg == "--stdio") {
      // The default and only transport.
    } else if (arg == "--mcp") {
      o.mcp = true;
    } else if (arg == "--text") {
      o.text = true;
    } else if (arg == "--stats") {
      o.stats = true;
    } else if (o.command.empty() && arg.rfind("-", 0) != 0) {
      o.command = arg;
    } else if (arg == "-" || arg.rfind("-", 0) != 0) {
      o.files.push_back(arg);
    } else {
      std::cerr << "mariadb-lsp: unexpected argument " << arg << "\n";
      return std::nullopt;
    }
  }
  if (o.command != "split" && o.command != "tokens" && o.command != "check" &&
      o.command != "tree" && o.command != "outline" && o.command != "lsp" &&
      o.command != "complete" && o.command != "connections") {
    std::cerr << (o.command.empty() ? std::string("mariadb-lsp: no command given\n")
                                    : "mariadb-lsp: unknown command " + o.command + "\n")
              << k_usage;
    return std::nullopt;
  }
  if (o.command == "lsp" || o.command == "connections") {
    if (!o.files.empty()) {
      std::cerr << "mariadb-lsp: " << o.command << " takes no files\n";
      return std::nullopt;
    }
    return o;
  }
  if (o.files.empty()) {
    std::cerr << "mariadb-lsp: no input file (use - for stdin)\n";
    return std::nullopt;
  }
  if (o.command == "complete" && !o.at) {
    std::cerr << "mariadb-lsp: complete needs --at LINE:COL\n";
    return std::nullopt;
  }
  if (o.command != "check" && o.files.size() > 1) {
    std::cerr << "mariadb-lsp: " << o.command << " takes one file\n";
    return std::nullopt;
  }
  return o;
}

int run_lsp(const Options &o) {
  std::ofstream log;
  mdbls::lsp::Server_options so;
  so.threads = o.threads;
  so.defaults = o.index;
  so.defaults.sql_mode = mdbls::normalize_sql_mode(o.index.sql_mode);
  so.shell_bin_dir = o.shell_bin_dir;
  so.mcp = o.mcp;
  if (!o.log.empty()) {
    log.open(o.log, std::ios::app);
    if (!log) {
      std::cerr << "mariadb-lsp: cannot open " << o.log << "\n";
      return k_exit_usage;
    }
    so.log = &log;
  }
  mdbls::lsp::Fd_input in(0);
  mdbls::lsp::Fd_output out(1);
  mdbls::lsp::Server server(in, out, so);
  return server.run();
}

}  // namespace

// mariadb-lsp connections
int run_connections(const Options &o) {
  const mdbls::Secret_store store({o.shell_bin_dir, ""});
  if (!store.error().empty()) {
    std::cerr << "mariadb-lsp: " << store.error() << "\n";
    return k_exit_usage;
  }
  std::string error;
  const auto conns = store.connections(&error);
  if (!error.empty()) {
    std::cerr << "mariadb-lsp: " << error << "\n";
    return k_exit_usage;
  }
  if (o.json) {
    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> w(buf);
    w.StartArray();
    for (const auto &c : conns) {
      w.StartObject();
      w.Key("kind");
      w.String(c.kind.c_str());
      w.Key("uri");
      w.String(c.uri.c_str());
      w.Key("normalized");
      const auto n = mdbls::normalize_connection_uri(c.uri);
      if (n) {
        w.String(n->c_str());
      } else {
        w.Null();
      }
      w.EndObject();
    }
    w.EndArray();
    std::cout << buf.GetString() << '\n';
  } else {
    for (const auto &c : conns) std::cout << c.kind << '\t' << c.uri << '\n';
  }
  return k_exit_ok;
}

// A URI as it may be shown: normalized, so without its password.
std::string redacted(const std::string &uri) {
  return mdbls::normalize_connection_uri(uri).value_or("(the URI)");
}

// --connection: reads the server's catalog, sql_mode and version once.
bool read_connection(Options &o) {
  const auto uri = mdbls::parse_connection_uri(o.connection);
  std::string error;
  std::optional<std::string> password = uri->password;
  if (!password) {
    const mdbls::Secret_store store({o.shell_bin_dir, ""});
    password = store.password_for(o.connection, nullptr, &error);
    if (!password) {
      std::cerr << "mariadb-lsp: --connection " << redacted(o.connection) << ": " << error << "\n";
      return false;
    }
  }
  auto snap = mdbls::fetch_live_catalog(*uri, std::move(*password), &error);
  if (!snap) {
    std::cerr << "mariadb-lsp: --connection " << redacted(o.connection) << ": " << error << "\n";
    return false;
  }
  if (!o.sql_mode_given) o.index.sql_mode = mdbls::normalize_sql_mode(snap->server.sql_mode);
  if (!o.version_given && mdbls::version_id_supported(snap->server.version_id)) {
    o.index.version_id = snap->server.version_id;
  }
  if (o.stats) {
    std::fprintf(stderr, "connection: server %s, sql_mode %s, %zu tables, %zu columns\n",
                 snap->server.version.c_str(), snap->server.sql_mode.c_str(), snap->tables,
                 snap->columns);
  }
  g_live = std::make_shared<const mdbls::Live_snapshot>(std::move(*snap));
  return true;
}

int main(int argc, char **argv) {
  auto o = parse_args(argc, argv);
  if (!o) return k_exit_usage;
  if (o->command == "lsp") return run_lsp(*o);
  if (o->command == "connections") return run_connections(*o);
  if (!o->connection.empty() && !read_connection(*o)) return k_exit_usage;

  std::unique_ptr<mdbls::Parser_pool> pool;
  if (o->command == "check" || o->command == "outline" || o->command == "complete") {
    pool = std::make_unique<mdbls::Parser_pool>(o->threads);
  }

  rapidjson::StringBuffer json_buf;
  rapidjson::Writer<rapidjson::StringBuffer> json(json_buf);
  if (o->command == "check" && o->json) {
    json.StartObject();
    json.Key("files");
    json.StartArray();
  }

  size_t total_errors = 0;
  bool unreadable = false;
  for (const auto &file : o->files) {
    auto t0 = Clock::now();
    std::string text;
    if (!read_input(file, &text)) {
      std::cerr << "mariadb-lsp: cannot read " << file << "\n";
      return k_exit_usage;
    }
    std::string charset;
    std::vector<std::string> charset_notes;
    if (!to_utf8(*o, file, &text, &charset, &charset_notes)) {
      unreadable = true;
      continue;
    }
    for (const auto &n : charset_notes) {
      std::cerr << (n.rfind(file + ":", 0) == 0 ? n : file + ": note: " + n) << "\n";
    }
    const double read_ms = ms_since(t0);

    t0 = Clock::now();
    const mdbls::Line_map lines(text);
    const double lines_ms = ms_since(t0);

    t0 = Clock::now();
    const auto index = mdbls::Statement_index::build_parallel(text, o->index, o->threads);
    const double index_ms = ms_since(t0);

    t0 = Clock::now();
    if (o->command == "split") {
      run_split(*o, text, lines, index);
    } else if (o->command == "tokens") {
      run_tokens(*o, text, lines, index);
    } else if (o->command == "tree") {
      run_tree(*o, text, lines, index);
    } else if (o->command == "complete") {
      run_complete(*o, text, lines, index, *pool);
    } else if (o->command == "outline") {
      mdbls::Outline_options oo;
      oo.version_id = o->index.version_id;
      const auto outline = mdbls::build_outline(text, index, *pool, oo);
      std::cout << mdbls::outline_json(outline, text, lines, file) << '\n';
    } else {
      std::string out;
      const auto sum = run_check(*o, file, text, lines, index, *pool, &out,
                                 o->json ? &json : nullptr);
      std::cout << out;
      total_errors += sum.errors;
    }
    std::cout.flush();
    const double output_ms = ms_since(t0);

    if (o->stats) {
      std::fprintf(stderr,
                   "%s: bytes %zu, lines %u, statements %zu, sql_modes %zu\n"
                   "read %.1f ms, line map %.1f ms, index %.1f ms, %s %.1f ms%s\n",
                   file.c_str(), text.size(), lines.line_count(), index.statements().size(),
                   index.modes().size(), read_ms, lines_ms, index_ms, o->command.c_str(),
                   output_ms,
                   pool ? (" (" + std::to_string(pool->max_threads()) + " threads)").c_str() : "");
    }
  }
  if (o->command == "check" && o->json) {
    json.EndArray();
    json.Key("errors");
    json.Uint64(total_errors);
    json.EndObject();
    std::cout << json_buf.GetString() << '\n';
  }
  // Only check counts errors.
  if (unreadable) return k_exit_usage;
  return total_errors > 0 ? k_exit_errors : k_exit_ok;
}
