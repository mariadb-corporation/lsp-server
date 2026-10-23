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

# libFuzzer targets, with -DWITH_FUZZERS=ON (cmake/compiler.cmake), after
# MariaDB Shell's cmake/fuzzer.cmake: each is a test that fuzzes for
# MDBLS_FUZZ_SECONDS from the SQL fixtures and the inputs earlier runs found
# (<build>/fuzz/corpus/<target>, kept between runs).  A crash is written to
# <build>/fuzz/artifacts/<target>/ and fails the test; run the target on that
# file to reproduce it.

set(MDBLS_FUZZ_SECONDS 10 CACHE STRING "How long each fuzz test runs, in seconds")

function(mdbls_add_fuzzer target)
  add_executable(${target} ${target}.cc)
  mdbls_target_warnings(${target})
  mdbls_sanitizer_defaults(${target})
  target_link_libraries(${target} PRIVATE mariadblanguageserver)
  target_compile_options(${target} PRIVATE -fsanitize=fuzzer)
  target_link_options(${target} PRIVATE -fsanitize=fuzzer)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

  set(corpus "${CMAKE_CURRENT_BINARY_DIR}/corpus/${target}")
  set(artifacts "${CMAKE_CURRENT_BINARY_DIR}/artifacts/${target}")
  file(MAKE_DIRECTORY "${corpus}" "${artifacts}")
  add_test(NAME ${target}
           COMMAND ${target} -max_total_time=${MDBLS_FUZZ_SECONDS} -timeout=10 -max_len=8192
                   -artifact_prefix=${artifacts}/ "${corpus}" "${PROJECT_SOURCE_DIR}/tests/fixtures/sql")
  set_tests_properties(${target} PROPERTIES LABELS fuzz)
endfunction()

mdbls_add_fuzzer(splitter_fuzz)
mdbls_add_fuzzer(edit_fuzz)
mdbls_add_fuzzer(format_fuzz)
