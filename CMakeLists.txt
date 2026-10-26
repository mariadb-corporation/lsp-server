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
# Without the triplet the host's is used (arm64-osx, x64-linux, ...);
# -DWITH_VCPKG_TRIPLET=OFF takes the dependencies from the system.

cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED WITH_TESTS)
  set(WITH_TESTS 1)
endif()
if(WITH_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

# Without -DWITH_VCPKG_TRIPLET or a toolchain file (a plain configure after
# checkout, or an IDE's), vcpkg is used with the host's triplet;
# -DWITH_VCPKG_TRIPLET=OFF takes the dependencies from the system instead.
if(NOT DEFINED WITH_VCPKG_TRIPLET AND NOT CMAKE_TOOLCHAIN_FILE
   AND "$ENV{CMAKE_TOOLCHAIN_FILE}" STREQUAL "")
  # CMAKE_HOST_SYSTEM_PROCESSOR is not set before project().
  cmake_host_system_information(RESULT _mdbls_host_arch QUERY OS_PLATFORM)
  string(TOLOWER "${_mdbls_host_arch}" _mdbls_host_arch)
  if(_mdbls_host_arch MATCHES "^(arm64|aarch64)$")
    set(_mdbls_host_arch arm64)
  elseif(_mdbls_host_arch MATCHES "^(x86_64|amd64)$")
    set(_mdbls_host_arch x64)
  else()
    message(FATAL_ERROR "No default vcpkg triplet for the host processor "
      "'${_mdbls_host_arch}': pass -DWITH_VCPKG_TRIPLET=<triplet>, or "
      "-DWITH_VCPKG_TRIPLET=OFF to use the system's packages.")
  endif()
  if(CMAKE_HOST_APPLE)
    set(_mdbls_host_os osx)
  elseif(CMAKE_HOST_WIN32)
    set(_mdbls_host_os windows)
  else()
    set(_mdbls_host_os linux)
  endif()
  set(WITH_VCPKG_TRIPLET "${_mdbls_host_arch}-${_mdbls_host_os}" CACHE STRING
      "vcpkg triplet to fetch the dependencies with (OFF: the system's packages)")
  message(STATUS "WITH_VCPKG_TRIPLET not given: using the host's, '${WITH_VCPKG_TRIPLET}'")
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

# Without vcpkg, say which package is missing rather than fail deep in a
# subdirectory's find_package().
if(NOT CMAKE_TOOLCHAIN_FILE MATCHES "[Vv]cpkg")
  set(_mdbls_missing "")
  find_package(RapidJSON CONFIG QUIET)
  if(NOT RapidJSON_FOUND)
    list(APPEND _mdbls_missing RapidJSON)
  endif()
  if(WITH_TESTS)
    find_package(GTest CONFIG QUIET)
    if(NOT GTest_FOUND)
      list(APPEND _mdbls_missing "GTest (or -DWITH_TESTS=0)")
    endif()
  endif()
  if(_mdbls_missing)
    list(JOIN _mdbls_missing ", " _mdbls_missing)
    message(FATAL_ERROR "Not found on the system: ${_mdbls_missing}.  Install them, "
      "or leave WITH_VCPKG_TRIPLET unset (or pass -DWITH_VCPKG_TRIPLET=<triplet>) "
      "to fetch the dependencies with vcpkg.")
  endif()
endif()

include(compiler)
include(bison)
if(WITH_MARIADB_CLIENT)
  include(mariadb_client)
endif()

enable_testing()

# MariaDB Shell's binary directory: the secret store and live-connection
# tests use its mariadb-secret-store-login-path helper (and the shell's
# sandboxes), and are skipped without it.  Unless set, looked up at every
# configure (so a shell installed later is found): the directory of the
# mariadb-shell on the PATH (an installed shell), else a build of the shell
# next to this checkout.  The normal variable set here, which the
# subdirectories see, holds the directory found.
set(MDBLS_SHELL_BIN_DIR "" CACHE PATH
    "Directory of MariaDB Shell's binaries, for the secret store and live-connection tests (empty: looked up)")
if(NOT MDBLS_SHELL_BIN_DIR)
  # NO_CACHE: a cached result would never be looked up again.
  find_program(MDBLS_SHELL_PROGRAM mariadb-shell NO_CACHE)
  if(MDBLS_SHELL_PROGRAM)
    file(REAL_PATH "${MDBLS_SHELL_PROGRAM}" _shell)
    get_filename_component(MDBLS_SHELL_BIN_DIR "${_shell}" DIRECTORY)
  else()
    set(MDBLS_SHELL_BIN_DIR "${PROJECT_SOURCE_DIR}/../mariadb-shell/build/bin")
  endif()
endif()

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
