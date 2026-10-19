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

# What `cmake --install` puts where, and the zip package `cpack` makes of it:
#
#   mariadb-lsp-<version>-<platform>/
#     bin/mariadb-lsp            the CLI and language server (stripped)
#     LICENSE, README.md
#     docs/language-server.md, docs/outline-format.md
#
# The binary links only system libraries (the client library, the parser and
# the vcpkg dependencies are static), so it runs from wherever it is copied.
# The name follows MariaDB Shell's packages (its cmake/packaging.cmake):
# <platform> is macos<major>-<arch>, windows-<arch> or
# linux-glibc<version>-<arch>, <arch> arm-64bit or x86-64bit; pass
# -DMDBLS_PLATFORM=... to name it otherwise.

install(TARGETS mariadb-lsp RUNTIME DESTINATION bin)
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_SOURCE_DIR}/README.md" DESTINATION .)
install(FILES "${PROJECT_SOURCE_DIR}/docs/language-server.md"
              "${PROJECT_SOURCE_DIR}/docs/outline-format.md"
        DESTINATION docs)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]|[Aa][Aa][Rr][Cc][Hh]")
  set(_mdbls_arch "arm-64bit")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(_mdbls_arch "x86-32bit")
else()
  set(_mdbls_arch "x86-64bit")
endif()

if(NOT MDBLS_PLATFORM)
  if(APPLE)
    execute_process(COMMAND sw_vers -productVersion OUTPUT_VARIABLE _mdbls_macos
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "^[0-9]+" _mdbls_macos "${_mdbls_macos}")
    set(MDBLS_PLATFORM "macos${_mdbls_macos}-${_mdbls_arch}")
  elseif(WIN32)
    set(MDBLS_PLATFORM "windows-${_mdbls_arch}")
  else()
    execute_process(COMMAND getconf GNU_LIBC_VERSION OUTPUT_VARIABLE _mdbls_glibc
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    string(REGEX MATCH "[0-9]+\\.[0-9]+" _mdbls_glibc "${_mdbls_glibc}")
    if(_mdbls_glibc)
      set(MDBLS_PLATFORM "linux-glibc${_mdbls_glibc}-${_mdbls_arch}")
    else()
      set(MDBLS_PLATFORM "linux-${_mdbls_arch}")
    endif()
  endif()
endif()

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_NAME "mariadb-lsp")
set(CPACK_PACKAGE_VENDOR "MariaDB plc")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "MariaDB SQL language server and command line tools ${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_FILE_NAME "mariadb-lsp-${PROJECT_VERSION}-${MDBLS_PLATFORM}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  string(APPEND CPACK_PACKAGE_FILE_NAME "-debug")
endif()
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
if(NOT WIN32)
  set(CPACK_STRIP_FILES "bin/mariadb-lsp")
endif()
include(CPack)
