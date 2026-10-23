# libmariadbparser: bison over the committed, hand-maintained grammar in
# generated/, then a static and a shared library and the mdbparse CLI.
# No Python and no MariaDB source tree are needed at build time; see
# UPSTREAM.md for where generated/ comes from.

# 3.7.4 is the oldest bison tried: Rocky Linux 9's and winflexbison3's.  Its
# tables are 3.8.2's but for yydefgoto[0] (the start symbol, never used).
if(NOT BISON_FOUND)
  message(FATAL_ERROR "libmariadbparser needs bison 3.7.4 or newer "
    "(Homebrew bison on macOS, winflexbison3 on Windows).")
endif()
if(BISON_VERSION VERSION_LESS "3.7.4")
  message(FATAL_ERROR "libmariadbparser needs bison 3.7.4 or newer, found ${BISON_VERSION}")
endif()

set(MDBP_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")
set(MDBP_BISON_DIR "${CMAKE_CURRENT_BINARY_DIR}/bison")
file(MAKE_DIRECTORY "${MDBP_BISON_DIR}")

# The library version comes from include/mdbparse.h, the one place it is kept.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/mdbparse.h" _mdbp_version_lines
     REGEX "^#define MDBP_VERSION_(MAJOR|MINOR|PATCH) [0-9]+")
foreach(_part MAJOR MINOR PATCH)
  string(REGEX MATCH "MDBP_VERSION_${_part} ([0-9]+)" _m "${_mdbp_version_lines}")
  if(NOT _m)
    message(FATAL_ERROR "MDBP_VERSION_${_part} not found in include/mdbparse.h")
  endif()
  set(MDBP_VERSION_${_part} "${CMAKE_MATCH_1}")
endforeach()
set(MDBP_VERSION "${MDBP_VERSION_MAJOR}.${MDBP_VERSION_MINOR}.${MDBP_VERSION_PATCH}")

# -k emits yytname[], from which the runtime builds its token tables;
# -Dparse.lac=full makes the expected-token set exact.  Neither changes the
# automaton.  parser.c is #included by src/parser_glue.c, never compiled alone.
add_custom_command(
  OUTPUT "${MDBP_BISON_DIR}/parser.c" "${MDBP_BISON_DIR}/parser.h"
  COMMAND "${BISON_EXECUTABLE}" -k -d -Dparse.lac=full
          -o "${MDBP_BISON_DIR}/parser.c"
          "${MDBP_GENERATED_DIR}/yy_stub.yy"
  DEPENDS "${MDBP_GENERATED_DIR}/yy_stub.yy"
  COMMENT "bison: libmariadbparser grammar"
  VERBATIM)
add_custom_target(mariadbparser_bison
  DEPENDS "${MDBP_BISON_DIR}/parser.c" "${MDBP_BISON_DIR}/parser.h")

set(_mdbp_sources
  src/parser_glue.c
  src/mdbp_runtime.c
  src/mdbp_api.c
  src/mdbp_lex.cc)
set_source_files_properties(src/parser_glue.c PROPERTIES
  OBJECT_DEPENDS "${MDBP_BISON_DIR}/parser.c")

add_library(mariadbparser_objects OBJECT ${_mdbp_sources})
add_dependencies(mariadbparser_objects mariadbparser_bison)
target_include_directories(mariadbparser_objects
  PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
          "${MDBP_GENERATED_DIR}"
          "${MDBP_BISON_DIR}")
set_target_properties(mariadbparser_objects PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  # The extracted lexer is MariaDB's code, compiled as written; only the
  # library's own files are held to -Werror.
  target_compile_options(mariadbparser_objects PRIVATE -Wall -Wextra)
  set_source_files_properties(src/mdbp_runtime.c src/mdbp_api.c
    PROPERTIES COMPILE_OPTIONS "-Werror")
  # parser_glue.c #includes bison's parser.c, whose yynerrs is set and never
  # read (the shell silences the same warning for its own bison output).
  set_source_files_properties(src/parser_glue.c
    PROPERTIES COMPILE_OPTIONS "-Werror;-Wno-unused-but-set-variable")
endif()
find_package(Threads REQUIRED)
target_link_libraries(mariadbparser_objects PUBLIC Threads::Threads)

add_library(mariadbparser STATIC $<TARGET_OBJECTS:mariadbparser_objects>)
target_include_directories(mariadbparser PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(mariadbparser PUBLIC Threads::Threads)
# On Windows the DLL's import library is mariadbparser.lib, the name the
# static library would get.
if(WIN32)
  set_target_properties(mariadbparser PROPERTIES OUTPUT_NAME mariadbparser_static)
endif()

add_library(mariadbparser_shared SHARED $<TARGET_OBJECTS:mariadbparser_objects>)
target_include_directories(mariadbparser_shared PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(mariadbparser_shared PUBLIC Threads::Threads)
set_target_properties(mariadbparser_shared PROPERTIES
  OUTPUT_NAME mariadbparser
  VERSION "${MDBP_VERSION}"
  SOVERSION "${MDBP_VERSION_MAJOR}"
  LINKER_LANGUAGE CXX
  # The C API has no export annotations: on Windows export everything, the
  # map file's job elsewhere.
  WINDOWS_EXPORT_ALL_SYMBOLS ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(mariadbparser_shared PRIVATE
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/libmariadbparser.map")
endif()

# mdbparse: a command-line front end for inspecting verdicts, tokens and
# derivations of single statements, built on the public API only.
find_package(RapidJSON CONFIG QUIET)
if(RapidJSON_FOUND)
  add_executable(mdbparse src/mdbparse.cc)
  target_link_libraries(mdbparse PRIVATE mariadbparser rapidjson)
  mdbls_target_warnings(mdbparse)
  mdbls_sanitizer_defaults(mdbparse)
else()
  message(STATUS "RapidJSON not found: mdbparse is not built")
endif()
