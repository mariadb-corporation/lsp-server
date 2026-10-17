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
#include <iterator>
#include <set>
#include <system_error>

#include "mdbls/charset.h"
#include "text_util.h"

namespace mdbls {

namespace fs = std::filesystem;

namespace {

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

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
  std::string out;
  for (size_t i = 0; i < uri.size(); ++i) {
    if (uri[i] == '%' && i + 2 < uri.size() && hex_value(uri[i + 1]) >= 0 &&
        hex_value(uri[i + 2]) >= 0) {
      out.push_back(static_cast<char>(hex_value(uri[i + 1]) * 16 + hex_value(uri[i + 2])));
      i += 2;
    } else {
      out.push_back(uri[i]);
    }
  }
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
  std::string out = "file://";
  if (p.empty() || p[0] != '/') out.push_back('/');
  static const char *hex = "0123456789ABCDEF";
  for (const char ch : p) {
    const auto c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(ch);
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 15]);
    }
  }
  return out;
}

bool read_sql_file(const std::string &path, std::string *text) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  text->assign(std::istreambuf_iterator<char>(in), {});
  if (in.bad()) return false;
  if (first_invalid_utf8(*text)) *text = latin1_to_utf8(*text);
  return true;
}

const Workspace_file *Workspace_catalog::file(std::string_view uri) const {
  const std::string path = file_uri_to_path(uri);
  for (const auto &f : files) {
    if (f.uri == uri || (!path.empty() && f.path == path)) return &f;
  }
  return nullptr;
}

std::vector<std::string> expand_catalog_paths(const std::vector<std::string> &entries,
                                              const std::string &root,
                                              std::vector<std::string> *errors) {
  std::vector<std::string> out;
  std::set<std::string> seen;
  auto add = [&](const fs::path &p) {
    std::error_code ec;
    fs::path canonical = fs::weakly_canonical(p, ec);
    const std::string s = (ec ? p : canonical).generic_string();
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
  // Each file is split and folded in order; the files' texts stay put, so
  // the origins point into them.
  for (size_t k = 0; k < out.files.size(); ++k) {
    const auto &f = out.files[k];
    const auto index = Statement_index::build_parallel(f.text, options, 0);
    fold_outline(out.outline, f.text, index, pool, oo, static_cast<uint32_t>(k));
  }
  return out;
}

}  // namespace mdbls
