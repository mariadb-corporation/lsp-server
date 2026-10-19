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

#include "mdbls/workspace_catalog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>

#include "mdbls/charset.h"
#include "text_util.h"

namespace mdbls {

namespace fs = std::filesystem;

namespace {

// A path as the catalog records it: links resolved as far as it exists, in
// generic form, so a client's spelling of a file (/tmp/x on macOS, c:/x on
// Windows) and the catalog's (/private/tmp/x, C:/x) compare equal.
std::string catalog_path(const fs::path &p) {
  std::error_code ec;
  const fs::path canonical = fs::weakly_canonical(p, ec);
  return (ec ? p : canonical).generic_string();
}

}  // namespace

std::string canonical_path(const std::string &path) { return catalog_path(fs::path(path)); }

std::string file_uri_to_path(std::string_view uri) {
  constexpr std::string_view k_scheme = "file://";
  if (uri.size() < k_scheme.size() || !iequals(uri.substr(0, k_scheme.size()), k_scheme)) return {};
  uri.remove_prefix(k_scheme.size());
  // An authority (file://host/path) other than localhost is not ours.
  const size_t slash = uri.find('/');
  if (slash == std::string_view::npos) return {};
  const auto host = uri.substr(0, slash);
  if (!host.empty() && !iequals(host, "localhost")) return {};
  uri.remove_prefix(slash);
  auto decoded = percent_decode(uri);
  if (!decoded) return {};
  std::string out = std::move(*decoded);
  // file:///c:/x -> c:/x
  if (out.size() >= 3 && out[0] == '/' && std::isalpha(static_cast<unsigned char>(out[1])) &&
      out[2] == ':') {
    out.erase(0, 1);
  }
  return out;
}

std::string path_to_file_uri(std::string_view path) {
  std::string p(path);
  std::replace(p.begin(), p.end(), '\\', '/');
  // A drive letter in lower case, as VS Code writes it: "C:/x" is file:///c%3A/x.
  if (p.size() >= 2 && p[1] == ':' && std::isalpha(static_cast<unsigned char>(p[0]))) {
    p[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(p[0])));
  }
  std::string out = "file://";
  if (p.empty() || p[0] != '/') out.push_back('/');
  out += percent_encode(p, "/");
  return out;
}

bool read_sql_file(const std::string &path, std::string *text) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return false;
  const auto size = in.tellg();
  if (size < 0) return false;
  text->resize(static_cast<size_t>(size));
  in.seekg(0);
  if (!in.read(text->data(), static_cast<std::streamsize>(text->size()))) return false;
  if (first_invalid_utf8(*text)) *text = latin1_to_utf8(*text);
  return true;
}

const Workspace_file *Workspace_catalog::file(std::string_view uri) const {
  const std::string path = file_uri_to_path(uri);
  const std::string canonical = path.empty() ? std::string() : catalog_path(path);
  for (const auto &f : files) {
    if (f.uri == uri || (!canonical.empty() && f.path == canonical)) return &f;
  }
  return nullptr;
}

std::vector<std::string> expand_catalog_paths(const std::vector<std::string> &entries,
                                              const std::string &root,
                                              std::vector<std::string> *errors) {
  std::vector<std::string> out;
  std::set<std::string> seen;
  auto add = [&](const fs::path &p) {
    const std::string s = catalog_path(p);
    if (seen.insert(s).second) out.push_back(s);
  };
  for (const auto &e : entries) {
    if (e.empty()) continue;
    fs::path p = fs::path(e);
    if (p.is_relative() && !root.empty()) p = fs::path(root) / p;
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
      std::vector<fs::path> found;
      for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end;
           !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file(ec) && iequals(it->path().extension().string(), ".sql")) {
          found.push_back(it->path());
        }
      }
      std::sort(found.begin(), found.end());
      for (const auto &f : found) add(f);
    } else if (fs::is_regular_file(p, ec)) {
      add(p);
    } else if (errors != nullptr) {
      errors->push_back(e + ": no such file or directory");
    }
  }
  return out;
}

Workspace_catalog build_workspace_catalog(const std::vector<std::string> &paths, Parser_pool &pool,
                                          const Index_options &options, int priority) {
  Workspace_catalog out;
  Outline_options oo;
  oo.version_id = options.version_id;
  oo.priority = priority;
  for (const auto &path : paths) {
    Workspace_file f;
    f.path = path;
    f.uri = path_to_file_uri(path);
    if (!read_sql_file(path, &f.text)) {
      out.errors.push_back(path + ": cannot read");
      continue;
    }
    f.lines = Line_map(f.text);
    out.files.push_back(std::move(f));
  }
  // SOURCE directives are expanded.  A listed file that another listed file
  // includes is folded only through that include, so its objects are there
  // once, in the context of the script that includes it.
  const size_t listed = out.files.size();
  std::vector<Source_expansion> expansions(listed);
  std::set<std::string> included;
  for (size_t k = 0; k < listed; ++k) {
    const auto &f = out.files[k];
    expansions[k] = expand_sources(f.text, fs::path(f.path).parent_path().generic_string());
    for (const auto &inc : expansions[k].includes) {
      const std::string path = catalog_path(inc.path);
      if (inc.error.empty() && path != f.path) included.insert(path);
    }
  }
  // An included file is a source too: the file entry of its path, added
  // after the listed ones when it is not listed.
  auto source_of = [&out](const Source_include &inc) {
    const std::string path = catalog_path(inc.path);
    for (size_t k = 0; k < out.files.size(); ++k)
      if (out.files[k].path == path) return static_cast<uint32_t>(k);
    Workspace_file f;
    f.path = path;
    f.uri = path_to_file_uri(path);
    f.text = inc.text;
    f.lines = inc.lines;
    out.files.push_back(std::move(f));
    return static_cast<uint32_t>(out.files.size() - 1);
  };
  // Each file is split and folded in order; the files' texts stay put, so
  // the origins point into them.
  for (size_t k = 0; k < listed; ++k) {
    if (included.contains(out.files[k].path)) continue;
    const auto &e = expansions[k];
    if (e.empty()) {
      const auto &f = out.files[k];
      const auto index = Statement_index::build_parallel(f.text, options, 0);
      fold_outline(out.outline, f.text, index, pool, oo, static_cast<uint32_t>(k));
      continue;
    }
    std::vector<uint32_t> sources;
    for (const auto &inc : e.includes) sources.push_back(inc.error.empty() ? source_of(inc) : 0);
    fold_outline(out.outline, e, pool, options, oo, static_cast<uint32_t>(k), sources);
  }
  return out;
}

}  // namespace mdbls
