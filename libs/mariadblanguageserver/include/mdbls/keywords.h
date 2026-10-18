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

#ifndef MDBLS_KEYWORDS_H_
#define MDBLS_KEYWORDS_H_

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mdbls {

// The words a user types for terminal `symbol` (a grammar symbol), from
// MariaDB's keyword table: SELECT_SYM -> {"SELECT"}, DATABASE -> {"DATABASE",
// "SCHEMA"}.  Operators and the names that are keywords only before '('
// (sql_functions[]) are left out.  Empty for a terminal no word spells.
const std::vector<std::string_view> &keyword_spellings(int symbol);

// Whether terminal `symbol` is a keyword that can also stand for a name
// (a non-reserved keyword: STATUS, USER, COMMENT), found by asking the
// parser whether it accepts the keyword as a table name.
bool is_nonreserved_keyword(int symbol);

// IDENT, IDENT_QUOTED, or a non-reserved keyword: a token that can be a
// name.
bool is_name_symbol(int symbol);

// A nonterminal that only wraps one name token: ident, IDENT_sys,
// keyword_*.
bool is_name_wrapper(int symbol);

// '(' or one of the lexer's spellings of it (LEFT_PAREN_ALT, ...).
bool is_left_paren(int symbol);

// Whether `word` must be quoted to be read as a name: it is empty, holds a
// character other than a letter, digit, '_' or '$' (or any byte >= 0x80,
// which is allowed), is all digits, or is a reserved keyword.
bool needs_quoting(std::string_view word);

// `name` as it must be written: as is, or in backticks with backticks
// doubled.
std::string quote_identifier(std::string_view name);
// `name` in backticks, backticks doubled, whether it needs them or not.
std::string backtick_quote(std::string_view name);

// A built-in function, from scripts/gen_builtin_functions.py.  `forms` holds
// the call forms the server's help tables give, one per line ("" when the
// help does not describe the function).
struct Builtin_function {
  std::string_view name;
  std::string_view forms;
  std::string_view summary;
  std::string_view url;
};

std::span<const Builtin_function> builtin_functions();

// Markdown describing `f`: its call forms (when `forms`), summary and link.
std::string builtin_markdown(const Builtin_function &f, bool forms = true);
// The function called `name` (any case), or nullptr.
const Builtin_function *builtin_function(std::string_view name);

// The parameters of one call form, "DATE_FORMAT(date, format[, locale])":
// the byte ranges in `form` of "date" and "format[, locale]", split at the
// commas outside parentheses and brackets; a comma inside "[, x]" stays
// with the parameter before it.  Empty for a form without parameters.
std::vector<std::pair<size_t, size_t>> call_form_parameters(std::string_view form);

}  // namespace mdbls

#endif  // MDBLS_KEYWORDS_H_
