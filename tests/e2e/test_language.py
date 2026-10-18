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

"""End-to-end tests of completion, hover, navigation, rename, signature help,
workspace symbols and the workspace catalog (P5), over LSP."""

import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from lsp_client import Client, Lsp_error  # noqa: E402
import sandbox  # noqa: E402
from test_lsp import BINARY, URI, Lsp_test  # noqa: E402

SCHEMA = """CREATE DATABASE shop;
USE shop;
CREATE TABLE customer (id INT PRIMARY KEY, name VARCHAR(80) NOT NULL COMMENT 'full name',
  email VARCHAR(120));
CREATE TABLE orders (id INT PRIMARY KEY, customer_id INT, total DECIMAL(10,2),
  FOREIGN KEY (customer_id) REFERENCES customer (id));
CREATE FUNCTION order_count(c INT) RETURNS INT RETURN 1;
"""


def position_of(text, needle, offset=0):
    at = text.index(needle) + offset
    before = text[:at]
    return {"line": before.count("\n"), "character": len(before) - (before.rfind("\n") + 1)}


def split_cursor(text):
    """`text` without its '|', and the LSP position of the '|'."""
    at = text.index("|")
    return text[:at] + text[at + 1:], position_of(text, "|")


def range_text(text, r):
    lines = text.split("\n")
    s, e = r["start"], r["end"]
    if s["line"] == e["line"]:
        return lines[s["line"]][s["character"]:e["character"]]
    return None


class Language_test(Lsp_test):
    def open_at(self, sql, options=None, capabilities=None):
        """Opens SCHEMA + sql; returns (text, position of the |)."""
        c = self.client
        c.initialize(options, capabilities)
        text, pos = split_cursor(SCHEMA + sql)
        c.open(URI, text)
        return text, pos

    def at(self, method, pos, **params):
        return self.client.request(method, {"textDocument": {"uri": URI}, "position": pos,
                                            **params})


class Completion(Language_test):
    def test_columns_of_an_alias(self):
        text, pos = self.open_at("SELECT c.| FROM customer c;")
        r = self.at("textDocument/completion", pos)
        labels = sorted(i["label"] for i in r["items"])
        self.assertEqual(labels, ["email", "id", "name"])
        name = next(i for i in r["items"] if i["label"] == "name")
        self.assertEqual(name["kind"], 5)
        self.assertEqual(name["detail"], "VARCHAR(80) NOT NULL")
        self.assertEqual(name["textEdit"]["range"]["start"], pos)

    def test_tables_with_a_prefix_and_its_range(self):
        text, pos = self.open_at("SELECT * FROM cu|")
        r = self.at("textDocument/completion", pos)
        self.assertEqual([i["label"] for i in r["items"]], ["customer"])
        edit = r["items"][0]["textEdit"]
        self.assertEqual(range_text(text, edit["range"]), "cu")
        self.assertEqual(edit["newText"], "customer")

    def test_keywords_and_functions(self):
        text, pos = self.open_at("SELECT * FROM orders WHERE total |")
        labels = [i["label"] for i in self.at("textDocument/completion", pos)["items"]]
        for k in ("BETWEEN", "IN", "IS", "LIKE"):
            self.assertIn(k, labels)
        self.client.change(URI, 2, [{"text": text.replace("WHERE total ", "WHERE coal")}])
        text2 = text.replace("WHERE total ", "WHERE coal")
        r = self.at("textDocument/completion", position_of(text2, "WHERE coal", 10))
        coalesce = next(i for i in r["items"] if i["label"] == "COALESCE")
        self.assertEqual(coalesce["kind"], 3)
        self.assertIn("COALESCE(value,...)", coalesce["documentation"]["value"])

    def test_snippets_when_the_client_takes_them(self):
        caps = {"textDocument": {"completion": {"completionItem": {"snippetSupport": True}}}}
        text, pos = self.open_at("SELECT order_c|", capabilities=caps)
        item = next(i for i in self.at("textDocument/completion", pos)["items"]
                    if i["label"] == "order_count")
        self.assertEqual(item["textEdit"]["newText"], "order_count($0)")
        self.assertEqual(item["insertTextFormat"], 2)

    def test_lower_case_keywords(self):
        text, pos = self.open_at("sel|", {"keywordCase": "lower"})
        labels = [i["label"] for i in self.at("textDocument/completion", pos)["items"]]
        self.assertIn("select", labels)


class Navigation(Language_test):
    def test_hover(self):
        text, pos = self.open_at("SELECT o.total, c.na|me FROM orders o JOIN customer c "
                                 "ON c.id = o.customer_id;")
        h = self.at("textDocument/hover", pos)
        self.assertIn("**column** `customer`.`name`", h["contents"]["value"])
        self.assertIn("full name", h["contents"]["value"])
        self.assertEqual(range_text(text, h["range"]), "name")
        self.assertIsNone(self.at("textDocument/hover", position_of(text, "SELECT", 2)))

    def test_definition_in_the_document(self):
        text, pos = self.open_at("SELECT * FROM ord|ers;")
        locs = self.at("textDocument/definition", pos)
        self.assertEqual(len(locs), 1)
        self.assertEqual(locs[0]["uri"], URI)
        self.assertEqual(locs[0]["range"]["start"], position_of(text, "orders ("))
        # An alias goes to where it is defined.
        text2 = text.replace("SELECT * FROM orders;", "SELECT x.id FROM orders x;")
        self.client.change(URI, 2, [{"text": text2}])
        locs = self.at("textDocument/definition", position_of(text2, "x.id"))
        self.assertEqual(locs[0]["range"]["start"], position_of(text2, "x;"))

    def test_references_and_highlight(self):
        text, pos = self.open_at("SELECT c.name FROM customer c WHERE c.id > 0;\n"
                                 "UPDATE customer SET na|me = 'x';\n")
        refs = self.at("textDocument/references", pos, context={"includeDeclaration": True})
        self.assertEqual([range_text(text, r["range"]) for r in refs], ["name"] * 3)
        refs = self.at("textDocument/references", pos, context={"includeDeclaration": False})
        self.assertEqual(len(refs), 2)
        hl = self.at("textDocument/documentHighlight", position_of(text, "c WHERE"))
        self.assertEqual(len(hl), 3)  # c.name, the alias, c.id
        self.assertEqual(sorted(h["kind"] for h in hl), [2, 2, 3])

    def test_rename(self):
        text, pos = self.open_at("SELECT c|.name FROM customer c WHERE c.id > 0;")
        prep = self.at("textDocument/prepareRename", pos)
        self.assertEqual(prep["placeholder"], "c")
        edit = self.at("textDocument/rename", pos, newName="cust")
        edits = edit["changes"][URI]
        self.assertEqual(len(edits), 3)
        self.assertTrue(all(e["newText"] == "cust" for e in edits))
        # A table the document defines, everywhere; a reserved word is quoted.
        edit = self.at("textDocument/rename", position_of(text, "customer c"), newName="order")
        edits = edit["changes"][URI]
        self.assertEqual(len(edits), 3)  # CREATE TABLE, REFERENCES, FROM
        self.assertTrue(all(e["newText"] == "`order`" for e in edits))
        # A built-in function cannot be renamed.
        text2 = text + "\nSELECT NOW();"
        self.client.change(URI, 2, [{"text": text2}])
        with self.assertRaises(Lsp_error):
            self.at("textDocument/prepareRename", position_of(text2, "NOW", 1))

    def test_signature_help(self):
        text, pos = self.open_at("SELECT SUBSTRING(name, 2, |) FROM customer;")
        h = self.at("textDocument/signatureHelp", pos)
        self.assertEqual(h["activeParameter"], 2)
        sig = h["signatures"][h["activeSignature"]]
        self.assertEqual(sig["label"], "SUBSTRING(str,pos,len)")
        b, e = sig["parameters"][2]["label"]
        self.assertEqual(sig["label"][b:e], "len")
        text2 = text.replace("SUBSTRING(name, 2, )", "order_count(")
        self.client.change(URI, 2, [{"text": text2}])
        h = self.at("textDocument/signatureHelp", position_of(text2, "order_count(", 12))
        self.assertEqual(h["signatures"][0]["label"], "order_count(c INT) RETURNS INT")


class Workspace(unittest.TestCase):
    """catalogFiles: names defined in other files, in the order given."""

    def setUp(self):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="mdbls-ws-"))
        (self.dir / "db").mkdir()
        (self.dir / "db" / "01_schema.sql").write_text(
            "CREATE DATABASE app;\nUSE app;\nCREATE TABLE account (id INT, login VARCHAR(40));\n")
        (self.dir / "db" / "02_more.sql").write_text(
            "ALTER TABLE account ADD COLUMN created DATETIME;\n"
            "CREATE VIEW active AS SELECT id FROM account;\n")
        self.client = Client(BINARY)

    def tearDown(self):
        self.assertEqual(self.client.close(), 0)
        shutil.rmtree(self.dir, ignore_errors=True)

    def initialize(self):
        self.client.initialize({"catalogFiles": ["db"]}, root_uri=self.dir.as_uri())

    def complete_until(self, text, pos, label, timeout=20):
        """The workspace catalog is read in the background."""
        deadline = time.monotonic() + timeout
        while True:
            r = self.client.request("textDocument/completion",
                                    {"textDocument": {"uri": URI}, "position": pos})
            labels = [i["label"] for i in r["items"]]
            if label in labels or time.monotonic() > deadline:
                return labels
            time.sleep(0.1)

    def test_names_from_catalog_files(self):
        self.initialize()
        text, pos = split_cursor("USE app;\nSELECT a.| FROM account a;\n")
        self.client.open(URI, text)
        labels = self.complete_until(text, pos, "created")
        self.assertEqual(sorted(labels), ["created", "id", "login"])
        locs = self.client.request("textDocument/definition", {
            "textDocument": {"uri": URI}, "position": position_of(text, "account a")})
        self.assertEqual(len(locs), 1)
        self.assertTrue(locs[0]["uri"].endswith("/db/01_schema.sql"))
        self.assertEqual(locs[0]["range"]["start"], {"line": 2, "character": 13})
        h = self.client.request("textDocument/hover", {
            "textDocument": {"uri": URI}, "position": position_of(text, "account a")})
        self.assertIn("Defined in the workspace.", h["contents"]["value"])

    def test_workspace_symbols(self):
        self.initialize()
        self.client.open(URI, "CREATE TABLE local_thing (a INT);\n")
        deadline = time.monotonic() + 20
        while True:
            syms = self.client.request("workspace/symbol", {"query": "acc"})
            if syms or time.monotonic() > deadline:
                break
            time.sleep(0.1)
        self.assertEqual([s["name"] for s in syms], ["account"])
        self.assertEqual(syms[0]["containerName"], "app")
        self.assertTrue(syms[0]["location"]["uri"].endswith("01_schema.sql"))
        syms = self.client.request("workspace/symbol", {"query": "local"})
        self.assertEqual([(s["name"], s["location"]["uri"]) for s in syms],
                         [("local_thing", URI)])


def generated_document(size):
    """SCHEMA, then queries of all kinds until `size` bytes."""
    parts = [SCHEMA]
    n = 0
    total = len(SCHEMA)
    while total < size:
        chunk = (
            "SELECT c.name, o.total FROM customer c JOIN orders o ON o.customer_id = c.id "
            "WHERE o.total > %d ORDER BY c.name;\n"
            "INSERT INTO orders (id, customer_id, total) VALUES (%d, %d, %d.50);\n"
            "UPDATE customer SET email = 'x%d@example.com' WHERE id = %d;\n" % (n, n, n, n, n, n))
        parts.append(chunk)
        total += len(chunk)
        n += 1
    return "".join(parts)


class Performance(Language_test):
    """P5's target: completion at a 1 MB document, p95 under 20 ms (release
    builds; MDBLS_COMPLETION_P95_MS sets the bound)."""

    def test_completion_latency_at_1_mb(self):
        c = self.client
        c.initialize()
        text = generated_document(1 << 20)
        c.open(URI, text)
        lines = text.split("\n")
        # Cursor positions spread over the document: after "o." in a
        # SELECT, after "FROM ", in a WHERE.
        positions = []
        for k in range(60):
            line = 8 + (len(lines) - 10) * k // 60
            while "SELECT c.name" not in lines[line]:
                line += 1
            l = lines[line]
            # ... and one letter into a word, where keywords are probed.
            for col in (l.index("o.total") + 2, l.index("FROM ") + 5, l.index("WHERE ") + 6,
                        l.index("WHERE ") + 7, l.index("ORDER BY ") + 10):
                positions.append({"line": line, "character": col})
        self.at("textDocument/completion", positions[0])  # the outline is built once
        times = []
        for p in positions:
            t0 = time.perf_counter()
            r = self.at("textDocument/completion", p)
            times.append((time.perf_counter() - t0) * 1000)
            self.assertTrue(r["items"])
        times.sort()
        p95 = times[int(len(times) * 0.95) - 1]
        bound = float(os.environ.get("MDBLS_COMPLETION_P95_MS", "100"))
        print("\ncompletion at %d bytes: median %.2f ms, p95 %.2f ms, max %.2f ms (bound %g)" %
              (len(text), times[len(times) // 2], p95, times[-1], bound), file=sys.stderr)
        self.assertLess(p95, bound)


@unittest.skipUnless(sandbox.mariadbd() and sandbox.shell_bin_dir(),
                     "needs mariadbd and MariaDB Shell's mariadb-secret-store-plaintext")
class Live_connection(unittest.TestCase):
    """A sandbox server and a private secret store: the catalog, sql_mode and
    version come from the server named by mariadb/documentContext."""

    @classmethod
    def setUpClass(cls):
        cls.server = sandbox.Sandbox(
            sql_mode="ANSI_QUOTES,STRICT_TRANS_TABLES",
            setup_sql="CREATE USER app@'%' IDENTIFIED BY 'app pw'; GRANT ALL ON *.* TO app@'%';"
                      "CREATE DATABASE store; CREATE TABLE store.item (id INT PRIMARY KEY, "
                      "label VARCHAR(40) NOT NULL COMMENT 'shown to buyers', price DECIMAL(10,2));"
                      "CREATE VIEW store.cheap AS SELECT id, label FROM store.item;"
                      "CREATE PROCEDURE store.restock(IN n INT, OUT done INT) SELECT n;")
        cls.secrets = sandbox.Secret_store()
        cls.uri = "app@127.0.0.1:%d" % cls.server.port
        # As the MCP server stores it: a URI key, the password marked raw.
        cls.secrets.store("generic", "MCP:CONN:" + cls.uri, "rapp pw")

    @classmethod
    def tearDownClass(cls):
        cls.secrets.close()
        cls.server.stop()

    def setUp(self):
        self.client = Client(BINARY)
        self.client.initialize({"shellBinDir": self.secrets.bin})

    def tearDown(self):
        self.assertEqual(self.client.close(), 0)

    def test_catalog_sql_mode_and_names_from_the_server(self):
        c = self.client
        text, pos = split_cursor('CREATE TABLE "t2" (x INT);\nSELECT i.|id FROM item i;\n')
        c.open(URI, text)
        # Without the server's ANSI_QUOTES, "t2" is a string: a syntax error.
        self.assertEqual(len(c.diagnostics(URI)), 1)
        mark = c.mark()
        c.notify("mariadb/documentContext", {"textDocument": {"uri": URI},
                                             "connection": "mariadb://" + self.uri,
                                             "currentSchema": "store"})
        # The server's sql_mode arrives with its catalog: checked again.
        c.wait_notification("textDocument/publishDiagnostics",
                            lambda p: p["uri"] == URI and p["diagnostics"] == [], 30, mark)
        r = c.request("textDocument/completion", {"textDocument": {"uri": URI}, "position": pos})
        self.assertEqual(sorted(i["label"] for i in r["items"]), ["id", "label", "price"])
        h = c.request("textDocument/hover", {"textDocument": {"uri": URI},
                                             "position": position_of(text, "item i")})
        self.assertIn("**table** `store`.`item`", h["contents"]["value"])
        self.assertIn("From the connected server.", h["contents"]["value"])
        # Live objects have no location.
        self.assertIsNone(c.request("textDocument/definition", {
            "textDocument": {"uri": URI}, "position": position_of(text, "item i")}))

    def test_refresh_reads_new_tables(self):
        c = self.client
        text, pos = split_cursor("SELECT * FROM |")
        c.open(URI, text)
        c.notify("mariadb/documentContext", {"textDocument": {"uri": URI},
                                             "connection": self.uri, "currentSchema": "store"})

        def labels():
            r = c.request("textDocument/completion", {"textDocument": {"uri": URI},
                                                      "position": pos})
            return [i["label"] for i in r["items"]]

        deadline = time.monotonic() + 30
        while "item" not in labels() and time.monotonic() < deadline:
            time.sleep(0.1)
        self.assertIn("cheap", labels())
        self.server.sql("CREATE TABLE store.fresh (a INT)")
        self.assertNotIn("fresh", labels())
        c.request("mariadb/refreshCatalog", {"textDocument": {"uri": URI}})
        deadline = time.monotonic() + 30
        while "fresh" not in labels() and time.monotonic() < deadline:
            time.sleep(0.1)
        self.assertIn("fresh", labels())

    def test_a_new_refresh_interval_applies_to_connections_already_read(self):
        c = self.client
        text, pos = split_cursor("SELECT * FROM |")
        c.open(URI, text)
        c.notify("mariadb/documentContext", {"textDocument": {"uri": URI},
                                             "connection": self.uri, "currentSchema": "store"})

        def labels():
            r = c.request("textDocument/completion", {"textDocument": {"uri": URI},
                                                      "position": pos})
            return [i["label"] for i in r["items"]]

        deadline = time.monotonic() + 30
        while "item" not in labels() and time.monotonic() < deadline:
            time.sleep(0.1)
        self.assertIn("item", labels())
        # Read once, then only when asked: until the interval is set.
        c.notify("workspace/didChangeConfiguration",
                 {"settings": {"mariadb": {"catalogRefreshSeconds": 1}}})
        self.server.sql("CREATE TABLE store.ticked (a INT)")
        deadline = time.monotonic() + 30
        while "ticked" not in labels() and time.monotonic() < deadline:
            time.sleep(0.1)
        self.assertIn("ticked", labels())

    def test_a_connection_without_a_password_is_reported_once(self):
        c = self.client
        c.open(URI, "SELECT 1;")
        c.notify("mariadb/documentContext", {"textDocument": {"uri": URI},
                                             "connection": "nobody@127.0.0.1:%d" % self.server.port})
        msg, _ = c.wait_notification("window/showMessage",
                                     lambda p: "connection not configured" in p["message"], 30)
        self.assertEqual(msg["type"], 2)

    def test_cli(self):
        env = dict(os.environ)
        with tempfile.NamedTemporaryFile("w", suffix=".sql", delete=False) as f:
            f.write('USE store;\nSELECT "label", lable FROM item;\nSELECT * FROM nothing;\n')
        try:
            r = subprocess.run([BINARY, "check", "--shell-bin-dir", self.secrets.bin,
                                "--connection", self.uri, f.name],
                               capture_output=True, text=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertIn("2:17: warning: unknown column `lable` in table `item`", r.stdout)
            self.assertIn("3:15: warning: unknown table `nothing`", r.stdout)
            r = subprocess.run([BINARY, "connections", "--shell-bin-dir", self.secrets.bin,
                                "--format", "json"], capture_output=True, text=True, env=env)
            self.assertEqual(r.returncode, 0, r.stderr)
            self.assertEqual(json.loads(r.stdout), [{
                "kind": "mcp", "uri": self.uri, "normalized": "mariadb://" + self.uri}])
        finally:
            os.unlink(f.name)


if __name__ == "__main__":
    unittest.main()
