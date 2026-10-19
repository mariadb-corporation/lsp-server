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

#ifndef MDBLS_WORKSPACE_CATALOG_H_
#define MDBLS_WORKSPACE_CATALOG_H_

#include <string>
#include <string_view>
#include <vector>

#include "mdbls/line_map.h"
#include "mdbls/outline.h"
#include "mdbls/parser_pool.h"
#include "mdbls/statement_index.h"

namespace mdbls {

// "file:///home/a/b%20c.sql" -> "/home/a/b c.sql"; "file:///c%3A/x" ->
// "c:/x".  "" for a URI that is not a file URI.
std::string file_uri_to_path(std::string_view uri);
// The file URI of an absolute path, percent-encoded as VS Code writes it,
// a drive letter in lower case.
std::string path_to_file_uri(std::string_view path);

// A path as catalogs and includes compare them: links resolved as far as
// the path exists, forward slashes.
std::string canonical_path(const std::string &path);

// Reads a SQL file as UTF-8: as is when it is valid UTF-8, converted from
// latin1 otherwise.  False when it cannot be read.
bool read_sql_file(const std::string &path, std::string *text);

struct Workspace_file {
  std::string path;
  std::string uri;
  std::string text;
  Line_map lines;
};

// The schema objects a set of SQL files define, folded file by file in the
// order given, as one script: a later file's ALTER changes an earlier
// file's table.  An Origin's `source` is the index of its file in `files`.
// SOURCE directives are expanded: the files they include follow the listed
// ones in `files`, and a listed file that another listed file includes is
// folded only through that include.
struct Workspace_catalog {
  std::vector<Workspace_file> files;
  Outline outline;
  std::vector<std::string> errors;  // files that could not be read

  // The file `uri` names (by its path, links resolved), or nullptr.
  const Workspace_file *file(std::string_view uri) const;
};

// The files `entries` name, in order: a file as it is, a directory as every
// *.sql file below it sorted by path.  Relative entries are relative to
// `root`.  A file named twice is taken once, the first time.
std::vector<std::string> expand_catalog_paths(const std::vector<std::string> &entries,
                                              const std::string &root,
                                              std::vector<std::string> *errors);

Workspace_catalog build_workspace_catalog(const std::vector<std::string> &paths,
                                          Parser_pool &pool, const Index_options &options,
                                          int priority = 0);

}  // namespace mdbls

#endif  // MDBLS_WORKSPACE_CATALOG_H_
