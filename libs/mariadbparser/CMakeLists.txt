# libmariadbparser -- written in phase P0 of docs/plans/core-implementation.md.
#
# Intended shape: bison over generated/yy_stub.yy (with -Dparse.lac=full and the
# XML report that gen_tables.py reads), then one static and one shared library from
# src/*.c, src/*.cc and generated/*.cc, and the mdbparse CLI. No Python and no server
# source tree at build time; generated/ is committed and maintained by hand.

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/yy_stub.yy")
  message(FATAL_ERROR
    "libs/mariadbparser/generated/ is empty: the one-shot extraction has not been run "
    "yet. See libs/mariadbparser/UPSTREAM.md and phase P0 of "
    "docs/plans/core-implementation.md.")
endif()

message(FATAL_ERROR "libs/mariadbparser: build not written yet (phase P0)")
