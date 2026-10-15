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

#ifndef MDBLS_SEMANTIC_TOKENS_H_
#define MDBLS_SEMANTIC_TOKENS_H_

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "mdbls/line_map.h"
#include "mdbls/statement_index.h"

namespace mdbls {

class Parser_cache;

// Semantic token types and modifiers, in the order of the legend the server
// announces (semantic_token_types(), semantic_token_modifiers()).
enum class Token_type : uint8_t {
  keyword,
  string,
  number,
  comment,
  variable,   // @user_var, @@system_var (defaultLibrary)
  operator_,
  function,   // a name followed by "(": defaultLibrary for built-ins
  parameter,  // ? markers
};
constexpr uint8_t k_modifier_default_library = 1;

std::span<const char *const> semantic_token_types();
std::span<const char *const> semantic_token_modifiers();

struct Semantic_token {
  uint32_t begin = 0;  // absolute byte offsets
  uint32_t end = 0;
  Token_type type = Token_type::keyword;
  uint8_t modifiers = 0;

  bool operator==(const Semantic_token &) const = default;
};

// The tokens of statement `i`, in text order, from the lexer of its
// sql_mode: keywords, literals, variables, operators, functions, and the
// comments between tokens.  Identifiers are not classified here (that needs
// the catalog).  A statement the lexer refuses gets its comments only.
void statement_semantic_tokens(std::string_view text, const Statement_index &index, size_t i,
                               Parser_cache &cache, unsigned long version_id,
                               std::vector<Semantic_token> *out);

// Appends `tokens` (in text order, not overlapping) to `data` in the LSP
// relative encoding, one token per line a token covers.  `last` is the
// start of the previous token encoded ({0, 0} initially) and is updated.
void encode_semantic_tokens(std::string_view text, const Line_map &lines,
                            std::span<const Semantic_token> tokens, std::vector<uint32_t> *data,
                            Position *last);

// Folding regions inside statement `i` from its tokens: BEGIN ... END,
// IF ... END IF, CASE ... END CASE, LOOP, WHILE and REPEAT blocks, as
// (begin, end) byte offsets of the opening and the closing keyword.
std::vector<std::pair<uint32_t, uint32_t>> statement_blocks(std::string_view text,
                                                            const Statement_index &index,
                                                            size_t i, Parser_cache &cache,
                                                            unsigned long version_id);

}  // namespace mdbls

#endif  // MDBLS_SEMANTIC_TOKENS_H_

