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

#ifndef MDBLS_LSP_LANGUAGE_FEATURES_H_
#define MDBLS_LSP_LANGUAGE_FEATURES_H_

#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "mdbls/catalog_view.h"
#include "mdbls/completion.h"
#include "mdbls/document.h"
#include "mdbls/formatter.h"
#include "mdbls/live_catalog.h"
#include "mdbls/lsp/json_position.h"
#include "mdbls/outline.h"
#include "mdbls/parser_pool.h"
#include "mdbls/workspace_catalog.h"

// Completion, hover, navigation, rename, signature help and workspace
// symbols, computed from a snapshot and its catalogs and written as LSP
// JSON.  Private to the server.
namespace mdbls::lsp {

// The catalogs a request about one document sees, in this order.
struct Catalog_sources {
  std::shared_ptr<const Outline> document;            // the document's own DDL
  std::shared_ptr<const Workspace_catalog> workspace;  // the catalog files
  std::shared_ptr<const Live_snapshot> live;           // the connection's
  // Where `live` came from: reads the details of its stubs on demand.
  std::shared_ptr<Live_catalog> live_source;
  std::string schema_hint;  // documentContext's currentSchema
};

// When `pos` is in the path of a SOURCE directive: writes a CompletionList
// of the folders and .sql files there (`folder` is the document's, for
// relative paths) and returns true.
bool source_path_completion(Json_writer &w, const Snapshot &s, Position pos,
                            const std::string &folder);

// The catalogs as one view, the current schema the one in use at `offset`.
Catalog_view catalog_view(const Snapshot &s, const Catalog_sources &c, uint32_t offset);

// How long a request waits for the details of stubs it met.
constexpr std::chrono::milliseconds k_details_wait{3000};

// Completion, hover and signature help run in passes when they meet stubs
// of the live catalog (Table::stub).  The first pass names them in
// `wanted` and writes nothing; the server has them read without holding a
// thread, then runs the request again, `final`, on the snapshot that has
// them, or without them after k_details_wait (`read` false).
struct Details_pass {
  bool final = false;
  bool read = false;  // the details came in time
  std::set<Object_key> wanted;
};

void completion(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                Parser_cache &cache, const Completion_options &options, Details_pass &pass);

void hover(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
           Parser_cache &cache, Details_pass &pass);

// Location[] (or null) of what the name at `pos` refers to.
void definition(Json_writer &w, const Snapshot &s, const std::string &uri, Position pos,
                const Catalog_sources &c, Parser_cache &cache);

// The open script that includes a document (SOURCE), for references and
// rename requested in the included one: they run in the script's context
// and cover the script and every file it includes.
struct Including_script {
  std::shared_ptr<const Snapshot> snapshot;  // the script's
  std::string uri;                           // the script's
  Catalog_sources catalogs;                  // the script's; `document` its fresh outline
  std::string included_path;                 // the included document's canonical path
  std::string included_uri;                  // and its URI
};

// Location[] of every reference in the document, and in the files it
// includes (or, with `script`, in the script that includes it and its
// files).
void references(Json_writer &w, const Snapshot &s, const std::string &uri, Position pos,
                bool include_declaration, const Catalog_sources &c, Parser_pool &pool,
                Parser_cache &cache, int priority, const Including_script *script = nullptr);

// DocumentHighlight[] within the statement at `pos`.
void document_highlight(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                        Parser_cache &cache);

// prepareRename: {range, placeholder}; false with `error` when the name at
// `pos` cannot be renamed (only aliases, CTEs, variables and the objects
// the document itself defines can).
bool prepare_rename(Json_writer &w, const Snapshot &s, const std::string &uri, Position pos,
                    const Catalog_sources &c, Parser_cache &cache, std::string *error,
                    const Including_script *script = nullptr);

// rename: a WorkspaceEdit for this document; false with `error` as above.
bool rename(Json_writer &w, const Snapshot &s, const std::string &uri, Position pos,
            const std::string &new_name, const Catalog_sources &c, Parser_pool &pool,
            Parser_cache &cache, int priority, std::string *error,
            const Including_script *script = nullptr);

void signature_help(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                    Parser_cache &cache, Details_pass &pass);

// TextEdit[] laying out the queries of the statements that overlap
// [from, to), or of the whole document when `range` is false.
void formatting(Json_writer &w, const Snapshot &s, bool range, Position from, Position to,
                const Format_options &o, Parser_cache &cache);

struct Symbol_document {
  std::string uri;
  std::shared_ptr<const Snapshot> snapshot;
  std::shared_ptr<const Outline> outline;
};

// SymbolInformation[] of the objects whose name contains `query` (any
// case): the open documents' first, then the workspace catalog's files
// that are not open.
void workspace_symbols(Json_writer &w, std::string_view query,
                       const std::vector<Symbol_document> &documents,
                       const Workspace_catalog *workspace, size_t max_symbols);

}  // namespace mdbls::lsp

#endif  // MDBLS_LSP_LANGUAGE_FEATURES_H_
