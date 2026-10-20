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
#     lib/                       the shared libraries it loads, with a
#                                -dynamic vcpkg triplet (Windows: in bin/)
#     LICENSE, README.md
#     docs/language-server.md, docs/outline-format.md
#
# The client library and the parser are always static. With a static vcpkg
# triplet (arm64-osx, x64-linux) so are OpenSSL and zlib, and the binary
# links only system libraries; with a -dynamic one (and the Windows triplets)
# they are shared libraries, installed beside it and found through its RPATH.
# Either way the package runs from wherever it is unpacked.
# The name follows MariaDB Shell's packages (its cmake/packaging.cmake):
# <platform> is macos<major>-<arch>, windows-<arch> or
# linux-glibc<version>-<arch>, <arch> arm-64bit or x86-64bit; pass
# -DMDBLS_PLATFORM=... to name it otherwise.

install(TARGETS mariadb-lsp RUNTIME_DEPENDENCY_SET mdbls_runtime_libraries
        RUNTIME DESTINATION bin)

# The shared libraries mariadb-lsp loads that are not the system's: vcpkg's,
# empty with a static triplet. The build tree's RPATH points at vcpkg's lib
# directory; the installed binary's points at the package's lib/.
set(_mdbls_vcpkg_prefix "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  string(APPEND _mdbls_vcpkg_prefix "/debug")
endif()
install(RUNTIME_DEPENDENCY_SET mdbls_runtime_libraries
        DIRECTORIES "${_mdbls_vcpkg_prefix}/lib" "${_mdbls_vcpkg_prefix}/bin"
        PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
        POST_EXCLUDE_REGEXES "^/usr/lib" "^/System/" "^/lib" "^/usr/local/lib"
                             "[Ss]ystem32" "[Ww]indows"
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        FRAMEWORK DESTINATION lib)
if(APPLE)
  set_target_properties(mariadb-lsp PROPERTIES INSTALL_RPATH "@loader_path/../lib")
elseif(UNIX)
  set_target_properties(mariadb-lsp PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()

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
