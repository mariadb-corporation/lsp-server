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

// The statement index of any text: statements in order, inside the text, not
// overlapping, find() agreeing with them, and the parallel split equal to the
// sequential one.

#include <cstddef>
#include <cstdint>
#include <string>

#include "fuzz_util.h"
#include "mdbls/statement_index.h"

using namespace mdbls;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t size) {
  FuzzedDataProvider data(bytes, size);
  const Index_options options = fuzz::consume_options(data);
  const auto threads = data.ConsumeIntegralInRange<unsigned>(2, 4);
  const auto chunk = data.ConsumeIntegralInRange<size_t>(1, 64);
  const std::string text = data.ConsumeRemainingBytesAsString();

  const auto index = Statement_index::build(text, options);
  const auto &statements = index.statements();
  uint32_t previous_end = 0;
  for (size_t i = 0; i < statements.size(); ++i) {
    const auto &s = statements[i];
    MDBLS_FUZZ_CHECK(previous_end <= s.begin);
    MDBLS_FUZZ_CHECK(s.begin <= s.body_end && s.body_end <= s.end);
    MDBLS_FUZZ_CHECK(s.end <= text.size());
    MDBLS_FUZZ_CHECK(s.begin < s.end);
    MDBLS_FUZZ_CHECK(s.mode < index.modes().size());
    MDBLS_FUZZ_CHECK(s.delimiter < index.delimiters().size());
    MDBLS_FUZZ_CHECK(s.active_delimiter < index.delimiters().size());
    MDBLS_FUZZ_CHECK(index.find(s.begin) == i);
    MDBLS_FUZZ_CHECK(index.find(s.end - 1) == i);
    previous_end = s.end;
  }
  MDBLS_FUZZ_CHECK(index.find(static_cast<uint32_t>(text.size())) == statements.size());

  MDBLS_FUZZ_CHECK(test::same_index(Statement_index::build_parallel(text, options, threads, chunk), index));
  return 0;
}
