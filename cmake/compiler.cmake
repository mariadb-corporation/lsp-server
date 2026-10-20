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
  message(STATUS "Using Visual Studio version: ${MSVC_VERSION} (${MSVC_TOOLSET_VERSION})")
  # C++23 is /std:c++23preview.  CMake's own flag for CXX_STANDARD 23 is
  # /std:c++latest, which also enables the C++26 features MSVC has
  # implemented, so code using them would build without being C++23.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(/std:c++23preview MDBLS_HAVE_STD_CXX23PREVIEW)
  if(NOT MDBLS_HAVE_STD_CXX23PREVIEW)
    message(FATAL_ERROR "Compiler ${CMAKE_CXX_COMPILER} does not accept "
      "/std:c++23preview (Visual Studio 2022 17.13 or newer)")
  endif()
  set(CMAKE_CXX23_STANDARD_COMPILE_OPTION /std:c++23preview)
  set(CMAKE_CXX23_EXTENSION_COMPILE_OPTION /std:c++23preview)
  set(MDBLS_WARNING_FLAGS /W4 /WX)
  # getenv, strerror, sscanf and the like are standard C; their _s
  # replacements are not portable (the MariaDB server's build does the same).
  # NOMINMAX: <windows.h> would define min and max as macros, which breaks
  # std::min and std::max in every file that includes it.
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
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
