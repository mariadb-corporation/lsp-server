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

#include "mdbls/syntax_check.h"

#include <atomic>
#include <condition_variable>
#include <mutex>

#include "mdbparse.h"

namespace mdbls {

namespace {

void check_one(std::string_view text, const Statement_index &index, const Statement &s,
               Parser_cache &cache, unsigned long version_id, Statement_check *out) {
  mdbp_parser *p = cache.get(index.modes()[s.mode].mode, version_id);
  if (p == nullptr) {
    out->status = Check_status::unsupported_mode;
    return;
  }
  const std::string_view sql = index.text_of(text, s);
  mdbp_result r;
  const mdbp_status st = mdbp_parse_sql(p, sql.data(), sql.size(), &r);
  if (st == MDBP_INTERNAL) {
    out->status = Check_status::internal_error;
    return;
  }
  if (st == MDBP_UNSUPPORTED) {
    // Invalid UTF-8, or a character token the grammar has no terminal for.
    if (r.tokens == nullptr) {
      out->status = Check_status::invalid_utf8;
      return;
    }
    out->status = Check_status::syntax_error;
  } else if (r.accepted) {
    out->status = Check_status::ok;
    return;
  } else {
    out->status = Check_status::syntax_error;
  }

  if (r.error_token_index < r.ntokens && r.tokens[r.error_token_index].byte_start >= 0) {
    const mdbp_token &t = r.tokens[r.error_token_index];
    out->error_offset = s.begin + static_cast<uint32_t>(t.byte_start);
    out->error_length = static_cast<uint32_t>(t.byte_end - t.byte_start);
  } else {
    out->error_offset = s.body_end;
  }
  out->error_token = r.error_token != nullptr ? r.error_token : "";
  if (r.expected != nullptr) {
    out->expected.assign(r.expected, r.expected + r.nexpected);
  }
}

}  // namespace

std::vector<Statement_check> check_statements(std::string_view text,
                                              const Statement_index &index,
                                              Parser_pool &pool,
                                              const Check_options &options) {
  const auto &stmts = index.statements();
  std::vector<Statement_check> results(stmts.size());

  // Jobs of consecutive statements, about chunk_bytes of text each.  Every
  // job writes only its own elements of `results`, which is never resized.
  struct Sync {
    std::mutex mutex;
    std::condition_variable cv;
    size_t pending = 0;
  } sync;

  auto run = [&](size_t from, size_t to) {
    {
      std::lock_guard lock(sync.mutex);
      ++sync.pending;
    }
    pool.submit(options.priority, [&, from, to](Parser_cache &cache) {
      for (size_t i = from; i < to; ++i) {
        if (stmts[i].kind != Statement_kind::sql) continue;
        check_one(text, index, stmts[i], cache, options.version_id, &results[i]);
      }
      std::lock_guard lock(sync.mutex);
      if (--sync.pending == 0) sync.cv.notify_all();
    });
  };

  size_t from = 0;
  size_t bytes = 0;
  for (size_t i = 0; i < stmts.size(); ++i) {
    bytes += stmts[i].end - stmts[i].begin;
    if (bytes >= options.chunk_bytes) {
      run(from, i + 1);
      from = i + 1;
      bytes = 0;
    }
  }
  if (from < stmts.size()) run(from, stmts.size());

  std::unique_lock lock(sync.mutex);
  sync.cv.wait(lock, [&] { return sync.pending == 0; });
  return results;
}

}  // namespace mdbls
