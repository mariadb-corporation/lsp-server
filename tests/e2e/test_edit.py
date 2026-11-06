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

"""End-to-end tests of `mariadb-lsp edit` (docs/edit-format.md): the edits are
applied here as msm applies them, and `mariadb-lsp outline` of the edited
files must equal the outline the command returned."""

import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from test_lsp import BINARY, FIXTURES  # noqa: E402

MRS_PROJECT = pathlib.Path(os.environ.get(
    "MDBLS_MRS_PROJECT",
    pathlib.Path.home() / "git" / "mariadb-shell-diagrams" / "mysqlshdk" / "libs" / "mrs"
    / "db_schema" / "mariadb_rest_service.msm.project"))


def line_starts(text):
    """The offset of each line, as LSP counts lines ("\\n", "\\r\\n", "\\r")."""
    starts = [0]
    for m in re.finditer(r"\r\n|\r|\n", text):
        starts.append(m.end())
    return starts


def offset(text, starts, position):
    """An LSP position (UTF-16 character) as an index into `text`."""
    i = starts[position["line"]] if position["line"] < len(starts) else len(text)
    units = position["character"]
    while units > 0 and i < len(text) and text[i] not in "\r\n":
        units -= 2 if ord(text[i]) > 0xFFFF else 1
        i += 1
    return i


def apply_edits(text, edits):
    """`text` with LSP-range edits applied, back to front."""
    starts = line_starts(text)
    spans = [(offset(text, starts, e["range"]["start"]), offset(text, starts, e["range"]["end"]),
              e["new_text"]) for e in edits]
    for (b, e, _), (b2, _, _) in zip(spans, spans[1:]):
        assert e <= b2, "edits overlap or are not sorted"
    for b, e, t in reversed(spans):
        text = text[:b] + t + text[e:]
    return text


class Edit_project:
    """A copy of an MSM project's development folder and its script."""

    def __init__(self, test, development, script_name, blank=None):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="mdbls-edit-"))
        test.addCleanup(shutil.rmtree, self.dir, True)
        self.dev = self.dir / "development"
        shutil.copytree(development, self.dev)
        self.script_path = self.dev / script_name
        # Read and written as bytes: the files keep their own line breaks.
        self.script = self.script_path.read_bytes().decode()
        if blank:
            # As msm blanks Section 160: the lines stay, positions match.
            self.script = re.sub(blank, lambda m: "", self.script, flags=re.M)

    def run(self, *args, stdin=None, edits=None):
        if edits is not None:
            request = self.dir / "request.json"
            request.write_text(json.dumps(edits))
            args = ("--edits", str(request)) + args
        return subprocess.run([BINARY, *args], input=(self.script if stdin is None else stdin).encode(),
                              capture_output=True, timeout=120)

    def edit(self, edits, schema):
        answer = self.run("edit", "--source-dir", str(self.dev), "-",
                          edits={"format": 1, "schema": schema, "edits": edits})
        return answer.returncode, json.loads(answer.stdout)

    def apply(self, result):
        """Writes the edits to the files; the script's go to self.script."""
        for f in result["files"]:
            if f["file"] == "-":
                self.script = apply_edits(self.script, f["edits"])
            else:
                path = self.dev / f["file"]
                path.write_bytes(apply_edits(path.read_bytes().decode(), f["edits"]).encode())

    def outline(self):
        answer = self.run("outline", "--source-dir", str(self.dev), "-")
        return json.loads(answer.stdout)


def table_names(outline):
    return [t["name"] for s in outline["schemas"] for t in s["tables"]]


class Edit_fixture(unittest.TestCase):
    def setUp(self):
        self.p = Edit_project(self, FIXTURES / "msm" / "development", "shop_next.sql")

    def test_help_names_the_command(self):
        # msm detects support by this line.
        answer = subprocess.run([BINARY, "--help"], capture_output=True, text=True)
        self.assertRegex(answer.stdout, r"(?m)^  edit ")

    def test_rename_across_includes(self):
        code, result = self.p.edit([
            {"op": "rename_table", "table": "customer", "new_name": "client"},
            {"op": "rename_column", "table": "client", "column": "name", "new_name": "full_name"},
        ], "shop")
        self.assertEqual(code, 0, result)
        files = {f["file"]: f["edits"] for f in result["files"]}
        self.assertEqual(sorted(files), ["sections/140_tables.sql", "sections/150_routines.sql",
                                         "sections/170_roles.sql"])
        self.assertEqual(result["migration"], [
            {"edit": 0, "sql": "RENAME TABLE `customer` TO `client`"},
            {"edit": 1, "sql": "ALTER TABLE `client` RENAME COLUMN `name` TO `full_name`"}])
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        self.assertIn("client", table_names(result["outline"]))
        self.assertNotIn("customer", table_names(result["outline"]))
        tables = (self.p.dev / "sections" / "140_tables.sql").read_text()
        self.assertIn("CREATE TABLE client (", tables)
        self.assertIn("  full_name VARCHAR(40)", tables)
        self.assertIn("REFERENCES client (id)", tables)

    def test_all_or_nothing(self):
        code, result = self.p.edit([
            {"op": "rename_table", "table": "customer", "new_name": "client"},
            {"op": "rename_column", "table": "customer", "column": "name", "new_name": "n"},
        ], "shop")
        self.assertEqual(code, 1)
        self.assertEqual(result, {"error": {"edit": 1, "kind": "unknown-object",
                                            "message": "there is no table `customer`"}})

    def test_errors(self):
        cases = [
            ([{"op": "rename_table", "table": "customer", "new_name": "orders"}], "name-taken"),
            ([{"op": "rename_table", "table": "customer", "new_name": "customer_orders"}], "name-taken"),
            ([{"op": "rename_column", "table": "customer", "column": "id", "new_name": "NAME"}],
             "name-taken"),
            ([{"op": "drop_table", "table": "customer"}], "unsupported"),
            ([{"op": "rename_tables", "table": "customer"}], "bad-request"),
            ([{"op": "rename_table", "table": "customer"}], "bad-request"),
        ]
        for edits, kind in cases:
            code, result = self.p.edit(edits, "shop")
            self.assertEqual(code, 1, edits)
            self.assertEqual(result["error"]["kind"], kind, edits)
            self.assertEqual(result["error"]["edit"], 0, edits)

    def test_routine_rename(self):
        code, result = self.p.edit([{"op": "rename_routine", "routine": "order_count",
                                     "new_name": "orders_of"}], "shop")
        self.assertEqual(code, 0, result)
        self.assertEqual(result["migration"][0], {"edit": 0, "sql": "DROP FUNCTION `order_count`"})
        self.assertTrue(result["migration"][1]["sql"].startswith("CREATE FUNCTION orders_of(c INT)"))
        self.assertEqual([n["kind"] for n in result["notes"]], ["routine-recreated"])
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])


@unittest.skipUnless((MRS_PROJECT / "development").is_dir(),
                     "no MariaDB REST Service MSM project (MDBLS_MRS_PROJECT)")
class Edit_mrs_project(unittest.TestCase):
    def setUp(self):
        self.p = Edit_project(self, MRS_PROJECT / "development", "mariadb_rest_service_next.sql",
                              blank=r"^SOURCE './sections/160_schema_diagrams\.sql'.*$")

    def test_rename_a_referenced_table(self):
        before = self.p.outline()
        fks = [fk for s in before["schemas"] for t in s["tables"] for fk in t["foreign_keys"]
               if fk["references"]["table"] == "url_host"]
        self.assertGreater(len(fks), 0)
        code, result = self.p.edit([
            {"op": "rename_table", "table": "url_host", "new_name": "url_hosts"},
            {"op": "rename_column", "table": "url_hosts", "column": "name", "new_name": "host_name"},
        ], "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.assertGreater(len(result["files"]), 4)
        self.p.apply(result)
        after = self.p.outline()
        self.assertEqual(after, result["outline"])
        self.assertEqual(len(after["notes"]), len(before["notes"]))
        self.assertEqual(
            len([fk for s in after["schemas"] for t in s["tables"] for fk in t["foreign_keys"]
                 if fk["references"]["table"] == "url_hosts"]), len(fks))
        self.assertNotIn("url_host", table_names(after))
        # The edited project still checks.
        check = self.p.run("check", "--source-dir", str(self.p.dev), "-")
        self.assertEqual(check.returncode, 0, check.stdout.decode())


if __name__ == "__main__":
    unittest.main()
