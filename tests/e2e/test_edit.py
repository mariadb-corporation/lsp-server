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

    def edit(self, edits, schema, **request):
        answer = self.run("edit", "--source-dir", str(self.dev), "-",
                          edits={"format": 1, "schema": schema, "edits": edits, **request})
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


def help_ops():
    """The ops on the `edit ops:` line of --help."""
    answer = subprocess.run([BINARY, "--help"], capture_output=True, text=True)
    return re.search(r"(?m)^ +edit ops: (.*)$", answer.stdout).group(1).split()


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
        ops = help_ops()
        self.assertIn("rename_table", ops)
        self.assertIn("add_table", ops)
        self.assertIn("drop_routine", ops)
        self.assertIn("set_primary_key", ops)
        for op in ("add_trigger", "drop_trigger", "rename_trigger", "add_event", "drop_event", "rename_event",
                   "add_sequence", "drop_sequence", "rename_sequence", "rename_schema"):
            self.assertIn(op, ops)

    def test_ops_as_json(self):
        # What msm reads: a binary that answers `edit --ops` also tags its
        # migration statements.
        answer = subprocess.run([BINARY, "edit", "--ops"], capture_output=True, text=True)
        self.assertEqual(answer.returncode, 0, answer.stderr)
        listed = json.loads(answer.stdout)
        self.assertEqual(listed["format"], 1)
        self.assertEqual(listed["ops"], help_ops())
        # The request's merge_alters is understood, and the add ops' at and form.
        self.assertIs(listed["merge_alters"], True)
        self.assertIs(listed["placement"], True)
        # In any order of the arguments.
        self.assertEqual(subprocess.run([BINARY, "--ops", "edit"], capture_output=True, text=True).stdout,
                         answer.stdout)

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
            {"edit": 0, "sql": "RENAME TABLE `customer` TO `client`",
             "action": "rename", "object": "table", "name": "customer", "new_name": "client"},
            {"edit": 1, "sql": "ALTER TABLE `client` RENAME COLUMN `name` TO `full_name`",
             "action": "rename", "object": "column", "name": "name", "table": "client", "new_name": "full_name"}])
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        self.assertIn("client", table_names(result["outline"]))
        self.assertNotIn("customer", table_names(result["outline"]))
        tables = (self.p.dev / "sections" / "140_tables.sql").read_text()
        self.assertIn("CREATE TABLE client (", tables)
        self.assertIn("  full_name VARCHAR(40)", tables)
        self.assertIn("REFERENCES client (id)", tables)

    def test_rename_schema(self):
        # The script writes the schema with MSM placeholders; 170_roles.sql
        # names a bare `shop`, which is another schema once msm substitutes
        # them.  Each rename takes only its own.
        code, result = self.p.edit([{"op": "rename_schema", "schema": "shop", "new_name": "shop_ro"}], "shop")
        self.assertEqual(code, 0, result)
        self.assertEqual([f["file"] for f in result["files"] if f["edits"]], ["sections/170_roles.sql"])
        self.p.apply(result)
        self.assertIn("GRANT SELECT ON shop_ro.customer TO shop_reader;",
                      (self.p.dev / "sections" / "170_roles.sql").read_text())
        old = "/*<msm:schema_prefix>*/shop/*<msm:schema_postfix>*/"
        new = "/*<msm:schema_prefix>*/store/*<msm:schema_postfix>*/"
        code, result = self.p.edit([{"op": "rename_schema", "schema": old, "new_name": new}], "shop")
        self.assertEqual(code, 0, result)
        self.assertEqual(result["migration"], [])
        self.assertEqual([f["file"] for f in result["files"] if f["edits"]], ["-"])
        self.p.apply(result)
        self.assertIn("USE " + new + ";", self.p.script)
        self.assertEqual(self.p.outline(), result["outline"])

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
            ([{"op": "rename_tables", "table": "customer"}], "unsupported"),
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
        self.assertEqual(result["migration"][0], {"edit": 0, "sql": "DROP FUNCTION `order_count`", "action": "drop",
                                                  "object": "function", "name": "order_count"})
        self.assertTrue(result["migration"][1]["sql"].startswith("CREATE FUNCTION orders_of(c INT)"))
        self.assertEqual((result["migration"][1]["action"], result["migration"][1]["object"],
                          result["migration"][1]["name"]), ("create", "function", "orders_of"))
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

    def test_merged_alters(self):
        edits = [
            {"op": "add_column", "table": "customer", "column": {"name": "phone", "type": "VARCHAR(20)"}},
            {"op": "add_column", "table": "orders", "column": {"name": "qty", "type": "INT"}},
            {"op": "add_index", "table": "customer", "index": {"name": "ix_phone", "kind": "index",
                                                               "columns": ["phone"]}},
        ]
        code, plain = self.p.edit(edits, "shop")
        self.assertEqual(code, 0, plain)
        self.assertEqual(len(plain["migration"]), 3)
        self.assertNotIn("clauses", plain["migration"][0])
        code, result = self.p.edit(edits, "shop", merge_alters=True)
        self.assertEqual(code, 0, result)
        # The same files; customer's statements merge past orders'.
        self.assertEqual(result["files"], plain["files"])
        self.assertEqual(result["migration"], [
            {"edit": 0, "edits": [0, 2], "action": "alter", "object": "table", "name": "customer", "table": "customer",
             "sql": "ALTER TABLE `customer` ADD COLUMN `phone` VARCHAR(20), ADD INDEX `ix_phone` (`phone`)",
             "spec": "ADD COLUMN `phone` VARCHAR(20), ADD INDEX `ix_phone` (`phone`)",
             "clauses": [{"edit": 0, "spec": "ADD COLUMN `phone` VARCHAR(20)", "action": "add", "object": "column",
                          "name": "phone", "table": "customer"},
                         {"edit": 2, "spec": "ADD INDEX `ix_phone` (`phone`)", "action": "add", "object": "index",
                          "name": "ix_phone", "table": "customer"}]},
            {"edit": 1, "edits": [1], "action": "add", "object": "column", "name": "qty", "table": "orders",
             "sql": "ALTER TABLE `orders` ADD COLUMN `qty` INT", "spec": "ADD COLUMN `qty` INT",
             "clauses": [{"edit": 1, "spec": "ADD COLUMN `qty` INT", "action": "add", "object": "column",
                          "name": "qty", "table": "orders"}]},
        ])

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

    def test_replace_a_view_and_a_routine_in_place(self):
        """msm's set_schema_statements: objects the project has are replaced
        where they stand, in their included file and DELIMITER block."""
        code, result = self.p.edit([
            {"op": "replace_view", "view": {"name": "customer_orders",
                                            "definition": "SELECT c.id, c.name FROM customer c"}},
            {"op": "replace_routine", "routine": {"name": "order_count", "kind": "function",
                                                  "parameters": [{"name": "c", "type": "INT"}],
                                                  "returns": "INT", "body": "BEGIN\n  RETURN 0;\nEND"}},
        ], "shop")
        self.assertEqual(code, 0, result)
        self.assertEqual([(m["action"], m["object"], m["name"]) for m in result["migration"]],
                         [("create", "view", "customer_orders"), ("create", "function", "order_count")])
        self.assertTrue(all(m["sql"].startswith("CREATE OR REPLACE ") for m in result["migration"]))
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        routines = (self.p.dev / "sections" / "150_routines.sql").read_text()
        self.assertIn("DELIMITER %%\nCREATE FUNCTION order_count(c INT) RETURNS INT\nBEGIN\n  RETURN 0;\nEND%%\n"
                      "DELIMITER ;\nCREATE VIEW customer_orders AS SELECT c.id, c.name FROM customer c;", routines)
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
        self.assertEqual(result["migration"], [{"edit": 0, "sql": "DROP TABLE `orders`",
                                                "action": "drop", "object": "table", "name": "orders"}])
        # The function and the view still select from it.
        self.assertEqual([n["kind"] for n in result["notes"]], ["still-referenced", "still-referenced"])
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        self.assertNotIn("orders", table_names(result["outline"]))


MSM_SCRIPT = """\
-- MSM Section 110: Database Schema Creation
CREATE SCHEMA IF NOT EXISTS `shop`;
USE `shop`;

-- #############################################################################
-- MSM Section 140: Non-idempotent Schema Objects
-- #############################################################################

CREATE TABLE `customer` (
  `id` INT NOT NULL,
  PRIMARY KEY (`id`)
);

CREATE TABLE `orders` (
  `id` INT NOT NULL,
  `customer_id` INT NOT NULL,
  PRIMARY KEY (`id`),
  CONSTRAINT `fk_orders_customer` FOREIGN KEY (`customer_id`) REFERENCES `customer` (`id`)
);

-- #############################################################################
-- MSM Section 150: Idempotent Schema Objects
-- #############################################################################

DELIMITER %%

DROP PROCEDURE IF EXISTS `count_customers`%%
CREATE PROCEDURE `count_customers`()
BEGIN
  SELECT COUNT(*) AS `customers` FROM `customer`;
END%%

CREATE OR REPLACE VIEW `customer_ids` AS
    SELECT `id` FROM `customer`%%

DELIMITER ;

-- #############################################################################
-- MSM Section 170: Authorization
-- #############################################################################

SELECT 1;
"""

ADD_VIEW = {"op": "add_view", "view": {"name": "order_ids", "definition": "SELECT `id` FROM `orders`"}}
ADD_ROUTINE = {"op": "add_routine", "routine": {
    "name": "count_orders", "kind": "procedure", "body": "BEGIN\n  SELECT COUNT(*) FROM `orders`;\nEND"}}
# What the schema diagrams' tool palette sends.
ADD_TRIGGER = {"op": "add_trigger", "trigger": {
    "name": "orders_bi", "timing": "BEFORE", "events": "INSERT", "table": "orders", "body": "BEGIN\nEND"}}
ADD_EVENT = {"op": "add_event", "event": {"name": "purge", "schedule": "EVERY 1 DAY",
                                          "body": "DELETE FROM `orders`"}}


class Edit_msm_sections(unittest.TestCase):
    """Section 150 of an MSM script runs on every deployment: what the edits
    write there must be safe to run again.  msm gives the place and form
    (`at`, `form`); without them a new object follows its neighbour."""

    def setUp(self):
        dev = pathlib.Path(tempfile.mkdtemp(prefix="mdbls-msm-"))
        self.addCleanup(shutil.rmtree, dev, True)
        (dev / "shop_next.sql").write_text(MSM_SCRIPT)
        self.p = Edit_project(self, dev, "shop_next.sql")

    def apply(self, edits):
        code, result = self.p.edit(edits, "shop")
        self.assertEqual(code, 0, result)
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        check = self.p.run("check", "--source-dir", str(self.p.dev), "-")
        self.assertEqual(check.returncode, 0, check.stdout.decode())
        return result

    def line_of(self, text):
        """The 0-based line of the script that starts with `text`."""
        return self.p.script[:self.p.script.index("\n" + text) + 1].count("\n")

    def section(self, number):
        start = self.p.script.index("-- MSM Section %d" % number)
        end = self.p.script.find("-- MSM Section", start + 1)
        return self.p.script[start:end]

    def test_a_view_is_created_or_replaced(self):
        self.apply([ADD_VIEW])
        self.assertIn("CREATE OR REPLACE VIEW `order_ids` AS SELECT `id` FROM `orders`%%",
                      self.section(150))
        self.apply([{"op": "drop_view", "view": "order_ids"}])
        self.assertEqual(self.p.script, MSM_SCRIPT)

    def test_a_routine_is_dropped_before_its_create(self):
        result = self.apply([ADD_ROUTINE])
        self.assertIn("END%%\n\nDROP PROCEDURE IF EXISTS `count_orders`%%\nCREATE PROCEDURE `count_orders`()",
                      self.section(150))
        self.assertEqual([m["sql"].split("\n")[0] for m in result["migration"]],
                         ["CREATE PROCEDURE `count_orders`()"])
        self.apply([{"op": "drop_routine", "routine": "count_orders"}])
        self.assertEqual(self.p.script, MSM_SCRIPT)

    def test_a_sequence_goes_before_the_first_table(self):
        result = self.apply([{"op": "add_sequence", "sequence": {"name": "order_ids"},
                              "at": {"file": "", "line": self.line_of("CREATE TABLE `customer`")}}])
        section = self.section(140)
        self.assertLess(section.index("CREATE SEQUENCE `order_ids`;"), section.index("CREATE TABLE"))
        self.assertEqual([(m["action"], m["object"], m["name"]) for m in result["migration"]],
                         [("create", "sequence", "order_ids")])
        self.assertEqual(result["notes"], [])
        result = self.apply([{"op": "rename_sequence", "sequence": "order_ids", "new_name": "order_numbers"}])
        self.assertEqual([m["sql"] for m in result["migration"]], ["RENAME TABLE `order_ids` TO `order_numbers`"])
        self.apply([{"op": "drop_sequence", "sequence": "order_numbers"}])
        self.assertEqual(self.p.script, MSM_SCRIPT)

    def test_a_trigger_and_an_event_are_dropped_before_their_create(self):
        result = self.apply([ADD_TRIGGER | {"form": "drop_first"}, ADD_EVENT | {"form": "drop_first"}])
        section = self.section(150)
        self.assertIn("DROP TRIGGER IF EXISTS `orders_bi`%%\nCREATE TRIGGER `orders_bi` BEFORE INSERT ON `orders`",
                      section)
        self.assertIn("DROP EVENT IF EXISTS `purge`%%\nCREATE EVENT `purge` ON SCHEDULE EVERY 1 DAY", section)
        self.assertEqual([(m["action"], m["object"], m["name"]) for m in result["migration"]],
                         [("create", "trigger", "orders_bi"), ("create", "event", "purge")])
        self.assertEqual([n["kind"] for n in result["notes"]], ["trigger-created", "event-created"])
        # The palette's undo.
        self.apply([{"op": "drop_event", "event": "purge"},
                    {"op": "drop_trigger", "trigger": "orders_bi", "table": "orders"}])
        self.assertEqual(self.p.script, MSM_SCRIPT)

    def test_a_first_view_goes_where_msm_puts_it(self):
        self.apply([{"op": "drop_view", "view": "customer_ids"}])
        self.apply([ADD_VIEW | {"at": {"file": "", "line": self.line_of("DELIMITER ;")}, "form": "or_replace"}])
        self.assertIn("CREATE OR REPLACE VIEW `order_ids`", self.section(150))
        self.assertNotIn("order_ids", self.section(140))


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

    def test_rename_the_schema(self):
        old = "/*<msm:schema_prefix>*/mariadb_rest_service/*<msm:schema_postfix>*/"
        new = "/*<msm:schema_prefix>*/mrs_renamed/*<msm:schema_postfix>*/"
        code, result = self.p.edit([{"op": "rename_schema", "schema": old, "new_name": new, "comments": True}],
                                   "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.assertEqual(result["migration"], [])
        self.p.apply(result)
        after = self.p.outline()
        self.assertEqual(after, result["outline"])
        self.assertEqual([s["name"] for s in after["schemas"] if s["name"]], ["mrs_renamed"])
        # The banner, CREATE SCHEMA and USE: nothing names the old one.
        self.assertIn("-- `mrs_renamed`", self.p.script)
        self.assertNotIn("mariadb_rest_service`", self.p.script)
        self.assertNotIn("mariadb_rest_service/", self.p.script)
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

    def test_rename_a_trigger_and_an_event_and_back(self):
        files = {f: f.read_bytes() for f in self.p.dev.rglob("*.sql")}
        code, result = self.p.edit([
            {"op": "rename_trigger", "trigger": "config_AFTER_INSERT_AUDIT_LOG", "new_name": "config_ai"},
            {"op": "rename_event", "event": "audit_log_dump_event", "new_name": "audit_log_dump"},
        ], "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.assertEqual([(m["action"], m["object"], m["name"]) for m in result["migration"]],
                         [("drop", "trigger", "config_AFTER_INSERT_AUDIT_LOG"), ("create", "trigger", "config_ai"),
                          ("rename", "event", "audit_log_dump_event")])
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        triggers = (self.p.dev / "sections" / "150-40_audit_log_triggers.sql").read_text()
        self.assertNotIn("`config_AFTER_INSERT_AUDIT_LOG`", triggers)
        events = (self.p.dev / "sections" / "150-50_events.sql").read_text()
        self.assertIn("DROP EVENT IF EXISTS `audit_log_dump`%%\nCREATE EVENT `audit_log_dump`", events)
        check = self.p.run("check", "--source-dir", str(self.p.dev), "-")
        self.assertEqual(check.returncode, 0, check.stdout.decode())
        code, result = self.p.edit([
            {"op": "rename_event", "event": "audit_log_dump", "new_name": "audit_log_dump_event"},
            {"op": "rename_trigger", "trigger": "config_ai", "new_name": "config_AFTER_INSERT_AUDIT_LOG"},
        ], "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.p.apply(result)
        self.assertEqual({f: f.read_bytes() for f in self.p.dev.rglob("*.sql")}, files)

    def test_add_and_drop_a_trigger_and_an_event(self):
        before = self.p.outline()
        files = {f: f.read_bytes() for f in self.p.dev.rglob("*.sql")}
        code, result = self.p.edit([
            {"op": "add_trigger", "trigger": {"name": "url_host_bi", "timing": "BEFORE", "events": "INSERT",
                                              "table": "url_host", "body": "BEGIN\nEND"}},
            {"op": "add_event", "event": {"name": "event1", "schedule": "EVERY 1 DAY",
                                          "body": "BEGIN\n  DELETE FROM `audit_log` WHERE FALSE;\nEND"}},
        ], "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.p.apply(result)
        after = self.p.outline()
        self.assertEqual(after, result["outline"])
        self.assertEqual(len(after["notes"]), len(before["notes"]))
        # After the last trigger and the last event, in their sections' files.
        self.assertIn("DROP TRIGGER IF EXISTS `url_host_bi`%%",
                      (self.p.dev / "sections" / "150-40_audit_log_triggers.sql").read_text())
        self.assertIn("DROP EVENT IF EXISTS `event1`%%", (self.p.dev / "sections" / "150-50_events.sql").read_text())
        check = self.p.run("check", "--source-dir", str(self.p.dev), "-")
        self.assertEqual(check.returncode, 0, check.stdout.decode())
        code, result = self.p.edit([
            {"op": "drop_event", "event": "event1"},
            {"op": "drop_trigger", "trigger": "url_host_bi", "table": "url_host"},
        ], "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.p.apply(result)
        self.assertEqual(self.p.outline(), before)
        self.assertEqual({f: f.read_bytes() for f in self.p.dev.rglob("*.sql")}, files)

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

    def test_placement_as_msm_sends_it(self):
        """msm places each add op (`at`, `form`) from the outline's ranges."""
        before = self.p.outline()
        files = {f: f.read_bytes() for f in self.p.dev.rglob("*.sql")}
        script = self.p.script
        schema = next(s for s in before["schemas"] if s["name"] == "mariadb_rest_service")
        includes = before["includes"]

        def after_last(kind):
            o = schema[kind][-1]
            return {"file": includes[o["source"]]["file"], "after": o["range"]["end"]}

        def rel(name):
            return "sections/" + name

        tables = rel("140-10_tables.sql")
        first_table = schema["tables"][0]
        table_banner = first_table["range"]["start"]["line"] - 3  # its "-- ---" border
        views = (self.p.dev / rel("150-10_views.sql")).read_text()
        last_table = after_last("tables")
        last_table["file"] = tables  # by its path relative to --source-dir
        code, result = self.p.edit([
            {"op": "add_table", "table": {"name": "zz_table", "columns": [{"name": "id", "type": "INT"}]},
             "form": "create", "at": last_table},
            {"op": "add_sequence", "sequence": {"name": "zz_sequence"}, "form": "create",
             "at": {"file": tables, "line": table_banner}},
            {"op": "add_view", "view": {"name": "zz_view", "definition": "SELECT 1"}, "form": "or_replace",
             "at": {"file": rel("150-10_views.sql"), "line": views.count("\n")}},
            ADD_ROUTINE | {"routine": dict(ADD_ROUTINE["routine"], name="zz_routine"), "form": "drop_first",
                           "at": {"file": "-", "line": script[:script.index("-- MSM Section 170")].count("\n") - 1}},
            {"op": "add_event", "event": {"name": "zz_event", "schedule": "EVERY 1 DAY",
                                          "body": "BEGIN\n  DELETE FROM `audit_log` WHERE FALSE;\nEND"},
             "form": "or_replace", "at": after_last("events")},
        ], "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.p.apply(result)
        self.assertEqual(self.p.outline(), result["outline"])
        check = self.p.run("check", "--source-dir", str(self.p.dev), "-")
        self.assertEqual(check.returncode, 0, check.stdout.decode())
        text = (self.p.dev / tables).read_text()
        self.assertLess(text.index("CREATE SEQUENCE `zz_sequence`;"), text.index("-- Table `url_host`"))
        self.assertIn("CREATE TABLE `zz_table`", text)
        self.assertTrue((self.p.dev / rel("150-10_views.sql")).read_text().endswith(
            "\nCREATE OR REPLACE VIEW `zz_view` AS SELECT 1;\n"))
        # In the events' DELIMITER block, with its delimiter.
        self.assertIn("CREATE OR REPLACE EVENT `zz_event`",
                      (self.p.dev / rel("150-50_events.sql")).read_text().split("DELIMITER ;")[-2])
        # A block of its own: the line is outside one.
        self.assertRegex(self.p.script, r"DELIMITER (\S+)\nDROP PROCEDURE IF EXISTS `zz_routine`\1\n"
                                        r"CREATE PROCEDURE `zz_routine`\(\)")
        self.assertEqual([(m["action"], m["object"], m["name"]) for m in result["migration"]],
                         [("create", "table", "zz_table"), ("create", "sequence", "zz_sequence"),
                          ("create", "view", "zz_view"), ("create", "procedure", "zz_routine"),
                          ("create", "event", "zz_event")])
        # Dropped again: the files as they were.
        code, result = self.p.edit([
            {"op": "drop_event", "event": "zz_event"},
            {"op": "drop_routine", "routine": "zz_routine"},
            {"op": "drop_view", "view": "zz_view"},
            {"op": "drop_sequence", "sequence": "zz_sequence"},
            {"op": "drop_table", "table": "zz_table"},
        ], "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.p.apply(result)
        self.assertEqual(self.p.outline(), before)
        self.assertEqual(self.p.script, script)
        self.assertEqual({f: f.read_bytes() for f in self.p.dev.rglob("*.sql")}, files)

    def test_new_views_and_routines_follow_their_neighbours(self):
        code, result = self.p.edit([ADD_VIEW | {"view": {"name": "view1", "definition": "SELECT 1"}},
                                    ADD_ROUTINE | {"routine": dict(ADD_ROUTINE["routine"], name="routine1")}],
                                   "mariadb_rest_service")
        self.assertEqual(code, 0, result)
        self.p.apply(result)
        # After the last view, written as it is.
        self.assertRegex(self.p.script, r"CREATE OR REPLACE\s.*VIEW `view1`")
        # After restore_roles, the last routine, which the script drops first.
        self.assertIn("DROP PROCEDURE IF EXISTS `routine1`%%\nCREATE PROCEDURE `routine1`()", self.p.script)

    def test_add_and_drop(self):
        before = self.p.outline()
        files = {f: f.read_bytes() for f in self.p.dev.rglob("*.sql")}
        script = self.p.script
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
        self.assertEqual({f: f.read_bytes() for f in self.p.dev.rglob("*.sql")}, files)
        self.assertEqual(self.p.script, script)


if __name__ == "__main__":
    unittest.main()
