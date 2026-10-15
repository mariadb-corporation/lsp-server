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

// Replays libs/mariadbparser/tests/lex-fixtures/*.jsonl through
// mdbp_lex_sql().  Each fixture names the sql_lex.cc line it exercises, the
// input, the sql_mode, and the expected (terminal, text) pairs.
//
// Only fixtures lexed without a parser ("valid": "none") apply: that is what
// mdbp_lex_sql() does.  Fixtures that track a SET sql_mode across statements
// are skipped, since that tracking belongs to the language server, not to
// the library.  ORACLE and MSSQL fixtures must be refused at parser creation.

#include <gtest/gtest.h>

#include <rapidjson/document.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "mdbparse.h"

namespace {

struct Lex_case {
  std::string id;
  std::string file;
  std::string sql;
  std::string mode;
  int prepare_mode = 0;
  std::vector<std::pair<std::string, std::string>> tokens;
};

std::string from_hex(const std::string &hex) {
  std::string out;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    out.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
  }
  return out;
}

std::vector<Lex_case> load_cases() {
  std::vector<Lex_case> cases;
  std::vector<std::filesystem::path> files;
  for (const auto &entry : std::filesystem::directory_iterator(MDBP_LEX_FIXTURE_DIR)) {
    if (entry.path().extension() == ".jsonl") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());

  for (const auto &path : files) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
      rapidjson::Document d;
      d.Parse(line.c_str());
      if (d.HasParseError() || !d.IsObject()) continue;
      if (!d.HasMember("valid") || !d["valid"].IsString() ||
          std::string(d["valid"].GetString()) != "none") {
        continue;
      }
      if (d.HasMember("track_sql_mode") && d["track_sql_mode"].GetBool()) continue;

      Lex_case c;
      c.id = d["id"].GetString();
      c.file = path.filename().string();
      c.mode = d["mode"].GetString();
      if (d.HasMember("sql_hex")) {
        c.sql = from_hex(d["sql_hex"].GetString());
      } else if (d.HasMember("sql")) {
        c.sql.assign(d["sql"].GetString(), d["sql"].GetStringLength());
      } else {
        continue;
      }
      if (d.HasMember("prepare_mode")) c.prepare_mode = d["prepare_mode"].GetBool() ? 1 : 0;
      const bool hex_texts = d.HasMember("tokens_hex");
      const auto &tokens = hex_texts ? d["tokens_hex"] : d["tokens"];
      for (const auto &t : tokens.GetArray()) {
        std::string text(t[1].GetString(), t[1].GetStringLength());
        c.tokens.emplace_back(t[0].GetString(), hex_texts ? from_hex(text) : text);
      }
      cases.push_back(std::move(c));
    }
  }
  return cases;
}

bool refused_mode(const std::string &mode) {
  return mode.find("ORACLE") != std::string::npos ||
         mode.find("MSSQL") != std::string::npos;
}

// Strict UTF-8 check (no overlongs, no surrogates, nothing above U+10FFFF).
bool valid_utf8(const std::string &s) {
  size_t i = 0;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    size_t n;
    unsigned cp;
    if (c < 0x80) { ++i; continue; }
    if (c >= 0xC2 && c <= 0xDF) { n = 1; cp = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { n = 2; cp = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { n = 3; cp = c & 0x07; }
    else return false;
    if (i + n >= s.size()) return false;
    for (size_t k = 1; k <= n; ++k) {
      const auto cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    if ((n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ||
        (n == 3 && (cp < 0x10000 || cp > 0x10FFFF))) {
      return false;
    }
    i += n + 1;
  }
  return true;
}

class Lex_fixture : public ::testing::TestWithParam<Lex_case> {};

}  // namespace

TEST_P(Lex_fixture, tokens_match) {
  const Lex_case &c = GetParam();
  mdbp_options opts;
  mdbp_options_init(&opts);
  opts.sql_mode = c.mode.c_str();
  opts.prepare_mode = c.prepare_mode;
  std::unique_ptr<mdbp_parser, decltype(&mdbp_parser_free)> p{mdbp_parser_new(&opts),
                                                              &mdbp_parser_free};
  if (refused_mode(c.mode)) {
    EXPECT_EQ(nullptr, p.get()) << c.id << ": sql_mode " << c.mode << " must be refused";
    return;
  }
  ASSERT_NE(nullptr, p.get()) << c.id;

  mdbp_result r;
  if (!valid_utf8(c.sql)) {
    // The fixture records what the server's lexer does with these bytes; the
    // library refuses input that is not valid UTF-8 instead of lexing it as
    // something else (include/mdbparse.h, mdbp_lex_sql).
    EXPECT_EQ(MDBP_UNSUPPORTED, mdbp_lex_sql(p.get(), c.sql.data(), c.sql.size(), &r))
        << c.id;
    return;
  }
  ASSERT_EQ(MDBP_OK, mdbp_lex_sql(p.get(), c.sql.data(), c.sql.size(), &r)) << c.id;

  std::vector<std::pair<std::string, std::string>> got;
  for (size_t i = 0; i < r.ntokens; ++i) {
    const auto &t = r.tokens[i];
    std::string text;
    if (t.text != nullptr && t.byte_end > t.byte_start) {
      text.assign(t.text, static_cast<size_t>(t.byte_end - t.byte_start));
    }
    got.emplace_back(t.name, text);
  }
  EXPECT_EQ(c.tokens, got) << c.file << ": " << c.id;
}

INSTANTIATE_TEST_SUITE_P(
    Fixtures, Lex_fixture, ::testing::ValuesIn(load_cases()),
    [](const ::testing::TestParamInfo<Lex_case> &info) {
      std::string name = info.param.id;
      for (auto &ch : name) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) ch = '_';
      }
      return name;
    });
