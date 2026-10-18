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
target_compile_definitions(mariadb-lsp PRIVATE MDBLS_VERSION="${PROJECT_VERSION}")
mdbls_target_warnings(mariadb-lsp)
set_target_properties(mariadb-lsp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Smoke tests of the command line.
set(_fixtures "${PROJECT_SOURCE_DIR}/tests/fixtures/sql")
add_test(NAME mariadb-lsp.split COMMAND mariadb-lsp split "${_fixtures}/sakila-schema.sql")
add_test(NAME mariadb-lsp.split-json
         COMMAND mariadb-lsp split --format json "${_fixtures}/mysqldump_t.sql")
add_test(NAME mariadb-lsp.tokens COMMAND mariadb-lsp tokens "${_fixtures}/world.sql")
add_test(NAME mariadb-lsp.usage COMMAND mariadb-lsp)
add_test(NAME mariadb-lsp.version COMMAND mariadb-lsp --version)
set_tests_properties(mariadb-lsp.version PROPERTIES
  PASS_REGULAR_EXPRESSION "^mariadb-lsp ${PROJECT_VERSION}\n$")
set_tests_properties(mariadb-lsp.usage PROPERTIES WILL_FAIL TRUE)
set_tests_properties(mariadb-lsp.split PROPERTIES
  PASS_REGULAR_EXPRESSION "CREATE TABLE actor")
add_test(NAME mariadb-lsp.check-valid
         COMMAND mariadb-lsp check "${_fixtures}/sakila-schema.sql" "${_fixtures}/mrs_grammar_test.sql")
add_test(NAME mariadb-lsp.check-json
         COMMAND mariadb-lsp check --format json "${_fixtures}/mysqldump_t.sql")
set_tests_properties(mariadb-lsp.check-json PROPERTIES
  PASS_REGULAR_EXPRESSION "sql_mode ORACLE is not supported")
add_test(NAME mariadb-lsp.check-latin1-declared COMMAND mariadb-lsp check "${_fixtures}/world.sql")
add_test(NAME mariadb-lsp.check-latin1-undeclared
         COMMAND mariadb-lsp check "${_fixtures}/latin1_undeclared.sql")
set_tests_properties(mariadb-lsp.check-latin1-undeclared PROPERTIES
  PASS_REGULAR_EXPRESSION "latin1_undeclared.sql:2:30: error: the file is not valid UTF-8 \\(byte 0xB4\\)")
add_test(NAME mariadb-lsp.check-latin1-option
         COMMAND mariadb-lsp check --charset latin1 "${_fixtures}/latin1_undeclared.sql")
add_test(NAME mariadb-lsp.outline COMMAND mariadb-lsp outline "${_fixtures}/sakila-schema.sql")
set_tests_properties(mariadb-lsp.outline PROPERTIES
  PASS_REGULAR_EXPRESSION "\"ddl\":35,\"applied\":35")
add_test(NAME mariadb-lsp.tree COMMAND mariadb-lsp tree "${_fixtures}/mrs_grammar_test.sql")

# End-to-end tests of the language server (tests/e2e): Python 3, standard
# library only.  MDBLS_E2E_LARGE_MB sets the size of the generated dump the
# large-file tests open; MDBLS_LARGE_TESTS adds a run at 256 MB.  The
# completion latency bound is P5's 20 ms (p95 at 1 MB) for optimized builds.
# The live-connection tests need mariadbd on PATH and MDBLS_SHELL_BIN_DIR.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    set(_p95 20)
  else()
    set(_p95 100)
  endif()
  add_test(NAME mariadb-lsp.e2e
           COMMAND Python3::Interpreter -m unittest discover -s "${PROJECT_SOURCE_DIR}/tests/e2e" -v)
  set_tests_properties(mariadb-lsp.e2e PROPERTIES
    ENVIRONMENT "MARIADB_LSP=$<TARGET_FILE:mariadb-lsp>;MDBLS_E2E_LARGE_MB=16;MDBLS_COMPLETION_P95_MS=${_p95};MDBLS_SHELL_BIN_DIR=${MDBLS_SHELL_BIN_DIR}"
    TIMEOUT 300)
  option(MDBLS_LARGE_TESTS "Run the end-to-end tests on a 256 MB dump too" OFF)
  if(MDBLS_LARGE_TESTS)
    add_test(NAME mariadb-lsp.e2e-large
             COMMAND Python3::Interpreter -m unittest discover -s "${PROJECT_SOURCE_DIR}/tests/e2e"
                     -k Large_file -v)
    set_tests_properties(mariadb-lsp.e2e-large PROPERTIES
      ENVIRONMENT "MARIADB_LSP=$<TARGET_FILE:mariadb-lsp>;MDBLS_E2E_LARGE_MB=256"
      TIMEOUT 900)
  endif()
else()
  message(STATUS "Python 3 not found: the language server's end-to-end tests are skipped")
endif()
