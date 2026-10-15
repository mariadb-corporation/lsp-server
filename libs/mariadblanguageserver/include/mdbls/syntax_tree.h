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

#ifndef MDBLS_SYNTAX_TREE_H_
#define MDBLS_SYNTAX_TREE_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct mdbp_parser;
struct mdbp_result;

namespace mdbls {

// Grammar symbols by name, read once from libmariadbparser.
namespace grammar {
// The symbol called `name` ("create_table_stmt", "IDENT_QUOTED", "'('"), or
// -1.
int symbol(std::string_view name);
// The name of `symbol`, or "" for none.
const char *name(int symbol);
bool is_terminal(int symbol);
}  // namespace grammar

// The parse tree of one statement.  Nodes are numbered in the order they
// were built: every child before its parent.  A leaf is a token; an inner
// node is a nonterminal with the rule that produced it.  Mid-rule actions
// ($@N) appear as empty nonterminals.
//
// Offsets are absolute byte offsets in the document the statement came from.
class Syntax_tree {
 public:
  static constexpr uint32_t npos = UINT32_MAX;

  struct Token {
    int symbol;       // terminal symbol
    uint32_t begin;   // absolute byte offsets
    uint32_t end;
  };

  struct Node {
    int symbol;               // terminal or nonterminal symbol
    int rule;                 // the rule of a nonterminal, -1 for a token
    uint32_t first_token;     // the tokens this node covers
    uint32_t token_count;
    uint32_t parent = npos;
    uint32_t first_child = 0; // into the children array
    uint32_t child_count = 0;
  };

  // Builds the tree of the parse `result` that `parser` produced last, for a
  // statement starting at byte `statement_offset` of its document.  Empty
  // when the result has no derivation (want_derivation 0) or no tokens.
  static Syntax_tree build(const mdbp_parser *parser, const mdbp_result &result,
                           uint32_t statement_offset);

  bool empty() const { return m_nodes.empty(); }
  // The nodes on the parser's stack at the end: one, the start symbol, for
  // an accepted statement; what had been built so far for a rejected one.
  std::span<const uint32_t> roots() const { return m_roots; }
  const Node &node(uint32_t n) const { return m_nodes[n]; }
  size_t size() const { return m_nodes.size(); }
  std::span<const uint32_t> children(uint32_t n) const {
    return {m_children.data() + m_nodes[n].first_child, m_nodes[n].child_count};
  }
  const std::vector<Token> &tokens() const { return m_tokens; }

  const char *name(uint32_t n) const { return grammar::name(m_nodes[n].symbol); }
  bool is_token(uint32_t n) const { return m_nodes[n].rule < 0; }

  // Byte range of the tokens under `n`; begin == end for an empty node.
  uint32_t begin(uint32_t n) const;
  uint32_t end(uint32_t n) const;
  std::string_view text(std::string_view document, uint32_t n) const {
    return document.substr(begin(n), end(n) - begin(n));
  }

  // The first child with symbol `symbol`, or npos.
  uint32_t child(uint32_t n, int symbol) const;
  // The first node with symbol `symbol` in `n`'s subtree, `n` included,
  // in document order; npos if none.
  uint32_t find(uint32_t n, int symbol) const;
  // Every node with symbol `symbol` in `n`'s subtree, in document order,
  // not descending into a match.
  std::vector<uint32_t> find_all(uint32_t n, int symbol) const;

 private:
  std::vector<Node> m_nodes;
  std::vector<uint32_t> m_children;
  std::vector<uint32_t> m_roots;
  std::vector<Token> m_tokens;
};

}  // namespace mdbls

#endif  // MDBLS_SYNTAX_TREE_H_
