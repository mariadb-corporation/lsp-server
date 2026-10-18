# Copyright (c) 2026, MariaDB plc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License, version 2.0,
# as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,  but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
# the GNU General Public License, version 2.0, for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

# The language server engine.  C++23 internally; the C API for embedding in
# MariaDB Shell comes later (see docs/plans/core-implementation.md).

add_library(mariadblanguageserver STATIC
  src/catalog.cc
  src/catalog_view.cc
  src/charset.cc
  src/completion.cc
  src/connection_uri.cc
  src/document.cc
  src/keywords.cc
  src/line_map.cc
  src/live_catalog.cc
  src/lsp/features.cc
  src/lsp/language_features.cc
  src/lsp/server.cc
  src/lsp/transport.cc
  src/navigation.cc
  src/outline.cc
  src/secret_store.cc
  src/semantic_tokens.cc
  src/sql_mode.cc
  src/sql_names.cc
  src/statement_index.cc
  src/parser_pool.cc
  src/syntax_check.cc
  src/syntax_tree.cc
  src/workspace_catalog.cc)
target_include_directories(mariadblanguageserver PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
find_package(Threads REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)
# rapidjson is PUBLIC: include/mdbls/lsp/json_position.h writes with it.
target_link_libraries(mariadblanguageserver PUBLIC sqlsplitter mariadbparser Threads::Threads
                      rapidjson)
# PUBLIC: mariadb-lsp --version prints it too.
target_compile_definitions(mariadblanguageserver PUBLIC MDBLS_VERSION="${PROJECT_VERSION}")
# Live connections (src/live_catalog.cc) need the client library.
if(WITH_MARIADB_CLIENT)
  target_link_libraries(mariadblanguageserver PRIVATE mariadb::client)
  target_compile_definitions(mariadblanguageserver PRIVATE MDBLS_WITH_CLIENT=1)
else()
  target_compile_definitions(mariadblanguageserver PRIVATE MDBLS_WITH_CLIENT=0)
endif()
mdbls_target_warnings(mariadblanguageserver)
