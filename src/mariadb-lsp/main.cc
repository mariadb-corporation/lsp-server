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
//   mariadb-lsp split  FILE   the statement index of a SQL file
//   mariadb-lsp tokens FILE   the tokens of every statement
//
// The LSP mode (`lsp`), `check` and `outline` follow in later phases.

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mdbls/line_map.h"
#include "mdbls/statement_index.h"
#include "mdbparse.h"

namespace {

constexpr int k_exit_ok = 0;
constexpr int k_exit_usage = 2;

const char *const k_usage =
    "usage: mariadb-lsp <command> [options] FILE|-\n"
    "\n"
    "commands:\n"
    "  split    list the statements of a SQL file\n"
    "  tokens   list the tokens of every statement of a SQL file\n"
    "\n"
    "options:\n"
    "  --sql-mode LIST    the sql_mode at the start of the file; SET sql_mode in\n"
    "                     the file is followed from there\n"
    "  --delimiter D      the delimiter at the start of the file (default ;)\n"
    "  --version-id N     release id versioned comments are judged against\n"
    "                     (major*10000 + minor*100 + patch; default 130100)\n"
    "  --format tsv|json  output format (default tsv)\n"
    "  --text             split: include each statement's text in the output\n"
    "  --stats            print sizes and timings to stderr\n"
    "  --help             this text\n"
    "\n"
    "Lines and columns are 1-based in tsv output and 0-based (LSP, UTF-16\n"
    "columns) in json output.\n";

struct Options {
  std::string command;
  std::string file;
  mdbls::Index_options index;
  bool json = false;
  bool text = false;
  bool stats = false;
};

using Clock = std::chrono::steady_clock;

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

const char *open_name(mdbls::Open_context c) {
  switch (c) {
    case mdbls::Open_context::none:
      return nullptr;
    case mdbls::Open_context::single_quote_string:
      return "single-quoted string";
    case mdbls::Open_context::double_quote_string:
      return "double-quoted string";
    case mdbls::Open_context::backtick_identifier:
      return "backtick-quoted identifier";
    case mdbls::Open_context::double_quote_identifier:
      return "double-quoted identifier";
    case mdbls::Open_context::comment:
      return "comment";
    default:
      return "construct";
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

void write_position(rapidjson::Writer<rapidjson::StringBuffer> &w, mdbls::Position p) {
  w.StartObject();
  w.Key("line");
  w.Uint(p.line);
  w.Key("character");
  w.Uint(p.character);
  w.EndObject();
}

int run_split(const Options &o, const std::string &text, const mdbls::Line_map &lines,
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
    if (const char *open = open_name(index.open_at_end())) {
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
    return k_exit_ok;
  }

  std::string out;
  for (const auto &s : stmts) {
    const auto a = lines.position(text, s.begin);
    const auto b = lines.position(text, s.end);
    const auto &mode = index.modes()[s.mode];
    out += std::to_string(a.line + 1) + ":" + std::to_string(a.character + 1) + "-" +
           std::to_string(b.line + 1) + ":" + std::to_string(b.character + 1) + "\t" +
           kind_name(s.kind) + "\t" + escape_tsv(index.delimiters()[s.delimiter], 16) + "\t" +
           (mode.mode.empty() ? std::string("-") : mode.mode) + (mode.known ? "" : "?") + "\t" +
           escape_tsv(index.text_of(text, s), o.text ? std::string::npos : 60) + "\n";
    if (out.size() > (1u << 20)) {
      std::cout << out;
      out.clear();
    }
  }
  std::cout << out;
  if (const char *open = open_name(index.open_at_end())) {
    std::cerr << "mariadb-lsp: the file ends inside an unterminated " << open << "\n";
  }
  for (const auto &e : index.errors()) std::cerr << "mariadb-lsp: " << e << "\n";
  return k_exit_ok;
}

struct Parser_deleter {
  void operator()(mdbp_parser *p) const { mdbp_parser_free(p); }
};

int run_tokens(const Options &o, const std::string &text, const mdbls::Line_map &lines,
               const mdbls::Statement_index &index) {
  // One parser per distinct sql_mode; nullptr when the parser refuses it.
  std::map<uint16_t, std::unique_ptr<mdbp_parser, Parser_deleter>> parsers;
  auto parser_for = [&](uint16_t mode) -> mdbp_parser * {
    auto it = parsers.find(mode);
    if (it == parsers.end()) {
      mdbp_options opts;
      mdbp_options_init(&opts);
      opts.sql_mode = index.modes()[mode].mode.c_str();
      opts.want_derivation = 0;
      std::unique_ptr<mdbp_parser, Parser_deleter> p{mdbp_parser_new(&opts)};
      if (p) mdbp_parser_set_version_id(p.get(), o.index.version_id);
      it = parsers.emplace(mode, std::move(p)).first;
    }
    return it->second.get();
  };

  rapidjson::StringBuffer buf;
  rapidjson::Writer<rapidjson::StringBuffer> w(buf);
  std::string out;
  if (o.json) w.StartArray();
  for (size_t i = 0; i < index.statements().size(); ++i) {
    const auto &s = index.statements()[i];
    if (s.kind != mdbls::Statement_kind::sql) continue;
    const auto stmt = index.text_of(text, s);
    mdbp_parser *p = parser_for(s.mode);
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
      uint32_t last_offset = s.begin;
      mdbls::Position last = lines.position(text, s.begin);
      for (size_t k = 0; st == MDBP_OK && k < r.ntokens; ++k) {
        const auto &t = r.tokens[k];
        const auto begin = static_cast<uint32_t>(s.begin + t.byte_start);
        last = lines.position_after(text, last_offset, last, begin);
        last_offset = begin;
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
      }
      w.EndArray();
      w.EndObject();
      continue;
    }
    if (st != MDBP_OK) {
      const auto a = lines.position(text, s.begin);
      out += std::to_string(a.line + 1) + ":" + std::to_string(a.character + 1) +
             "\t(not lexed: " +
             (p ? std::string("the statement is not valid UTF-8")
                : "sql_mode " + index.modes()[s.mode].mode + " is not supported") +
             ")\n";
      continue;
    }
    uint32_t last_offset = s.begin;
    mdbls::Position last = lines.position(text, s.begin);
    for (size_t k = 0; k < r.ntokens; ++k) {
      const auto &t = r.tokens[k];
      const auto off = static_cast<uint32_t>(s.begin + t.byte_start);
      const auto pos = lines.position_after(text, last_offset, last, off);
      last_offset = off;
      last = pos;
      std::string_view tt;
      if (t.text != nullptr && t.byte_end > t.byte_start) {
        tt = std::string_view(t.text, static_cast<size_t>(t.byte_end - t.byte_start));
      }
      out += std::to_string(pos.line + 1) + ":" + std::to_string(pos.character + 1) + "\t" +
             t.name + "\t" + escape_tsv(tt, 80) + "\n";
    }
    if (out.size() > (1u << 20)) {
      std::cout << out;
      out.clear();
    }
  }
  if (o.json) {
    w.EndArray();
    std::cout << buf.GetString() << '\n';
  } else {
    std::cout << out;
  }
  return k_exit_ok;
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
    if (arg == "--help" || arg == "-h") {
      std::cout << k_usage;
      std::exit(k_exit_ok);
    } else if (arg == "--sql-mode") {
      auto v = value();
      if (!v) return std::nullopt;
      o.index.sql_mode = *v;
    } else if (arg == "--delimiter") {
      auto v = value();
      if (!v || v->empty()) return std::nullopt;
      o.index.delimiter = *v;
    } else if (arg == "--version-id") {
      auto v = value();
      if (!v) return std::nullopt;
      char *end = nullptr;
      o.index.version_id = std::strtoul(v->c_str(), &end, 10);
      if (v->empty() || *end != '\0') {
        std::cerr << "mariadb-lsp: --version-id needs a number\n";
        return std::nullopt;
      }
    } else if (arg == "--format") {
      auto v = value();
      if (!v || (*v != "tsv" && *v != "json")) {
        std::cerr << "mariadb-lsp: --format is tsv or json\n";
        return std::nullopt;
      }
      o.json = *v == "json";
    } else if (arg == "--text") {
      o.text = true;
    } else if (arg == "--stats") {
      o.stats = true;
    } else if (o.command.empty() && arg.rfind("-", 0) != 0) {
      o.command = arg;
    } else if (o.file.empty() && (arg == "-" || arg.rfind("-", 0) != 0)) {
      o.file = arg;
    } else {
      std::cerr << "mariadb-lsp: unexpected argument " << arg << "\n";
      return std::nullopt;
    }
  }
  if (o.command != "split" && o.command != "tokens") {
    std::cerr << (o.command.empty() ? std::string("mariadb-lsp: no command given\n")
                                    : "mariadb-lsp: unknown command " + o.command + "\n")
              << k_usage;
    return std::nullopt;
  }
  if (o.file.empty()) {
    std::cerr << "mariadb-lsp: no input file (use - for stdin)\n";
    return std::nullopt;
  }
  return o;
}

}  // namespace

int main(int argc, char **argv) {
  const auto o = parse_args(argc, argv);
  if (!o) return k_exit_usage;

  auto t0 = Clock::now();
  std::string text;
  if (!read_input(o->file, &text)) {
    std::cerr << "mariadb-lsp: cannot read " << o->file << "\n";
    return k_exit_usage;
  }
  const double read_ms = ms_since(t0);

  t0 = Clock::now();
  const mdbls::Line_map lines(text);
  const double lines_ms = ms_since(t0);

  t0 = Clock::now();
  const auto index = mdbls::Statement_index::build(text, o->index);
  const double index_ms = ms_since(t0);

  t0 = Clock::now();
  const int rc = o->command == "split" ? run_split(*o, text, lines, index)
                                       : run_tokens(*o, text, lines, index);
  std::cout.flush();
  const double output_ms = ms_since(t0);

  if (o->stats) {
    std::fprintf(stderr,
                 "bytes %zu, lines %u, statements %zu, sql_modes %zu\n"
                 "read %.1f ms, line map %.1f ms, index %.1f ms, %s %.1f ms\n",
                 text.size(), lines.line_count(), index.statements().size(),
                 index.modes().size(), read_ms, lines_ms, index_ms, o->command.c_str(),
                 output_ms);
  }
  return rc;
}
