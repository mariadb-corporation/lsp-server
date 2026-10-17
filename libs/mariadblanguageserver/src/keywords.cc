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

#include "mdbls/keywords.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

#include "mdbls/syntax_tree.h"
#include "mdbparse.h"
#include "text_util.h"

namespace mdbls {

namespace {

constexpr Builtin_function k_functions[] = {
#define FN(name, forms, summary, url) {name, forms, summary, url},
#include "builtin_functions.inc"
#undef FN
};

struct Keyword_tables {
  // grammar symbol -> spellings (words only, symbols[] only)
  std::unordered_map<int, std::vector<std::string_view>> spellings;
  // upper-case word -> grammar symbol, for every word of symbols[]
  std::unordered_map<std::string, int> words;
  std::vector<uint8_t> nonreserved;  // by grammar symbol
};

bool is_word(std::string_view s) {
  return !s.empty() && std::isalpha(static_cast<unsigned char>(s[0]));
}

// Whether the parser accepts `SELECT * FROM <terminal>`: the terminal can
// stand for a table name.
bool accepted_as_name(mdbp_parser *p, const char *terminal) {
  const char *names[] = {"SELECT_SYM", "'*'", "FROM", terminal, "END_OF_INPUT"};
  mdbp_token tokens[5];
  for (size_t i = 0; i < 5; ++i) {
    tokens[i] = mdbp_token{names[i], names[i], 0, -1, -1};
  }
  mdbp_result r;
  return mdbp_parse_tokens(p, tokens, 5, &r) == MDBP_OK && r.accepted;
}

const Keyword_tables &tables() {
  static const Keyword_tables t = [] {
    Keyword_tables out;
    out.nonreserved.assign(static_cast<size_t>(mdbp_grammar_symbol_count()), 0);
    mdbp_options o;
    mdbp_options_init(&o);
    o.want_derivation = 0;
    mdbp_parser *p = mdbp_parser_new(&o);
    const size_t n = mdbp_keyword_count();
    for (size_t i = 0; i < n; ++i) {
      mdbp_keyword k;
      if (!mdbp_keyword_at(i, &k) || k.symbol < 0 || k.terminal == nullptr) continue;
      const std::string_view spelling(k.spelling);
      if (!is_word(spelling) || k.function) continue;
      auto &v = out.spellings[k.symbol];
      if (std::find(v.begin(), v.end(), spelling) == v.end()) v.push_back(spelling);
      out.words.emplace(upper(spelling), k.symbol);
      if (p != nullptr && static_cast<size_t>(k.symbol) < out.nonreserved.size() &&
          accepted_as_name(p, k.terminal)) {
        out.nonreserved[static_cast<size_t>(k.symbol)] = 1;
      }
    }
    mdbp_parser_free(p);
    return out;
  }();
  return t;
}

}  // namespace

const std::vector<std::string_view> &keyword_spellings(int symbol) {
  static const std::vector<std::string_view> none;
  const auto &t = tables();
  const auto it = t.spellings.find(symbol);
  return it == t.spellings.end() ? none : it->second;
}

bool is_nonreserved_keyword(int symbol) {
  const auto &t = tables();
  return symbol >= 0 && static_cast<size_t>(symbol) < t.nonreserved.size() &&
         t.nonreserved[static_cast<size_t>(symbol)] != 0;
}

bool needs_quoting(std::string_view word) {
  if (word.empty()) return true;
  bool digits = true;
  for (const char ch : word) {
    const auto c = static_cast<unsigned char>(ch);
    if (c >= 0x80) {
      digits = false;
      continue;
    }
    if (!std::isalnum(c) && c != '_' && c != '$') return true;
    if (!std::isdigit(c)) digits = false;
  }
  if (digits) return true;
  const auto &t = tables();
  const auto it = t.words.find(upper(word));
  return it != t.words.end() && !is_nonreserved_keyword(it->second);
}

std::string quote_identifier(std::string_view name) {
  if (!needs_quoting(name)) return std::string(name);
  std::string out = "`";
  for (const char c : name) {
    out.push_back(c);
    if (c == '`') out.push_back('`');
  }
  out.push_back('`');
  return out;
}

std::span<const Builtin_function> builtin_functions() { return k_functions; }

const Builtin_function *builtin_function(std::string_view name) {
  static const std::unordered_map<std::string, const Builtin_function *> by_name = [] {
    std::unordered_map<std::string, const Builtin_function *> out;
    for (const auto &f : k_functions) out.emplace(std::string(f.name), &f);
    return out;
  }();
  const auto it = by_name.find(upper(name));
  return it == by_name.end() ? nullptr : it->second;
}

std::vector<std::pair<size_t, size_t>> call_form_parameters(std::string_view form) {
  std::vector<std::pair<size_t, size_t>> out;
  const size_t open = form.find('(');
  if (open == std::string_view::npos) return out;
  int depth = 0;
  size_t start = open + 1;
  auto add = [&](size_t end) {
    size_t b = start, e = end;
    while (b < e && std::isspace(static_cast<unsigned char>(form[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(form[e - 1]))) --e;
    if (e > b) out.emplace_back(b, e);
  };
  for (size_t i = open + 1; i < form.size(); ++i) {
    const char c = form[i];
    if (c == '(' || c == '[') {
      ++depth;
    } else if (c == ']') {
      --depth;
    } else if (c == ')') {
      if (depth == 0) {
        add(i);
        return out;
      }
      --depth;
    } else if (c == ',' && depth == 0) {
      add(i);
      start = i + 1;
    }
  }
  add(form.size());
  return out;
}

}  // namespace mdbls
