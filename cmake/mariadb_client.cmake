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

# The MariaDB client library, linked statically from a MariaDB server build
# exactly as MariaDB Shell links it (the MARIADB_SOURCE_DIR block of the shell's
# CMakeLists.txt): libmariadbclient plus the server's mysys, mysys_ssl, strings
# and dbug, headers from the source and build trees.  Exposed as the INTERFACE
# target mariadb::client.

if(NOT MARIADB_SOURCE_DIR OR NOT MARIADB_BUILD_DIR)
  message(FATAL_ERROR "mariadb_client.cmake needs MARIADB_SOURCE_DIR and MARIADB_BUILD_DIR")
endif()
file(TO_CMAKE_PATH "${MARIADB_SOURCE_DIR}" MARIADB_SOURCE_DIR)
file(TO_CMAKE_PATH "${MARIADB_BUILD_DIR}" MARIADB_BUILD_DIR)

find_library(MARIADB_CLIENT_LIBRARY
  NAMES mariadbclient libmariadbclient
  PATHS "${MARIADB_BUILD_DIR}/libmariadb/libmariadb"
  NO_DEFAULT_PATH)
if(NOT MARIADB_CLIENT_LIBRARY)
  message(FATAL_ERROR "Could not find libmariadbclient in "
    "${MARIADB_BUILD_DIR}/libmariadb/libmariadb")
endif()

set(_slp "${CMAKE_STATIC_LIBRARY_PREFIX}")
set(_sls "${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(_mariadb_client_libs
  "${MARIADB_CLIENT_LIBRARY}"
  "${MARIADB_BUILD_DIR}/mysys/${_slp}mysys${_sls}"
  "${MARIADB_BUILD_DIR}/mysys_ssl/${_slp}mysys_ssl${_sls}"
  "${MARIADB_BUILD_DIR}/strings/${_slp}strings${_sls}"
  "${MARIADB_BUILD_DIR}/dbug/${_slp}dbug${_sls}"
  "${MARIADB_BUILD_DIR}/mysys/${_slp}mysys${_sls}"
  "${MARIADB_BUILD_DIR}/strings/${_slp}strings${_sls}")
foreach(_lib ${_mariadb_client_libs})
  if(NOT EXISTS "${_lib}")
    message(FATAL_ERROR "MariaDB client library part missing: ${_lib}")
  endif()
endforeach()

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

add_library(mariadb_client INTERFACE)
add_library(mariadb::client ALIAS mariadb_client)
target_include_directories(mariadb_client SYSTEM INTERFACE
  "${MARIADB_SOURCE_DIR}/libmariadb/include"
  "${MARIADB_BUILD_DIR}/libmariadb/include"
  "${MARIADB_SOURCE_DIR}/include"
  "${MARIADB_BUILD_DIR}/include")
# The server's include/mysql.h pulls in mariadb_capi_rename.h, which renames
# the client C API to server_* for the server's own use.  Defining its include
# guard keeps the unprefixed names the client library exports (the shell does
# the same with -DMARIADB_CAPI_RENAME_INCLUDED).
target_compile_definitions(mariadb_client INTERFACE MARIADB_CAPI_RENAME_INCLUDED)
target_link_libraries(mariadb_client INTERFACE
  ${_mariadb_client_libs}
  OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
  target_link_libraries(mariadb_client INTERFACE
    secur32 crypt32 bcrypt shlwapi ws2_32 advapi32)
elseif(APPLE)
  target_link_libraries(mariadb_client INTERFACE iconv)
endif()

# The caching_sha2_password client plugin the shell bundles.
set(_plugin_ext "so")
if(WIN32)
  set(_plugin_ext "dll")
endif()
file(GLOB MARIADB_CLIENT_PLUGINS
  "${MARIADB_BUILD_DIR}/libmariadb/caching_sha2_password.${_plugin_ext}")
message(STATUS "MariaDB client: ${MARIADB_CLIENT_LIBRARY}")
