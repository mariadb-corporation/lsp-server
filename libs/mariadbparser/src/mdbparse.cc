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

// mdbparse: a command-line front end to libmariadbparser, for inspecting
// verdicts, tokens, expected sets and derivations.  It uses the public API in
// include/mdbparse.h only.

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "mdbparse.h"

namespace {

constexpr int k_exit_ok = 0;
constexpr int k_exit_internal = 1;
constexpr int k_exit_usage = 2;
constexpr int k_exit_refused = 3;
constexpr int k_exit_rejected = 4;

const char *const k_usage =
    "usage: mdbparse (--sql TEXT | --file FILE|- | --jsonl FILE|-) [options]\n"
    "\n"
    "Parses MariaDB SQL with libmariadbparser and prints one JSON object per\n"
    "statement.  A rejection means the SQL is invalid; an acceptance does not\n"
    "guarantee it is valid, since the server's semantic checks are not here.\n"
    "\n"
    "input:\n"
    "  --sql TEXT          one statement\n"
    "  --file FILE|-       one statement read from a file ('-' = stdin)\n"
    "  --jsonl FILE|-      one record per line: {\"id\": ..., \"sql\": \"...\"} is\n"
    "                      lexed and parsed; {\"id\": ..., \"tokens\": [...]} is\n"
    "                      parsed as a token stream, each token a terminal name,\n"
    "                      a [name, text] pair or a {\"name\": ...} object\n"
    "\n"
    "options:\n"
    "  --lex-only          lex, do not parse\n"
    "  --max-tokens N      with --lex-only: stop after N tokens\n"
    "  --sql-mode LIST     comma-separated sql_mode; ORACLE and MSSQL are refused\n"
    "  --charset NAME      connection charset; only utf8mb4 is supported\n"
    "  --version-id N      release id versioned comments are judged against,\n"
    "                      major*10000 + minor*100 + patch (110803 is 11.8.3)\n"
    "  --no-prepare-mode   '?' stays a character token instead of PARAM_MARKER\n"
    "  --no-derivation     do not record the derivation\n"
    "  --with-tokens       also print the tokens of a parse\n"
    "  --verdict-exit      exit 4 if any statement is rejected\n"
    "  --help              this text\n"
    "\n"
    "exit: 0 done, 1 internal error, 2 usage or unreadable input,\n"
    "      3 option refused, 4 rejection (with --verdict-exit)\n";

struct Options {
  std::optional<std::string> sql;
  std::optional<std::string> file;
  std::optional<std::string> jsonl;
  bool lex_only = false;
  std::optional<size_t> max_tokens;
  std::string sql_mode;
  std::optional<std::string> charset;
  std::optional<unsigned long> version_id;
  bool prepare_mode = true;
  bool derivation = true;
  bool with_tokens = false;
  bool verdict_exit = false;
};

using Writer = rapidjson::Writer<rapidjson::StringBuffer>;

const char *status_name(mdbp_status st) {
  switch (st) {
    case MDBP_OK:
      return "ok";
    case MDBP_UNSUPPORTED:
      return "unsupported";
    default:
      return "internal";
  }
}

void write_string(Writer &w, const char *s) {
  if (s == nullptr) {
    w.Null();
  } else {
    w.String(s);
  }
}

void write_tokens(Writer &w, const mdbp_result &r) {
  w.Key("tokens");
  if (r.tokens == nullptr) {
    w.Null();
    return;
  }
  w.StartArray();
  for (size_t i = 0; i < r.ntokens; ++i) {
    const mdbp_token &t = r.tokens[i];
    w.StartArray();
    w.String(t.name);
    if (t.text != nullptr && t.byte_end >= t.byte_start && t.byte_start >= 0) {
      w.String(t.text, static_cast<rapidjson::SizeType>(t.byte_end - t.byte_start));
    } else {
      w.Null();
    }
    w.Int64(t.byte_start);
    w.Int64(t.byte_end);
    w.EndArray();
  }
  w.EndArray();
}

void write_result(Writer &w, const mdbp_result &r, bool lexed, bool with_tokens) {
  w.Key("status");
  w.String(status_name(r.status));
  if (lexed) {
    write_tokens(w, r);
    w.Key("eof_token");
    write_string(w, r.eof_token);
    return;
  }
  w.Key("accepted");
  if (r.status == MDBP_OK) {
    w.Bool(r.accepted != 0);
  } else {
    w.Null();
  }
  w.Key("derivation");
  if (r.derivation == nullptr) {
    w.Null();
  } else {
    w.StartArray();
    for (size_t i = 0; i < r.derivation_len; ++i) w.Int(r.derivation[i]);
    w.EndArray();
  }
  w.Key("tokens_consumed");
  if (r.status == MDBP_OK) {
    w.Uint64(r.tokens_consumed);
  } else {
    w.Null();
  }
  w.Key("eof_token");
  write_string(w, r.eof_token);
  w.Key("error");
  if (!r.error_seen) {
    w.Null();
  } else {
    w.StartObject();
    w.Key("at_index");
    w.Uint64(r.error_token_index);
    w.Key("token");
    write_string(w, r.error_token);
    w.Key("state");
    if (r.error_state >= 0) {
      w.Int(r.error_state);
    } else {
      w.Null();
    }
    w.Key("byte_pos");
    if (r.error_byte_pos >= 0) {
      w.Int64(r.error_byte_pos);
    } else {
      w.Null();
    }
    w.Key("expected_sample");
    if (r.expected == nullptr) {
      w.Null();
    } else {
      w.StartArray();
      for (size_t i = 0; i < r.nexpected; ++i) w.String(r.expected[i]);
      w.EndArray();
    }
    w.EndObject();
  }
  if (with_tokens) write_tokens(w, r);
}

struct Parser_deleter {
  void operator()(mdbp_parser *p) const { mdbp_parser_free(p); }
};
using Parser = std::unique_ptr<mdbp_parser, Parser_deleter>;

// Lexes or parses one SQL text and prints its object.  Returns whether the
// statement was rejected.
bool run_sql(mdbp_parser *p, const Options &o, const std::string &sql,
             const rapidjson::Value *id, int *exit_code) {
  mdbp_result r;
  mdbp_status st;
  int more = 0;
  if (o.lex_only && o.max_tokens) {
    st = mdbp_lex_sql_prefix(p, sql.data(), sql.size(), *o.max_tokens, &r, &more);
  } else if (o.lex_only) {
    st = mdbp_lex_sql(p, sql.data(), sql.size(), &r);
  } else {
    st = mdbp_parse_sql(p, sql.data(), sql.size(), &r);
  }
  if (st == MDBP_INTERNAL) *exit_code = k_exit_internal;

  rapidjson::StringBuffer buf;
  Writer w(buf);
  w.StartObject();
  if (id != nullptr) {
    w.Key("id");
    id->Accept(w);
  }
  write_result(w, r, o.lex_only, o.with_tokens);
  if (o.lex_only && o.max_tokens) {
    w.Key("more");
    w.Bool(more != 0);
  }
  w.EndObject();
  std::cout << buf.GetString() << '\n';
  return st == MDBP_OK && !o.lex_only && !r.accepted;
}

// Parses a token stream from a --jsonl record.  Returns whether it was
// rejected; std::nullopt when the record is malformed.
std::optional<bool> run_tokens(mdbp_parser *p,
                               const rapidjson::Value &tokens,
                               const rapidjson::Value *id, int *exit_code) {
  std::vector<mdbp_token> vec;
  for (const auto &t : tokens.GetArray()) {
    const rapidjson::Value *name = nullptr;
    const rapidjson::Value *text = nullptr;
    if (t.IsString()) {
      name = &t;
    } else if (t.IsArray() && t.Size() >= 1 && t[0].IsString()) {
      name = &t[0];
      if (t.Size() >= 2 && t[1].IsString()) text = &t[1];
    } else if (t.IsObject() && t.HasMember("name") && t["name"].IsString()) {
      name = &t["name"];
      if (t.HasMember("text") && t["text"].IsString()) text = &t["text"];
    } else {
      return std::nullopt;
    }
    vec.push_back({name->GetString(), text ? text->GetString() : nullptr, 0, -1, -1});
  }
  mdbp_result r;
  const mdbp_status st = mdbp_parse_tokens(p, vec.data(), vec.size(), &r);
  if (st == MDBP_INTERNAL) *exit_code = k_exit_internal;

  rapidjson::StringBuffer buf;
  Writer w(buf);
  w.StartObject();
  if (id != nullptr) {
    w.Key("id");
    id->Accept(w);
  }
  write_result(w, r, false, false);
  w.EndObject();
  std::cout << buf.GetString() << '\n';
  return st == MDBP_OK && !r.accepted;
}

bool read_all(const std::string &path, std::string *out) {
  if (path == "-") {
    out->assign(std::istreambuf_iterator<char>(std::cin), {});
    return !std::cin.bad();
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out->assign(std::istreambuf_iterator<char>(in), {});
  return !in.bad();
}

int run_jsonl(mdbp_parser *p, const Options &o, bool *rejected) {
  std::ifstream file;
  std::istream *in = &std::cin;
  if (*o.jsonl != "-") {
    file.open(*o.jsonl, std::ios::binary);
    if (!file) {
      std::cerr << "mdbparse: cannot read " << *o.jsonl << ": " << std::strerror(errno)
                << "\n";
      return k_exit_usage;
    }
    in = &file;
  }
  int exit_code = k_exit_ok;
  std::string line;
  unsigned long lineno = 0;
  while (std::getline(*in, line)) {
    ++lineno;
    if (line.empty()) continue;
    rapidjson::Document d;
    d.Parse(line.c_str(), line.size());
    const bool has_sql = !d.HasParseError() && d.IsObject() && d.HasMember("sql") &&
                         d["sql"].IsString();
    const bool has_tokens = !d.HasParseError() && d.IsObject() &&
                            d.HasMember("tokens") && d["tokens"].IsArray();
    if (has_sql == has_tokens) {
      std::cerr << "mdbparse: --jsonl line " << lineno
                << ": need an object with exactly one of \"sql\" or \"tokens\"\n";
      return k_exit_usage;
    }
    const rapidjson::Value *id = d.HasMember("id") ? &d["id"] : nullptr;
    if (has_sql) {
      const std::string sql(d["sql"].GetString(), d["sql"].GetStringLength());
      if (run_sql(p, o, sql, id, &exit_code)) *rejected = true;
    } else {
      const auto r = run_tokens(p, d["tokens"], id, &exit_code);
      if (!r) {
        std::cerr << "mdbparse: --jsonl line " << lineno << ": malformed \"tokens\"\n";
        return k_exit_usage;
      }
      if (*r) *rejected = true;
    }
    if (exit_code == k_exit_internal) return exit_code;
  }
  return exit_code;
}

bool parse_args(int argc, char **argv, Options *o) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&](std::string *out) {
      if (i + 1 >= argc) {
        std::cerr << "mdbparse: " << arg << " needs a value\n";
        return false;
      }
      *out = argv[++i];
      return true;
    };
    std::string v;
    if (arg == "--help") {
      std::cout << k_usage;
      std::exit(k_exit_ok);
    } else if (arg == "--sql") {
      if (!value(&v)) return false;
      o->sql = v;
    } else if (arg == "--file") {
      if (!value(&v)) return false;
      o->file = v;
    } else if (arg == "--jsonl") {
      if (!value(&v)) return false;
      o->jsonl = v;
    } else if (arg == "--lex-only") {
      o->lex_only = true;
    } else if (arg == "--max-tokens") {
      if (!value(&v)) return false;
      char *end = nullptr;
      const unsigned long n = std::strtoul(v.c_str(), &end, 10);
      if (v.empty() || *end != '\0') {
        std::cerr << "mdbparse: --max-tokens needs a number\n";
        return false;
      }
      o->max_tokens = n;
    } else if (arg == "--sql-mode") {
      if (!value(&v)) return false;
      o->sql_mode = v;
    } else if (arg == "--charset") {
      if (!value(&v)) return false;
      o->charset = v;
    } else if (arg == "--version-id") {
      if (!value(&v)) return false;
      char *end = nullptr;
      const unsigned long n = std::strtoul(v.c_str(), &end, 10);
      if (v.empty() || *end != '\0') {
        std::cerr << "mdbparse: --version-id needs a number\n";
        return false;
      }
      o->version_id = n;
    } else if (arg == "--no-prepare-mode") {
      o->prepare_mode = false;
    } else if (arg == "--no-derivation") {
      o->derivation = false;
    } else if (arg == "--with-tokens") {
      o->with_tokens = true;
    } else if (arg == "--verdict-exit") {
      o->verdict_exit = true;
    } else {
      std::cerr << "mdbparse: unknown option " << arg << "\n" << k_usage;
      return false;
    }
  }
  const int inputs = (o->sql ? 1 : 0) + (o->file ? 1 : 0) + (o->jsonl ? 1 : 0);
  if (inputs != 1) {
    std::cerr << "mdbparse: give exactly one of --sql, --file, --jsonl\n" << k_usage;
    return false;
  }
  if (o->max_tokens && !o->lex_only) {
    std::cerr << "mdbparse: --max-tokens is only valid with --lex-only\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  Options o;
  if (!parse_args(argc, argv, &o)) return k_exit_usage;

  mdbp_options opts;
  mdbp_options_init(&opts);
  opts.sql_mode = o.sql_mode.c_str();
  if (o.charset) opts.charset = o.charset->c_str();
  opts.prepare_mode = o.prepare_mode ? 1 : 0;
  opts.want_derivation = o.derivation ? 1 : 0;
  Parser p{mdbp_parser_new(&opts)};
  if (!p) {
    std::cerr << "mdbparse: the parser refused these options (sql_mode ORACLE or "
                 "MSSQL, or a charset other than utf8mb4)\n";
    return k_exit_refused;
  }
  if (o.version_id && mdbp_parser_set_version_id(p.get(), *o.version_id) != MDBP_OK) {
    std::cerr << "mdbparse: --version-id must be between 10000 and 999999\n";
    return k_exit_refused;
  }

  bool rejected = false;
  int exit_code = k_exit_ok;
  if (o.jsonl) {
    exit_code = run_jsonl(p.get(), o, &rejected);
  } else {
    std::string sql;
    if (o.sql) {
      sql = *o.sql;
    } else if (!read_all(*o.file, &sql)) {
      std::cerr << "mdbparse: cannot read " << *o.file << "\n";
      return k_exit_usage;
    }
    rejected = run_sql(p.get(), o, sql, nullptr, &exit_code);
  }
  if (exit_code != k_exit_ok) return exit_code;
  return (o.verdict_exit && rejected) ? k_exit_rejected : k_exit_ok;
}
