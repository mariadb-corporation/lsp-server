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
#
##############################################################################
#
# bootstrap_mariadb.cmake
#
# Copied from MariaDB Shell's cmake/bootstrap_mariadb.cmake.  Differences: the
# default ref is the commit libs/mariadbparser/UPSTREAM.md pins, read from that file, so
# the client library and the grammar come from the same release; the default
# location is ../mariadb-server-lsp, so this never moves the checkout a shell
# build uses; and a new checkout is made with a depth-1 fetch, which works for
# a commit hash as well as a branch.
#
# Configure-time fallback that fetches the MariaDB server sources and builds
# only the pieces the shell links against, then sets MARIADB_SOURCE_DIR and
# MARIADB_BUILD_DIR in the parent scope so the main build proceeds unchanged.
#
# The shell consumes, from a MariaDB server tree (see the root CMakeLists.txt):
#   headers : libmariadb/include, include, sql, <build>/include,
#             <build>/libmariadb/include
#   libs    : libmariadbclient, mysys, mysys_ssl, strings, dbug  (static)
#   plugin  : caching_sha2_password (client auth plugin)
#   cache   : <build>/CMakeCache.txt   (read to detect Debug/DBUG)
#   version : <source>/VERSION
#
# This module therefore configures the full server tree (needed for generated
# headers) but builds ONLY that target subset -- storage engines and the
# server binary are never compiled.
#
##############################################################################

# The MariaDB server this port tracks. NOTE: the shell links the server's
# INTERNAL static libraries (mysys/strings/dbug), which are not a stable ABI --
# a mismatched ref will fail to build or link. Pin MARIADB_GIT_TAG to a ref
# known to match this shell revision.
SET(MARIADB_GIT_REPOSITORY "https://github.com/MariaDB/server.git"
  CACHE STRING "Git URL of the MariaDB server to fetch when no source dir is given")
# The ref comes from cmake/mariadb_pin.cmake: MARIADB_GIT_TAG when given,
# else the commit libs/mariadbparser/UPSTREAM.md pins.
INCLUDE(${CMAKE_CURRENT_LIST_DIR}/mariadb_pin.cmake)
SET(_mdb_ref "${MDBLS_MARIADB_REF}")

# Where the server is cloned and built. Kept OUTSIDE the shell build dir by
# default so a clean shell rebuild does not force a full server re-clone/rebuild.
SET(MARIADB_BOOTSTRAP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mariadb-server-lsp"
  CACHE PATH "Directory holding the auto-fetched MariaDB server source + build")

GET_FILENAME_COMPONENT(MARIADB_BOOTSTRAP_DIR "${MARIADB_BOOTSTRAP_DIR}" ABSOLUTE)
SET(_mdb_src "${MARIADB_BOOTSTRAP_DIR}")
SET(_mdb_bld "${MARIADB_BOOTSTRAP_DIR}/bld")

# Build type to configure the server with. Detecting Debug matters: the shell
# reads the server's CMakeCache.txt to decide whether the DBUG runtime is
# present (see root CMakeLists.txt). Mirror the shell's build type when known.
IF(CMAKE_BUILD_TYPE)
  SET(_mdb_build_type "${CMAKE_BUILD_TYPE}")
ELSE()
  SET(_mdb_build_type "RelWithDebInfo")
ENDIF()

MESSAGE(STATUS "==========================================================")
MESSAGE(STATUS "No MYSQL/MARIADB source dir given -- bootstrapping MariaDB.")
MESSAGE(STATUS "  repo    : ${MARIADB_GIT_REPOSITORY}")
MESSAGE(STATUS "  ref     : ${_mdb_ref}")
MESSAGE(STATUS "  location: ${MARIADB_BOOTSTRAP_DIR}")
MESSAGE(STATUS "  (override with -DMARIADB_SOURCE_DIR / -DMARIADB_GIT_TAG /")
MESSAGE(STATUS "   -DMARIADB_BOOTSTRAP_DIR; an existing checkout is updated to the")
MESSAGE(STATUS "   ref and rebuilt incrementally on every fresh configure)")
MESSAGE(STATUS "==========================================================")

FIND_PACKAGE(Git REQUIRED)

FILE(MAKE_DIRECTORY "${MARIADB_BOOTSTRAP_DIR}")

##############################################################################
# 1. Clone the server source, or update an existing checkout, to the ref.
##############################################################################
IF(NOT EXISTS "${_mdb_src}/.git")
  # An empty repository plus a depth-1 fetch of the ref, rather than
  # `git clone --branch`, so that a commit hash is fetched shallowly too
  # (the language server pins a commit; --branch cannot take one and would
  # fall back to cloning the whole history).
  MESSAGE(STATUS "Initialising a MariaDB server checkout at ${_mdb_src}...")
  EXECUTE_PROCESS(COMMAND "${GIT_EXECUTABLE}" init -q "${_mdb_src}" RESULT_VARIABLE _rc)
  IF(NOT _rc EQUAL 0)
    MESSAGE(FATAL_ERROR "git init failed in ${_mdb_src}")
  ENDIF()
  EXECUTE_PROCESS(
    COMMAND "${GIT_EXECUTABLE}" -C "${_mdb_src}" remote add origin "${MARIADB_GIT_REPOSITORY}"
    RESULT_VARIABLE _rc)
  IF(NOT _rc EQUAL 0)
    MESSAGE(FATAL_ERROR "git remote add failed in ${_mdb_src}")
  ENDIF()
ENDIF()

# Bring the checkout to the ref (a fresh one, or one left by an earlier run).
MESSAGE(STATUS "Updating the MariaDB checkout at ${_mdb_src} to '${_mdb_ref}'...")
EXECUTE_PROCESS(
  COMMAND "${GIT_EXECUTABLE}" -C "${_mdb_src}" fetch --progress --depth 1
          --no-tags origin "${_mdb_ref}"
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0)
  MESSAGE(FATAL_ERROR "Could not fetch '${_mdb_ref}' into ${_mdb_src}. "
    "To build against that tree as it is (e.g. offline), configure with "
    "-DMARIADB_SOURCE_DIR=${_mdb_src} -DMARIADB_BUILD_DIR=${_mdb_bld} instead.")
ENDIF()
EXECUTE_PROCESS(
  COMMAND "${GIT_EXECUTABLE}" -C "${_mdb_src}" checkout --detach FETCH_HEAD
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0)
  MESSAGE(FATAL_ERROR "Failed to check out '${_mdb_ref}' in ${_mdb_src}")
ENDIF()

# The Connector/C (libmariadb) lives in a git submodule; the shell links it.
# Initialise only that submodule -- the storage-engine submodules are large and
# unused here.
MESSAGE(STATUS "Initialising the libmariadb submodule...")
# --depth 1 fetches only the submodule tip (no history); --progress keeps the
# meter visible in non-TTY build panels, matching the clone above.
EXECUTE_PROCESS(
  COMMAND "${GIT_EXECUTABLE}" -C "${_mdb_src}" submodule update --init --progress
          --depth 1 libmariadb
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0)
  MESSAGE(FATAL_ERROR "Failed to init the libmariadb submodule")
ENDIF()

##############################################################################
# 2. Configure the server build tree.
##############################################################################
FILE(MAKE_DIRECTORY "${_mdb_bld}")

SET(_mdb_configure_args
  "-DCMAKE_BUILD_TYPE=${_mdb_build_type}"
  # Build the Connector/C against OpenSSL. On macOS/Linux it otherwise defaults
  # to GnuTLS, which clashes with the shell's OpenSSL at link time.
  "-DCONC_WITH_SSL=OPENSSL"
  # We consume the server as a dependency, we do not develop it, so its
  # maintainer warning set must not gate our build. This is not cosmetic: the
  # default (AUTO) turns warnings into errors for Debug builds, and the set
  # includes -Wcast-function-type-strict, which OpenSSL 3.x's own headers
  # violate -- SKM_DEFINE_STACK_OF_INTERNAL in safestack.h and
  # OSSL_CORE_MAKE_FUNC in core_dispatch.h both cast function pointers. So a
  # Debug build of the Connector/C fails to compile openssl.c on any toolchain
  # whose compiler implements that warning (Clang >= 16, so current Apple
  # Clang). NO makes cmake/maintainer.cmake return before adding any of it.
  "-DMYSQL_MAINTAINER_MODE=NO"
  # Trim configure/build cost: we never build the server binary or engines.
  "-DPLUGIN_TOKUDB=NO"
  "-DPLUGIN_ROCKSDB=NO"
  "-DPLUGIN_MROONGA=NO"
  "-DPLUGIN_SPIDER=NO"
  "-DPLUGIN_COLUMNSTORE=NO"
  "-DWITHOUT_SERVER=1")

# Propagate the OpenSSL location the caller chose for the shell, so the server
# and the shell agree on OpenSSL (see MARIADB_PORT.md). When the shell is built
# through vcpkg and the caller did not pin OpenSSL explicitly, default to the
# vcpkg install prefix so the server links the same OpenSSL vcpkg just installed.
IF(NOT OPENSSL_ROOT_DIR AND VCPKG_INSTALLED_PREFIX)
  SET(OPENSSL_ROOT_DIR "${VCPKG_INSTALLED_PREFIX}")
ENDIF()
IF(OPENSSL_ROOT_DIR)
  LIST(APPEND _mdb_configure_args "-DOPENSSL_ROOT_DIR=${OPENSSL_ROOT_DIR}")
ENDIF()

# When the shell is configured through vcpkg (Windows, or macOS for a
# redistributable build), the server tree must use the same toolchain file and
# target triplet or its dependencies (OpenSSL, etc.) will not resolve and the
# ABI can mismatch the shell. Propagate what the caller chose. This mirrors the
# USING_VCPKG detection in the top-level CMakeLists.txt, replicated here because
# this file is included before that variable is defined.
IF(VCPKG_TARGET_TRIPLET AND CMAKE_TOOLCHAIN_FILE MATCHES "[Vv]cpkg")
  LIST(APPEND _mdb_configure_args "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
  LIST(APPEND _mdb_configure_args "-DVCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}")
  # Point the server's dependency search at the prefix vcpkg already populated
  # for the shell (see bootstrap_vcpkg.cmake), so it reuses that closure --
  # zlib, zstd, OpenSSL -- rather than re-resolving from the system.
  IF(VCPKG_INSTALLED_PREFIX)
    LIST(APPEND _mdb_configure_args "-DCMAKE_PREFIX_PATH=${VCPKG_INSTALLED_PREFIX}")
  ENDIF()
ENDIF()

# Match the shell's generator so nested "cmake --build" uses the same toolchain.
IF(CMAKE_GENERATOR)
  LIST(APPEND _mdb_configure_args "-G" "${CMAKE_GENERATOR}")
ENDIF()

# Invoke via WORKING_DIRECTORY + trailing source path rather than -S/-B so this
# works on the older CMake versions the project still supports (-S/-B need 3.13).
MESSAGE(STATUS "Configuring MariaDB server (${_mdb_build_type})...")
EXECUTE_PROCESS(
  COMMAND "${CMAKE_COMMAND}" ${_mdb_configure_args} "${_mdb_src}"
  WORKING_DIRECTORY "${_mdb_bld}"
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0)
  MESSAGE(FATAL_ERROR "MariaDB server configure failed (see output above)")
ENDIF()

##############################################################################
# 3. Build only the targets the shell links against.
##############################################################################
# Core static libs + the connector -- these MUST succeed. Build one target per
# invocation: passing several targets to a single "--build --target" needs
# CMake >= 3.15, and the project still supports older versions.
# Parallel build: "cmake --build --parallel" is CMake >= 3.12. On older CMake we
# omit it (Ninja still parallelises by default; Make/MSBuild fall back to serial).
SET(_mdb_build_parallel "")
IF(NOT CMAKE_VERSION VERSION_LESS "3.12")
  SET(_mdb_build_parallel "--parallel")
ENDIF()

# On Windows with a dynamic vcpkg triplet (e.g. arm64-windows), the server runs
# freshly-built HOST TOOLS during its own build -- comp_err generates
# mysqld_error.h, the charset/uca generators run, etc. Those tools link vcpkg
# DLLs (zlib -> zlib1.dll, OpenSSL -> libcrypto-3-*.dll, ...) that live in the
# vcpkg per-triplet bin dir, which is neither beside the tool exes nor on PATH.
# The loader then aborts the tool with STATUS_DLL_NOT_FOUND (exit -1073741515 /
# 0xC0000135) before it runs, failing the nested build (e.g. GenError). Prepend
# that bin dir to PATH for the build via "cmake -E env --modify" so the tools
# launch. RelWithDebInfo/Release tools link the release DLLs (<prefix>/bin); a
# Debug server links the debug ones (<prefix>/debug/bin) -- pick by build type
# since same-named DLLs (OpenSSL) exist in both. Only meaningful on Windows;
# WIN32 is not defined this early (before project()), so key off CMAKE_HOST_WIN32.
SET(_mdb_env_launcher "")
IF(CMAKE_HOST_WIN32 AND VCPKG_INSTALLED_PREFIX)
  IF(_mdb_build_type STREQUAL "Debug")
    SET(_vcpkg_dll_dir "${VCPKG_INSTALLED_PREFIX}/debug/bin")
  ELSE()
    SET(_vcpkg_dll_dir "${VCPKG_INSTALLED_PREFIX}/bin")
  ENDIF()
  IF(CMAKE_VERSION VERSION_LESS "3.25")
    # --modify PATH=path_list_prepend was added in 3.25; older CMake can't do
    # this cleanly (assembling PATH by hand trips over ';' list-splitting).
    MESSAGE(WARNING "CMake < 3.25: cannot prepend the vcpkg DLL dir to PATH for "
      "the nested server build. If a host tool fails with exit -1073741515 "
      "(missing DLL), prepend '${_vcpkg_dll_dir}' to PATH and reconfigure.")
  ELSE()
    SET(_mdb_env_launcher "${CMAKE_COMMAND}" -E env
        "--modify" "PATH=path_list_prepend:${_vcpkg_dll_dir}" "--")
  ENDIF()
ENDIF()

SET(_mdb_core_targets mariadbclient mysys mysys_ssl caching_sha2_password GenError)
FOREACH(_tgt ${_mdb_core_targets})
  MESSAGE(STATUS "Building MariaDB core lib: ${_tgt}")
  EXECUTE_PROCESS(
    COMMAND ${_mdb_env_launcher} "${CMAKE_COMMAND}" --build "${_mdb_bld}"
            --config "${_mdb_build_type}"
            --target "${_tgt}" ${_mdb_build_parallel}
    RESULT_VARIABLE _rc)
  IF(NOT _rc EQUAL 0)
    MESSAGE(FATAL_ERROR "Failed to build MariaDB dependency '${_tgt}'")
  ENDIF()
ENDFOREACH()

# Client auth plugin -- best-effort: some connector layouts fold it into the
# connector build, so a missing target here is a warning, not a hard failure.
# MESSAGE(STATUS "Building MariaDB client auth plugin: caching_sha2_password")
# EXECUTE_PROCESS(
#   COMMAND "${CMAKE_COMMAND}" --build "${_mdb_bld}"
#           --config "${_mdb_build_type}"
#           --target caching_sha2_password ${_mdb_build_parallel}
#   RESULT_VARIABLE _rc)
# IF(NOT _rc EQUAL 0)
#   MESSAGE(WARNING "Could not build 'caching_sha2_password' as a standalone "
#     "target; if authentication plugins are missing later, build it manually "
#     "in ${_mdb_bld}.")
# ENDIF()

##############################################################################
# 4. Export the resolved dirs for the rest of the build.
##############################################################################
SET(MARIADB_SOURCE_DIR "${_mdb_src}" CACHE PATH "Auto-fetched MariaDB server source" FORCE)
SET(MARIADB_BUILD_DIR  "${_mdb_bld}" CACHE PATH "Auto-built MariaDB server build"  FORCE)
# Remembered so that the top-level CMakeLists.txt re-runs this bootstrap when
# the ref moves (for example when the pin in UPSTREAM.md changes).
SET(MDBLS_BOOTSTRAPPED_REF "${_mdb_ref}" CACHE INTERNAL "Ref the client library was bootstrapped from")

MESSAGE(STATUS "MariaDB bootstrap complete:")
MESSAGE(STATUS "  MARIADB_SOURCE_DIR = ${MARIADB_SOURCE_DIR}")
MESSAGE(STATUS "  MARIADB_BUILD_DIR  = ${MARIADB_BUILD_DIR}")
