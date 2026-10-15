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

# MariaDB Shell's SQL statement splitter (mysqlshdk::utils::Sql_splitter),
# copied from the shell and maintained by hand; see UPSTREAM.md.

add_library(sqlsplitter STATIC src/utils_mysql_parsing.cc)
target_include_directories(sqlsplitter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
mdbls_target_warnings(sqlsplitter)
