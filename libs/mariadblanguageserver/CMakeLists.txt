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
  src/charset.cc
  src/document.cc
  src/line_map.cc
  src/lsp/features.cc
  src/lsp/server.cc
  src/lsp/transport.cc
  src/outline.cc
  src/semantic_tokens.cc
  src/sql_mode.cc
  src/statement_index.cc
  src/parser_pool.cc
  src/syntax_check.cc
  src/syntax_tree.cc)
target_include_directories(mariadblanguageserver PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
                           PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
find_package(Threads REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)
target_link_libraries(mariadblanguageserver PUBLIC sqlsplitter mariadbparser Threads::Threads
                      PRIVATE rapidjson)
mdbls_target_warnings(mariadblanguageserver)
