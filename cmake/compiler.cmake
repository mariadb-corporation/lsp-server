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

# Compiler settings, after MariaDB Shell's cmake/compiler.cmake: C++23 and the
# same warning set.  One difference: the shell appends its warnings to the
# global CMAKE_CXX_FLAGS, while here they are applied per target through
# mdbls_target_warnings(), because this tree also compiles MariaDB's own
# extracted lexer (libs/mariadbparser), which is built as written.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(MDBLS_WARNING_FLAGS
    -Werror -Wall -Wextra -Wpedantic -Wunused -Wshadow -Wdouble-promotion
    -Wformat-security -Wformat-y2k)
  if(CMAKE_COMPILER_IS_GNUCXX)
    list(APPEND MDBLS_WARNING_FLAGS
      -Wlogical-op -Wno-error=type-limits -Wno-error=free-nonheap-object
      -Wno-error=array-bounds)
  else()
    list(APPEND MDBLS_WARNING_FLAGS -Wno-nullability-extension)
  endif()
  if(ENABLE_GCOV)
    message(STATUS "Enabling code coverage using Gcov")
    add_compile_options(-fprofile-arcs -ftest-coverage)
    add_link_options(-fprofile-arcs -ftest-coverage)
  endif()
elseif(MSVC)
  if(MSVC_VERSION LESS 1925)
    message(FATAL_ERROR "Need at least Visual Studio 2019 Version 16.5")
  endif()
  message(STATUS "Using Visual Studio version: ${MSVC_VERSION} (${MSVC_TOOLSET_VERSION})")
  set(MDBLS_WARNING_FLAGS /W4 /WX)
  # getenv, strerror, sscanf and the like are standard C; their _s
  # replacements are not portable (the MariaDB server's build does the same).
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/Zc:preprocessor>
                      $<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus> /utf-8)
  if(ENABLE_GCOV)
    message(FATAL_ERROR "Code coverage not supported with MSVC")
  endif()
endif()

# Applies the project's warning set to one of this project's own C++ targets.
function(mdbls_target_warnings target)
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${MDBLS_WARNING_FLAGS}>)
endfunction()

# RapidJSON's SIMD scanning of strings and whitespace: a 256 MB didOpen is
# one JSON string.  Set for every target, so that all translation units see
# the same RapidJSON code.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
  add_compile_definitions(RAPIDJSON_NEON)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  add_compile_definitions(RAPIDJSON_SSE2)
endif()
