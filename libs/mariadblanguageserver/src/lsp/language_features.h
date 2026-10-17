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

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "mdbls/catalog_view.h"
#include "mdbls/completion.h"
#include "mdbls/document.h"
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
  std::string schema_hint;  // documentContext's currentSchema
};

// The catalogs as one view, the current schema the one in use at `offset`.
Catalog_view catalog_view(const Snapshot &s, const Catalog_sources &c, uint32_t offset);

void completion(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                Parser_cache &cache, const Completion_options &options);

void hover(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
           Parser_cache &cache);

// Location[] (or null) of what the name at `pos` refers to.
void definition(Json_writer &w, const Snapshot &s, const std::string &uri, Position pos,
                const Catalog_sources &c, Parser_cache &cache);

// Location[] of every reference in the document.
void references(Json_writer &w, const Snapshot &s, const std::string &uri, Position pos,
                bool include_declaration, const Catalog_sources &c, Parser_pool &pool,
                Parser_cache &cache, int priority);

// DocumentHighlight[] within the statement at `pos`.
void document_highlight(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                        Parser_cache &cache);

// prepareRename: {range, placeholder}; false with `error` when the name at
// `pos` cannot be renamed (only aliases, CTEs, variables and the objects
// the document itself defines can).
bool prepare_rename(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                    Parser_cache &cache, std::string *error);

// rename: a WorkspaceEdit for this document; false with `error` as above.
bool rename(Json_writer &w, const Snapshot &s, const std::string &uri, Position pos,
            const std::string &new_name, const Catalog_sources &c, Parser_pool &pool,
            Parser_cache &cache, int priority, std::string *error);

void signature_help(Json_writer &w, const Snapshot &s, Position pos, const Catalog_sources &c,
                    Parser_cache &cache);

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
