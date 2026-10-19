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

#include <algorithm>
#include <cctype>
#include <utility>

#include "mdbls/source_include.h"
#include "mdbparse.h"

namespace mdbls {

namespace {

void check_one(std::string_view text, const Statement_index &index, const Statement &s,
               Parser_cache &cache, unsigned long version_id, bool source_directives,
               Statement_check *out) {
  mdbp_parser *p = cache.get(index.modes()[s.mode].mode, version_id);
  if (p == nullptr) {
    out->status = Check_status::unsupported_mode;
    return;
  }
  std::string_view sql = index.text_of(text, s);
  std::string blanked;
  if (const auto directives = source_directives ? find_source_directives(text, s.begin, s.end)
                                                : std::vector<Source_directive>{};
      !directives.empty()) {
    const auto &d = directives.front();
    if (d.statement.begin == s.begin && d.statement.end >= s.body_end) {
      out->status = Check_status::source_directive;
      return;
    }
    // Blanks, not removed: the offsets stay those of the text.
    blanked = sql;
    for (const auto &dir : directives) {
      const uint32_t from = std::max(dir.line.begin, s.begin);
      const uint32_t to = std::min(dir.line.end, s.body_end);
      for (uint32_t k = from; k < to; ++k) {
        char &c = blanked[k - s.begin];
        if (c != '\n' && c != '\r') c = ' ';
      }
    }
    sql = blanked;
  }
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

Statement_check check_statement(std::string_view text, const Statement_index &index, size_t i,
                                Parser_cache &cache, unsigned long version_id,
                                bool source_directives) {
  Statement_check out;
  const auto &s = index.statements()[i];
  if (s.kind == Statement_kind::sql) check_one(text, index, s, cache, version_id, source_directives, &out);
  return out;
}

std::string readable_terminal(std::string_view name) {
  if (name == "$end" || name == "END_OF_INPUT") return "end of statement";
  if (name.size() == 3 && name.front() == '\'' && name.back() == '\'') {
    return std::string(name.substr(1, 1));
  }
  if (name == "IDENT" || name == "IDENT_QUOTED" || name == "LEX_HOSTNAME") return "identifier";
  if (name == "TEXT_STRING" || name == "NCHAR_STRING" || name == "UNDERSCORE_CHARSET")
    return "string";
  if (name == "NUM" || name == "LONG_NUM" || name == "ULONGLONG_NUM" || name == "DECIMAL_NUM" ||
      name == "FLOAT_NUM" || name == "HEX_NUM" || name == "HEX_STRING" || name == "BIN_NUM")
    return "number";
  if (name == "PARAM_MARKER") return "?";
  if (name.rfind("LEFT_PAREN", 0) == 0) return "(";
  if (name == "HINT_COMMENT" || name == "ABORT_SYM" || name == "FORCE_LOOKAHEAD" ||
      name == "IMPOSSIBLE_ACTION" || name == "error" ||
      name.find("_ORACLE_") != std::string_view::npos)
    return "";
  std::string out(name);
  for (const char *suffix : {"_SYM", "_SYMBOL"}) {
    const std::string_view sfx(suffix);
    if (out.size() > sfx.size() && out.compare(out.size() - sfx.size(), sfx.size(), sfx) == 0) {
      out.resize(out.size() - sfx.size());
      break;
    }
  }
  return out;
}

std::vector<std::string> readable_expected(const std::vector<std::string> &expected) {
  std::vector<std::string> out;
  for (const auto &e : expected) {
    auto r = readable_terminal(e);
    if (!r.empty() && std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
  }
  // Punctuation and word classes first, keywords after, as a reader scans them.
  std::stable_sort(out.begin(), out.end(), [](const std::string &a, const std::string &b) {
    const bool ka = !a.empty() && std::isupper(static_cast<unsigned char>(a[0]));
    const bool kb = !b.empty() && std::isupper(static_cast<unsigned char>(b[0]));
    return ka < kb;
  });
  return out;
}

std::string syntax_error_near(std::string_view text, const Statement_check &check) {
  std::string out = "syntax error near ";
  if (check.error_length == 0) return out + "end of statement";
  std::string t(text.substr(check.error_offset, std::min<uint32_t>(check.error_length, 40)));
  for (auto &ch : t) {
    if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
  }
  return out + "'" + t + (check.error_length > 40 ? "...'" : "'");
}

std::string syntax_error_message(std::string_view text, const Statement_check &check,
                                 size_t max_keywords) {
  std::string out = syntax_error_near(text, check);
  const auto expected = readable_expected(check.expected);
  if (expected.empty()) return out;
  // Word classes and punctuation are listed; keywords too when there are
  // few, otherwise only counted (where a name may stand, every non-reserved
  // keyword may too).
  std::vector<std::string> shown;
  size_t keywords = 0;
  for (const auto &e : expected) {
    if (std::isupper(static_cast<unsigned char>(e[0]))) {
      ++keywords;
    } else {
      shown.push_back(e);
    }
  }
  if (keywords <= max_keywords) {
    for (const auto &e : expected) {
      if (std::isupper(static_cast<unsigned char>(e[0]))) shown.push_back(e);
    }
    keywords = 0;
  }
  out += "; expected ";
  for (size_t k = 0; k < shown.size(); ++k) {
    out += (k == 0 ? "" : (k + 1 == shown.size() && keywords == 0) ? " or " : ", ") + shown[k];
  }
  if (keywords > 0) {
    out += std::string(shown.empty() ? "" : " or ") + "one of " + std::to_string(keywords) +
           " keywords";
  }
  return out;
}

const char *open_context_name(Open_context c) {
  switch (c) {
    case Open_context::none:
      return nullptr;
    case Open_context::single_quote_string:
      return "single-quoted string";
    case Open_context::double_quote_string:
      return "double-quoted string";
    case Open_context::backtick_identifier:
      return "backtick-quoted identifier";
    case Open_context::double_quote_identifier:
      return "double-quoted identifier";
    case Open_context::comment:
      return "comment";
    default:
      return "construct";
  }
}

std::vector<Statement_check> check_statements(std::string_view text,
                                              const Statement_index &index,
                                              Parser_pool &pool,
                                              const Check_options &options) {
  const auto &stmts = index.statements();
  std::vector<Statement_check> results(stmts.size());

  // Jobs of consecutive statements, about chunk_bytes of text each.  Every
  // job writes only its own elements of `results`, which is never resized.
  std::vector<std::pair<size_t, size_t>> jobs;
  size_t from = 0;
  size_t bytes = 0;
  for (size_t i = 0; i < stmts.size(); ++i) {
    bytes += stmts[i].end - stmts[i].begin;
    if (bytes >= options.chunk_bytes) {
      jobs.emplace_back(from, i + 1);
      from = i + 1;
      bytes = 0;
    }
  }
  if (from < stmts.size()) jobs.emplace_back(from, stmts.size());

  pool.run_all(options.priority, jobs.size(), [&](size_t job, Parser_cache &cache) {
    for (size_t i = jobs[job].first; i < jobs[job].second; ++i) {
      if (stmts[i].kind != Statement_kind::sql) continue;
      check_one(text, index, stmts[i], cache, options.version_id, options.source_directives,
                &results[i]);
    }
  });
  return results;
}

std::string Unchecked_notes::operator()(size_t i, Check_status status) {
  const auto &st = m_index.statements()[i];
  switch (status) {
    case Check_status::invalid_utf8:
      return "statement not checked: it is not valid UTF-8";
    case Check_status::internal_error:
      return "statement not checked: internal parser error";
    case Check_status::unsupported_mode: {
      const auto &mode = m_index.modes()[st.mode].mode;
      if (mode == m_last_unsupported) return {};
      m_last_unsupported = mode;
      return "statements not checked from here: sql_mode " + mode + " is not supported";
    }
    case Check_status::syntax_error:
      return {};
    default:
      if (st.kind == Statement_kind::sql) m_last_unsupported.clear();
      return {};
  }
}

}  // namespace mdbls
