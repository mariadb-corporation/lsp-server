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

# Reads the MariaDB server commit libs/mariadbparser is pinned to from
# libs/mariadbparser/UPSTREAM.md, the single place it is recorded, and sets
# MDBLS_MARIADB_PIN.  Also sets MDBLS_MARIADB_REF, the ref to build the client
# library from: MARIADB_GIT_TAG when given, else the pin.  The row read is the pins-table line that starts with
# "| MariaDB server"; its first 40-hex-digit word is the commit.
#
# Usable before project(): it only reads a file.

set(_mdbls_upstream "${CMAKE_CURRENT_LIST_DIR}/../libs/mariadbparser/UPSTREAM.md")
get_filename_component(_mdbls_upstream "${_mdbls_upstream}" ABSOLUTE)

# Editing UPSTREAM.md re-runs the configure step, so a moved pin takes effect.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_mdbls_upstream}")

file(STRINGS "${_mdbls_upstream}" _mdbls_pin_rows REGEX "^\\| MariaDB server")
list(LENGTH _mdbls_pin_rows _mdbls_pin_row_count)
if(NOT _mdbls_pin_row_count EQUAL 1)
  message(FATAL_ERROR "${_mdbls_upstream}: expected exactly one pins-table row "
    "starting with '| MariaDB server', found ${_mdbls_pin_row_count}")
endif()
string(REGEX MATCHALL "[0-9a-f]+" _mdbls_pin_words "${_mdbls_pin_rows}")
set(MDBLS_MARIADB_PIN "")
foreach(_word ${_mdbls_pin_words})
  string(LENGTH "${_word}" _len)
  if(_len EQUAL 40)
    set(MDBLS_MARIADB_PIN "${_word}")
    break()
  endif()
endforeach()
if(NOT MDBLS_MARIADB_PIN)
  message(FATAL_ERROR "${_mdbls_upstream}: the '| MariaDB server' row has no "
    "40-character commit hash")
endif()

set(MARIADB_GIT_TAG "" CACHE STRING
  "Git ref of the MariaDB server to fetch; empty = the pin in libs/mariadbparser/UPSTREAM.md")
if(MARIADB_GIT_TAG)
  set(MDBLS_MARIADB_REF "${MARIADB_GIT_TAG}")
  if(NOT MARIADB_GIT_TAG STREQUAL MDBLS_MARIADB_PIN)
    message(WARNING "MARIADB_GIT_TAG=${MARIADB_GIT_TAG} differs from the pin "
      "${MDBLS_MARIADB_PIN} in libs/mariadbparser/UPSTREAM.md: the client "
      "library and the grammar come from different server commits.")
  endif()
else()
  set(MDBLS_MARIADB_REF "${MDBLS_MARIADB_PIN}")
endif()
