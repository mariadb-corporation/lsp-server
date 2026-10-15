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

find_package(RapidJSON CONFIG REQUIRED)

add_executable(mariadb-lsp main.cc)
target_link_libraries(mariadb-lsp PRIVATE mariadblanguageserver rapidjson)
mdbls_target_warnings(mariadb-lsp)
set_target_properties(mariadb-lsp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Smoke tests of the command line.
set(_fixtures "${PROJECT_SOURCE_DIR}/tests/fixtures/sql")
add_test(NAME mariadb-lsp.split COMMAND mariadb-lsp split "${_fixtures}/sakila-schema.sql")
add_test(NAME mariadb-lsp.split-json
         COMMAND mariadb-lsp split --format json "${_fixtures}/mysqldump_t.sql")
add_test(NAME mariadb-lsp.tokens COMMAND mariadb-lsp tokens "${_fixtures}/world.sql")
add_test(NAME mariadb-lsp.usage COMMAND mariadb-lsp)
set_tests_properties(mariadb-lsp.usage PROPERTIES WILL_FAIL TRUE)
set_tests_properties(mariadb-lsp.split PROPERTIES
  PASS_REGULAR_EXPRESSION "CREATE TABLE actor")
