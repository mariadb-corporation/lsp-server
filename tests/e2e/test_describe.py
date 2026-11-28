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

"""End-to-end tests of `mariadb-lsp describe` (docs/describe-format.md):
what each statement does, as msm_plugin reads it to route statements."""

import json
import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from test_lsp import BINARY  # noqa: E402


def run_cli(*args, stdin=None):
    return subprocess.run([BINARY, *args], input=stdin, capture_output=True, text=True, timeout=60)


class Describe_test(unittest.TestCase):
    def describe(self, sql):
        r = run_cli("describe", "-", stdin=sql)
        self.assertEqual(r.returncode, 0, r.stderr)
        out = json.loads(r.stdout)
        self.assertEqual(out["format"], 1)
        return out["statements"]

    def test_help_names_the_command(self):
        # msm detects the command by this line.
        r = run_cli("--help")
        self.assertTrue(any(line.startswith("  describe ") for line in (r.stdout + r.stderr).splitlines()))

    def test_drops_msm_routes(self):
        sql = ("DROP VIEW v;\n"
               "DROP TABLE IF EXISTS t, u;\n"
               "DROP REST VIEW /n FROM SERVICE /app SCHEMA /main;\n")
        st = self.describe(sql)
        self.assertEqual(len(st), 3)
        self.assertEqual((st[0]["verb"], st[0]["kind"]), ("drop", "view"))
        self.assertEqual(st[0]["objects"], [{"schema": None, "name": "v"}])
        self.assertIsNone(st[0]["rest"])
        self.assertTrue(st[1]["if_exists"])
        self.assertEqual([o["name"] for o in st[1]["objects"]], ["t", "u"])
        self.assertEqual(st[2]["kind"], "rest_view")
        self.assertEqual(st[2]["rest"]["service"], {"path": "/app", "developers": [], "written": True})
        self.assertEqual(st[2]["rest"]["schema"], {"path": "/main", "written": True})
        self.assertEqual(st[2]["rest"]["path"], "/n")
        # The positions are split's: msm maps each entry back to its statement.
        for s in st:
            self.assertEqual(sql[s["begin"]:s["body_end"]].split()[0], "DROP")

    def test_a_statement_that_does_not_parse_has_an_entry(self):
        st = self.describe("SELEC 1;\nDROP VIEW v;\n")
        self.assertEqual(len(st), 2)
        self.assertEqual(st[0]["status"], "syntax-error")
        self.assertIn("SELEC", st[0]["message"])
        self.assertIsNone(st[0]["verb"])
        self.assertIsNone(st[0]["kind"])
        self.assertEqual(st[1]["status"], "ok")

    def test_rest_or_replace_and_if_not_exists(self):
        # What msm's rest_editing.py read with regexes.
        st = self.describe("CREATE OR REPLACE REST SERVICE /a;\n"
                           "CREATE REST SCHEMA IF NOT EXISTS /s ON SERVICE /a FROM db;\n")
        self.assertTrue(st[0]["or_replace"])
        self.assertFalse(st[0]["if_not_exists"])
        self.assertFalse(st[1]["or_replace"])
        self.assertTrue(st[1]["if_not_exists"])
        self.assertEqual(st[1]["rest"]["database_schema"], "db")


if __name__ == "__main__":
    unittest.main()
