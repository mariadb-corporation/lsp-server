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

// The parser and the formatter on any text: every statement checked, the
// whole text and a range of it formatted with options from the input, and
// the formatted text formatted again.  The edits are in order, inside the
// text and do not overlap.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "fuzz_util.h"
#include "mdbls/completion.h"
#include "mdbls/formatter.h"
#include "mdbls/parser_pool.h"
#include "mdbls/statement_index.h"
#include "mdbls/syntax_check.h"

using namespace mdbls;

namespace {

void check_edits(const std::vector<Text_edit> &edits, uint32_t size) {
  uint32_t previous_end = 0;
  for (const auto &e : edits) {
    MDBLS_FUZZ_CHECK(previous_end <= e.begin);
    MDBLS_FUZZ_CHECK(e.begin <= e.end && e.end <= size);
    previous_end = e.end;
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t size) {
  // One parser per sql_mode and release id for the whole run, as the server
  // keeps them.
  static Parser_cache cache;

  FuzzedDataProvider data(bytes, size);
  const Index_options index_options = fuzz::consume_options(data);
  Format_options options;
  options.version_id = index_options.version_id;
  const auto bits = data.ConsumeIntegral<uint8_t>();
  options.upper_case_keywords = bits & 1;
  options.indent_clauses = bits & 2;
  options.indent_routine_clauses = bits & 4;
  options.indent = bits & 8 ? "\t" : "  ";
  options.line_width = data.ConsumeIntegralInRange<size_t>(0, 120);
  const auto range_begin = data.ConsumeIntegral<uint16_t>();
  const auto range_length = data.ConsumeIntegral<uint16_t>();
  const std::string text = data.ConsumeRemainingBytesAsString();
  const auto size32 = static_cast<uint32_t>(text.size());

  const auto index = Statement_index::build(text, index_options);
  for (size_t i = 0; i < index.statements().size(); ++i) {
    const auto check = check_statement(text, index, i, cache, index_options.version_id);
    if (check.status == Check_status::syntax_error) {
      MDBLS_FUZZ_CHECK(check.error_offset <= size32);
      (void)syntax_error_message(text, check);
    }
  }

  auto edits = format_sql(text, index, 0, size32, cache, options);
  check_edits(edits, size32);
  const std::string formatted = apply_edits(text, edits);
  (void)format_sql(formatted, Statement_index::build(formatted, index_options), 0,
                   static_cast<uint32_t>(formatted.size()), cache, options);

  const uint32_t begin = std::min<uint32_t>(range_begin, size32);
  const uint32_t end = std::min<uint32_t>(begin + range_length, size32);
  edits = format_sql(text, index, begin, end, cache, options);
  check_edits(edits, size32);
  return 0;
}
