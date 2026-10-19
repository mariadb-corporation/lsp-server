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

#include "mdbls/source_include.h"

#include <algorithm>
#include <filesystem>
#include <optional>

#include "mdbls/workspace_catalog.h"

namespace mdbls {

namespace fs = std::filesystem;

namespace {

// Python's \s, ASCII only.
bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

size_t skip_spaces(std::string_view t, size_t p) {
  while (p < t.size() && is_space(t[p])) ++p;
  return p;
}

// `(?:ln)?-?\d*` at `p`; returns the end.
size_t skip_bound(std::string_view t, size_t p) {
  if (t.substr(p, 2) == "ln") p += 2;
  if (p < t.size() && t[p] == '-') ++p;
  while (p < t.size() && is_digit(t[p])) ++p;
  return p;
}

// `\s*(?:\[\s*B\s*:\s*B\s*\])?\s*;[^\n]*(?:\n|\Z)` after the closing quote.
bool match_tail(std::string_view t, size_t p, Source_directive *d) {
  p = skip_spaces(t, p);
  d->slice = {};
  d->start.clear();
  d->end.clear();
  if (p < t.size() && t[p] == '[') {
    const size_t open = p;
    p = skip_spaces(t, p + 1);
    const size_t start_end = skip_bound(t, p);
    d->start = t.substr(p, start_end - p);
    p = skip_spaces(t, start_end);
    if (p >= t.size() || t[p] != ':') return false;
    p = skip_spaces(t, p + 1);
    const size_t end_end = skip_bound(t, p);
    d->end = t.substr(p, end_end - p);
    p = skip_spaces(t, end_end);
    if (p >= t.size() || t[p] != ']') return false;
    d->slice = {static_cast<uint32_t>(open), static_cast<uint32_t>(p + 1)};
    p = skip_spaces(t, p + 1);
  }
  if (p >= t.size() || t[p] != ';') return false;
  d->statement.end = static_cast<uint32_t>(p + 1);
  // The rest of the line and its break ("\r\n" and a lone "\r" are one "\n"
  // to msm, which reads in text mode).
  while (p < t.size() && t[p] != '\n' && t[p] != '\r') ++p;
  if (p < t.size()) p += (t[p] == '\r' && p + 1 < t.size() && t[p + 1] == '\n') ? 2 : 1;
  d->line.end = static_cast<uint32_t>(p);
  return true;
}

// A directive whose `SOURCE` is at `at`, its line beginning at `line_begin`.
std::optional<Source_directive> match_at(std::string_view t, size_t line_begin, size_t at) {
  Source_directive d;
  d.line.begin = static_cast<uint32_t>(line_begin);
  d.statement.begin = static_cast<uint32_t>(at);
  size_t p = at + 6;
  const size_t q = skip_spaces(t, p);
  if (q == p || q >= t.size() || (t[q] != '\'' && t[q] != '"')) return std::nullopt;
  const char quote = t[q];
  const size_t path_begin = q + 1;
  // The path is the shortest one the rest of the directive matches after.
  for (size_t close = path_begin; close < t.size(); ++close) {
    const char c = t[close];
    if (c == '\n' || c == '\r') break;
    if (c != quote) continue;
    if (match_tail(t, close + 1, &d)) {
      d.path = {static_cast<uint32_t>(path_begin), static_cast<uint32_t>(close)};
      d.file = t.substr(path_begin, close - path_begin);
      return d;
    }
  }
  return std::nullopt;
}

// The byte offset of each line's start, as msm's line numbers count lines:
// a break is "\n", "\r\n" or a lone "\r", and a last line without one counts.
std::vector<uint32_t> line_starts(std::string_view t) {
  std::vector<uint32_t> out{0};
  for (size_t p = 0; p < t.size(); ++p) {
    if (t[p] == '\r' && p + 1 < t.size() && t[p + 1] == '\n') ++p;
    if (t[p] == '\n' || t[p] == '\r') out.push_back(static_cast<uint32_t>(p + 1));
  }
  if (out.back() != t.size()) out.push_back(static_cast<uint32_t>(t.size()));
  return out;
}

// Whether byte `p` starts a character as msm counts them: a code point,
// with "\r\n" one.
bool starts_character(std::string_view t, size_t p) {
  const auto c = static_cast<unsigned char>(t[p]);
  if ((c & 0xC0) == 0x80) return false;
  return !(c == '\n' && p > 0 && t[p - 1] == '\r');
}

// The byte offset of character `n` (clamped to the text).
uint32_t character_offset(std::string_view t, int64_t n) {
  int64_t seen = 0;
  for (size_t p = 0; p < t.size(); ++p) {
    if (!starts_character(t, p)) continue;
    if (seen == n) return static_cast<uint32_t>(p);
    ++seen;
  }
  return static_cast<uint32_t>(t.size());
}

int64_t character_count(std::string_view t) {
  int64_t n = 0;
  for (size_t p = 0; p < t.size(); ++p) n += starts_character(t, p) ? 1 : 0;
  return n;
}

// "-12" -> -12, saturating far beyond any text size.
int64_t parse_number(std::string_view s) {
  const bool negative = !s.empty() && s[0] == '-';
  int64_t v = 0;
  for (const char c : s.substr(negative ? 1 : 0)) v = std::min<int64_t>(v * 10 + (c - '0'), 1LL << 40);
  return negative ? -v : v;
}

}  // namespace

std::vector<Source_directive> find_source_directives(std::string_view text) {
  std::vector<Source_directive> out;
  size_t from = 0;
  for (;;) {
    const size_t at = text.find("SOURCE", from);
    if (at == std::string_view::npos) break;
    size_t line_begin = at;
    while (line_begin > 0 && (text[line_begin - 1] == ' ' || text[line_begin - 1] == '\t'))
      --line_begin;
    const bool at_line_start =
        line_begin == 0 || text[line_begin - 1] == '\n' || text[line_begin - 1] == '\r';
    if (at_line_start && line_begin >= from) {
      if (auto d = match_at(text, line_begin, at)) {
        from = d->line.end;
        out.push_back(std::move(*d));
        continue;
      }
    }
    from = at + 1;
  }
  return out;
}

Source_slice slice_source(std::string_view text, std::string_view start, std::string_view end) {
  Source_slice out;
  std::vector<uint32_t> lines;  // made when a bound needs them
  int64_t characters = -1;
  // The byte offset a bound stands for; nullopt when it is empty.
  auto offset = [&](std::string_view bound, bool is_end) -> std::optional<uint32_t> {
    if (bound.empty()) return std::nullopt;
    const bool is_line = bound.substr(0, 2) == "ln";
    const auto number = is_line ? bound.substr(2) : bound;
    if (number.empty() || number == "-" || (is_line && parse_number(number) == 0)) {
      if (out.error.empty())
        out.error = "the slice bound `" + std::string(bound) +
                    "` is neither a character offset nor a line number (lines count from 1)";
      return std::nullopt;
    }
    const int64_t n = parse_number(number);
    if (is_line) {
      if (lines.empty()) lines = line_starts(text);
      const auto count = static_cast<int64_t>(lines.size()) - 1;
      // The first line taken, or the first after the slice.
      const int64_t index = is_end ? (n > 0 ? n : count + n + 1) : (n > 0 ? n - 1 : count + n);
      return lines[static_cast<size_t>(std::clamp<int64_t>(index, 0, count))];
    }
    if (characters < 0) characters = character_count(text);
    const int64_t index = std::clamp<int64_t>(n < 0 ? n + characters : n, 0, characters);
    return character_offset(text, index);
  };
  const auto begin = offset(start, false);
  const auto finish = offset(end, true);
  if (!out.error.empty()) return out;
  out.range.begin = begin.value_or(0);
  out.range.end = std::max(out.range.begin, finish.value_or(static_cast<uint32_t>(text.size())));
  return out;
}

Source_expansion::Location Source_expansion::map(uint32_t offset) const {
  if (segments.empty()) return {0, offset};
  auto it = std::upper_bound(segments.begin(), segments.end(), offset,
                             [](uint32_t o, const Segment &s) { return o < s.begin; });
  if (it != segments.begin()) --it;
  return {it->source, it->source_begin + (offset - it->begin)};
}

Source_expansion::Located_range Source_expansion::map(Range range) const {
  const Location b = map(range.begin);
  if (range.end <= range.begin) return {b.source, {b.offset, b.offset}};
  const Location e = map(range.end - 1);
  if (e.source == b.source) return {b.source, {b.offset, e.offset + 1}};
  if (b.source > 0) return {b.source, {b.offset, includes[b.source - 1].selected.end}};
  // From the including text into an include: up to that include's directive.
  auto it = std::upper_bound(segments.begin(), segments.end(), range.begin,
                             [](uint32_t o, const Segment &s) { return o < s.begin; });
  while (it != segments.end() && it->source == 0) ++it;
  const uint32_t stop = it == segments.end() ? b.offset : includes[it->source - 1].directive.statement.end;
  return {0, {b.offset, std::max(b.offset, stop)}};
}

Source_expansion expand_sources(std::string_view text, const std::string &base_dir,
                                const Source_reader &read) {
  Source_expansion out;
  auto directives = find_source_directives(text);
  if (directives.empty()) return out;
  out.text.reserve(text.size());
  auto append = [&out](uint32_t source, std::string_view from, uint32_t begin, uint32_t end) {
    if (end <= begin) return;
    out.segments.push_back({static_cast<uint32_t>(out.text.size()), source, begin});
    out.text.append(from.substr(begin, end - begin));
  };
  uint32_t pos = 0;
  for (auto &d : directives) {
    append(0, text, pos, d.line.begin);
    pos = d.line.end;
    Source_include inc;
    const fs::path file(d.file);
    if (file.is_relative() && base_dir.empty()) {
      inc.error = "the path is relative, and the document is not in a folder";
    } else {
      inc.path = (fs::path(base_dir) / file).lexically_normal().generic_string();
      const bool ok = read ? read(inc.path, &inc.text) : read_sql_file(inc.path, &inc.text);
      if (!ok) {
        inc.text.clear();
        inc.error = "cannot read " + inc.path;
      }
    }
    if (inc.error.empty()) {
      const auto slice = slice_source(inc.text, d.start, d.end);
      inc.error = slice.error;
      inc.selected = slice.range;
      inc.lines = Line_map(inc.text);
    }
    inc.directive = std::move(d);
    out.includes.push_back(std::move(inc));
    Source_include &added = out.includes.back();
    if (!added.error.empty()) continue;
    const auto source = static_cast<uint32_t>(out.includes.size());
    append(source, added.text, added.selected.begin, added.selected.end);
    const std::string_view selected =
        std::string_view(added.text).substr(added.selected.begin, added.selected.end - added.selected.begin);
    for (auto n : find_source_directives(selected)) {
      for (Range *r : {&n.line, &n.statement, &n.path, &n.slice}) {
        r->begin += added.selected.begin;
        r->end += added.selected.begin;
      }
      added.nested.push_back(std::move(n));
    }
  }
  append(0, text, pos, static_cast<uint32_t>(text.size()));
  return out;
}

}  // namespace mdbls
