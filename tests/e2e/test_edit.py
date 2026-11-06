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
        # And the ops it does by this one.
        ops = re.search(r"(?m)^ +edit ops: (.*)$", answer.stdout).group(1).split()
        self.assertIn("rename_table", ops)
        self.assertIn("add_table", ops)
        self.assertIn("drop_routine", ops)
        self.assertIn("set_primary_key", ops)

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
            ([{"op": "add_index", "table": "customer", "index": {"kind": "primary", "columns": ["name"]}}],
             "name-taken"),
            ([{"op": "drop_index", "table": "customer", "kind": "primary"}], "referenced"),
            ([{"op": "drop_table", "table": "customer"}], "referenced"),
            ([{"op": "add_table", "table": {"name": "x", "columns": [{"name": "a", "type": "INTT"}]}}],
             "bad-request"),
            ([{"op": "rename_tables", "table": "customer"}], "bad-request"),
            ([{"op": "rename_table", "table": "customer"}], "bad-request"),
        ]
        for edits, kind in cases:
            code, result = self.p.edit(edits, "shop")
            self.assertEqual(code, 1, edits)
            self.assertEqual(result["error"]["kind"], kind, edits)
            self.assertEqual(result["error"]["edit"], 0, edits)

    def test_routine_rename(self):
        # Section 150 as MSM writes it: dropped, then created.
        routines = self.p.dev / "sections" / "150_routines.sql"
        routines.write_text(routines.read_text().replace(
            "CREATE FUNCTION order_count", "DROP FUNCTION IF EXISTS order_count%%\nCREATE FUNCTION order_count"))
        code, result = self.p.edit([{"op": "rename_routine", "routine": "order_count",
                                     "new_name": "orders_of"}], "shop")
        self.assertEqual(code, 0, result)
        self.assertEqual(result["migration"][0], {"edit": 0, "sql": "DROP FUNCTION `order_count`"})
        self.assertTrue(result["migration"][1]["sql"].startswith("CREATE FUNCTION orders_of(c INT)"))
        self.assertEqual([n["kind"] for n in result["notes"]], ["routine-recreated"])
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        self.assertIn("DROP FUNCTION IF EXISTS orders_of%%\nCREATE FUNCTION orders_of(", routines.read_text())

    def checks(self):
        check = self.p.run("check", "--source-dir", str(self.p.dev), "-")
        self.assertEqual(check.returncode, 0, check.stdout.decode())

    def test_add_table_and_columns(self):
        code, result = self.p.edit([
            {"op": "add_table", "table": {
                "name": "product",
                "columns": [{"name": "id", "type": "INT", "nullable": False},
                            {"name": "price", "type": "DECIMAL(10,2)"}],
                "indexes": [{"kind": "primary", "columns": ["id"]}]}},
            {"op": "add_column", "table": "orders",
             "column": {"name": "product_id", "type": "INT", "nullable": False}},
            # customer's last column comes from an ALTER TABLE.
            {"op": "add_column", "table": "customer", "column": {"name": "phone", "type": "VARCHAR(20)"}},
            {"op": "modify_column", "table": "customer", "column": "name",
             "new_column": {"name": "name", "type": "VARCHAR(80)", "nullable": False}},
        ], "shop")
        self.assertEqual(code, 0, result)
        # The new table goes after the last one, which 180_extra.sql defines.
        self.assertEqual([f["file"] for f in result["files"]],
                         ["sections/140_tables.sql", "sections/180_extra.sql"])
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        tables = (self.p.dev / "sections" / "140_tables.sql").read_text()
        self.assertIn("ALTER TABLE customer ADD COLUMN email VARCHAR(80);\n"
                      "ALTER TABLE customer ADD COLUMN phone VARCHAR(20) AFTER email;\n", tables)
        self.assertIn("  customer_id INT,\n  product_id INT NOT NULL,\n", tables)
        extra = (self.p.dev / "sections" / "180_extra.sql").read_text()
        self.assertIn("CREATE TABLE note (id INT);\nCREATE TABLE product (\n  id INT NOT NULL,\n", extra)
        # The footer the slice leaves out is still last.
        self.assertTrue(tables.endswith("-- footer\n"))
        self.assertEqual([m["sql"] for m in result["migration"]][1:], [
            "ALTER TABLE `orders` ADD COLUMN `product_id` INT NOT NULL",
            "ALTER TABLE `customer` ADD COLUMN `phone` VARCHAR(20)",
            "ALTER TABLE `customer` MODIFY COLUMN `name` VARCHAR(80) NOT NULL"])
        self.checks()

    def test_views_and_routines(self):
        code, result = self.p.edit([
            {"op": "add_view", "view": {"name": "view1", "definition": "SELECT 1"}},
            {"op": "add_routine", "routine": {"name": "routine1", "kind": "procedure",
                                              "body": "BEGIN\n  SELECT 1;\nEND"}},
            {"op": "drop_view", "view": "customer_orders"},
        ], "shop")
        self.assertEqual(code, 0, result)
        self.assertEqual([n["kind"] for n in result["notes"]], ["routine-created"])
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        # After the last routine, restore_roles, in the script's DELIMITER
        # block and with its delimiter.
        self.assertIn("END%%\nCREATE PROCEDURE routine1()\nBEGIN\n  SELECT 1;\nEND%%\nDELIMITER ;", self.p.script)
        routines = (self.p.dev / "sections" / "150_routines.sql").read_text()
        self.assertNotIn("customer_orders", routines)
        self.assertIn("CREATE VIEW view1 AS SELECT 1;", routines)
        self.checks()

    def files(self):
        return {f: f.read_bytes() for f in sorted(self.p.dev.rglob("*.sql"))}

    def test_a_relationship_and_its_undo(self):
        """The diagram viewer's identifying 1:n relationship, then its undo."""
        files, script = self.files(), self.p.script
        code, result = self.p.edit([
            {"op": "add_column", "table": "orders",
             "column": {"name": "customer_id1", "type": "INT", "nullable": False}},
            {"op": "set_primary_key", "table": "orders", "columns": ["id", "customer_id1"]},
            {"op": "add_foreign_key", "table": "orders", "foreign_key": {
                "name": "fk_orders_customer1", "columns": ["customer_id1"],
                "references": {"table": "customer", "columns": ["id"]}}},
            {"op": "add_index", "table": "customer",
             "index": {"name": "ix_name", "kind": "index", "columns": ["name"]}},
        ], "shop")
        self.assertEqual(code, 0, result)
        self.assertEqual([m["sql"] for m in result["migration"]][1:3], [
            "ALTER TABLE `orders` DROP PRIMARY KEY, ADD PRIMARY KEY (`id`, `customer_id1`)",
            "ALTER TABLE `orders` ADD CONSTRAINT `fk_orders_customer1` FOREIGN KEY (`customer_id1`) "
            "REFERENCES `customer` (`id`)"])
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        tables = (self.p.dev / "sections" / "140_tables.sql").read_text()
        self.assertIn("  customer_id1 INT NOT NULL,\n  PRIMARY KEY (id, customer_id1),\n", tables)
        self.checks()
        code, result = self.p.edit([
            {"op": "drop_index", "table": "customer", "name": "ix_name"},
            {"op": "drop_foreign_key", "table": "orders", "name": "fk_orders_customer1"},
            {"op": "set_primary_key", "table": "orders", "columns": ["id"]},
            {"op": "drop_column", "table": "orders", "column": "customer_id1"},
        ], "shop")
        self.assertEqual(code, 0, result)
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        # The column-level PRIMARY KEY came back as an element: the outline
        # is the one before, the text not quite.
        orders = [t for s in result["outline"]["schemas"] for t in s["tables"] if t["name"] == "orders"][0]
        self.assertEqual([ix["columns"] for ix in orders["indexes"] if ix["kind"] == "primary"], [["id"]])
        self.assertEqual(len(orders["foreign_keys"]), 1)
        self.assertEqual(self.p.script, script)
        self.assertEqual(self.files()[self.p.dev / "sections" / "150_routines.sql"],
                         files[self.p.dev / "sections" / "150_routines.sql"])

    def test_drop_table(self):
        code, result = self.p.edit([{"op": "drop_table", "table": "orders"}], "shop")
        self.assertEqual(code, 0, result)
        self.assertEqual(result["migration"], [{"edit": 0, "sql": "DROP TABLE `orders`"}])
        # The function and the view still select from it.
        self.assertEqual([n["kind"] for n in result["notes"]], ["still-referenced", "still-referenced"])
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        self.assertNotIn("orders", table_names(result["outline"]))


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

    def test_rename_a_routine_and_its_drop(self):
        code, result = self.p.edit([{"op": "rename_routine", "routine": "dump_audit_log",
                                     "kind": "procedure", "new_name": "dump_the_audit_log"}],
                                   "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.assertEqual(result["migration"][0]["sql"], "DROP PROCEDURE `dump_audit_log`")
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        routines = (self.p.dev / "sections" / "150-20_procedures_functions.sql").read_text()
        self.assertIn("DROP PROCEDURE IF EXISTS `dump_the_audit_log`%%", routines)
        self.assertNotIn("dump_audit_log`", routines)

    def test_keys_and_their_undo(self):
        before = self.p.outline()
        files = {f: f.read_bytes() for f in self.p.dev.rglob("*.sql")}
        code, result = self.p.edit([
            {"op": "add_column", "table": "url_host",
             "column": {"name": "alias_id", "type": "BINARY(16)"}},
            {"op": "add_index", "table": "url_host",
             "index": {"name": "fk_url_host_alias_UNIQUE", "kind": "unique", "columns": ["alias_id"]}},
            {"op": "add_foreign_key", "table": "url_host", "foreign_key": {
                "name": "fk_url_host_alias", "columns": ["alias_id"],
                "references": {"table": "url_host_alias", "columns": ["id"]}}},
        ], "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        check = self.p.run("check", "--source-dir", str(self.p.dev), "-")
        self.assertEqual(check.returncode, 0, check.stdout.decode())
        code, result = self.p.edit([
            {"op": "drop_foreign_key", "table": "url_host", "name": "fk_url_host_alias"},
            {"op": "drop_index", "table": "url_host", "name": "fk_url_host_alias_UNIQUE"},
            {"op": "drop_column", "table": "url_host", "column": "alias_id"},
        ], "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.p.apply(result)
        self.assertEqual(self.p.outline(), before)
        self.assertEqual({f: f.read_bytes() for f in self.p.dev.rglob("*.sql")}, files)

    def test_add_and_drop(self):
        before = self.p.outline()
        code, result = self.p.edit([
            {"op": "add_table", "table": {
                "name": "audit_note",
                "columns": [{"name": "id", "type": "BINARY(16)", "nullable": False},
                            {"name": "url_host_id", "type": "BINARY(16)", "nullable": False},
                            {"name": "note", "type": "TEXT"}],
                "indexes": [{"kind": "primary", "columns": ["id"]}],
                "foreign_keys": [{"name": "fk_audit_note_url_host", "columns": ["url_host_id"],
                                  "references": {"table": "url_host", "columns": ["id"]}}]}},
            {"op": "add_column", "table": "url_host", "column": {"name": "remark", "type": "TEXT"}},
            {"op": "add_view", "view": {"name": "view1", "definition": "SELECT 1"}},
            {"op": "add_routine", "routine": {"name": "routine1", "kind": "procedure",
                                              "body": "BEGIN\nEND"}},
        ], "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.p.apply(result)
        after = self.p.outline()
        self.assertEqual(after, result["outline"])
        self.assertEqual(len(after["notes"]), len(before["notes"]))
        self.assertIn("audit_note", table_names(after))
        check = self.p.run("check", "--source-dir", str(self.p.dev), "-")
        self.assertEqual(check.returncode, 0, check.stdout.decode())
        # And all of it undone.
        code, result = self.p.edit([
            {"op": "drop_routine", "routine": "routine1"},
            {"op": "drop_view", "view": "view1"},
            {"op": "drop_column", "table": "url_host", "column": "remark"},
            {"op": "drop_table", "table": "audit_note"},
        ], "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.p.apply(result)
        self.assertEqual(self.p.outline(), before)


if __name__ == "__main__":
    unittest.main()
