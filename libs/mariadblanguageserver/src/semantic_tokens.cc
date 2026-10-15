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

#include "mdbls/semantic_tokens.h"

#include <array>
#include <cctype>
#include <cstring>
#include <optional>
#include <string>

#include "mdbls/parser_pool.h"
#include "mdbparse.h"

namespace mdbls {

namespace {

constexpr std::array<const char *, 8> k_types = {
    "keyword", "string", "number", "comment", "variable", "operator", "function", "parameter"};
constexpr std::array<const char *, 1> k_modifiers = {"defaultLibrary"};

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

// The comments in [from, to) of `text`, a stretch between two tokens.
void gap_comments(std::string_view text, uint32_t from, uint32_t to,
                  std::vector<Semantic_token> *out) {
  auto line_end = [&](uint32_t i) {
    while (i < to && text[i] != '\n' && text[i] != '\r') ++i;
    return i;
  };
  uint32_t i = from;
  while (i < to) {
    const char c = text[i];
    const char n = i + 1 < to ? text[i + 1] : '\0';
    if (c == '#') {
      const uint32_t e = line_end(i);
      out->push_back({i, e, Token_type::comment, 0});
      i = e;
    } else if (c == '-' && n == '-' &&
               (i + 2 == to || is_space(text[i + 2]) ||
                static_cast<unsigned char>(text[i + 2]) < 0x20)) {
      const uint32_t e = line_end(i);
      out->push_back({i, e, Token_type::comment, 0});
      i = e;
    } else if (c == '/' && n == '*') {
      const size_t close = text.substr(0, to).find("*/", i + 2);
      const uint32_t e = close == std::string_view::npos ? to : static_cast<uint32_t>(close + 2);
      const bool versioned = (i + 2 < to && text[i + 2] == '!') ||
                             (i + 3 < to && text[i + 2] == 'M' && text[i + 3] == '!');
      if (versioned && close == std::string_view::npos) {
        // The opening of an executable comment whose content the lexer read
        // as tokens: skip "/*!" or "/*M!" and the version digits.
        i += text[i + 2] == '!' ? 3 : 4;
        while (i < to && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
        continue;
      }
      out->push_back({i, e, Token_type::comment, 0});
      i = e;
    } else {
      ++i;
    }
  }
}

bool is_one_of(const char *name, std::initializer_list<const char *> names) {
  for (const char *n : names) {
    if (std::strcmp(name, n) == 0) return true;
  }
  return false;
}

bool is_identifier(const char *name) {
  return is_one_of(name, {"IDENT", "IDENT_QUOTED", "LEX_HOSTNAME"});
}

bool is_string(const char *name) {
  return is_one_of(name, {"TEXT_STRING", "NCHAR_STRING", "HEX_STRING"});
}

bool is_number(const char *name) {
  return is_one_of(name, {"NUM", "LONG_NUM", "ULONGLONG_NUM", "DECIMAL_NUM", "FLOAT_NUM",
                          "HEX_NUM", "BIN_NUM"});
}

// Keywords after which a name followed by "(" is a table or index, not a
// function call.
bool names_an_object(const char *prev) {
  return prev != nullptr &&
         is_one_of(prev, {"TABLE_SYM", "INTO", "REFERENCES", "KEY_SYM", "INDEX_SYM", "UNIQUE_SYM",
                          "VIEW_SYM", "ON", "EXISTS", "TABLES", "JOIN_SYM"});
}

struct Lexed {
  const mdbp_token *tokens = nullptr;
  size_t count = 0;  // without END_OF_INPUT
};

Lexed lex(std::string_view sql, const Statement_mode &mode, Parser_cache &cache,
          unsigned long version_id, mdbp_result *r) {
  mdbp_parser *p = cache.get(mode.mode, version_id);
  if (p == nullptr || mdbp_lex_sql(p, sql.data(), sql.size(), r) != MDBP_OK) return {};
  Lexed out{r->tokens, r->ntokens};
  while (out.count > 0 && (out.tokens[out.count - 1].byte_start < 0 ||
                           std::strcmp(out.tokens[out.count - 1].name, "END_OF_INPUT") == 0)) {
    --out.count;
  }
  return out;
}

}  // namespace

std::span<const char *const> semantic_token_types() { return k_types; }
std::span<const char *const> semantic_token_modifiers() { return k_modifiers; }

void statement_semantic_tokens(std::string_view text, const Statement_index &index, size_t i,
                               Parser_cache &cache, unsigned long version_id,
                               std::vector<Semantic_token> *out) {
  const Statement &s = index.statements()[i];
  if (s.kind == Statement_kind::comment) {
    gap_comments(text, s.begin, s.body_end, out);
    return;
  }
  const std::string_view sql = index.text_of(text, s);
  mdbp_result r;
  const Lexed lexed = lex(sql, index.modes()[s.mode], cache, version_id, &r);
  if (lexed.tokens == nullptr) {
    gap_comments(text, s.begin, s.body_end, out);
    return;
  }

  uint32_t gap_from = s.begin;
  const char *prev = nullptr;  // the previous token's name
  uint32_t prev_end = 0;
  for (size_t k = 0; k < lexed.count; ++k) {
    const mdbp_token &t = lexed.tokens[k];
    const uint32_t begin = s.begin + static_cast<uint32_t>(t.byte_start);
    uint32_t end = s.begin + static_cast<uint32_t>(t.byte_end);
    gap_comments(text, gap_from, begin, out);
    const mdbp_token *next = k + 1 < lexed.count ? &lexed.tokens[k + 1] : nullptr;
    const uint32_t next_begin =
        next != nullptr ? s.begin + static_cast<uint32_t>(next->byte_start) : 0;
    const char first = begin < end ? text[begin] : '\0';
    std::optional<Semantic_token> tok;

    if (t.name[0] == '\'' && std::strcmp(t.name, "'@'") == 0 &&
        !(prev != nullptr && prev_end == begin)) {
      // @var, @'var', @@var, @@session.var: one variable token.
      size_t j = k + 1;
      uint8_t modifiers = 0;
      if (j < lexed.count && std::strcmp(lexed.tokens[j].name, "'@'") == 0) {
        modifiers = k_modifier_default_library;
        ++j;
        if (j + 1 < lexed.count && is_one_of(lexed.tokens[j].name, {"SESSION_SYM", "GLOBAL_SYM", "LOCAL_SYM"}) &&
            std::strcmp(lexed.tokens[j + 1].name, "'.'") == 0) {
          j += 2;
        }
      }
      if (j < lexed.count && lexed.tokens[j].byte_start == lexed.tokens[j - 1].byte_end) {
        end = s.begin + static_cast<uint32_t>(lexed.tokens[j].byte_end);
        out->push_back({begin, end, Token_type::variable, modifiers});
        gap_from = end;
        prev = lexed.tokens[j].name;
        prev_end = end;
        k = j;
        continue;
      }
      tok = Semantic_token{begin, end, Token_type::operator_, 0};
    } else if (is_string(t.name)) {
      tok = Semantic_token{begin, end, Token_type::string, 0};
    } else if (is_number(t.name)) {
      tok = Semantic_token{begin, end, Token_type::number, 0};
    } else if (std::strcmp(t.name, "PARAM_MARKER") == 0) {
      tok = Semantic_token{begin, end, Token_type::parameter, 0};
    } else if (std::strcmp(t.name, "HINT_COMMENT") == 0) {
      tok = Semantic_token{begin, end, Token_type::comment, 0};
    } else if (is_identifier(t.name)) {
      if (next != nullptr && next->name[0] == '\'' && std::strcmp(next->name, "'('") == 0 &&
          next_begin == end && first != '`' && first != '"' && !names_an_object(prev)) {
        tok = Semantic_token{begin, end, Token_type::function, 0};
      }
    } else if (std::isalpha(static_cast<unsigned char>(first)) || first == '_') {
      if (next != nullptr && std::strcmp(next->name, "'('") == 0 && next_begin == end &&
          !names_an_object(prev) && std::strncmp(t.name, "IF_SYM", 6) != 0) {
        tok = Semantic_token{begin, end, Token_type::function, k_modifier_default_library};
      } else {
        tok = Semantic_token{begin, end, Token_type::keyword, 0};
      }
    } else if (first != '\0' && std::strchr(",.();", first) == nullptr) {
      tok = Semantic_token{begin, end, Token_type::operator_, 0};
    }
    if (tok) out->push_back(*tok);
    gap_from = end;
    prev = t.name;
    prev_end = end;
  }
  gap_comments(text, gap_from, s.body_end, out);
}

void encode_semantic_tokens(std::string_view text, const Line_map &lines,
                            std::span<const Semantic_token> tokens, std::vector<uint32_t> *data,
                            Position *last) {
  if (tokens.empty()) return;
  uint32_t at = tokens.front().begin;
  Position pos = lines.position(text, at);
  auto emit = [&](Position p, uint32_t length, const Semantic_token &t) {
    if (length == 0) return;
    data->push_back(p.line - last->line);
    data->push_back(p.line == last->line ? p.character - last->character : p.character);
    data->push_back(length);
    data->push_back(static_cast<uint32_t>(t.type));
    data->push_back(t.modifiers);
    *last = p;
  };
  for (const auto &t : tokens) {
    pos = lines.position_after(text, at, pos, t.begin);
    at = t.begin;
    uint32_t seg = t.begin;
    Position p = pos;
    for (;;) {
      // The segment of the token on line p.line, without the line break.
      const uint32_t next_line = lines.line_start(p.line + 1);
      uint32_t content_end = std::min(next_line, t.end);
      if (content_end == next_line) {
        while (content_end > seg && (text[content_end - 1] == '\n' || text[content_end - 1] == '\r'))
          --content_end;
      }
      emit(p, utf16_length(text.substr(seg, content_end - seg)), t);
      if (t.end <= next_line || p.line + 1 >= lines.line_count()) break;
      seg = next_line;
      p = {p.line + 1, 0};
    }
  }
}

std::vector<std::pair<uint32_t, uint32_t>> statement_blocks(std::string_view text,
                                                            const Statement_index &index,
                                                            size_t i, Parser_cache &cache,
                                                            unsigned long version_id) {
  std::vector<std::pair<uint32_t, uint32_t>> out;
  const Statement &s = index.statements()[i];
  if (s.kind != Statement_kind::sql) return out;
  const std::string_view sql = index.text_of(text, s);
  mdbp_result r;
  const Lexed lexed = lex(sql, index.modes()[s.mode], cache, version_id, &r);

  struct Open {
    const char *kind;  // the token END must be followed by, "" for BEGIN
    uint32_t begin;
  };
  std::vector<Open> stack;
  auto name = [&](size_t k) { return k < lexed.count ? lexed.tokens[k].name : ""; };
  auto offset = [&](size_t k) { return s.begin + static_cast<uint32_t>(lexed.tokens[k].byte_start); };
  for (size_t k = 0; k < lexed.count; ++k) {
    const char *n = name(k);
    const bool after_end = k > 0 && std::strcmp(name(k - 1), "END") == 0;
    if (std::strcmp(n, "END") == 0) {
      const char *what = name(k + 1);
      const bool qualified =
          is_one_of(what, {"IF_SYM", "CASE_SYM", "LOOP_SYM", "WHILE_SYM", "REPEAT_SYM"});
      // Pop to the matching opener; unmatched ENDs are ignored.
      for (size_t d = stack.size(); d-- > 0;) {
        const bool match = qualified ? std::strcmp(stack[d].kind, what) == 0
                                     : (stack[d].kind[0] == '\0' ||
                                        std::strcmp(stack[d].kind, "CASE_SYM") == 0);
        if (match) {
          out.emplace_back(stack[d].begin, offset(k));
          stack.resize(d);
          break;
        }
      }
    } else if (after_end) {
      continue;
    } else if (std::strncmp(n, "BEGIN", 5) == 0) {
      // BEGIN of a compound statement, not BEGIN [WORK] of a transaction.
      const char *nx = name(k + 1);
      if (!is_one_of(nx, {"WORK_SYM", "';'", ""}) || k > 0) stack.push_back({"", offset(k)});
    } else if (is_one_of(n, {"CASE_SYM", "LOOP_SYM", "WHILE_SYM", "REPEAT_SYM"})) {
      stack.push_back({n, offset(k)});
    } else if (std::strcmp(n, "IF_SYM") == 0) {
      const char *nx = name(k + 1);
      const bool exists = std::strcmp(nx, "EXISTS") == 0 ||
                          (std::strcmp(nx, "NOT_SYM") == 0 && std::strcmp(name(k + 2), "EXISTS") == 0);
      if (!exists && std::strcmp(nx, "'('") != 0) stack.push_back({"IF_SYM", offset(k)});
    }
  }
  return out;
}

}  // namespace mdbls
