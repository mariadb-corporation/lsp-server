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
//   mariadb-lsp check  FILE...  syntax errors, or success
//   mariadb-lsp split  FILE     the statement index of a SQL file
//   mariadb-lsp tokens FILE     the tokens of every statement
//
// The LSP mode (`lsp`) and `outline` follow in later phases.

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
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "mdbls/charset.h"
#include "mdbls/line_map.h"
#include "mdbls/parser_pool.h"
#include "mdbls/statement_index.h"
#include "mdbls/syntax_check.h"
#include "mdbparse.h"

namespace {

constexpr int k_exit_ok = 0;
constexpr int k_exit_errors = 1;
constexpr int k_exit_usage = 2;

const char *const k_usage =
    "usage: mariadb-lsp <command> [options] FILE|-...\n"
    "\n"
    "commands:\n"
    "  check    check the syntax of SQL files: prints the errors, or nothing\n"
    "           (with --format json, a summary); exit 0 when there are none,\n"
    "           1 when there are\n"
    "  split    list the statements of a SQL file\n"
    "  tokens   list the tokens of every statement of a SQL file\n"
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
    "  --threads N        check: parser threads (default: one per CPU core)\n"
    "  --max-errors N     check: stop listing errors after N per file (default 100)\n"
    "  --stats            print sizes and timings to stderr\n"
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

// A terminal name as a reader would write it: SELECT_SYM -> SELECT,
// '(' -> (, IDENT -> identifier.  Empty for terminals nobody types.
std::string readable_terminal(std::string_view name) {
  if (name == "$end" || name == "END_OF_INPUT") return "end of statement";
  if (name.size() == 3 && name.front() == '\'' && name.back() == '\'') {
    return std::string(name.substr(1, 1));
  }
  if (name == "IDENT" || name == "IDENT_QUOTED" || name == "LEX_HOSTNAME") return "identifier";
  if (name == "TEXT_STRING" || name == "NCHAR_STRING" || name == "UNDERSCORE_CHARSET")
    return "string";
  if (name == "NUM" || name == "LONG_NUM" || name == "ULONGLONG_NUM" || name == "DECIMAL_NUM" ||
      name == "FLOAT_NUM" || name == "HEX_NUM" || name == "HEX_STRING" || name == "BIN_NUM")
    return "number";
  if (name == "PARAM_MARKER") return "?";
  if (name.rfind("LEFT_PAREN", 0) == 0) return "(";
  if (name == "HINT_COMMENT" || name == "ABORT_SYM" || name == "FORCE_LOOKAHEAD" ||
      name == "IMPOSSIBLE_ACTION" || name == "error" || name.find("_ORACLE_") != std::string_view::npos)
    return "";
  std::string out(name);
  for (const char *suffix : {"_SYM", "_SYMBOL"}) {
    const std::string_view sfx(suffix);
    if (out.size() > sfx.size() && out.compare(out.size() - sfx.size(), sfx.size(), sfx) == 0) {
      out.resize(out.size() - sfx.size());
      break;
    }
  }
  return out;
}

std::vector<std::string> readable_expected(const std::vector<std::string> &expected) {
  std::vector<std::string> out;
  for (const auto &e : expected) {
    auto r = readable_terminal(e);
    if (!r.empty() && std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
  }
  // Punctuation and word classes first, keywords after, as a reader scans them.
  std::stable_sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) {
    const bool ka = !a.empty() && std::isupper(static_cast<unsigned char>(a[0]));
    const bool kb = !b.empty() && std::isupper(static_cast<unsigned char>(b[0]));
    return ka < kb;
  });
  return out;
}

std::string near_text(std::string_view text, const mdbls::Statement_check &c) {
  if (c.error_length == 0) return "end of statement";
  std::string t(text.substr(c.error_offset, std::min<uint32_t>(c.error_length, 40)));
  for (auto &ch : t) {
    if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
  }
  return "'" + t + (c.error_length > 40 ? "...'" : "'");
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
  std::string last_unsupported;
  for (size_t i = 0; i < results.size(); ++i) {
    const auto &st = index.statements()[i];
    if (st.kind == mdbls::Statement_kind::sql) ++sum.sql_statements;
    switch (results[i].status) {
      case mdbls::Check_status::syntax_error:
        ++sum.errors;
        break;
      case mdbls::Check_status::invalid_utf8:
        notes.push_back({st.begin, "statement not checked: it is not valid UTF-8"});
        break;
      case mdbls::Check_status::unsupported_mode: {
        const auto &mode = index.modes()[st.mode].mode;
        if (mode != last_unsupported) {
          notes.push_back({st.begin, "statements not checked from here: sql_mode " + mode +
                                         " is not supported"});
          last_unsupported = mode;
        }
        break;
      }
      case mdbls::Check_status::internal_error:
        notes.push_back({st.begin, "statement not checked: internal parser error"});
        break;
      default:
        if (st.kind == mdbls::Statement_kind::sql) last_unsupported.clear();
        break;
    }
  }
  if (index.open_at_end() != mdbls::Open_context::none) ++sum.errors;
  for (const auto &e : index.errors()) notes.push_back({0, e});

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
      w->String(("syntax error near " + near_text(text, c)).c_str());
      w->Key("expected");
      w->StartArray();
      for (const auto &e : readable_expected(c.expected)) w->String(e.c_str());
      w->EndArray();
      w->EndObject();
    }
    if (const char *open = open_name(index.open_at_end())) {
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

  auto where = [&](uint32_t off) {
    const auto p = pos(off);
    return file + ":" + std::to_string(p.line + 1) + ":" + std::to_string(p.character + 1);
  };
  size_t listed = 0;
  for (size_t i = 0; i < results.size(); ++i) {
    const auto &c = results[i];
    if (c.status != mdbls::Check_status::syntax_error) continue;
    if (listed++ == o.max_errors) {
      *out += file + ": more errors not listed\n";
      break;
    }
    *out += where(c.error_offset) + ": error: syntax error near " + near_text(text, c);
    const auto expected = readable_expected(c.expected);
    if (!expected.empty()) {
      // Word classes and punctuation are listed; keywords too when there are
      // few, otherwise only counted (where a name may stand, every
      // non-reserved keyword may too).
      std::vector<std::string> shown;
      size_t keywords = 0;
      for (const auto &e : expected) {
        if (std::isupper(static_cast<unsigned char>(e[0]))) {
          ++keywords;
        } else {
          shown.push_back(e);
        }
      }
      if (keywords <= 8) {
        for (const auto &e : expected)
          if (std::isupper(static_cast<unsigned char>(e[0]))) shown.push_back(e);
        keywords = 0;
      }
      *out += "; expected ";
      for (size_t k = 0; k < shown.size(); ++k) {
        *out += (k == 0 ? "" : (k + 1 == shown.size() && keywords == 0) ? " or " : ", ") + shown[k];
      }
      if (keywords > 0) {
        *out += std::string(shown.empty() ? "" : " or ") + "one of " + std::to_string(keywords) +
                " keywords";
      }
    }
    *out += "\n";
  }
  if (const char *open = open_name(index.open_at_end())) {
    *out += where(static_cast<uint32_t>(text.size())) +
            ": error: the file ends inside an unterminated " + open + "\n";
  }
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
    return file + ":" + std::to_string(line + 1) + ":" + std::to_string(col + 1);
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
    } else if (arg == "--charset") {
      auto v = value();
      if (!v) return std::nullopt;
      o.charset = mdbls::parse_charset_name(*v);
      if (!o.charset) {
        std::cerr << "mariadb-lsp: --charset is utf8mb4 or latin1\n";
        return std::nullopt;
      }
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
    } else if (arg == "--threads" || arg == "--max-errors") {
      auto v = value();
      if (!v) return std::nullopt;
      char *end = nullptr;
      const unsigned long n = std::strtoul(v->c_str(), &end, 10);
      if (v->empty() || *end != '\0') {
        std::cerr << "mariadb-lsp: " << arg << " needs a number\n";
        return std::nullopt;
      }
      if (arg == "--threads") {
        o.threads = static_cast<unsigned>(n);
      } else {
        o.max_errors = n;
      }
    } else if (arg == "-" || arg.rfind("-", 0) != 0) {
      o.files.push_back(arg);
    } else {
      std::cerr << "mariadb-lsp: unexpected argument " << arg << "\n";
      return std::nullopt;
    }
  }
  if (o.command != "split" && o.command != "tokens" && o.command != "check") {
    std::cerr << (o.command.empty() ? std::string("mariadb-lsp: no command given\n")
                                    : "mariadb-lsp: unknown command " + o.command + "\n")
              << k_usage;
    return std::nullopt;
  }
  if (o.files.empty()) {
    std::cerr << "mariadb-lsp: no input file (use - for stdin)\n";
    return std::nullopt;
  }
  if (o.command != "check" && o.files.size() > 1) {
    std::cerr << "mariadb-lsp: " << o.command << " takes one file\n";
    return std::nullopt;
  }
  return o;
}

}  // namespace

int main(int argc, char **argv) {
  const auto o = parse_args(argc, argv);
  if (!o) return k_exit_usage;

  std::unique_ptr<mdbls::Parser_pool> pool;
  if (o->command == "check") pool = std::make_unique<mdbls::Parser_pool>(o->threads);

  rapidjson::StringBuffer json_buf;
  rapidjson::Writer<rapidjson::StringBuffer> json(json_buf);
  if (o->command == "check" && o->json) {
    json.StartObject();
    json.Key("files");
    json.StartArray();
  }

  int rc = k_exit_ok;
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
    const auto index = mdbls::Statement_index::build(text, o->index);
    const double index_ms = ms_since(t0);

    t0 = Clock::now();
    if (o->command == "split") {
      rc = run_split(*o, text, lines, index);
    } else if (o->command == "tokens") {
      rc = run_tokens(*o, text, lines, index);
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
  if (o->command == "check") {
    if (o->json) {
      json.EndArray();
      json.Key("errors");
      json.Uint64(total_errors);
      json.EndObject();
      std::cout << json_buf.GetString() << '\n';
    }
    rc = total_errors > 0 ? k_exit_errors : k_exit_ok;
  }
  return unreadable ? k_exit_usage : rc;
}
