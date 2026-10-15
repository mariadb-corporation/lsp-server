#!/usr/bin/env python3
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

"""Writes a mysqldump-style SQL file of about the given size, for benchmarks.

The file has what real dumps have: the versioned-comment prologue and
epilogue that save and restore sql_mode, DROP/CREATE TABLE per table,
extended INSERTs whose strings contain escaped quotes, semicolons and
newlines, and stored routines in DELIMITER ;; blocks.  The output is
deterministic for a given size and seed.

usage: gen_fixture_dump.py SIZE_MB OUTPUT [--rows-per-insert N] [--seed S]
"""

import argparse
import random
import sys

PROLOGUE = """-- MariaDB dump 10.19  Distrib 13.1.0-MariaDB, for Linux (x86_64)
--
-- Host: localhost    Database: bench
-- ------------------------------------------------------
-- Server version\t13.1.0-MariaDB

/*!40101 SET @OLD_CHARACTER_SET_CLIENT=@@CHARACTER_SET_CLIENT */;
/*!40101 SET @OLD_CHARACTER_SET_RESULTS=@@CHARACTER_SET_RESULTS */;
/*!40101 SET @OLD_COLLATION_CONNECTION=@@COLLATION_CONNECTION */;
/*!40101 SET NAMES utf8mb4 */;
/*!40103 SET @OLD_TIME_ZONE=@@TIME_ZONE */;
/*!40103 SET TIME_ZONE='+00:00' */;
/*!40014 SET @OLD_UNIQUE_CHECKS=@@UNIQUE_CHECKS, UNIQUE_CHECKS=0 */;
/*!40014 SET @OLD_FOREIGN_KEY_CHECKS=@@FOREIGN_KEY_CHECKS, FOREIGN_KEY_CHECKS=0 */;
/*!40101 SET @OLD_SQL_MODE=@@SQL_MODE, SQL_MODE='NO_AUTO_VALUE_ON_ZERO' */;
/*!40111 SET @OLD_SQL_NOTES=@@SQL_NOTES, SQL_NOTES=0 */;

"""

EPILOGUE = """/*!40103 SET TIME_ZONE=@OLD_TIME_ZONE */;

/*!40101 SET SQL_MODE=@OLD_SQL_MODE */;
/*!40014 SET FOREIGN_KEY_CHECKS=@OLD_FOREIGN_KEY_CHECKS */;
/*!40014 SET UNIQUE_CHECKS=@OLD_UNIQUE_CHECKS */;
/*!40101 SET CHARACTER_SET_CLIENT=@OLD_CHARACTER_SET_CLIENT */;
/*!40101 SET CHARACTER_SET_RESULTS=@OLD_CHARACTER_SET_RESULTS */;
/*!40101 SET COLLATION_CONNECTION=@OLD_COLLATION_CONNECTION */;
/*!40111 SET SQL_NOTES=@OLD_SQL_NOTES */;

-- Dump completed
"""

WORDS = ("alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu "
         "café naïve straße 日本 \U0001F600").split()


def table_ddl(n):
    return (
        f"--\n-- Table structure for table `t{n}`\n--\n\n"
        f"DROP TABLE IF EXISTS `t{n}`;\n"
        "/*!40101 SET @saved_cs_client     = @@character_set_client */;\n"
        "/*!40101 SET character_set_client = utf8mb4 */;\n"
        f"CREATE TABLE `t{n}` (\n"
        "  `id` int(11) NOT NULL AUTO_INCREMENT,\n"
        "  `name` varchar(100) NOT NULL DEFAULT '',\n"
        "  `note` text DEFAULT NULL,\n"
        "  `amount` decimal(10,2) DEFAULT NULL,\n"
        "  `created` datetime NOT NULL DEFAULT current_timestamp(),\n"
        "  PRIMARY KEY (`id`),\n"
        f"  KEY `idx_name_{n}` (`name`)\n"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_uca1400_ai_ci;\n"
        "/*!40101 SET character_set_client = @saved_cs_client */;\n\n"
        f"--\n-- Dumping data for table `t{n}`\n--\n\n"
        f"LOCK TABLES `t{n}` WRITE;\n"
        f"/*!40000 ALTER TABLE `t{n}` DISABLE KEYS */;\n")


def table_end(n):
    return (f"/*!40000 ALTER TABLE `t{n}` ENABLE KEYS */;\n"
            "UNLOCK TABLES;\n\n")


def routine(n):
    return (
        "DELIMITER ;;\n"
        f"CREATE DEFINER=`root`@`localhost` PROCEDURE `p{n}`(IN x INT)\n"
        "BEGIN\n"
        "  DECLARE done INT DEFAULT 0;\n"
        f"  SELECT COUNT(*) INTO done FROM `t{n}` WHERE id > x;\n"
        "  IF done > 0 THEN\n"
        "    UPDATE `t{n}` SET note = 'updated; again' WHERE id = x;\n"
        "  END IF;\n"
        "END ;;\n"
        "DELIMITER ;\n\n")


def row(rng, i):
    name = " ".join(rng.choice(WORDS) for _ in range(rng.randint(1, 4)))
    note = rng.choice([
        "NULL",
        "'it\\'s here; really'",
        "'line one\\nline two'",
        "'semi;colon and \\\"quote\\\"'",
        "'" + "x" * rng.randint(10, 200) + "'",
    ])
    amount = f"{rng.randint(0, 99999)}.{rng.randint(0, 99):02d}"
    return f"({i},'{name}',{note},{amount},'2026-10-15 12:00:00')"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("size_mb", type=float)
    ap.add_argument("output")
    ap.add_argument("--rows-per-insert", type=int, default=500)
    ap.add_argument("--rows-per-table", type=int, default=20000)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    target = int(args.size_mb * 1024 * 1024)
    rng = random.Random(args.seed)
    written = 0
    with open(args.output, "w", encoding="utf-8", newline="\n") as out:
        def emit(s):
            nonlocal written
            out.write(s)
            written += len(s.encode("utf-8"))

        emit(PROLOGUE)
        table = 0
        while written < target:
            emit(table_ddl(table))
            rows = 0
            while rows < args.rows_per_table and written < target:
                n = min(args.rows_per_insert, args.rows_per_table - rows)
                values = ",".join(row(rng, rows + k + 1) for k in range(n))
                emit(f"INSERT INTO `t{table}` VALUES {values};\n")
                rows += n
            emit(table_end(table))
            if table % 10 == 0:
                emit(routine(table))
            table += 1
        emit(EPILOGUE)
    print(f"{args.output}: {written} bytes, {table} tables", file=sys.stderr)


if __name__ == "__main__":
    main()
