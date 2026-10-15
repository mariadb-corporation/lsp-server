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
option(WITH_MARIADB_CLIENT "Link the MariaDB client library (live connections)" ON)
if(WITH_MARIADB_CLIENT AND NOT MARIADB_SOURCE_DIR)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bootstrap_mariadb.cmake)
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

add_subdirectory(libs/mariadbparser)

if(WITH_TESTS)
  add_subdirectory(unittest)
endif()
