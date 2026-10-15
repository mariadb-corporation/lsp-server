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

# One test binary, run_unit_tests, as in MariaDB Shell.  GoogleTest and
# RapidJSON come from vcpkg (-DWITH_VCPKG_TRIPLET=<triplet>) or from the system.

find_package(GTest CONFIG REQUIRED)
find_package(RapidJSON CONFIG REQUIRED)

file(GLOB _unit_test_sources CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*/*_t.cc")

if(NOT WITH_MARIADB_CLIENT)
  list(FILTER _unit_test_sources EXCLUDE REGEX "/client/")
endif()

add_executable(run_unit_tests ${_unit_test_sources})
mdbls_target_warnings(run_unit_tests)
target_link_libraries(run_unit_tests PRIVATE
  mariadbparser
  GTest::gtest
  GTest::gtest_main
  rapidjson
  $<$<BOOL:${WITH_MARIADB_CLIENT}>:mariadb::client>)
target_compile_definitions(run_unit_tests PRIVATE
  MDBP_LEX_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/libs/mariadbparser/tests/lex-fixtures")
set_target_properties(run_unit_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

include(GoogleTest)
gtest_discover_tests(run_unit_tests DISCOVERY_TIMEOUT 60)
