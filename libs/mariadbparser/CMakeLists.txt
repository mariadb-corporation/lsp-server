# libs/mariadbparser: the parser is generated and compiled at build time by
# tools/extract.sh's full pipeline against the MariaDB source tree named by
# -DMARIADB_SOURCE_DIR=<path>; CMake wraps the result as imported targets.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "mariadbparser: only Linux builds are supported yet; Windows and macOS are planned (extract.sh is to be ported to Python)")
endif()

# Indented lines are printed as they are, not reflowed.
set(_mdbp_worktree_hint
    "  git -C <mariadb-server-clone> worktree add --detach <mariadb-source> bab03b0fc44e7aeac869b20b0055c6a8f2083eb6\n"
    "  cmake --preset default -DMARIADB_SOURCE_DIR=<mariadb-source>\n")
if(MARIADB_SOURCE_DIR STREQUAL "" OR NOT EXISTS "${MARIADB_SOURCE_DIR}/sql/sql_yacc.yy")
  string(CONCAT _mdbp_msg
    "mariadbparser: MARIADB_SOURCE_DIR must name a MariaDB source tree "
    "(one with sql/sql_yacc.yy); got '${MARIADB_SOURCE_DIR}'. Create it and "
    "configure with it:\n" ${_mdbp_worktree_hint}
    "Or configure with -DMDBLS_WITH_MARIADBPARSER=OFF.")
  message(FATAL_ERROR "${_mdbp_msg}")
endif()
get_filename_component(_mdbp_source_dir "${MARIADB_SOURCE_DIR}" ABSOLUTE)

find_program(MDBP_UV uv)
if(NOT MDBP_UV)
  message(FATAL_ERROR "mariadbparser: uv is required (https://docs.astral.sh/uv/); it provides the tools' Python 3.12")
endif()
find_program(MDBP_BASH bash)
if(NOT MDBP_BASH)
  message(FATAL_ERROR "mariadbparser: bash is required to run tools/extract.sh")
endif()

set(MDBP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(MDBP_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/out")
set(MDBP_CORPUS_DIR "${CMAKE_CURRENT_BINARY_DIR}/corpus")

# The library version, from the public header's MDBP_VERSION_* macros (the
# one place extract.sh and tools/common.py read it from too).
set(_mdbp_header "${MDBP_SOURCE_DIR}/include/mdbparse.h")
foreach(_part MAJOR MINOR PATCH)
  file(STRINGS "${_mdbp_header}" _line
       REGEX "^#define[ \t]+MDBP_VERSION_${_part}[ \t]+[0-9]+" LIMIT_COUNT 1)
  if(NOT _line MATCHES "MDBP_VERSION_${_part}[ \t]+([0-9]+)")
    message(FATAL_ERROR "mariadbparser: MDBP_VERSION_${_part} not found in ${_mdbp_header}")
  endif()
  set(_mdbp_version_${_part} "${CMAKE_MATCH_1}")
endforeach()
set(MDBP_VERSION "${_mdbp_version_MAJOR}.${_mdbp_version_MINOR}.${_mdbp_version_PATCH}")
set(MDBP_SONAME "libmariadbparser.so.${_mdbp_version_MAJOR}")
set(MDBP_REALNAME "libmariadbparser.so.${MDBP_VERSION}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_mdbp_header}")

# The environment every custom command and ctest test gets.
set(MDBP_ENV
    "MDBP_BUILD_DIR=${MDBP_BUILD_DIR}"
    "MDBP_CORPUS_DIR=${MDBP_CORPUS_DIR}"
    "MARIADB_SOURCE_DIR=${_mdbp_source_dir}"
    "MDBP_GRAMMAR_CORPUS_DIR=${MDBP_GRAMMAR_CORPUS_DIR}"
    "MDBP_BISON_TS_DIR=${MDBP_BISON_TS_DIR}"
    "MDBP_BISON_TS_CORPUS_DIR=${MDBP_BISON_TS_CORPUS_DIR}"
    "MDBP_RECORD_HOST=${MDBLS_RECORD_HOST}")
set(MDBP_RUN ${CMAKE_COMMAND} -E env ${MDBP_ENV})
set(MDBP_PY ${MDBP_RUN} ${MDBP_UV} run --frozen -- python)

# --- mariadbparser-venv: uv sync --frozen ------------------------------------
set(_mdbp_venv_stamp "${CMAKE_CURRENT_BINARY_DIR}/venv.stamp")
add_custom_command(
  OUTPUT "${_mdbp_venv_stamp}"
  COMMAND ${MDBP_RUN} ${MDBP_UV} sync --frozen
  COMMAND ${CMAKE_COMMAND} -E touch "${_mdbp_venv_stamp}"
  DEPENDS "${MDBP_SOURCE_DIR}/pyproject.toml" "${MDBP_SOURCE_DIR}/uv.lock"
  WORKING_DIRECTORY "${MDBP_SOURCE_DIR}"
  COMMENT "mariadbparser: uv sync --frozen"
  VERBATIM)
add_custom_target(mariadbparser-venv DEPENDS "${_mdbp_venv_stamp}")

# --- mariadbparser-corpus: unpack corpus/archive/*.tar.zst --------------------
file(GLOB _mdbp_archive_files CONFIGURE_DEPENDS "${MDBP_SOURCE_DIR}/corpus/archive/*")
file(GLOB _mdbp_archives CONFIGURE_DEPENDS "${MDBP_SOURCE_DIR}/corpus/archive/*.tar.zst")
if(NOT _mdbp_archives)
  message(FATAL_ERROR "mariadbparser: no corpus archives in ${MDBP_SOURCE_DIR}/corpus/archive")
endif()
set(_mdbp_corpus_stamp "${CMAKE_CURRENT_BINARY_DIR}/corpus.stamp")
set(_mdbp_unpack)
foreach(_archive IN LISTS _mdbp_archives)
  list(APPEND _mdbp_unpack
       COMMAND ${CMAKE_COMMAND} -E chdir "${MDBP_CORPUS_DIR}"
               ${CMAKE_COMMAND} -E tar xf "${_archive}")
endforeach()
add_custom_command(
  OUTPUT "${_mdbp_corpus_stamp}"
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${MDBP_CORPUS_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${MDBP_CORPUS_DIR}"
  ${_mdbp_unpack}
  COMMAND ${CMAKE_COMMAND} -E touch "${_mdbp_corpus_stamp}"
  DEPENDS ${_mdbp_archive_files}
  COMMENT "mariadbparser: unpacking the corpus into ${MDBP_CORPUS_DIR}"
  VERBATIM)
add_custom_target(mariadbparser-corpus DEPENDS "${_mdbp_corpus_stamp}")

# --- mariadbparser-build: tools/extract.sh, then tools/compile_db.py ----------
set(_mdbp_outputs)
foreach(_name
    yy_mariadb.yy yy_oracle.yy yy_stub.yy stub-map.json grammar.xml
    grammar.output parser.c parser.h rule-map.json lexer_extracted.cc
    keywords.h lexstates.h state_maps_gen.cc state_maps.h lexslice.json
    mdbp_tables.h mdbp_lex.o parser_glue.o mdbp_runtime.o mdbp_api.o
    gxx.argv gcc-lib.argv gcc-shared.argv gcc.argv ar.argv
    libmariadbparser.a ${MDBP_REALNAME} ${MDBP_SONAME} libmariadbparser.so
    mdbparse mariadbparser.pc ldd-lib.txt ldd.txt
    extract.json build.json lexgen.json compile_commands.json)
  list(APPEND _mdbp_outputs "${MDBP_BUILD_DIR}/${_name}")
endforeach()
file(GLOB _mdbp_inputs CONFIGURE_DEPENDS
     "${MDBP_SOURCE_DIR}/src/*"
     "${MDBP_SOURCE_DIR}/include/*"
     "${MDBP_SOURCE_DIR}/tools/*.py"
     "${MDBP_SOURCE_DIR}/overrides/*")
list(APPEND _mdbp_inputs "${MDBP_SOURCE_DIR}/tools/extract.sh")
add_custom_command(
  OUTPUT ${_mdbp_outputs}
  COMMAND ${CMAKE_COMMAND} -E make_directory "${MDBP_BUILD_DIR}"
  COMMAND ${MDBP_RUN} ${MDBP_BASH} tools/extract.sh
          --source "${_mdbp_source_dir}"
          --out "${MDBP_BUILD_DIR}"
          --model "${MDBP_CORPUS_DIR}/grammar-model.json"
  COMMAND ${MDBP_PY} tools/compile_db.py --build-dir "${MDBP_BUILD_DIR}"
  DEPENDS ${_mdbp_inputs} "${_mdbp_corpus_stamp}" "${_mdbp_venv_stamp}"
  WORKING_DIRECTORY "${MDBP_SOURCE_DIR}"
  COMMENT "mariadbparser: generating and compiling the parser (tools/extract.sh)"
  USES_TERMINAL
  VERBATIM)
add_custom_target(mariadbparser-build ALL DEPENDS ${_mdbp_outputs})
add_dependencies(mariadbparser-build mariadbparser-corpus mariadbparser-venv)

# --- mariadbparser-corpus-archive: rebuild corpus/archive/ (explicit only) ----
add_custom_target(mariadbparser-corpus-archive
  COMMAND ${MDBP_PY} tools/corpus_archive.py
  WORKING_DIRECTORY "${MDBP_SOURCE_DIR}"
  COMMENT "mariadbparser: rebuilding out-of-date archives in corpus/archive/ from ${MDBP_CORPUS_DIR}"
  USES_TERMINAL
  VERBATIM)
add_dependencies(mariadbparser-corpus-archive mariadbparser-venv)

# --- imported targets -----------------------------------------------------------
add_library(mariadbparser::static STATIC IMPORTED GLOBAL)
set_target_properties(mariadbparser::static PROPERTIES
  IMPORTED_LOCATION "${MDBP_BUILD_DIR}/libmariadbparser.a"
  INTERFACE_INCLUDE_DIRECTORIES "${MDBP_SOURCE_DIR}/include")
add_dependencies(mariadbparser::static mariadbparser-build)

add_library(mariadbparser::shared SHARED IMPORTED GLOBAL)
set_target_properties(mariadbparser::shared PROPERTIES
  IMPORTED_LOCATION "${MDBP_BUILD_DIR}/${MDBP_REALNAME}"
  IMPORTED_SONAME "${MDBP_SONAME}"
  INTERFACE_INCLUDE_DIRECTORIES "${MDBP_SOURCE_DIR}/include")
add_dependencies(mariadbparser::shared mariadbparser-build)

# --- tests ------------------------------------------------------------------------
add_test(NAME mariadbparser.corpus-check
         COMMAND sh -c "rc=0; ${MDBP_UV} run --frozen -- python tools/corpus_check.py || rc=1; ${MDBP_UV} run --frozen -- python tools/corpus_archive.py --check || rc=1; exit $rc"
         WORKING_DIRECTORY "${MDBP_SOURCE_DIR}")
set_tests_properties(mariadbparser.corpus-check PROPERTIES ENVIRONMENT "${MDBP_ENV}")
