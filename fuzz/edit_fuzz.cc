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

// The edit path: a text and a series of edits.  After each edit the index
// Statement_index::update() made equals the one build() makes of the new
// text, with an unchanged prefix, a replaced middle and a shifted suffix as
// its splice says; and a Document given the same edit as LSP positions holds
// an index equal to build() of its text.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "fuzz_util.h"
#include "mdbls/document.h"
#include "mdbls/statement_index.h"

using namespace mdbls;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t size) {
  FuzzedDataProvider data(bytes, size);
  const Index_options options = fuzz::consume_options(data);
  std::string text = data.ConsumeRandomLengthString(4096);
  const auto &pieces = fuzz::edit_pieces();

  auto index = Statement_index::build(text, options);
  Document doc(text, 1, options);
  for (int n = 0; n < 64 && data.remaining_bytes() > 0; ++n) {
    const auto at = data.ConsumeIntegralInRange<uint32_t>(0, static_cast<uint32_t>(text.size()));
    const auto removed = data.ConsumeIntegralInRange<uint32_t>(
        0, std::min<uint32_t>(32, static_cast<uint32_t>(text.size()) - at));
    std::string inserted;
    if (data.ConsumeBool()) {
      for (auto k = data.ConsumeIntegralInRange<int>(1, 3); k > 0; --k)
        inserted += pieces[data.ConsumeIntegralInRange<size_t>(0, pieces.size() - 1)];
    } else {
      inserted = data.ConsumeRandomLengthString(16);
    }

    const auto before = fuzz::flatten(index);
    text.replace(at, removed, inserted);
    Index_splice splice;
    index = Statement_index::update(index, text, at, removed, static_cast<uint32_t>(inserted.size()),
                                    options, &splice);
    const auto built = Statement_index::build(text, options);
    MDBLS_FUZZ_CHECK(fuzz::same_index(index, built));

    const auto got = fuzz::flatten(index);
    MDBLS_FUZZ_CHECK(splice.first + splice.removed <= before.size());
    MDBLS_FUZZ_CHECK(before.size() - splice.removed + splice.added == got.size());
    for (size_t i = 0; i < splice.first; ++i) MDBLS_FUZZ_CHECK(got[i] == before[i]);
    for (size_t i = splice.first + splice.removed; i < before.size(); ++i) {
      auto f = before[i];
      std::get<0>(f) = static_cast<uint32_t>(std::get<0>(f) + splice.delta);
      std::get<1>(f) = static_cast<uint32_t>(std::get<1>(f) + splice.delta);
      std::get<2>(f) = static_cast<uint32_t>(std::get<2>(f) + splice.delta);
      MDBLS_FUZZ_CHECK(got[i - splice.removed + splice.added] == f);
    }

    // The document's positions are UTF-16 code units; on text that is not
    // UTF-8 they may land elsewhere than `at`, so its text is compared with
    // nothing but its own index.
    const auto from = doc.position(at);
    const auto to = doc.position(at + removed);
    doc.change(std::make_pair(from, to), inserted);
    MDBLS_FUZZ_CHECK(fuzz::same_index(doc.index(), Statement_index::build(doc.text(), options)));
    MDBLS_FUZZ_CHECK(doc.checks().size() == doc.index().statements().size());
  }
  return 0;
}
