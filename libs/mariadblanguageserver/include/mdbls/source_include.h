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

#ifndef MDBLS_SOURCE_INCLUDE_H_
#define MDBLS_SOURCE_INCLUDE_H_

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mdbls/catalog.h"
#include "mdbls/line_map.h"

namespace mdbls {

// The SOURCE directives of MariaDB Schema Management scripts, expanded as
// msm_plugin's prepare_release does (docs/plans/source-includes.md §1): a
// line `SOURCE '<path>'[<start>:<end>]; <rest>` is replaced by the part of
// the file the slice selects.  A text preprocessor: it matches anywhere a
// line starts with the directive, in a routine body or a comment too.

struct Source_directive {
  Range line;       // what is replaced: indentation to the line break, included
  Range statement;  // `SOURCE` to `;`
  Range path;       // between the quotes
  Range slice;      // `[` to `]`; empty when there is none
  std::string file;              // the path as written
  std::string start, end;        // the slice bounds as written; "" when empty
};

// Every directive of `text`, in order, as msm's regular expression matches
// them: the path ends at a matching quote on the directive's line, and `\s`
// is ASCII white space.
std::vector<Source_directive> find_source_directives(std::string_view text);
// The same for the directives whose `SOURCE` lies in [from, to) of `text`
// (one statement's), the rest of the text still deciding where lines start
// and end.
std::vector<Source_directive> find_source_directives(std::string_view text, size_t from,
                                                     size_t to);

// The bytes of a UTF-8 `text` the slice [start:end] selects, by msm's rules:
// a bound is empty, a character offset (a Python slice over code points,
// "\r\n" counting as one) or lnN (1-based line, the end line included, ln-1
// the last line).  `error` is set for a malformed bound, such as ln0.
struct Source_slice {
  Range range;
  std::string error;
};
Source_slice slice_source(std::string_view text, std::string_view start, std::string_view end);

// The state of the including script where an included text begins: what
// a file opened on its own is read with when an open script includes it.
// Inside a statement (a routine body) the delimiter is ";".
struct Source_context {
  bool known = false;
  std::string schema;  // in use; "" for none
  std::string sql_mode;
  std::string delimiter;

  bool operator==(const Source_context &) const = default;
};

// The file one directive includes.
struct Source_include {
  Source_directive directive;  // in the including text
  std::string path;            // resolved and normalized
  std::string text;            // the whole file; empty when it cannot be read
  Line_map lines;              // of `text`
  Range selected;              // the bytes of `text` the slice selects
  std::string error;           // why nothing was included
  bool read = false;           // the file was read: an error is the slice's
  // Directives in the selected text, offsets in `text`: msm expands one
  // level only, so they stay as they are.
  std::vector<Source_directive> nested;
  Source_context context;  // set by build_outline()
};

// The folder of a document's path, which its relative SOURCE paths are
// relative to; "" for "" (a document that is not a file).
std::string source_folder(const std::string &document_path);

// "140_tables.sql:12": the file name and the line (1-based) of `offset` in
// an include's text.
std::string source_location(const Source_include &include, uint32_t offset);

// The path being typed in a SOURCE directive: `offset` is on a line that
// starts with `SOURCE` and a quote, after the quote, with no closing quote
// before it.  `typed` is the path up to `offset`, `segment` where its last
// part (after the last '/') begins.
struct Source_path_at {
  uint32_t path = 0;
  uint32_t segment = 0;
  std::string typed;
};
std::optional<Source_path_at> source_path_at(std::string_view text, uint32_t offset);

// What may follow `typed` in a SOURCE path: the folders and .sql files of
// the folder its part up to the last '/' names, relative to `base_dir`.
// Names starting with '.' only when the last part does.  Folders first.
struct Source_path_candidate {
  std::string name;
  bool folder = false;
};
std::vector<Source_path_candidate> source_path_candidates(const std::string &base_dir,
                                                          std::string_view typed,
                                                          size_t max = 500);

// Reads a file as UTF-8; false when it cannot be read.
using Source_reader = std::function<bool(const std::string &path, std::string *text)>;

// A text with its directives expanded, and the way back: source 0 is the
// including text, source k is includes[k - 1].
struct Source_expansion {
  struct Segment {
    uint32_t begin;         // in `text`
    uint32_t source;
    uint32_t source_begin;  // in that source's text
  };
  struct Location {
    uint32_t source = 0;
    uint32_t offset = 0;
  };
  struct Located_range {
    uint32_t source = 0;
    Range range;
  };

  std::string text;  // empty when there are no directives
  std::vector<Source_include> includes;
  std::vector<Segment> segments;  // by `begin`; none empty

  bool empty() const { return includes.empty(); }

  // Where the byte at `offset` of `text` came from.
  Location map(uint32_t offset) const;
  // Where a range of `text` came from.  A range that starts in one source
  // and ends in another is clipped to the source it starts in: at the end
  // of the directive's statement when it starts in the including text, at
  // the end of the selected text when it starts in an include.
  Located_range map(Range range) const;
};

// Source_expansion::map() over segments and includes kept apart.
Source_expansion::Location map_source(const std::vector<Source_expansion::Segment> &segments,
                                      uint32_t offset);
Source_expansion::Located_range map_source(
    const std::vector<Source_expansion::Segment> &segments,
    const std::vector<Source_include> &includes, Range range);

// Something wrong with a directive, in the including text: a file that
// cannot be read (on the path), a malformed slice (on the slice), nothing
// selected, a directive in the included text that msm does not expand.
struct Source_problem {
  Range range;
  bool error = true;  // else a warning
  std::string message;

  bool operator==(const Source_problem &o) const {
    return range.begin == o.range.begin && range.end == o.range.end && error == o.error &&
           message == o.message;
  }
};
std::vector<Source_problem> source_problems(const std::vector<Source_include> &includes);
inline std::vector<Source_problem> source_problems(const Source_expansion &expansion) {
  return source_problems(expansion.includes);
}

// The path a directive's `file` names: relative to `base_dir`, normalized,
// with forward slashes; "" for a relative path when `base_dir` is empty.
std::string resolve_source_path(const std::string &base_dir, const std::string &file);

// Expands the directives of `text`, whose relative paths are relative to
// `base_dir` (none when it is empty).  `read` gives the files' texts; by
// default read_sql_file().
Source_expansion expand_sources(std::string_view text, const std::string &base_dir,
                                const Source_reader &read = {});

}  // namespace mdbls

#endif  // MDBLS_SOURCE_INCLUDE_H_
