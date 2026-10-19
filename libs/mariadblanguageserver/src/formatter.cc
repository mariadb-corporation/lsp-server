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

#include "mdbls/formatter.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <optional>

#include "mdbls/keywords.h"
#include "mdbls/syntax_tree.h"
#include "mdbparse.h"
#include "text_util.h"

namespace mdbls {

namespace {

constexpr uint32_t npos = Syntax_tree::npos;

// ---- the layout: a document of texts and possible line breaks, printed
// to a width (after Wadler's "A prettier printer" and Prettier's printer)

struct Doc {
  enum class Kind : uint8_t { text, line, concat, nest, align, group, fill };
  enum class Line : uint8_t { normal, soft, hard };
  Kind kind = Kind::concat;
  Line line = Line::normal;  // normal: a space when flat; soft: nothing
  bool hard = false;         // a hard line inside: the groups around it break
  bool force = false;        // a group that breaks whether it fits or not
  std::string text;          // text; nest: the indentation added
  std::vector<Doc> parts;    // concat, fill (content, separator, content, ...);
                             // one for nest, align and group
};

Doc text(std::string s) {
  Doc d;
  d.kind = Doc::Kind::text;
  d.text = std::move(s);
  return d;
}

Doc line(Doc::Line kind = Doc::Line::normal) {
  Doc d;
  d.kind = Doc::Kind::line;
  d.line = kind;
  d.hard = kind == Doc::Line::hard;
  return d;
}

Doc softline() { return line(Doc::Line::soft); }
Doc hardline() { return line(Doc::Line::hard); }

Doc concat(std::vector<Doc> parts) {
  Doc d;
  d.kind = Doc::Kind::concat;
  d.hard = std::any_of(parts.begin(), parts.end(), [](const Doc &p) { return p.hard; });
  d.parts = std::move(parts);
  return d;
}

Doc wrap(Doc::Kind kind, Doc inner) {
  Doc d;
  d.kind = kind;
  d.hard = inner.hard;
  d.parts.push_back(std::move(inner));
  return d;
}

Doc nest(std::string indent, Doc inner) {
  Doc d = wrap(Doc::Kind::nest, std::move(inner));
  d.text = std::move(indent);
  return d;
}

// `inner`, its lines indented to the column it starts at.
Doc align(Doc inner) { return wrap(Doc::Kind::align, std::move(inner)); }

Doc group(Doc inner, bool force = false) {
  Doc d = wrap(Doc::Kind::group, std::move(inner));
  d.force = force;
  return d;
}

Doc fill(std::vector<Doc> parts) {
  Doc d = concat(std::move(parts));
  d.kind = Doc::Kind::fill;
  return d;
}

class Printer {
 public:
  Printer(size_t width, std::string_view eol) : width_(width), eol_(eol) {}

  std::string print(const Doc &doc, std::string_view indent, size_t column) {
    out_.clear();
    column_ = column;
    // The indentations in use; a command points at its own.
    std::deque<std::string> indents{std::string(indent)};
    std::vector<Cmd> stack{{&indents.back(), false, &doc, 0}};
    while (!stack.empty()) {
      Cmd c = std::move(stack.back());
      stack.pop_back();
      const Doc &d = *c.doc;
      switch (d.kind) {
        case Doc::Kind::text:
          out_ += d.text;
          if (const auto nl = d.text.rfind('\n'); nl != std::string::npos) {
            column_ = char_count(std::string_view(d.text).substr(nl + 1));
          } else {
            column_ += char_count(d.text);
          }
          break;
        case Doc::Kind::concat:
          for (auto it = d.parts.rbegin(); it != d.parts.rend(); ++it) stack.push_back({c.indent, c.flat, &*it, 0});
          break;
        case Doc::Kind::nest:
          indents.push_back(*c.indent + d.text);
          stack.push_back({&indents.back(), c.flat, &d.parts[0], 0});
          break;
        case Doc::Kind::align: {
          const size_t width = char_count(*c.indent);
          indents.push_back(*c.indent + std::string(column_ > width ? column_ - width : 0, ' '));
          stack.push_back({&indents.back(), c.flat, &d.parts[0], 0});
          break;
        }
        case Doc::Kind::group: {
          bool flat = !d.force && !d.hard;
          if (flat && !c.flat) flat = fits({{c.indent, true, &d.parts[0], 0}}, &stack);
          stack.push_back({c.indent, flat, &d.parts[0], 0});
          break;
        }
        case Doc::Kind::line:
          if (c.flat && d.line != Doc::Line::hard) {
            if (d.line == Doc::Line::normal) {
              out_ += ' ';
              ++column_;
            }
          } else {
            while (!out_.empty() && (out_.back() == ' ' || out_.back() == '\t')) out_.pop_back();
            out_ += eol_;
            out_ += *c.indent;
            column_ = char_count(*c.indent);
          }
          break;
        case Doc::Kind::fill: {
          const size_t i = c.from;
          if (i >= d.parts.size()) break;
          if (c.flat) {
            for (size_t k = d.parts.size(); k-- > i;) stack.push_back({c.indent, true, &d.parts[k], 0});
            break;
          }
          const Doc &content = d.parts[i];
          const bool content_fits = fits({{c.indent, true, &content, 0}}, nullptr);
          if (i + 1 == d.parts.size()) {
            stack.push_back({c.indent, content_fits, &content, 0});
            break;
          }
          const Doc &separator = d.parts[i + 1];
          // The separator breaks unless the next content fits after it.
          const bool both_fit = fits({{c.indent, true, &d.parts[i + 2], 0},
                                      {c.indent, true, &separator, 0},
                                      {c.indent, true, &content, 0}},
                                     nullptr);
          stack.push_back({c.indent, false, c.doc, i + 2});
          stack.push_back({c.indent, both_fit, &separator, 0});
          stack.push_back({c.indent, content_fits, &content, 0});
          break;
        }
      }
    }
    return std::move(out_);
  }

 private:
  struct Cmd {
    const std::string *indent;  // null in fits(), which does not need it
    bool flat;
    const Doc *doc;
    size_t from;  // fill: the first part left
  };

  // Whether `next` (a stack, its top last), followed by `rest` in the modes
  // they have, fits on the line up to its next line break.
  bool fits(std::vector<Cmd> next, const std::vector<Cmd> *rest) const {
    if (width_ == 0) return true;
    auto left = static_cast<int64_t>(width_) - static_cast<int64_t>(column_);
    size_t rest_at = rest != nullptr ? rest->size() : 0;
    while (left >= 0) {
      if (next.empty()) {
        if (rest_at == 0) return true;
        next.push_back((*rest)[--rest_at]);
        continue;
      }
      const Cmd c = std::move(next.back());
      next.pop_back();
      const Doc &d = *c.doc;
      switch (d.kind) {
        case Doc::Kind::text: {
          const auto nl = d.text.find('\n');
          left -= static_cast<int64_t>(char_count(std::string_view(d.text).substr(0, nl)));
          if (nl != std::string::npos) return left >= 0;
          break;
        }
        case Doc::Kind::concat:
        case Doc::Kind::fill:
          for (size_t k = d.parts.size(); k-- > c.from;) next.push_back({nullptr, c.flat, &d.parts[k], 0});
          break;
        case Doc::Kind::nest:
        case Doc::Kind::align:
          next.push_back({nullptr, c.flat, &d.parts[0], 0});
          break;
        case Doc::Kind::group:
          next.push_back({nullptr, c.flat && !d.force && !d.hard, &d.parts[0], 0});
          break;
        case Doc::Kind::line:
          if (!c.flat || d.line == Doc::Line::hard) return true;
          if (d.line == Doc::Line::normal) --left;
          break;
      }
    }
    return false;
  }

  size_t width_;
  std::string_view eol_;
  std::string out_;
  size_t column_ = 0;
};

// ---- from the parse tree to the layout

struct Symbols {
  int select = grammar::symbol("select");
  int select_into = grammar::symbol("select_into");
  int select_item_list = grammar::symbol("select_item_list");
  int group_list = grammar::symbol("group_list");
  int order_list = grammar::symbol("order_list");
  int derived_table_list = grammar::symbol("derived_table_list");
  int join_table = grammar::symbol("join_table");
  int table_ref = grammar::symbol("table_ref");
  int table_factor = grammar::symbol("table_factor");
  int expr = grammar::symbol("expr");
  int and_ = grammar::symbol("and");
  int or_ = grammar::symbol("or");
  int on = grammar::symbol("ON");
  int comma = grammar::symbol("','");
  int right_paren = grammar::symbol("')'");
  int with_clause = grammar::symbol("with_clause");
  int unit_type_decl = grammar::symbol("unit_type_decl");
  int into = grammar::symbol("into");
  int opt_procedure_or_into = grammar::symbol("opt_procedure_or_into");
  int select_into_query_specification = grammar::symbol("select_into_query_specification");
  int query_specification_start = grammar::symbol("query_specification_start");
  // Each starts a line of its own when its query breaks.
  std::vector<int> clauses = {
      grammar::symbol("opt_from_clause"),        grammar::symbol("from_clause"),
      grammar::symbol("opt_where_clause"),       grammar::symbol("opt_group_clause"),
      grammar::symbol("opt_having_clause"),      grammar::symbol("opt_window_clause"),
      grammar::symbol("opt_query_expression_tail"), grammar::symbol("query_expression_tail"),
      grammar::symbol("opt_order_limit_lock"),   grammar::symbol("order_limit_lock"),
      grammar::symbol("order_or_limit"),         grammar::symbol("order_clause"),
      grammar::symbol("opt_limit_clause"),       grammar::symbol("limit_clause"),
      grammar::symbol("opt_select_lock_type"),   grammar::symbol("select_lock_type"),
      grammar::symbol("opt_procedure_or_into"),
  };
  // A query in parentheses: '(' one of these ')'.
  std::vector<int> queries = {
      grammar::symbol("query_expression"),
      grammar::symbol("query_expression_body_ext"),
      grammar::symbol("query_expression_no_with_clause"),
      grammar::symbol("query_expression_body_ext_parens"),
      grammar::symbol("subselect"),
  };
};

const Symbols &syms() {
  static const Symbols s;
  return s;
}

bool contains(const std::vector<int> &v, int symbol) {
  return std::find(v.begin(), v.end(), symbol) != v.end();
}

// What is between two tokens: blanks and comments.
struct Comment {
  std::string_view text;
  bool own_line = false;    // a line break before it
  bool line_after = false;  // a line comment, or a line break after it
  bool space_after = false; // blanks after it
};

struct Gap {
  bool any = false;      // anything at all
  bool newline = false;  // a line break
  std::vector<Comment> comments;
};

// Whether a line comment starts at s[i]: '#', or "--" and a blank.
bool line_comment_at(std::string_view s, size_t i) {
  return s[i] == '#' ||
         (s.substr(i, 2) == "--" && (i + 2 == s.size() || std::isspace(static_cast<unsigned char>(s[i + 2]))));
}

// The gap `s`, or false when it holds something else than blanks and
// comments, or a versioned comment.
bool read_gap(std::string_view s, Gap *gap) {
  gap->any = !s.empty();
  bool newline_seen = false;
  size_t i = 0;
  while (i < s.size()) {
    const char ch = s[i];
    if (ch == '\n') {
      gap->newline = newline_seen = true;
      if (!gap->comments.empty()) gap->comments.back().line_after = true;
      ++i;
    } else if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\f' || ch == '\v') {
      if (!gap->comments.empty()) gap->comments.back().space_after = true;
      ++i;
    } else if (line_comment_at(s, i)) {
      size_t e = s.find('\n', i);
      if (e == std::string_view::npos) e = s.size();
      size_t te = e;
      while (te > i && (s[te - 1] == '\r' || s[te - 1] == ' ' || s[te - 1] == '\t')) --te;
      gap->comments.push_back({s.substr(i, te - i), newline_seen, true});
      newline_seen = false;
      i = e;
    } else if (s.substr(i, 2) == "/*") {
      if (s.substr(i, 3) == "/*!" || s.substr(i, 4) == "/*M!") return false;
      const size_t e = s.find("*/", i + 2);
      if (e == std::string_view::npos) return false;
      gap->comments.push_back({s.substr(i, e + 2 - i), newline_seen, false});
      newline_seen = false;
      i = e + 2;
    } else {
      return false;
    }
  }
  return true;
}

class Builder {
 public:
  Builder(const Syntax_tree &tree, std::string_view text, const Format_options &options)
      : t_(tree), text_(text), o_(options), s_(syms()) {
    const auto &tokens = t_.tokens();
    gaps_.resize(tokens.size());
    leaves_.assign(tokens.size(), npos);
    for (uint32_t n = 0; n < t_.size(); ++n) {
      if (t_.is_token(n)) leaves_[t_.node(n).first_token] = n;
    }
    uint32_t prev = npos;
    for (uint32_t j = 0; j < tokens.size(); ++j) {
      if (tokens[j].begin == tokens[j].end) continue;
      if (prev != npos && !read_gap(text_.substr(tokens[prev].end, tokens[j].begin - tokens[prev].end), &gaps_[j])) {
        ok_ = false;
      }
      prev = j;
    }
  }

  bool ok() const { return ok_; }

  // The query `select`; its clauses on lines of their own when `multiline`.
  Doc statement(uint32_t select, bool multiline) { return group(build(select).doc, multiline); }

 private:
  struct Piece {
    Doc doc;
    uint32_t first = npos;  // the first non-empty token
    bool empty() const { return first == npos; }
  };

  enum class Sep : uint8_t { keep, space, line, soft, none };

  const Syntax_tree::Token &token(uint32_t j) const { return t_.tokens()[j]; }

  // The separator before token j: `kind`, with the comments of the gap
  // before it.  A comment on a line of its own breaks the line before it; a
  // line comment that ended the line of the token before stays there
  // (trailing); each comment that ended its line breaks the line after it.
  // With a kept layout (Sep::keep) every one of those breaks goes one level
  // deeper, so a comment and the token after it land on the same level.
  Doc separator(uint32_t j, Sep kind) const {
    const Gap &g = gaps_[j];
    auto line_break = [&] { return kind == Sep::keep ? nest(o_.indent, hardline()) : hardline(); };
    if (g.comments.empty()) {
      switch (kind) {
        case Sep::keep:
          return g.newline ? line_break() : text(g.any ? " " : "");
        case Sep::space:
          return text(" ");
        case Sep::line:
          return line();
        case Sep::soft:
          return softline();
        case Sep::none:
          return text("");
      }
      return text("");
    }
    std::vector<Doc> parts;
    const Comment &first = g.comments.front();
    if (first.own_line) {
      parts.push_back(line_break());
    } else if (kind == Sep::line && !first.line_after) {
      parts.push_back(line());
    } else {
      parts.push_back(text(" "));  // trailing, or a comment inside the line
    }
    for (const Comment &c : g.comments) {
      parts.push_back(text(std::string(c.text)));
      parts.push_back(c.line_after ? line_break() : text(c.space_after ? " " : ""));
    }
    // A soft break keeps its place after the comments: `( /* c */` and
    // `x /* c */` still break before the query and before `)`.
    if (kind == Sep::soft && !g.comments.back().line_after) parts.push_back(softline());
    return concat(std::move(parts));
  }

  // Token j, recased when it is a keyword.  The comments before it are the
  // separator's.
  Doc leaf(uint32_t j) const {
    const auto &tk = token(j);
    std::string word(text_.substr(tk.begin, tk.end - tk.begin));
    if (is_keyword(j, word)) word = o_.upper_case_keywords ? upper(word) : lower(word);
    return text(std::move(word));
  }

  // A keyword, and not one standing for a name.
  bool is_keyword(uint32_t j, std::string_view word) const {
    const auto &spellings = keyword_spellings(token(j).symbol);
    if (std::none_of(spellings.begin(), spellings.end(), [&](std::string_view s) { return iequals(s, word); })) {
      return false;
    }
    const uint32_t leaf_node = leaves_[j];
    if (leaf_node == npos) return false;
    const uint32_t parent = t_.node(leaf_node).parent;
    return parent == npos || !is_name_wrapper(symbol(parent));
  }

  int symbol(uint32_t n) const { return t_.node(n).symbol; }

  // The non-empty token before token j, or npos.
  uint32_t previous_token(uint32_t j) const {
    while (j-- > 0) {
      if (token(j).begin != token(j).end) return j;
    }
    return npos;
  }

  // Pieces joined, `sep(k)` the separator before piece k (k >= 1).
  template <typename F>
  Piece join(std::vector<Piece> pieces, F sep) const {
    Piece out;
    std::vector<Doc> parts;
    for (size_t k = 0; k < pieces.size(); ++k) {
      Piece &p = pieces[k];
      if (p.empty()) continue;
      if (out.empty()) {
        out.first = p.first;
      } else {
        parts.push_back(separator(p.first, sep(k)));
      }
      parts.push_back(std::move(p.doc));
    }
    out.doc = concat(std::move(parts));
    return out;
  }

  Piece build(uint32_t n) {
    if (t_.is_token(n)) {
      const uint32_t j = t_.node(n).first_token;
      if (token(j).begin == token(j).end) return {};
      return {leaf(j), j};
    }
    const int sym = symbol(n);
    if (sym == s_.select_item_list || sym == s_.group_list || sym == s_.order_list ||
        (sym == s_.derived_table_list && has_comma(n))) {
      return list(n);
    }
    if (sym == s_.join_table) return joins(n);
    if (sym == s_.expr && chain_operator(n) != -1) return chain(n);
    return children(n);
  }

  bool has_comma(uint32_t n) const {
    const auto kids = t_.children(n);
    return kids.size() > 1 && symbol(kids[1]) == s_.comma;
  }

  bool is_clause(uint32_t child, uint32_t parent) const {
    const int sym = symbol(child);
    if (sym == s_.into) {
      const int p = symbol(parent);
      return p == s_.opt_procedure_or_into || p == s_.select_into_query_specification;
    }
    return contains(s_.clauses, sym);
  }

  // A node's children, laid out by where they stand.
  Piece children(uint32_t n) {
    const auto kids = t_.children(n);
    const int sym = symbol(n);
    // Inside a clause the words are one space apart.
    const bool spaced = contains(s_.clauses, sym) || sym == s_.query_specification_start;
    std::vector<Piece> pieces;
    std::vector<Sep> seps;
    int previous = -1;  // the symbol of the last child with tokens
    for (size_t k = 0; k < kids.size(); ++k) {
      // '(' query ')': a subquery.
      if (t_.is_token(kids[k]) && is_left_paren(symbol(kids[k])) && k + 2 < kids.size()) {
        size_t q = k + 1;
        if (symbol(kids[q]) == s_.with_clause && q + 2 < kids.size()) ++q;
        if (contains(s_.queries, symbol(kids[q])) && t_.is_token(kids[q + 1]) &&
            symbol(kids[q + 1]) == s_.right_paren) {
          pieces.push_back(subquery(kids, k, q + 1));
          seps.push_back(Sep::keep);
          previous = symbol(kids[q + 1]);
          k = q + 1;
          continue;
        }
      }
      Sep sep = spaced ? Sep::space : Sep::keep;
      if (is_clause(kids[k], n) || symbol(kids[k]) == s_.unit_type_decl || previous == s_.unit_type_decl ||
          previous == s_.with_clause) {
        sep = Sep::line;
      }
      pieces.push_back(build(kids[k]));
      seps.push_back(sep);
      if (!pieces.back().empty()) previous = symbol(kids[k]);
    }
    return join(std::move(pieces), [&](size_t k) { return seps[k]; });
  }

  // kids[open] '(' ... kids[close] ')' around a query.
  Piece subquery(std::span<const uint32_t> kids, size_t open, size_t close) {
    std::vector<Piece> inner;
    for (size_t k = open + 1; k < close; ++k) inner.push_back(build(kids[k]));
    // The query after a WITH clause starts a line.
    Piece body = join(std::move(inner), [&](size_t k) {
      return k > 0 && symbol(kids[open + k]) == s_.with_clause ? Sep::line : Sep::keep;
    });
    Piece lp = build(kids[open]);
    Piece rp = build(kids[close]);
    if (body.empty()) return join({std::move(lp), std::move(rp)}, [](size_t) { return Sep::none; });
    Piece out;
    out.first = lp.first;
    out.doc = group(concat({std::move(lp.doc),
                            nest(o_.indent, concat({separator(body.first, Sep::soft), std::move(body.doc)})),
                            separator(rp.first, Sep::soft), std::move(rp.doc)}));
    return out;
  }

  // A comma list (select items, GROUP BY, ORDER BY, tables): the items fill
  // the line, the comma after each, and continue aligned with the first.
  Piece list(uint32_t n) {
    const int sym = symbol(n);
    std::vector<std::vector<uint32_t>> items;
    std::vector<uint32_t> commas;
    uint32_t at = n;
    while (true) {
      const auto kids = t_.children(at);
      if (kids.size() > 1 && symbol(kids[0]) == sym && symbol(kids[1]) == s_.comma) {
        items.emplace_back(kids.begin() + 2, kids.end());
        commas.push_back(kids[1]);
        at = kids[0];
      } else {
        items.emplace_back(kids.begin(), kids.end());
        break;
      }
    }
    std::reverse(items.begin(), items.end());
    std::reverse(commas.begin(), commas.end());
    std::vector<Piece> docs;
    for (size_t i = 0; i < items.size(); ++i) {
      std::vector<Piece> parts;
      for (const uint32_t c : items[i]) parts.push_back(build(c));
      if (i < commas.size()) parts.push_back(build(commas[i]));
      const size_t last = parts.size() - 1;
      docs.push_back(join(std::move(parts), [&](size_t k) {
        return i < commas.size() && k == last ? Sep::none : Sep::keep;
      }));
    }
    Piece out = join(std::move(docs), [](size_t) { return Sep::line; });
    out.doc = align(fill(std::move(out.doc.parts)));
    return out;
  }

  // A chain of joins (left-deep in the tree): the first table, then each
  // JOIN on a line of its own one level deeper when the query breaks.
  Piece joins(uint32_t n) {
    std::vector<uint32_t> chain;
    uint32_t left = n;
    while (true) {
      if (symbol(left) == s_.join_table) {
        chain.push_back(left);
        left = t_.children(left)[0];
      } else if (symbol(left) == s_.table_ref && t_.children(left).size() == 1 &&
                 symbol(t_.children(left)[0]) == s_.join_table) {
        left = t_.children(left)[0];
      } else {
        break;
      }
    }
    std::reverse(chain.begin(), chain.end());
    Piece out = build(left);
    if (out.empty()) return children(n);
    std::vector<Doc> joined;
    for (const uint32_t j : chain) {
      const auto kids = t_.children(j);
      size_t right = 1;
      while (right < kids.size() && symbol(kids[right]) != s_.table_ref && symbol(kids[right]) != s_.table_factor) {
        ++right;
      }
      if (right >= kids.size()) return children(n);
      std::vector<Piece> head;
      for (size_t k = 1; k <= right; ++k) head.push_back(build(kids[k]));
      Piece h = join(std::move(head), [](size_t) { return Sep::space; });
      if (h.empty()) continue;
      joined.push_back(separator(h.first, Sep::line));
      joined.push_back(std::move(h.doc));
      // ON stays on the JOIN's line, or goes one level deeper on the next.
      const size_t k = right + 1;
      const bool has_on = k < kids.size() && t_.is_token(kids[k]) && symbol(kids[k]) == s_.on;
      std::vector<Piece> rest;
      for (size_t r = has_on ? k + 1 : k; r < kids.size(); ++r) rest.push_back(build(kids[r]));
      Piece tail = join(std::move(rest), [&](size_t) { return has_on ? Sep::space : Sep::keep; });
      if (tail.empty()) continue;
      if (has_on) {
        Piece on = build(kids[k]);
        // A comment ending the ON line breaks it at the ON's level.
        joined.push_back(group(concat({nest(o_.indent, separator(on.first, Sep::line)), std::move(on.doc),
                                       nest(o_.indent, separator(tail.first, Sep::space)), std::move(tail.doc)})));
      } else {
        joined.push_back(separator(tail.first, Sep::space));
        joined.push_back(std::move(tail.doc));
      }
    }
    // The JOIN lines break with the query's clauses; in parentheses, (a
    // JOIN b ON ...), only when they do not fit.
    out.doc = concat({std::move(out.doc), nest(o_.indent, concat(std::move(joined)))});
    const uint32_t before = previous_token(out.first);
    if (before != npos && is_left_paren(token(before).symbol)) out.doc = group(std::move(out.doc));
    return out;
  }

  // AND or OR, when `n` is `expr op expr`; else -1.
  int chain_operator(uint32_t n) const {
    const auto kids = t_.children(n);
    if (kids.size() != 3) return -1;
    const int op = symbol(kids[1]);
    return op == s_.and_ || op == s_.or_ ? op : -1;
  }

  // a AND b AND c: a break before each operator, one level deeper, when the
  // chain does not fit.
  Piece chain(uint32_t n) {
    const int op = chain_operator(n);
    std::vector<uint32_t> operands;
    std::vector<uint32_t> operators;
    uint32_t at = n;
    while (symbol(at) == s_.expr && chain_operator(at) == op) {
      const auto kids = t_.children(at);
      operands.push_back(kids[2]);
      operators.push_back(kids[1]);
      at = kids[0];
    }
    operands.push_back(at);
    std::reverse(operands.begin(), operands.end());
    std::reverse(operators.begin(), operators.end());
    Piece out = build(operands[0]);
    std::vector<Doc> rest;
    for (size_t k = 0; k < operators.size(); ++k) {
      Piece o = build(operators[k]);
      Piece e = build(operands[k + 1]);
      if (o.empty() || e.empty() || out.empty()) return children(n);
      rest.push_back(separator(o.first, Sep::line));
      rest.push_back(std::move(o.doc));
      rest.push_back(separator(e.first, Sep::keep));
      rest.push_back(std::move(e.doc));
    }
    out.doc = group(concat({std::move(out.doc), nest(o_.indent, concat(std::move(rest)))}));
    return out;
  }

  const Syntax_tree &t_;
  std::string_view text_;
  const Format_options &o_;
  const Symbols &s_;
  std::vector<Gap> gaps_;
  std::vector<uint32_t> leaves_;
  bool ok_ = true;
};

// `s` without blanks, for comparing a layout with the original.
std::string without_blanks(std::string_view s) {
  std::string out;
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    if (!std::isspace(c)) out += static_cast<char>(std::tolower(c));
  }
  return out;
}

// Whether the statement starts with a query: SELECT, WITH, or '('.
bool starts_with_query(std::string_view s) {
  size_t i = 0;
  while (i < s.size()) {
    if (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == '(') {
      ++i;
    } else if (s.substr(i, 2) == "/*") {
      const size_t e = s.find("*/", i + 2);
      if (e == std::string_view::npos) return false;
      i = e + 2;
    } else if (line_comment_at(s, i)) {
      const size_t e = s.find('\n', i);
      if (e == std::string_view::npos) return false;
      i = e + 1;
    } else {
      break;
    }
  }
  return istarts_with(s.substr(i), "select") || istarts_with(s.substr(i), "with");
}

bool parses(mdbp_parser *parser, std::string_view sql) {
  mdbp_result r;
  return mdbp_parse_sql(parser, sql.data(), sql.size(), &r) == MDBP_OK && r.accepted;
}

// The layout of the statement [st_begin, st_end) of `text`, parsed with
// `parser` (one that records derivations), its lines ending in `eol`: the
// edit replacing its first token to its last, or nullopt when the statement
// stays as it is - it is already laid out, it is not a query (SELECT, WITH,
// a parenthesized query), does not parse whole, holds a versioned comment,
// or the layout would not keep its tokens and comments.
std::optional<Text_edit> format_statement(std::string_view text, uint32_t st_begin, uint32_t st_end,
                                          mdbp_parser *parser, const Format_options &options,
                                          std::string_view eol) {
  const std::string_view sql = text.substr(st_begin, st_end - st_begin);
  if (parser == nullptr || !starts_with_query(sql)) return std::nullopt;
  mdbp_result r;
  if (mdbp_parse_sql(parser, sql.data(), sql.size(), &r) != MDBP_OK || !r.accepted) return std::nullopt;
  const Syntax_tree tree = Syntax_tree::build(parser, r, st_begin);
  if (tree.empty() || tree.roots().size() != 1) return std::nullopt;
  const auto &s = syms();
  uint32_t select = tree.find(tree.roots()[0], s.select);
  if (select == npos) select = tree.find(tree.roots()[0], s.select_into);
  if (select == npos) return std::nullopt;
  // Nothing but the query: no other token before or after it.
  uint32_t first = npos, last = npos;
  for (uint32_t j = 0; j < tree.tokens().size(); ++j) {
    if (tree.tokens()[j].begin == tree.tokens()[j].end) continue;
    if (first == npos) first = j;
    last = j;
  }
  if (first == npos || tree.begin(select) != tree.tokens()[first].begin ||
      tree.end(select) != tree.tokens()[last].end) {
    return std::nullopt;
  }
  const uint32_t b = tree.tokens()[first].begin;
  const uint32_t e = tree.tokens()[last].end;
  const std::string_view original = text.substr(b, e - b);
  // No versioned comment before or after the query either (the Builder
  // checks those between its tokens).
  Gap outside;
  if (!read_gap(text.substr(st_begin, b - st_begin), &outside) || !read_gap(text.substr(e, st_end - e), &outside)) {
    return std::nullopt;
  }

  Builder builder(tree, text, options);
  if (!builder.ok()) return std::nullopt;
  const Doc doc = builder.statement(select, original.find('\n') != std::string_view::npos);

  // At the indentation and the column of the line the statement starts on.
  // A statement that spans lines keeps its own line break; one on a single
  // line takes the document's (`eol`).
  const uint32_t line_start = line_begin(text, b);
  Printer printer(options.line_width, original.find('\n') != std::string_view::npos ? eol_of(original) : eol);
  std::string out = printer.print(doc, indentation(text, b), char_count(text.substr(line_start, b - line_start)));
  if (out == original) return std::nullopt;

  // The same tokens and comments, and still a statement that parses.
  if (without_blanks(out) != without_blanks(original)) return std::nullopt;
  std::string check(text.substr(st_begin, b - st_begin));
  check += out;
  check += text.substr(e, st_end - e);
  if (!parses(parser, check)) return std::nullopt;
  return Text_edit{b, e, std::move(out)};
}

}  // namespace

std::vector<Text_edit> format_sql(std::string_view text, const Statement_index &index, uint32_t begin,
                                  uint32_t end, Parser_cache &cache, const Format_options &options) {
  std::vector<Text_edit> edits;
  // The document's line break, for a statement on one line, which has none
  // of its own.
  const std::string_view eol = eol_of(text);
  const auto &stmts = index.statements();
  for (size_t i = index.find(begin); i < stmts.size(); ++i) {
    const Statement &st = stmts[i];
    if (st.begin >= end && !(begin == end && st.begin == end)) break;
    if (st.body_end < begin || st.kind != Statement_kind::sql) continue;
    mdbp_parser *parser = cache.get(index.modes()[st.mode].mode, options.version_id, true);
    if (auto edit = format_statement(text, st.begin, st.body_end, parser, options, eol)) {
      edits.push_back(std::move(*edit));
    }
  }
  return edits;
}

}  // namespace mdbls
