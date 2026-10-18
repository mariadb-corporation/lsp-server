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

#ifndef MDBLS_MCP_CATALOG_H_
#define MDBLS_MCP_CATALOG_H_

// A server's catalog read through MariaDB Shell's MCP server (its db.*
// tools) rather than a direct connection: what `mariadb-lsp lsp --mcp`
// does, the tool calls made by its client, which has the MCP server.

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <rapidjson/document.h>

#include "mdbls/live_catalog.h"

namespace mdbls {

// Why a tool call failed.
struct Tool_failure {
  std::string message;
  bool permanent = false;  // another call would fail the same way
};

// Calls one tool of the MCP server on the connection the catalog is read
// for: `arguments_json` is the tool's arguments as a JSON object, and the
// tool's result is parsed into `result`.  nullopt on success.
using Tool_caller = std::function<std::optional<Tool_failure>(
    std::string_view tool, const std::string &arguments_json, rapidjson::Document *result)>;

struct Mcp_catalog_options {
  // Asked between tool calls: true gives up the reading.
  std::function<bool()> cancelled;
};

// Reads the server's sql_mode, version and current schema (db.execute_sql),
// its schemas (db.list_schemas), each schema's tables and views
// (db.list_objects), and, for the user schemas, their routines, triggers,
// events and sequences, with the columns, keys and foreign keys of each
// table, the columns of each view, the parameters of each routine and the
// table of each trigger (db.get_object_details).  System schemas are listed
// with their tables and views, without details: one call per object would
// make them cost more than they are used.  performance_schema is left out,
// as the direct reader leaves it out.  nullopt, with `error` and `permanent`
// set, on the first call that fails.
std::optional<Live_snapshot> fetch_mcp_catalog(const Tool_caller &call, std::string *error,
                                               bool *permanent,
                                               const Mcp_catalog_options &options = {});

}  // namespace mdbls

#endif  // MDBLS_MCP_CATALOG_H_
