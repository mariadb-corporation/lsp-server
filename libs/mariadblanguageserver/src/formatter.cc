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
#include "statement_kinds.h"
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

// The parts given, moved in: a braced list would copy each (and all of
// what it holds) out of its const initializer_list.
template <typename... Parts>
  requires(sizeof...(Parts) > 1 && (std::is_same_v<Parts, Doc> && ...))
Doc concat(Parts &&...parts) {
  std::vector<Doc> v;
  v.reserve(sizeof...(parts));
  (v.push_back(std::move(parts)), ...);
  return concat(std::move(v));
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
  int verb_clause = grammar::symbol("verb_clause");
  int insert = grammar::symbol("insert");
  int replace = grammar::symbol("replace");
  int update = grammar::symbol("update");
  int delete_ = grammar::symbol("delete");
  int create = grammar::symbol("create");
  int alter = grammar::symbol("alter");
  int view = grammar::symbol("VIEW_SYM");
  int insert_field_spec = grammar::symbol("insert_field_spec");
  int insert_values = grammar::symbol("insert_values");
  int create_select_query_expression = grammar::symbol("create_select_query_expression");
  int single_multi = grammar::symbol("single_multi");
  int view_select = grammar::symbol("view_select");
  int opt_with_clause = grammar::symbol("opt_with_clause");
  int table = grammar::symbol("TABLE_SYM");
  int create_body = grammar::symbol("create_body");
  int create_field_list_parens = grammar::symbol("create_field_list_parens");
  int opt_create_select = grammar::symbol("opt_create_select");
  int alter_commands = grammar::symbol("alter_commands");
  int alter_list = grammar::symbol("alter_list");
  int part_defs = grammar::symbol("part_defs");
  int opt_sub_partition = grammar::symbol("opt_sub_partition");
  int create_table_options = grammar::symbol("create_table_options");
  int opt_create_table_options = grammar::symbol("opt_create_table_options");
  int from = grammar::symbol("FROM");
  int using_ = grammar::symbol("USING");
  int opt_insert_update = grammar::symbol("opt_insert_update");
  int opt_returning = grammar::symbol("opt_returning");
  int opt_order_clause = grammar::symbol("opt_order_clause");
  int delete_limit_clause = grammar::symbol("delete_limit_clause");
  int view_check_option = grammar::symbol("view_check_option");
  // Routines and compound statements.
  int directly_executable_statement = grammar::symbol("directly_executable_statement");
  int compound_statement = grammar::symbol("compound_statement");
  int create_routine = grammar::symbol("create_routine");
  int trigger_tail = grammar::symbol("trigger_tail");
  int event_tail = grammar::symbol("event_tail");
  int sp_tail_standalone = grammar::symbol("sp_tail_standalone");
  int sf_c_chistics_and_body_standalone = grammar::symbol("sf_c_chistics_and_body_standalone");
  int sp_c_chistics = grammar::symbol("sp_c_chistics");
  int sp_parenthesized_pdparam_list = grammar::symbol("sp_parenthesized_pdparam_list");
  int sf_parameters = grammar::symbol("sf_parameters");
  int sp_parenthesized_fdparam_list = grammar::symbol("sp_parenthesized_fdparam_list");
  int sp_pdparams = grammar::symbol("sp_pdparams");
  int sp_fdparams = grammar::symbol("sp_fdparams");
  int sp_proc_stmt = grammar::symbol("sp_proc_stmt");
  int sp_proc_stmt_in_returns_clause = grammar::symbol("sp_proc_stmt_in_returns_clause");
  int expr_lex = grammar::symbol("expr_lex");
  int then = grammar::symbol("THEN_SYM");
  int ev_sql_stmt = grammar::symbol("ev_sql_stmt");
  int sp_decl = grammar::symbol("sp_decl");
  int case_stmt_specification = grammar::symbol("case_stmt_specification");
  int case_stmt_body = grammar::symbol("case_stmt_body");
  int else_clause_opt = grammar::symbol("else_clause_opt");
  int do_ = grammar::symbol("DO_SYM");
  int sp_cursor_stmt = grammar::symbol("sp_cursor_stmt");
  int sp_decl_body = grammar::symbol("sp_decl_body");
  // An event's clauses besides ON SCHEDULE, each on a line of its own.
  std::vector<int> event_clauses = {
      grammar::symbol("opt_ev_on_completion"),
      grammar::symbol("opt_ev_status"),
      grammar::symbol("opt_ev_comment"),
  };
  // BEGIN ... END: a routine's body of one at the header's level.
  std::vector<int> blocks = {
      grammar::symbol("sp_unlabeled_block"), grammar::symbol("sp_labeled_block"),
      grammar::symbol("sp_unlabeled_block_not_atomic"),
  };
  // Blocks, IF, CASE and loops: their statements on lines of their own.
  std::vector<int> compounds = {
      grammar::symbol("sp_unlabeled_block"), grammar::symbol("sp_labeled_block"),
      grammar::symbol("sp_unlabeled_block_not_atomic"), grammar::symbol("sp_proc_stmt_if"),
      grammar::symbol("sp_labeled_control"), grammar::symbol("sp_unlabeled_control"),
      case_stmt_specification,
  };
  // The parts of those that only group their words and statement lists.
  std::vector<int> compound_parts = {
      grammar::symbol("sp_if"), grammar::symbol("sp_elseifs"), grammar::symbol("loop_body"),
      grammar::symbol("while_body"), grammar::symbol("repeat_body"), grammar::symbol("for_loop_statements"),
      grammar::symbol("sp_if_then_statements"), grammar::symbol("sp_case_then_statements"),
  };
  // Statements and declarations, each with its ';' (left-recursive), and the
  // WHEN arms of a CASE.
  std::vector<int> statement_lists = {
      grammar::symbol("sp_decls"), grammar::symbol("sp_proc_stmts"), grammar::symbol("sp_proc_stmts1"),
  };
  std::vector<int> when_lists = {
      grammar::symbol("simple_when_clause_list"), grammar::symbol("searched_when_clause_list"),
  };
  // Comma lists: select items, GROUP BY, ORDER BY, SET a = 1, b = 2, ON
  // DUPLICATE KEY UPDATE ..., the columns of INSERT and CREATE VIEW, the
  // tables of a DELETE.
  std::vector<int> lists = {
      select_item_list,
      group_list,
      order_list,
      grammar::symbol("update_list"),
      grammar::symbol("insert_update_list"),
      grammar::symbol("fields"),
      grammar::symbol("view_list"),
      grammar::symbol("table_alias_ref_list"),
  };
  // Nodes whose words are one space apart, besides the clauses: the start
  // of a query, the statements besides queries and their parts.
  std::vector<int> spaced = {
      query_specification_start, insert, replace, update, delete_, create, alter, insert_field_spec,
      single_multi, grammar::symbol("delete_part2"), grammar::symbol("delete_single_table"), opt_insert_update,
      opt_returning, delete_limit_clause, view_check_option, create_body, alter_commands,
      grammar::symbol("partition"), grammar::symbol("part_definition"),
  };
  // Clauses of those statements, as {clause, the statement part it is a
  // clause of}: each starts a line of its own when the statement breaks.
  // (ORDER BY and LIMIT are clauses only there, not inside GROUP_CONCAT.)
  std::vector<std::pair<int, int>> statement_clauses = {
      {grammar::symbol("SET"), update},
      {insert_values, insert_field_spec},
      {opt_insert_update, insert},
      {opt_returning, insert},
      {opt_returning, replace},
      {opt_returning, update},
      {opt_returning, single_multi},
      {opt_order_clause, update},
      {opt_order_clause, single_multi},
      {delete_limit_clause, update},
      {delete_limit_clause, single_multi},
      {grammar::symbol("opt_order_or_limit"), single_multi},
      {view_select, create},
      {view_select, alter},
      {view_check_option, view_select},
      {grammar::symbol("opt_create_partitioning"), create_body},
      {grammar::symbol("opt_partitioning"), alter_commands},
      {grammar::symbol("remove_partitioning"), alter_commands},
      {sp_cursor_stmt, sp_decl_body},
  };
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
  bool blank_after = false; // an empty line after it
};

struct Gap {
  bool any = false;      // anything at all
  bool newline = false;  // a line break
  bool blank = false;    // an empty line before the first comment, or the token
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
  int newlines = 0;  // since the last comment
  size_t i = 0;
  while (i < s.size()) {
    const char ch = s[i];
    if (ch == '\n') {
      gap->newline = newline_seen = true;
      if (++newlines == 2) (gap->comments.empty() ? gap->blank : gap->comments.back().blank_after) = true;
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
      newlines = 0;
      i = e;
    } else if (s.substr(i, 2) == "/*") {
      if (s.substr(i, 3) == "/*!" || s.substr(i, 4) == "/*M!") return false;
      const size_t e = s.find("*/", i + 2);
      if (e == std::string_view::npos) return false;
      gap->comments.push_back({s.substr(i, e + 2 - i), newline_seen, false});
      newline_seen = false;
      newlines = 0;
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
    mark_statement_clauses();
  }

  bool ok() const { return ok_; }

  // The statement `root`; its clauses on lines of their own when `multiline`.
  // A hard line inside (one per line) breaks it too, as when the statement
  // was on several lines: formatting it again changes nothing.
  // With `indent_clauses`, the lines of a query's or DML statement's
  // clauses one level deeper than its first.
  Doc statement(uint32_t root, bool multiline) {
    Doc d = group(build(root).doc, multiline);
    if (o_.indent_clauses && has_clauses(t_, root)) return nest(o_.indent, std::move(d));
    return d;
  }

 private:
  struct Piece {
    Doc doc;
    uint32_t first = npos;  // the first non-empty token
    bool empty() const { return first == npos; }
  };

  // keep: the original "space or not"; space: one space inside a clause;
  // word: one space between the words of one keyword (LEFT OUTER JOIN);
  // line: a break of the enclosing group; soft: the same, nothing when flat;
  // hard: a line break whatever the group (and the groups around it) does;
  // close: a line break before a closing parenthesis, the comments before
  // it one level deeper, with what they close.
  enum class Sep : uint8_t { keep, space, word, line, soft, hard, close, none };

  const Syntax_tree::Token &token(uint32_t j) const { return t_.tokens()[j]; }

  // The first token of each clause of a statement besides a query: the
  // line breaks before it as before a query's clauses.
  void mark_statement_clauses() {
    clause_starts_.assign(t_.tokens().size(), false);
    block_starts_.assign(t_.tokens().size(), false);
    begin_starts_.assign(t_.tokens().size(), false);
    for (uint32_t n = 0; n < t_.size(); ++n) {
      const auto &node = t_.node(n);
      if (contains(s_.compounds, node.symbol)) {
        if (const uint32_t j = first_token(n); j != npos) {
          block_starts_[j] = true;
          if (contains(s_.blocks, node.symbol)) begin_starts_[j] = true;
        }
      }
      if (node.parent == npos) continue;
      const std::pair<int, int> at{node.symbol, symbol(node.parent)};
      bool clause = std::find(s_.statement_clauses.begin(), s_.statement_clauses.end(), at) !=
                    s_.statement_clauses.end();
      // FROM and USING after the tables of a DELETE (DELETE t FROM t JOIN
      // u ...), not the FROM it starts with (DELETE FROM t USING ...).
      if (at.second == s_.single_multi && (at.first == s_.from || at.first == s_.using_)) {
        clause = t_.children(node.parent)[0] != n;
      }
      if (!clause) continue;
      if (const uint32_t j = first_token(n); j != npos) clause_starts_[j] = true;
    }
  }

  // The first non-empty token of node n, or npos.
  uint32_t first_token(uint32_t n) const {
    const auto &node = t_.node(n);
    for (uint32_t j = node.first_token; j < node.first_token + node.token_count; ++j) {
      if (token(j).begin != token(j).end) return j;
    }
    return npos;
  }

  // Whether node n spans lines in the original.
  bool spans_lines(uint32_t n) const { return t_.text(text_, n).find('\n') != std::string_view::npos; }

  // The separator before token j: `kind`, with the comments of the gap
  // before it.  A comment on a line of its own breaks the line before it; a
  // line comment that ended the line of the token before stays there
  // (trailing); each comment that ended its line breaks the line after it.
  // With a kept layout (Sep::keep) and inside a clause (Sep::space) every
  // one of those breaks goes one level deeper, so a comment and the token
  // after it land on the same level, under the clause's keyword (between
  // the words of one keyword, Sep::word, they stay on its level):
  // `SELECT -- c` puts the items on the next line, indented.  Between
  // lines of their own (Sep::hard) an empty line stays, before END or `)`
  // (Sep::close) only before a comment.
  Doc separator(uint32_t j, Sep kind) const {
    const Gap &g = gaps_[j];
    auto line_break = [&] {
      return kind == Sep::keep || kind == Sep::space ? nest(o_.indent, hardline()) : hardline();
    };
    const bool blanks = kind == Sep::hard || kind == Sep::close;
    // A line break, and an empty line before it when `blank`.
    auto lines = [&](Doc d, bool blank) {
      if (blanks && blank) return concat(hardline(), std::move(d));
      return d;
    };
    if (g.comments.empty()) {
      switch (kind) {
        case Sep::keep:
          return g.newline ? line_break() : text(g.any ? " " : "");
        case Sep::space:
        case Sep::word:
          return text(" ");
        case Sep::line:
          return line();
        case Sep::soft:
          return softline();
        case Sep::hard:
          return lines(hardline(), g.blank);
        case Sep::close:
          return hardline();  // no empty line before END or `)`
        case Sep::none:
          return text("");
      }
      return text("");
    }
    std::vector<Doc> parts;
    const Comment &first = g.comments.front();
    if (first.own_line) {
      parts.push_back(lines(line_break(), g.blank));
    } else if ((kind == Sep::line || kind == Sep::hard) && !first.line_after) {
      parts.push_back(kind == Sep::hard ? hardline() : line());
    } else {
      parts.push_back(text(" "));  // trailing, or a comment inside the line
    }
    for (const Comment &c : g.comments) {
      parts.push_back(text(std::string(c.text)));
      parts.push_back(c.line_after ? lines(line_break(), c.blank_after) : text(c.space_after ? " " : ""));
    }
    // A soft break keeps its place after the comments: `( /* c */` and
    // `x /* c */` still break before the query and before `)`.
    if (kind == Sep::soft && !g.comments.back().line_after) parts.push_back(softline());
    if (kind == Sep::close) {
      parts.pop_back();  // what followed the last comment
      return concat(nest(o_.indent, concat(std::move(parts))), hardline());
    }
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

  // Pieces joined, `sep(k)` the separator before piece k (k >= 1); a
  // break of the group before a statement's clause, a space before a block
  // (DECLARE ... HANDLER FOR ... BEGIN).
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
        const Sep kind = clause_starts_[p.first] ? Sep::line : block_starts_[p.first] ? Sep::space : sep(k);
        parts.push_back(separator(p.first, kind));
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
    if (contains(s_.lists, sym) || (sym == s_.derived_table_list && has_comma(n))) {
      return list(n);
    }
    if (sym == s_.join_table) return joins(n);
    if (sym == s_.create_field_list_parens || sym == s_.part_defs || sym == s_.opt_sub_partition) {
      return rows_in_parentheses(n);
    }
    if (sym == s_.create_table_options && t_.node(n).parent != npos &&
        symbol(t_.node(n).parent) == s_.opt_create_table_options) {
      return table_options(n);
    }
    if (sym == s_.alter) {
      if (const uint32_t changes = several_changes(n); changes != npos) return alter_table(n, changes);
    }
    if (sym == s_.expr && chain_operator(n) != -1) return chain(n);
    if (sym == s_.create_routine || sym == s_.trigger_tail || sym == s_.event_tail) return routine(n);
    if (sym == s_.case_stmt_specification) return case_statement(n);
    if (sym == s_.sp_parenthesized_pdparam_list || sym == s_.sp_parenthesized_fdparam_list) return parameters(n);
    if (contains(s_.compounds, sym)) return compound(n);
    // A statement of a routine's body: its clauses break with it, apart
    // from the routine's, and always when it was on several lines.
    if (sym == s_.sp_proc_stmt || sym == s_.sp_decl) return body_statement(n);
    return children(n);
  }

  // With `indent_routine_clauses`, a query or DML statement, or a cursor's
  // query, puts the lines of its clauses one level deeper than its first.
  Piece body_statement(uint32_t n) {
    Piece p = children(n);
    p.doc = group(std::move(p.doc), spans_lines(n));
    if (o_.indent_routine_clauses && indents_its_clauses(n)) p.doc = nest(o_.indent, std::move(p.doc));
    return p;
  }

  // Along the nodes that only wrap it: no search of the statement's
  // subtree, which for a block holds every statement in it.
  bool indents_its_clauses(uint32_t n) const {
    if (symbol(n) == s_.sp_decl) {
      const uint32_t body = t_.child(n, s_.sp_decl_body);
      return body != npos && t_.child(body, s_.sp_cursor_stmt) != npos;
    }
    uint32_t at = n;
    while (at != npos && symbol(at) != s_.verb_clause) at = only_child(at);
    return at != npos && t_.children(at).size() == 1 && has_clauses(t_, t_.children(at)[0]);
  }

  // The one child of n with tokens, or npos.
  uint32_t only_child(uint32_t n) const {
    uint32_t out = npos;
    for (const uint32_t k : t_.children(n)) {
      if (first_token(k) == npos) continue;
      if (out != npos) return npos;
      out = k;
    }
    return out;
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
    const bool spaced = contains(s_.clauses, sym) || contains(s_.spaced, sym);
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
          previous == s_.with_clause || previous == s_.opt_with_clause) {
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
    out.doc = group(concat(std::move(lp.doc),
                            nest(o_.indent, concat(separator(body.first, Sep::soft), std::move(body.doc))),
                            separator(rp.first, Sep::soft), std::move(rp.doc)));
    return out;
  }

  // A comma list (select items, GROUP BY, ORDER BY, tables): the items fill
  // the line, the comma after each, and continue aligned with the first.
  Piece list(uint32_t n) {
    Piece out = join(items(n), [](size_t) { return Sep::line; });
    out.doc = align(fill(std::move(out.doc.parts)));
    return out;
  }

  // The items of the comma list `n` (left-recursive: n ',' item), each with
  // the comma after it.
  std::vector<Piece> items(uint32_t n) {
    // Every group but the first starts with its comma.
    const auto groups = left_recursive(n, s_.comma);
    std::vector<Piece> out;
    for (size_t i = 0; i < groups.size(); ++i) {
      std::vector<Piece> parts;
      for (size_t k = i == 0 ? 0 : 1; k < groups[i].size(); ++k) parts.push_back(build(groups[i][k]));
      const bool comma = i + 1 < groups.size();
      if (comma) parts.push_back(build(groups[i + 1][0]));
      const size_t last = parts.size() - 1;
      out.push_back(join(std::move(parts), [&](size_t k) { return comma && k == last ? Sep::none : Sep::keep; }));
    }
    return out;
  }

  // Items on lines of their own, one level deeper.
  Doc one_per_line(std::vector<Piece> pieces) {
    std::vector<Doc> lines;
    for (Piece &p : pieces) {
      if (p.empty()) continue;
      lines.push_back(separator(p.first, Sep::hard));
      lines.push_back(std::move(p.doc));
    }
    return nest(o_.indent, concat(std::move(lines)));
  }

  // The ( columns, keys, constraints ) of CREATE TABLE, its ( partitions )
  // and their ( subpartitions ): one per line, the parenthesis closing on a
  // line of its own.
  Piece rows_in_parentheses(uint32_t n) {
    const auto kids = t_.children(n);
    if (kids.size() != 3) return children(n);
    Piece lp = build(kids[0]);
    Piece rp = build(kids[2]);
    if (lp.empty() || rp.empty()) return children(n);
    Piece out;
    out.first = lp.first;
    out.doc = concat(std::move(lp.doc), one_per_line(items(kids[1])), separator(rp.first, Sep::close),
                      std::move(rp.doc));
    return out;
  }

  // ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 ...: the options fill the line,
  // then continue one level deeper.  (The list is right-recursive: option
  // [','] options.)
  Piece table_options(uint32_t n) {
    std::vector<Piece> options;
    for (uint32_t at = n; at != npos;) {
      const auto kids = t_.children(at);
      if (kids.empty()) break;
      const bool comma = kids.size() == 3;
      std::vector<Piece> parts{build(kids[0])};
      if (comma) parts.push_back(build(kids[1]));
      options.push_back(join(std::move(parts), [](size_t) { return Sep::none; }));
      at = kids.size() > 1 && symbol(kids.back()) == s_.create_table_options ? kids.back() : npos;
    }
    Piece out = join(std::move(options), [](size_t) { return Sep::line; });
    out.doc = nest(o_.indent, fill(std::move(out.doc.parts)));
    return out;
  }

  // The list of changes of ALTER TABLE `n` when it has more than one, else
  // npos.
  uint32_t several_changes(uint32_t n) const {
    const uint32_t commands = t_.child(n, s_.alter_commands);
    if (commands == npos) return npos;
    const auto kids = t_.children(commands);
    return !kids.empty() && symbol(kids[0]) == s_.alter_list && has_comma(kids[0]) ? kids[0] : npos;
  }

  // ALTER TABLE t with its changes `changes` one per line, one level deeper,
  // between the words before them and the clauses after them.
  Piece alter_table(uint32_t n, uint32_t changes) {
    const uint32_t commands = t_.node(changes).parent;
    std::vector<Piece> head;
    std::vector<Piece> tail;
    bool after = false;
    for (const uint32_t k : t_.children(n)) {
      if (k != commands) {
        (after ? tail : head).push_back(build(k));
        continue;
      }
      for (const uint32_t c : t_.children(commands)) {
        if (c != changes) tail.push_back(build(c));
      }
      after = true;
    }
    Piece out = join(std::move(head), [](size_t) { return Sep::space; });
    Piece rest = join(std::move(tail), [](size_t) { return Sep::space; });
    if (out.empty()) return children(n);
    std::vector<Doc> parts{std::move(out.doc), one_per_line(items(changes))};
    if (!rest.empty()) {
      parts.push_back(separator(rest.first, clause_starts_[rest.first] ? Sep::line : Sep::space));
      parts.push_back(std::move(rest.doc));
    }
    out.doc = concat(std::move(parts));
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
      // The table is the head's clause, after the words of the JOIN.
      const size_t table = head.size() - 1;
      Piece h = join(std::move(head), [&](size_t k) { return k == table ? Sep::space : Sep::word; });
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
        // The condition is ON's clause: after a comment ending the ON line
        // it goes one level deeper than ON.
        joined.push_back(group(concat(nest(o_.indent, separator(on.first, Sep::line)), std::move(on.doc),
                                       nest(o_.indent, separator(tail.first, Sep::space)), std::move(tail.doc))));
      } else {
        joined.push_back(separator(tail.first, Sep::space));
        joined.push_back(std::move(tail.doc));
      }
    }
    // The JOIN lines break with the query's clauses; in parentheses, (a
    // JOIN b ON ...), only when they do not fit.
    out.doc = concat(std::move(out.doc), nest(o_.indent, concat(std::move(joined))));
    const uint32_t before = previous_token(out.first);
    if (before != npos && is_left_paren(token(before).symbol)) out.doc = group(std::move(out.doc));
    return out;
  }

  // ---- routines

  // The parts of a routine: CREATE ... PROCEDURE p(...) or FUNCTION f(...)
  // RETURNS ..., TRIGGER ... FOR EACH ROW, EVENT e; the characteristics or
  // event clauses after it; DO before an event's body; the body.
  // A header and the clauses after it, as pieces.
  struct Clauses {
    std::vector<Piece> head;
    std::vector<std::vector<Piece>> clauses;
    // `p` after what came before; `starts`: it starts a clause.
    void add(Piece p, bool starts) {
      if (p.empty()) return;
      if (starts) clauses.emplace_back();
      (clauses.empty() ? head : clauses.back()).push_back(std::move(p));
    }
  };

  // Each clause one level deeper on a line of its own when the statement
  // breaks, its words one space apart.
  void append_clauses(std::vector<std::vector<Piece>> clauses, std::vector<Doc> *parts) const {
    for (auto &c : clauses) {
      Piece clause = join(std::move(c), [](size_t) { return Sep::space; });
      if (clause.empty()) continue;
      parts->push_back(nest(o_.indent, concat(separator(clause.first, Sep::line), std::move(clause.doc))));
    }
  }

  struct Routine_parts {
    Clauses words;
    Piece intro;
    Piece body;
    size_t params = SIZE_MAX;  // the parameter list's place in the head
  };

  void routine_parts(uint32_t n, Routine_parts *r) {
    const int parent = symbol(n);
    for (const uint32_t k : t_.children(n)) {
      const int sym = symbol(k);
      if (sym == s_.sp_tail_standalone || sym == s_.sf_c_chistics_and_body_standalone) {
        routine_parts(k, r);
        continue;
      }
      if (sym == s_.sp_c_chistics) {
        for (Piece &p : joined_groups(k, Sep::space)) r->words.add(std::move(p), true);
        continue;
      }
      if (sym == s_.sp_proc_stmt || sym == s_.sp_proc_stmt_in_returns_clause) {
        r->body = body_statement(k);
        continue;
      }
      // Its sp_proc_stmt is the body statement: not nested twice.
      if (sym == s_.ev_sql_stmt) {
        r->body = build(k);
        continue;
      }
      Piece p = build(k);
      if (p.empty()) continue;
      if (sym == s_.do_ && parent == s_.event_tail) {
        r->intro = std::move(p);
        continue;
      }
      if ((sym == s_.sp_parenthesized_pdparam_list || sym == s_.sf_parameters) && r->words.clauses.empty()) {
        r->params = r->words.head.size();
      }
      // SCHEDULE ... goes with the ON before it.
      r->words.add(std::move(p), contains(s_.event_clauses, sym) || (sym == s_.on && parent == s_.event_tail));
    }
  }

  // The header on its line; the characteristics or clauses one level
  // deeper on lines of their own when the statement breaks; the body on a
  // line of its own, a block at the header's level, any other statement one
  // level deeper; an event's DO at the header's level, its body after it.
  Piece routine(uint32_t n) {
    Routine_parts r;
    routine_parts(n, &r);
    // No space before the parameter list: p(IN a INT).
    Piece out = join(std::move(r.words.head), [&](size_t k) { return k == r.params ? Sep::none : Sep::space; });
    if (out.empty() || r.body.empty()) return children(n);
    std::vector<Doc> parts{std::move(out.doc)};
    append_clauses(std::move(r.words.clauses), &parts);
    if (!r.intro.empty()) {
      parts.push_back(separator(r.intro.first, Sep::line));
      parts.push_back(std::move(r.intro.doc));
      parts.push_back(separator(r.body.first, Sep::space));
      parts.push_back(std::move(r.body.doc));
    } else if (begin_starts_[r.body.first]) {
      parts.push_back(separator(r.body.first, Sep::line));
      parts.push_back(std::move(r.body.doc));
    } else {
      parts.push_back(nest(o_.indent, concat(separator(r.body.first, Sep::line), std::move(r.body.doc))));
    }
    out.doc = concat(std::move(parts));
    return out;
  }

  // A routine's ( parameters ): on the header's line when they fit, else
  // one per line one level deeper, the parenthesis closing at the header's
  // level.
  Piece parameters(uint32_t n) {
    const auto kids = t_.children(n);
    if (kids.size() < 3) return children(n);
    const uint32_t params = t_.find(n, symbol(n) == s_.sp_parenthesized_pdparam_list ? s_.sp_pdparams : s_.sp_fdparams);
    Piece lp = build(kids.front());
    Piece rp = build(kids.back());
    if (params == npos || lp.empty() || rp.empty()) return children(n);
    std::vector<Doc> inner;
    for (Piece &p : items(params)) {
      if (p.empty()) continue;
      inner.push_back(separator(p.first, inner.empty() ? Sep::soft : Sep::line));
      inner.push_back(std::move(p.doc));
    }
    Piece out;
    out.first = lp.first;
    out.doc = group(concat(std::move(lp.doc), nest(o_.indent, concat(std::move(inner))),
                            separator(rp.first, Sep::soft), std::move(rp.doc)));
    return out;
  }

  // The groups of children of the left-recursive list n (n: n x y ... |
  // x y ...), in order.  With `sep`, only n `sep` x y ... recurses, and the
  // groups after the first start with their `sep`.
  std::vector<std::vector<uint32_t>> left_recursive(uint32_t n, int sep = -1) const {
    const int sym = symbol(n);
    std::vector<std::vector<uint32_t>> groups;
    uint32_t at = n;
    while (true) {
      const auto kids = t_.children(at);
      if (!kids.empty() && symbol(kids[0]) == sym && (sep < 0 || (kids.size() > 1 && symbol(kids[1]) == sep))) {
        groups.emplace_back(kids.begin() + 1, kids.end());
        at = kids[0];
      } else {
        if (!kids.empty()) groups.emplace_back(kids.begin(), kids.end());
        break;
      }
    }
    std::reverse(groups.begin(), groups.end());
    return groups;
  }

  // Each group of the left-recursive list n, its parts `sep` apart: a
  // statement and its ';', a characteristic.
  std::vector<Piece> joined_groups(uint32_t n, Sep sep) {
    std::vector<Piece> out;
    for (const auto &g : left_recursive(n)) {
      std::vector<Piece> parts;
      for (const uint32_t k : g) parts.push_back(build(k));
      out.push_back(join(std::move(parts), [sep](size_t) { return sep; }));
    }
    return out;
  }

  // The words and statement lists of the compound statement n, in order,
  // through the nodes that only group them.
  void compound_elements(uint32_t n, std::vector<uint32_t> *out) const {
    for (const uint32_t k : t_.children(n)) {
      if (contains(s_.compound_parts, symbol(k))) {
        compound_elements(k, out);
      } else {
        out->push_back(k);
      }
    }
  }

  // BEGIN ... END, IF ... THEN ... ELSE ... END IF, the loops, a WHEN arm:
  // the words one space apart, each statement list one statement per line
  // one level deeper, the words after it back on a line of their own.  A
  // THEN or DO after a condition that breaks starts a line of its own.
  Piece compound(uint32_t n) {
    std::vector<uint32_t> elements;
    compound_elements(n, &elements);
    Piece out;
    std::vector<Doc> parts;
    bool after_list = false;
    size_t condition = npos;  // parts: the keyword before the last condition
    for (const uint32_t e : elements) {
      const int sym = symbol(e);
      if ((sym == s_.then || sym == s_.do_) && condition != npos && t_.is_token(e)) {
        Piece p = build(e);
        if (p.empty()) continue;
        std::vector<Doc> head(std::make_move_iterator(parts.begin() + static_cast<std::ptrdiff_t>(condition)),
                              std::make_move_iterator(parts.end()));
        parts.resize(condition);
        head.push_back(separator(p.first, Sep::line));
        head.push_back(std::move(p.doc));
        parts.push_back(group(concat(std::move(head))));
        condition = npos;
        after_list = false;
        continue;
      }

      if (contains(s_.statement_lists, symbol(e))) {
        parts.push_back(one_per_line(joined_groups(e, Sep::none)));
        after_list = true;
        continue;
      }
      Piece p = build(e);
      if (p.empty()) continue;
      // The condition after IF, ELSEIF, WHEN, WHILE: from the keyword on.
      condition = sym == s_.expr_lex && !parts.empty() && !after_list ? parts.size() - 1 : npos;
      if (out.empty()) {
        out.first = p.first;
      } else {
        parts.push_back(separator(p.first, after_list ? Sep::close : Sep::space));
      }
      after_list = false;
      parts.push_back(std::move(p.doc));
    }
    if (out.empty()) return children(n);
    out.doc = concat(std::move(parts));
    return out;
  }

  // CASE [expr] WHEN ... THEN ... ELSE ... END CASE: the arms one level
  // deeper on lines of their own, their statements one more.
  Piece case_statement(uint32_t n) {
    std::vector<Piece> head;
    std::vector<Piece> arms;
    std::vector<Piece> tail;
    for (const uint32_t k : t_.children(n)) {
      const int sym = symbol(k);
      if (sym == s_.case_stmt_body) {
        for (const uint32_t b : t_.children(k)) {
          if (contains(s_.when_lists, symbol(b))) {
            for (const auto &g : left_recursive(b)) arms.push_back(compound(g.back()));
          } else {
            head.push_back(build(b));
          }
        }
      } else if (sym == s_.else_clause_opt) {
        if (first_token(k) != npos) arms.push_back(compound(k));
      } else {
        (arms.empty() ? head : tail).push_back(build(k));
      }
    }
    Piece out = join(std::move(head), [](size_t) { return Sep::space; });
    Piece end = join(std::move(tail), [](size_t) { return Sep::space; });
    if (out.empty() || end.empty() || arms.empty()) return children(n);
    out.doc = concat(std::move(out.doc), one_per_line(std::move(arms)), separator(end.first, Sep::close),
                      std::move(end.doc));
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
    out.doc = group(concat(std::move(out.doc), nest(o_.indent, concat(std::move(rest)))));
    return out;
  }

  const Syntax_tree &t_;
  std::string_view text_;
  const Format_options &o_;
  const Symbols &s_;
  std::vector<Gap> gaps_;
  std::vector<uint32_t> leaves_;
  std::vector<bool> clause_starts_;  // by token
  std::vector<bool> block_starts_;   // by token: a compound statement starts
  std::vector<bool> begin_starts_;   // by token: a BEGIN ... END block starts
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

// The first `max` words of a statement outside parentheses, lowercased, to
// `f` until it returns true: a parenthesis as "(" and the first word inside
// it ("(select"), quoted strings and names as "", a qualified name as one
// word ("db.value", "db."), other characters left out.  A backslash escapes
// in strings unless `backslash_escapes` is false (NO_BACKSLASH_ESCAPES).  It ends at a versioned comment, which keeps the statement as it is
// anyway.  Cheap, so the INSERT ... VALUES of a dump is never parsed.
template <typename F>
void leading_words(std::string_view s, size_t max, bool backslash_escapes, F f) {
  size_t count = 0;
  auto add = [&](std::string w) {
    ++count;
    return f(std::move(w));
  };
  size_t i = 0;
  int depth = 0;
  // Skips blanks and comments; false at a versioned or unterminated one.
  auto skip = [&] {
    while (i < s.size()) {
      if (std::isspace(static_cast<unsigned char>(s[i]))) {
        ++i;
      } else if (s.substr(i, 2) == "/*") {
        const size_t e = s.find("*/", i + 2);
        if (s.substr(i, 3) == "/*!" || s.substr(i, 4) == "/*M!" || e == std::string_view::npos) return false;
        i = e + 2;
      } else if (line_comment_at(s, i)) {
        i = std::min(s.find('\n', i), s.size());
      } else {
        break;
      }
    }
    return true;
  };
  auto is_word = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$'; };
  auto is_quote = [](char c) { return c == '\'' || c == '"' || c == '`'; };
  auto word = [&] {
    const size_t b = i;
    while (i < s.size() && is_word(s[i])) ++i;
    return lower(s.substr(b, i - b));
  };
  // A word, or a quoted string or name as "".
  auto part = [&] {
    if (!is_quote(s[i])) return word();
    const char q = s[i];
    // Doubled quotes need no care: they only end it later.
    for (++i; i < s.size() && s[i] != q; ++i) {
      if (s[i] == '\\' && q != '`' && backslash_escapes) ++i;
    }
    ++i;
    return std::string();
  };
  while (count < max && skip() && i < s.size()) {
    const char c = s[i];
    if (is_quote(c) || is_word(c)) {
      std::string w = part();
      while (i + 1 < s.size() && s[i] == '.' && (is_quote(s[i + 1]) || is_word(s[i + 1]))) {
        ++i;
        w += '.';
        w += part();
      }
      if (depth == 0 && add(std::move(w))) return;
    } else if (c == '(') {
      ++i;
      if (depth++ == 0) {
        while (skip() && i < s.size() && s[i] == '(') {
          ++i;
          ++depth;
        }
        if (add("(" + word())) return;
      }
    } else if (c == ')') {
      ++i;
      if (depth > 0) --depth;
    } else {
      ++i;
    }
  }
}

// Whether the statement can be one the formatter lays out: a query
// (SELECT, WITH, a parenthesized one), INSERT or REPLACE ... SELECT,
// UPDATE, DELETE, CREATE or ALTER VIEW or TABLE, CREATE PROCEDURE,
// FUNCTION, TRIGGER or EVENT, a compound statement (BEGIN NOT ATOMIC, IF,
// CASE, a loop).  A cheap filter before the parse; `statement_node` decides
// on the tree, so the two take the same statements.
bool may_format(std::string_view s, bool backslash_escapes) {
  // insert_head: before the table's name, which may be `value`.
  enum { start, insert_head, insert, create, yes, no } state = start;
  leading_words(s, 24, backslash_escapes, [&](std::string w) {
    if (state == start) {
      const std::string_view first = std::string_view(w).substr(w.starts_with('(') ? 1 : 0);
      if (first == "select" || first == "with" || first == "update" || first == "delete" || first == "begin" ||
          first == "if" || first == "case" || first == "loop" || first == "while" || first == "repeat" ||
          first == "for") {
        state = yes;
      } else if (first == "insert" || first == "replace") {
        state = insert_head;
      } else if (first == "create" || first == "alter") {
        state = create;
      } else {
        state = no;
      }
    } else if (state == insert_head) {
      if (w != "low_priority" && w != "delayed" && w != "high_priority" && w != "ignore" && w != "into") {
        state = insert;
      }
    } else if (state == insert) {
      if (w == "select" || w == "with" || w == "(select" || w == "(with") state = yes;
      if (w == "values" || w == "value" || w == "set") state = no;
    } else if (state == create) {
      if (w == "view" || w == "table" || w == "procedure" || w == "function" || w == "trigger" || w == "event") {
        state = yes;
      }
    }
    return state == yes || state == no;
  });
  return state == yes;
}

// The statement node of a tree the formatter lays out, or npos: a query,
// INSERT or REPLACE ... SELECT, UPDATE, DELETE, CREATE or ALTER VIEW,
// CREATE TABLE with its columns (not ... LIKE, not ... SELECT), ALTER TABLE,
// CREATE PROCEDURE, FUNCTION (not a UDF's SONAME), TRIGGER, EVENT, a
// compound statement.  `may_format` lets the same ones through to the
// parse.
uint32_t statement_node(const Syntax_tree &tree) {
  const auto &s = syms();
  const uint32_t top = tree.child(tree.roots()[0], s.directly_executable_statement);
  if (top == npos) return npos;
  if (const uint32_t compound = tree.child(top, s.compound_statement); compound != npos) return compound;
  const uint32_t verb = tree.find(top, s.verb_clause);
  if (verb == npos || tree.children(verb).size() != 1) return npos;
  const uint32_t n = tree.children(verb)[0];
  const int sym = tree.node(n).symbol;
  if (sym == s.select || sym == s.select_into || sym == s.update || sym == s.delete_) return n;
  auto has_child = [&](uint32_t x, int symbol) { return tree.child(x, symbol) != npos; };
  if (sym == s.insert || sym == s.replace) {
    const uint32_t spec = tree.child(n, s.insert_field_spec);
    const uint32_t values = spec == npos ? npos : tree.child(spec, s.insert_values);
    if (values == npos) return npos;
    const auto v = tree.children(values);
    return v.size() == 1 && tree.node(v[0]).symbol == s.create_select_query_expression ? n : npos;
  }
  if ((sym == s.create || sym == s.alter) && has_child(n, s.view) && has_child(n, s.view_select)) return n;
  if (sym == s.create) {
    const uint32_t routine = tree.child(n, s.create_routine);
    if (routine != npos && (has_child(routine, s.sp_tail_standalone) ||
                            has_child(routine, s.sf_c_chistics_and_body_standalone))) {
      return n;
    }
    if (has_child(n, s.trigger_tail) || has_child(n, s.event_tail)) return n;
  }
  if (sym == s.alter && has_child(n, s.table) && has_child(n, s.alter_commands)) return n;
  if (sym == s.create && has_child(n, s.table)) {
    const uint32_t body = tree.child(n, s.create_body);
    if (body == npos || !has_child(body, s.create_field_list_parens)) return npos;
    const uint32_t select = tree.child(body, s.opt_create_select);
    return select == npos || tree.begin(select) == tree.end(select) ? n : npos;
  }
  return npos;
}

bool parses(mdbp_parser *parser, std::string_view sql) {
  mdbp_result r;
  return mdbp_parse_sql(parser, sql.data(), sql.size(), &r) == MDBP_OK && r.accepted;
}

// The layout of the statement [st_begin, st_end) of `text`, parsed with
// `parser` (one that records derivations), its lines ending in `eol`: the
// edit replacing its first token to its last, or nullopt when the statement
// stays as it is - it is already laid out, it is none of the statements
// `statement_node` takes, does not parse whole, holds a versioned comment,
// or the layout would not keep its tokens and comments.
std::optional<Text_edit> format_statement(std::string_view text, uint32_t st_begin, uint32_t st_end,
                                          mdbp_parser *parser, uint32_t mode_bits,
                                          const Format_options &options, std::string_view eol) {
  const std::string_view sql = text.substr(st_begin, st_end - st_begin);
  if (parser == nullptr || !may_format(sql, (mode_bits & k_no_backslash_escapes) == 0)) return std::nullopt;
  mdbp_result r;
  if (mdbp_parse_sql(parser, sql.data(), sql.size(), &r) != MDBP_OK || !r.accepted) return std::nullopt;
  const Syntax_tree tree = Syntax_tree::build(parser, r, st_begin);
  if (tree.empty() || tree.roots().size() != 1) return std::nullopt;
  const uint32_t root = statement_node(tree);
  if (root == npos) return std::nullopt;
  // Nothing but the statement: no other token before or after it.
  uint32_t first = npos, last = npos;
  for (uint32_t j = 0; j < tree.tokens().size(); ++j) {
    if (tree.tokens()[j].begin == tree.tokens()[j].end) continue;
    if (first == npos) first = j;
    last = j;
  }
  if (first == npos || tree.begin(root) != tree.tokens()[first].begin ||
      tree.end(root) != tree.tokens()[last].end) {
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
  const Doc doc = builder.statement(root, original.find('\n') != std::string_view::npos);

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
    const Statement_mode &mode = index.modes()[st.mode];
    mdbp_parser *parser = cache.get(mode.mode, options.version_id, true);
    if (auto edit = format_statement(text, st.begin, st.body_end, parser, mode.bits, options, eol)) {
      edits.push_back(std::move(*edit));
    }
  }
  return edits;
}

}  // namespace mdbls
