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
# bootstrap_vcpkg.cmake
#
# Configure-time convenience: when the caller only asks for a triplet via
# -DWITH_VCPKG_TRIPLET=<triplet>, fetch and bootstrap vcpkg automatically and
# derive the two variables the rest of the build (and the MariaDB server
# bootstrap) already understand:
#
#   CMAKE_TOOLCHAIN_FILE = <VCPKG_ROOT>/scripts/buildsystems/vcpkg.cmake
#   VCPKG_TARGET_TRIPLET = <triplet>
#
# so the user no longer has to clone vcpkg, run its bootstrap script, and pass
# the long -DCMAKE_TOOLCHAIN_FILE / -DVCPKG_TARGET_TRIPLET pair by hand.
#
# This MUST run before the first project() call (CMake reads
# CMAKE_TOOLCHAIN_FILE there) and before bootstrap_mariadb.cmake (which
# propagates the toolchain + triplet to the server tree).
#
# Explicitly passing -DCMAKE_TOOLCHAIN_FILE / -DVCPKG_TARGET_TRIPLET keeps
# working and takes precedence: WITH_VCPKG_TRIPLET is a shortcut, not a
# requirement.
#
##############################################################################

# Nothing to do unless the shortcut was requested.
IF(NOT WITH_VCPKG_TRIPLET)
  RETURN()
ENDIF()

# The triplet is what the caller ultimately wants selected. Honour an explicit
# VCPKG_TARGET_TRIPLET if one was also passed, otherwise adopt the shortcut's.
IF(NOT VCPKG_TARGET_TRIPLET)
  SET(VCPKG_TARGET_TRIPLET "${WITH_VCPKG_TRIPLET}"
    CACHE STRING "vcpkg target triplet" FORCE)
ENDIF()

##############################################################################
# Where the dependency closure lives. Derived purely from the build dir and the
# triplet, so it is settled here -- ABOVE the early return below, which also
# fires on a re-configure once CMAKE_TOOLCHAIN_FILE has been cached.
#
# Two related but DIFFERENT paths, kept in separate variables on purpose.
#
# VCPKG_INSTALLED_DIR is owned by the vcpkg toolchain, which reads it as the
# install ROOT holding one subdirectory per triplet: it appends the triplet
# itself and passes the result as --x-install-root. Setting it to the
# per-triplet prefix therefore makes the toolchain resolve
# <root>/<triplet>/<triplet> and run a SECOND full manifest install there --
# a duplicate closure, with the shell resolving one copy while the server
# bootstrap points at the other. So it must stay the root. We set it anyway (to
# the value the toolchain would pick by default) so the explicit install in
# step 3 and the toolchain-driven one at project() cannot disagree.
SET(VCPKG_INSTALLED_DIR "${CMAKE_BINARY_DIR}/vcpkg_installed"
  CACHE PATH "vcpkg install root (holds one subdir per triplet)" FORCE)

# VCPKG_INSTALLED_PREFIX is ours: the per-triplet prefix, with headers under
# include/, libs under lib/ and tools under tools/. Consumers that run at
# configure time before project() -- the Python and MariaDB server bootstraps
# -- point their dependency search here to reuse the closure just installed
# instead of the system. Do NOT fold this back into VCPKG_INSTALLED_DIR.
SET(VCPKG_INSTALLED_PREFIX "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
  CACHE PATH "Per-triplet vcpkg install prefix for this build" FORCE)
##############################################################################

# If the caller already supplied a vcpkg toolchain file, respect it and only
# make sure the triplet is set -- do not clone a second copy.
IF(CMAKE_TOOLCHAIN_FILE AND CMAKE_TOOLCHAIN_FILE MATCHES "[Vv]cpkg")
  MESSAGE(STATUS "WITH_VCPKG_TRIPLET: reusing the vcpkg toolchain already given "
                 "(${CMAKE_TOOLCHAIN_FILE}); triplet '${VCPKG_TARGET_TRIPLET}'")
  RETURN()
ENDIF()

# Where to fetch vcpkg from, and which ref.
#
# The ref is not spelled out here. It is read from the "builtin-baseline" field
# of the repo's vcpkg.json, so the commit lives in exactly one place: bumping
# the baseline moves the checkout with it, and the two cannot drift apart.
#
# Checking out that same commit matters. Tracking "master" is NOT equivalent:
# the baseline pins the *port versions* (which release of openssl, curl, ...
# gets built), but the port *infrastructure* -- the scripts/cmake/*.cmake
# helpers every portfile runs through -- always comes from whatever the tip of
# the default branch happens to be at clone time. Those two drift apart, and an
# upstream change to the infrastructure lands in a build that never asked for
# it. That is not hypothetical: in July 2026 upstream moved SPDX manifest
# generation into scripts/cmake/z_vcpkg_spdx.cmake using
# string(JSON ... STRING_ENCODE), which requires CMake >= 4.3, and every port
# build broke on runners carrying an older CMake.
SET(VCPKG_GIT_REPOSITORY "https://github.com/microsoft/vcpkg.git"
  CACHE STRING "Git URL of vcpkg to fetch when WITH_VCPKG_TRIPLET is set")

# Skipped when the caller already pinned a ref (-DVCPKG_GIT_TAG=..., or a cache
# entry from an earlier configure): the SET(... CACHE ...) below would not
# overwrite it anyway, so there is nothing to read.
SET(_vcpkg_ref_origin "")
IF(NOT VCPKG_GIT_TAG)
  # Same manifest root the vcpkg install step passes via --x-manifest-root.
  SET(_vcpkg_manifest "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg.json")
  IF(NOT EXISTS "${_vcpkg_manifest}")
    MESSAGE(FATAL_ERROR
      "WITH_VCPKG_TRIPLET is set but ${_vcpkg_manifest} does not exist. "
      "Pass -DVCPKG_GIT_TAG=<commit> to pin the vcpkg checkout explicitly.")
  ENDIF()
  FILE(READ "${_vcpkg_manifest}" _vcpkg_manifest_text)

  SET(_vcpkg_baseline "")
  IF(NOT CMAKE_VERSION VERSION_LESS "3.19")
    STRING(JSON _vcpkg_baseline ERROR_VARIABLE _vcpkg_json_error
           GET "${_vcpkg_manifest_text}" "builtin-baseline")
    IF(_vcpkg_json_error)
      SET(_vcpkg_baseline "")
    ENDIF()
  ENDIF()
  IF(NOT _vcpkg_baseline)
    # string(JSON) landed in CMake 3.19, and this file is included before any
    # cmake_minimum_required() runs -- so the interpreter here can predate it
    # (a self-hosted runner with a distro CMake, say). Scan for the field.
    STRING(REGEX MATCH
      "\"builtin-baseline\"[ \t\r\n]*:[ \t\r\n]*\"([0-9a-fA-F]+)\""
      _vcpkg_baseline_match "${_vcpkg_manifest_text}")
    SET(_vcpkg_baseline "${CMAKE_MATCH_1}")
  ENDIF()

  IF(NOT _vcpkg_baseline MATCHES "^[0-9a-fA-F]+$")
    MESSAGE(FATAL_ERROR
      "Could not read a \"builtin-baseline\" commit from ${_vcpkg_manifest}. "
      "Add one, or pass -DVCPKG_GIT_TAG=<commit> to pin the checkout directly.")
  ENDIF()
  SET(_vcpkg_ref_origin "  (vcpkg.json builtin-baseline)")
ENDIF()

SET(VCPKG_GIT_TAG "${_vcpkg_baseline}"
  CACHE STRING "Git ref (branch, tag, or commit) of vcpkg to fetch")

# Where vcpkg is cloned. Honour an existing VCPKG_ROOT (cache or environment)
# so a shared checkout is reused; otherwise clone next to the shell source,
# outside the build dir, so a clean rebuild does not re-clone/re-bootstrap.
IF(VCPKG_ROOT)
  SET(_vcpkg_root "${VCPKG_ROOT}")
ELSEIF(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
  SET(_vcpkg_root "$ENV{VCPKG_ROOT}")
ELSE()
  SET(_vcpkg_root "${CMAKE_CURRENT_SOURCE_DIR}/../vcpkg")
ENDIF()
GET_FILENAME_COMPONENT(_vcpkg_root "${_vcpkg_root}" ABSOLUTE)

# The bootstrap script and the executable it produces differ by host OS. WIN32
# is not set this early (before project()), so key off CMAKE_HOST_WIN32.
IF(CMAKE_HOST_WIN32)
  SET(_vcpkg_bootstrap "${_vcpkg_root}/bootstrap-vcpkg.bat")
  SET(_vcpkg_exe "${_vcpkg_root}/vcpkg.exe")
ELSE()
  SET(_vcpkg_bootstrap "${_vcpkg_root}/bootstrap-vcpkg.sh")
  SET(_vcpkg_exe "${_vcpkg_root}/vcpkg")
ENDIF()

MESSAGE(STATUS "==========================================================")
MESSAGE(STATUS "WITH_VCPKG_TRIPLET='${WITH_VCPKG_TRIPLET}' -- bootstrapping vcpkg.")
MESSAGE(STATUS "  repo    : ${VCPKG_GIT_REPOSITORY}")
MESSAGE(STATUS "  ref     : ${VCPKG_GIT_TAG}${_vcpkg_ref_origin}")
MESSAGE(STATUS "  location: ${_vcpkg_root}")
MESSAGE(STATUS "  (override with -DVCPKG_ROOT / -DVCPKG_GIT_TAG; this runs once)")
MESSAGE(STATUS "==========================================================")

##############################################################################
# 1. Clone (or reuse) the vcpkg checkout.
##############################################################################
IF(NOT EXISTS "${_vcpkg_root}/.git")
  FIND_PACKAGE(Git REQUIRED)
  MESSAGE(STATUS "Cloning vcpkg...")
  GET_FILENAME_COMPONENT(_vcpkg_parent "${_vcpkg_root}" DIRECTORY)
  FILE(MAKE_DIRECTORY "${_vcpkg_parent}")
  # --depth 1 + --single-branch (implied) grab only the tip; --progress keeps
  # the meter visible in non-TTY build panels.
  EXECUTE_PROCESS(
    COMMAND "${GIT_EXECUTABLE}" clone --progress
            --branch "${VCPKG_GIT_TAG}"
            "${VCPKG_GIT_REPOSITORY}" "${_vcpkg_root}"
    RESULT_VARIABLE _rc)
  IF(NOT _rc EQUAL 0)
    # --branch (and shallow) fails on a raw commit SHA; fall back to a full
    # clone + checkout so an arbitrary SHA is reachable.
    MESSAGE(STATUS "Branch/tag clone failed; cloning default and checking out ref...")
    FILE(REMOVE_RECURSE "${_vcpkg_root}")
    EXECUTE_PROCESS(
      COMMAND "${GIT_EXECUTABLE}" clone --progress
              "${VCPKG_GIT_REPOSITORY}" "${_vcpkg_root}"
      RESULT_VARIABLE _rc)
    IF(NOT _rc EQUAL 0)
      MESSAGE(FATAL_ERROR "Failed to clone ${VCPKG_GIT_REPOSITORY}")
    ENDIF()
    EXECUTE_PROCESS(
      COMMAND "${GIT_EXECUTABLE}" -C "${_vcpkg_root}" checkout "${VCPKG_GIT_TAG}"
      RESULT_VARIABLE _rc)
    IF(NOT _rc EQUAL 0)
      MESSAGE(FATAL_ERROR "Failed to check out vcpkg ref '${VCPKG_GIT_TAG}'")
    ENDIF()
  ENDIF()
ELSE()
  MESSAGE(STATUS "Reusing existing vcpkg checkout at ${_vcpkg_root}")
ENDIF()

##############################################################################
# 2. Bootstrap vcpkg (builds the vcpkg executable) unless already present.
##############################################################################
IF(NOT EXISTS "${_vcpkg_exe}")
  IF(NOT EXISTS "${_vcpkg_bootstrap}")
    MESSAGE(FATAL_ERROR "vcpkg bootstrap script not found: ${_vcpkg_bootstrap}")
  ENDIF()
  MESSAGE(STATUS "Running vcpkg bootstrap (${_vcpkg_bootstrap})...")
  IF(CMAKE_HOST_WIN32)
    EXECUTE_PROCESS(
      COMMAND cmd /c "${_vcpkg_bootstrap}" -disableMetrics
      WORKING_DIRECTORY "${_vcpkg_root}"
      RESULT_VARIABLE _rc)
  ELSE()
    EXECUTE_PROCESS(
      COMMAND "${_vcpkg_bootstrap}" -disableMetrics
      WORKING_DIRECTORY "${_vcpkg_root}"
      RESULT_VARIABLE _rc)
  ENDIF()
  IF(NOT _rc EQUAL 0)
    MESSAGE(FATAL_ERROR "vcpkg bootstrap failed (see output above)")
  ENDIF()
ELSE()
  MESSAGE(STATUS "Reusing existing vcpkg executable at ${_vcpkg_exe}")
ENDIF()

##############################################################################
# 2.5 Configuration(s) built for each port: whatever the triplet says.
#
#     We deliberately do NOT restrict this. vcpkg's only knob here is
#     VCPKG_BUILD_TYPE (release|debug) set inside a triplet, and an earlier
#     revision generated an overlay triplet that derived it from
#     CMAKE_BUILD_TYPE to halve dependency build time. That does not work:
#
#       - VCPKG_BUILD_TYPE=debug is not a usable standalone mode. vcpkg runs
#         only the debug install, so everything lands under <prefix>/debug and
#         no top-level include/, tools/ or share/ is ever created. Portfiles
#         assume the release prefix exists, so they fail outright -- openssl
#         (renames bin/c_rehash), zlib (patches include/zconf.h) and zstd all
#         break -- and nothing relocates debug/include to include/, so even a
#         port that built would put headers where find_package cannot see them.
#
#       - Exempting the broken ports one by one is a losing race against
#         upstream portfile changes.
#
#     Release-only would work (release IS the canonical prefix), but on Windows
#     it links a /MDd shell against /MD dependencies -- separate CRT heaps,
#     which MSVC does not support. So we simply let vcpkg do its default thing
#     and build both configurations. Slower to bootstrap, correct everywhere.
#
#     Clean up the overlay a previous configure of this build dir may have
#     generated, so existing build trees recover without a fresh start.
##############################################################################
SET(_stale_overlay_dir "${CMAKE_BINARY_DIR}/vcpkg-overlay-triplets")
IF(VCPKG_OVERLAY_TRIPLETS STREQUAL "${_stale_overlay_dir}")
  UNSET(VCPKG_OVERLAY_TRIPLETS CACHE)
ENDIF()
IF(EXISTS "${_stale_overlay_dir}")
  FILE(REMOVE_RECURSE "${_stale_overlay_dir}")
ENDIF()

MESSAGE(STATUS "vcpkg: building both debug and release for every port "
               "(triplet default).")

##############################################################################
# 3. Install the manifest dependencies NOW, before any consumer that runs at
#    configure time (notably the MariaDB server bootstrap) needs them.
#
#    In manifest mode vcpkg installs the vcpkg.json closure as a side effect of
#    the first project() call reading the toolchain file -- which happens AFTER
#    the MariaDB bootstrap is included. Doing the install explicitly here closes
#    that gap: the deps (OpenSSL, zlib, ...) are on disk before the server is
#    configured. We install into VCPKG_INSTALLED_DIR, the same root the
#    toolchain resolves, so the later toolchain-driven install at project()
#    finds everything up to date and is a near no-op. Passing anything else
#    here would populate one tree and leave the toolchain to build another.
##############################################################################
#    The optional features are part of "anything else": VCPKG_MANIFEST_FEATURES
#    is what the toolchain will ask for, so this install has to ask for exactly
#    the same set. Left out here, this call would install the featureless
#    closure and the toolchain would then re-resolve it at project().
SET(_vcpkg_feature_args "")
FOREACH(_vcpkg_feature ${VCPKG_MANIFEST_FEATURES})
  LIST(APPEND _vcpkg_feature_args "--x-feature=${_vcpkg_feature}")
ENDFOREACH()

MESSAGE(STATUS "Installing vcpkg manifest dependencies (triplet "
               "'${VCPKG_TARGET_TRIPLET}', features: "
               "'${VCPKG_MANIFEST_FEATURES}')...")
EXECUTE_PROCESS(
  COMMAND "${_vcpkg_exe}" install
          --triplet "${VCPKG_TARGET_TRIPLET}"
          --x-manifest-root "${CMAKE_CURRENT_SOURCE_DIR}"
          --x-install-root "${VCPKG_INSTALLED_DIR}"
          ${_vcpkg_feature_args}
  RESULT_VARIABLE _rc)
IF(NOT _rc EQUAL 0)
  MESSAGE(FATAL_ERROR "vcpkg manifest install failed (see output above)")
ENDIF()

##############################################################################
# 4. Export VCPKG_ROOT + the derived toolchain/triplet for the rest of the
#    build. Cached (FORCE) so they persist across reconfigures and so the
#    first project() call below sees CMAKE_TOOLCHAIN_FILE.
##############################################################################
SET(VCPKG_ROOT "${_vcpkg_root}"
  CACHE PATH "Root of the vcpkg checkout used for this build" FORCE)

SET(CMAKE_TOOLCHAIN_FILE "${_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
  CACHE FILEPATH "Vcpkg toolchain file (derived from WITH_VCPKG_TRIPLET)" FORCE)

IF(NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
  MESSAGE(FATAL_ERROR "vcpkg toolchain file missing: ${CMAKE_TOOLCHAIN_FILE}")
ENDIF()

MESSAGE(STATUS "vcpkg bootstrap complete:")
MESSAGE(STATUS "  VCPKG_ROOT             = ${VCPKG_ROOT}")
MESSAGE(STATUS "  CMAKE_TOOLCHAIN_FILE   = ${CMAKE_TOOLCHAIN_FILE}")
MESSAGE(STATUS "  VCPKG_TARGET_TRIPLET   = ${VCPKG_TARGET_TRIPLET}")
MESSAGE(STATUS "  VCPKG_INSTALLED_DIR    = ${VCPKG_INSTALLED_DIR}")
MESSAGE(STATUS "  VCPKG_INSTALLED_PREFIX = ${VCPKG_INSTALLED_PREFIX}")
