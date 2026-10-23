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

# The MariaDB LSP Server.  The build follows MariaDB Shell's:
#
#   cmake .. -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_VCPKG_TRIPLET=<triplet>
#
# fetches vcpkg (into ../vcpkg unless VCPKG_ROOT says otherwise) and installs
# the manifest in vcpkg.json; -DWITH_TESTS=0 leaves the test dependencies out.

cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED WITH_TESTS)
  set(WITH_TESTS 1)
endif()
if(WITH_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

# Must run before project(): it sets CMAKE_TOOLCHAIN_FILE.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bootstrap_vcpkg.cmake)

# The MariaDB client library comes from a MariaDB server tree, as in MariaDB
# Shell: -DMARIADB_SOURCE_DIR=<src> -DMARIADB_BUILD_DIR=<bld> for an existing
# build (the client targets built), else the tree is fetched and the client
# targets built at configure time (cmake/bootstrap_mariadb.cmake).
# The server commit is pinned in libs/mariadbparser/UPSTREAM.md, read by
# cmake/mariadb_pin.cmake; moving the pin there re-runs the bootstrap.
option(WITH_MARIADB_CLIENT "Link the MariaDB client library (live connections)" ON)
# MariaDB Shell builds this project with its own server tree, which follows
# the server's main: the client library only, the grammar is in generated/.
option(MDBLS_CHECK_SERVER_PIN
  "Warn when MARIADB_SOURCE_DIR is not at the pin of libs/mariadbparser/UPSTREAM.md" ON)
if(WITH_MARIADB_CLIENT)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/mariadb_pin.cmake)
  if(NOT MARIADB_SOURCE_DIR OR
     (MDBLS_BOOTSTRAPPED_REF AND NOT MDBLS_BOOTSTRAPPED_REF STREQUAL MDBLS_MARIADB_REF))
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bootstrap_mariadb.cmake)
  elseif(NOT MDBLS_BOOTSTRAPPED_REF AND MDBLS_CHECK_SERVER_PIN)
    # A server tree given with -DMARIADB_SOURCE_DIR: check it is at the pin.
    find_package(Git QUIET)
    if(GIT_FOUND)
      execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MARIADB_SOURCE_DIR}" rev-parse HEAD
                      OUTPUT_VARIABLE _mdbls_tree_head OUTPUT_STRIP_TRAILING_WHITESPACE
                      ERROR_QUIET RESULT_VARIABLE _rc)
      if(_rc EQUAL 0 AND NOT _mdbls_tree_head STREQUAL MDBLS_MARIADB_PIN)
        message(WARNING "MARIADB_SOURCE_DIR is at ${_mdbls_tree_head}, not at the pin "
          "${MDBLS_MARIADB_PIN} in libs/mariadbparser/UPSTREAM.md: the client "
          "library and the grammar come from different server commits.")
      endif()
    endif()
  endif()
endif()

# The project version comes from the VERSION file ("VERSION = x.y.z").
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _mdbls_version_line
     REGEX "^VERSION[ \t]*=[ \t]*[0-9]+\\.[0-9]+\\.[0-9]+[ \t]*$"
     LIMIT_COUNT 1)
if(NOT _mdbls_version_line MATCHES "^VERSION[ \t]*=[ \t]*([0-9]+\\.[0-9]+\\.[0-9]+)[ \t]*$")
  message(FATAL_ERROR "VERSION must contain a line of the form 'VERSION = x.y.z'")
endif()
set(_mdbls_version "${CMAKE_MATCH_1}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

project(mariadb-lsp-server VERSION ${_mdbls_version} LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(compiler)
include(bison)
if(WITH_MARIADB_CLIENT)
  include(mariadb_client)
endif()

enable_testing()

# MariaDB Shell's binary directory: the secret store and live-connection
# tests use its mariadb-secret-store-login-path helper (and the shell's
# sandboxes), and are skipped without it.  By default the directory of the
# mariadb-shell on the PATH (an installed shell), else a build of the shell
# next to this checkout.
find_program(MDBLS_SHELL_PROGRAM mariadb-shell)
if(MDBLS_SHELL_PROGRAM)
  file(REAL_PATH "${MDBLS_SHELL_PROGRAM}" _shell)
  get_filename_component(_shell_bin_dir "${_shell}" DIRECTORY)
else()
  set(_shell_bin_dir "${PROJECT_SOURCE_DIR}/../mariadb-shell/build/bin")
endif()
set(MDBLS_SHELL_BIN_DIR "${_shell_bin_dir}" CACHE PATH
    "Directory of MariaDB Shell's binaries, for the secret store and live-connection tests")

add_subdirectory(libs/mariadbparser)
add_subdirectory(libs/sqlsplitter)
add_subdirectory(libs/mariadblanguageserver)
add_subdirectory(src/mariadb-lsp)

if(WITH_TESTS)
  add_subdirectory(unittest)
endif()
if(WITH_FUZZERS)
  add_subdirectory(fuzz)
endif()

# cmake --install and cpack: bin/mariadb-lsp and its documents, as a zip.
include(packaging)
