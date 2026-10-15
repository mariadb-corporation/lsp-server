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

#include "mdbls/syntax_tree.h"

#include <string>
#include <unordered_map>

#include "mdbparse.h"

namespace mdbls {

namespace grammar {

namespace {

struct Names {
  std::unordered_map<std::string, int> by_name;
  int count = 0;
  int terminals = 0;
};

const Names &names() {
  static const Names n = [] {
    Names out;
    out.count = mdbp_grammar_symbol_count();
    out.terminals = mdbp_grammar_terminal_count();
    for (int s = 0; s < out.count; ++s) {
      if (const char *name = mdbp_grammar_symbol_name(s)) out.by_name.emplace(name, s);
    }
    return out;
  }();
  return n;
}

}  // namespace

int symbol(std::string_view name) {
  const auto &n = names();
  const auto it = n.by_name.find(std::string(name));
  return it == n.by_name.end() ? -1 : it->second;
}

const char *name(int symbol) {
  const char *n = mdbp_grammar_symbol_name(symbol);
  return n != nullptr ? n : "";
}

bool is_terminal(int symbol) { return symbol >= 0 && symbol < names().terminals; }

}  // namespace grammar

Syntax_tree Syntax_tree::build(const mdbp_parser *parser, const mdbp_result &result,
                               uint32_t statement_offset) {
  Syntax_tree t;
  const int *depths = mdbp_derivation_depths(parser);
  if (result.tokens == nullptr || result.ntokens == 0) return t;

  t.m_tokens.reserve(result.ntokens);
  for (size_t i = 0; i < result.ntokens; ++i) {
    const mdbp_token &tok = result.tokens[i];
    const uint32_t b = statement_offset + static_cast<uint32_t>(tok.byte_start < 0 ? 0 : tok.byte_start);
    const uint32_t e = statement_offset + static_cast<uint32_t>(tok.byte_end < 0 ? 0 : tok.byte_end);
    t.m_tokens.push_back({mdbp_grammar_token_symbol(tok.token_number), b, e});
  }
  if (depths == nullptr || result.derivation == nullptr) return t;

  std::vector<uint32_t> stack;
  uint32_t next = 0;
  auto push_leaf = [&] {
    Node leaf{t.m_tokens[next].symbol, -1, next, 1};
    t.m_nodes.push_back(leaf);
    stack.push_back(static_cast<uint32_t>(t.m_nodes.size() - 1));
    ++next;
  };

  t.m_nodes.reserve(result.derivation_len * 2);
  for (size_t i = 0; i < result.derivation_len; ++i) {
    const int rule = result.derivation[i];
    const auto depth = static_cast<size_t>(depths[i]);
    while (stack.size() < depth && next < t.m_tokens.size()) push_leaf();
    const auto len = static_cast<size_t>(mdbp_grammar_rule_length(rule));
    if (len > stack.size()) return Syntax_tree{};  // cannot happen for a real derivation

    Node n{mdbp_grammar_rule_lhs(rule), rule, next, 0};
    n.first_child = static_cast<uint32_t>(t.m_children.size());
    n.child_count = static_cast<uint32_t>(len);
    const size_t from = stack.size() - len;
    if (len > 0) {
      n.first_token = t.m_nodes[stack[from]].first_token;
      const Node &last = t.m_nodes[stack.back()];
      n.token_count = last.first_token + last.token_count - n.first_token;
    }
    const auto id = static_cast<uint32_t>(t.m_nodes.size());
    for (size_t k = from; k < stack.size(); ++k) {
      t.m_children.push_back(stack[k]);
      t.m_nodes[stack[k]].parent = id;
    }
    stack.resize(from);
    t.m_nodes.push_back(n);
    stack.push_back(id);
  }
  t.m_roots = std::move(stack);
  return t;
}

uint32_t Syntax_tree::begin(uint32_t n) const {
  const Node &x = m_nodes[n];
  if (x.token_count == 0) {
    return x.first_token < m_tokens.size() ? m_tokens[x.first_token].begin
                                           : (m_tokens.empty() ? 0 : m_tokens.back().end);
  }
  return m_tokens[x.first_token].begin;
}

uint32_t Syntax_tree::end(uint32_t n) const {
  const Node &x = m_nodes[n];
  if (x.token_count == 0) return begin(n);
  return m_tokens[x.first_token + x.token_count - 1].end;
}

uint32_t Syntax_tree::child(uint32_t n, int symbol) const {
  for (const uint32_t c : children(n)) {
    if (m_nodes[c].symbol == symbol) return c;
  }
  return npos;
}

// Iterative: list rules are left-recursive, so a tree can be as deep as a
// statement is long (one level per row of an INSERT).
uint32_t Syntax_tree::find(uint32_t n, int symbol) const {
  std::vector<uint32_t> todo{n};
  while (!todo.empty()) {
    const uint32_t x = todo.back();
    todo.pop_back();
    if (m_nodes[x].symbol == symbol) return x;
    const auto ch = children(x);
    for (auto it = ch.rbegin(); it != ch.rend(); ++it) todo.push_back(*it);
  }
  return npos;
}

std::vector<uint32_t> Syntax_tree::find_all(uint32_t n, int symbol) const {
  std::vector<uint32_t> out;
  std::vector<uint32_t> todo{n};
  while (!todo.empty()) {
    const uint32_t x = todo.back();
    todo.pop_back();
    if (m_nodes[x].symbol == symbol) {
      out.push_back(x);
      continue;
    }
    const auto ch = children(x);
    for (auto it = ch.rbegin(); it != ch.rend(); ++it) todo.push_back(*it);
  }
  return out;
}

}  // namespace mdbls
