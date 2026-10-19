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

"""End-to-end tests of MSM SOURCE directives: the outline, diagnostics,
navigation and links take in the files a script includes."""

import os
import pathlib
import json
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.parse

sys.path.insert(0, os.path.dirname(__file__))
from lsp_client import Client  # noqa: E402
from test_lsp import BINARY, FIXTURES  # noqa: E402

# A reference to an included table, so navigation has a name to start from.
TAIL = "SELECT name FROM customer;\n"


def position_of(text, needle, offset=0):
    at = text.index(needle) + offset
    before = text[:at]
    return {"line": before.count("\n"), "character": len(before) - (before.rfind("\n") + 1)}


def uri_path(uri):
    """A file URI's path, decoded, a drive letter in lower case: what two
    spellings of one file's URI share (the server writes `file:///c%3A/x`
    as VS Code does, Python's as_uri() `file:///C:/x`)."""
    path = urllib.parse.unquote(urllib.parse.urlparse(uri).path)
    if len(path) > 2 and path[0] == "/" and path[2] == ":":
        path = "/" + path[1].lower() + path[2:]
    return path


def by_path(edits):
    """A WorkspaceEdit's changes keyed by uri_path()."""
    return {uri_path(uri): e for uri, e in edits.items()}


def find_symbol(symbols, name):
    for s in symbols:
        if s["name"] == name:
            return s
        found = find_symbol(s.get("children", []), name)
        if found:
            return found
    return None


class Source_includes(unittest.TestCase):
    def setUp(self):
        self.dir = pathlib.Path(tempfile.mkdtemp(prefix="mdbls-src-"))
        shutil.copytree(FIXTURES / "msm", self.dir / "msm")
        self.dev = self.dir / "msm" / "development"
        self.script = self.dev / "shop_next.sql"
        self.uri = self.script.as_uri()
        self.text = self.script.read_text() + TAIL
        self.client = Client(BINARY)
        self.client.initialize(capabilities={
            "workspace": {"didChangeWatchedFiles": {"dynamicRegistration": True}}},
            root_uri=self.dir.as_uri())

    def tearDown(self):
        self.assertEqual(self.client.close(), 0)
        shutil.rmtree(self.dir, ignore_errors=True)

    def symbols(self, uri=None):
        return self.client.request("textDocument/documentSymbol",
                                   {"textDocument": {"uri": uri or self.uri}})

    def symbols_until(self, name, present=True, timeout=20):
        """Included files are read again in the background."""
        deadline = time.monotonic() + timeout
        while True:
            s = self.symbols()
            if (find_symbol(s, name) is not None) == present or time.monotonic() > deadline:
                return s
            time.sleep(0.1)

    def diagnostics_until(self, predicate, after=0, timeout=20):
        """The first diagnostics of the script that satisfy `predicate`, of
        the notifications from `after` (Client.mark()) on."""
        params, _ = self.client.wait_notification(
            "textDocument/publishDiagnostics",
            lambda p: p["uri"] == self.uri and predicate(p["diagnostics"]), timeout, after)
        return params["diagnostics"]

    def test_outline_holds_the_included_objects(self):
        self.client.open(self.uri, self.text)
        symbols = self.symbols()
        shop = find_symbol(symbols, "shop")
        self.assertIsNotNone(shop)
        names = [c["name"] for c in shop["children"]]
        for n in ("customer", "orders", "note", "customer_orders", "order_count", "restore_roles"):
            self.assertIn(n, names)
        # An included table sits on its directive, its detail naming the file.
        customer = find_symbol(symbols, "customer")
        self.assertEqual(customer["detail"], "table · 140_tables.sql:3")
        directive = position_of(self.text, "SOURCE './sections/140")
        self.assertEqual(customer["range"]["start"], directive)
        self.assertEqual(customer["selectionRange"]["start"], directive)
        # The procedure whose body includes a file is the script's own.
        restore = find_symbol(symbols, "restore_roles")
        self.assertEqual(restore["range"]["start"], position_of(self.text, "CREATE PROCEDURE"))
        self.assertNotIn("·", restore.get("detail", ""))

    def test_outline_json(self):
        self.client.open(self.uri, self.text)
        o = self.client.request("mariadb/outline", {"textDocument": {"uri": self.uri}})
        self.assertEqual([pathlib.Path(i["file"]).name for i in o["includes"]],
                         ["140_tables.sql", "150_routines.sql", "170_roles.sql", "180_extra.sql"])
        self.assertEqual(o["includes"][0]["slice"], "[ln3:ln-2]")
        self.assertEqual(o["includes"][0]["range"]["start"],
                         position_of(self.text, "SOURCE './sections/140"))
        tables = {t["name"]: t for t in o["schemas"][0]["tables"]}
        # Positions in the included file, which `source` names.
        self.assertEqual(tables["customer"]["source"], 0)
        self.assertEqual(tables["customer"]["selection_range"]["start"], {"line": 2, "character": 13})
        self.assertEqual(tables["note"]["source"], 3)
        procs = {r["name"]: r for r in o["schemas"][0]["routines"]}
        self.assertNotIn("source", procs["restore_roles"])

    def test_diagnostics(self):
        self.client.open(self.uri, self.text)
        diags = self.diagnostics_until(lambda d: any(x.get("code") == "source" for x in d))
        self.assertEqual([d for d in diags if d.get("code") == "syntax-error"], [])
        source = [d for d in diags if d.get("code") == "source"]
        self.assertEqual(len(source), 1)
        self.assertEqual(source[0]["severity"], 2)
        self.assertIn("SOURCE './deeper.sql'", source[0]["message"])
        self.assertEqual(source[0]["range"]["start"], position_of(self.text, "SOURCE './sections/180"))

        # A file that is not there is an error on its path; fixing it clears it.
        broken = self.text.replace("180_extra.sql", "181_extra.sql")
        mark = self.client.mark()
        self.client.change(self.uri, 2, [{"text": broken}])
        diags = self.diagnostics_until(
            lambda d: any(x.get("severity") == 1 and x.get("code") == "source" for x in d), mark)
        errors = [d for d in diags if d.get("code") == "source" and d["severity"] == 1]
        self.assertEqual(len(errors), 1)
        self.assertIn("cannot read", errors[0]["message"])
        self.assertEqual(errors[0]["range"]["start"], position_of(broken, "./sections/181"))
        mark = self.client.mark()
        self.client.change(self.uri, 3, [{"text": self.text}])
        self.diagnostics_until(
            lambda d: not any(x.get("severity") == 1 and x.get("code") == "source" for x in d), mark)

        # Deleting the directive's line, an edit that removes statements and
        # adds none, takes its warning away too.
        line = position_of(self.text, "SOURCE './sections/180")["line"]
        mark = self.client.mark()
        self.client.change(self.uri, 4, [{"range": {"start": {"line": line, "character": 0},
                                                    "end": {"line": line + 1, "character": 0}},
                                          "text": ""}])
        self.diagnostics_until(lambda d: not any(x.get("code") == "source" for x in d), mark)

    def test_navigation_into_an_include(self):
        self.client.open(self.uri, self.text)
        at = {"textDocument": {"uri": self.uri},
              "position": position_of(self.text, "customer;", 2)}
        locs = self.client.request("textDocument/definition", at)
        self.assertEqual(len(locs), 1)
        self.assertEqual(uri_path(locs[0]["uri"]),
                         uri_path((self.dev / "sections" / "140_tables.sql").as_uri()))
        self.assertEqual(locs[0]["range"]["start"], {"line": 2, "character": 13})
        # The hover says which file defines it.
        hover = self.client.request("textDocument/hover", at)
        self.assertIn("Defined in `140_tables.sql:3`, which this document includes.",
                      hover["contents"]["value"])
        # References and rename cover the files the script includes, the
        # procedure body's too.
        refs = self.client.request("textDocument/references",
                                   {**at, "context": {"includeDeclaration": True}})
        lines = {}
        for r in refs:
            lines.setdefault(pathlib.Path(r["uri"][len("file://"):]).name, []).append(
                r["range"]["start"]["line"])
        self.assertEqual(lines, {"140_tables.sql": [2, 6, 10], "150_routines.sql": [9],
                                 "170_roles.sql": [0], "shop_next.sql": [16]})
        self.client.request("textDocument/prepareRename", at)
        edits = self.client.request("textDocument/rename", {**at, "newName": "client"})["changes"]
        edits = by_path(edits)
        tables = edits[uri_path((self.dev / "sections" / "140_tables.sql").as_uri())]
        self.assertEqual([e["range"]["start"] for e in tables],
                         [{"line": 2, "character": 13}, {"line": 6, "character": 12},
                          {"line": 10, "character": 39}])
        self.assertEqual({e["newText"] for e in tables}, {"client"})
        self.assertEqual(len(edits[uri_path(self.uri)]), 1)
        # Workspace symbols point into the included file too.
        found = self.client.request("workspace/symbol", {"query": "customer_orders"})
        self.assertEqual([uri_path(s["location"]["uri"]) for s in found],
                         [uri_path((self.dev / "sections" / "150_routines.sql").as_uri())])

    def test_path_completion(self):
        def complete(text, trigger=None):
            uri = (self.dev / "new_next.sql").as_uri()
            self.client.open(uri, text)
            line = text.count("\n")
            params = {"textDocument": {"uri": uri},
                      "position": {"line": line, "character": len(text) - text.rfind("\n") - 1}}
            if trigger:
                params["context"] = {"triggerKind": 2, "triggerCharacter": trigger}
            items = self.client.request("textDocument/completion", params)["items"]
            self.client.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
            return items

        items = complete("USE shop;\nSOURCE './")
        self.assertIn("sections/", [i["label"] for i in items])
        folder = next(i for i in items if i["label"] == "sections/")
        self.assertEqual(folder["kind"], 19)
        self.assertEqual(folder["command"]["command"], "editor.action.triggerSuggest")
        items = complete("SOURCE './sections/1", "/")
        self.assertEqual([i["label"] for i in items],
                         ["140_tables.sql", "150_routines.sql", "170_roles.sql", "180_extra.sql"])
        # The typed part of the name is what the item replaces.
        self.assertEqual(items[0]["textEdit"]["range"]["start"], {"line": 0, "character": 19})
        self.assertEqual(items[0]["textEdit"]["range"]["end"], {"line": 0, "character": 20})
        # Elsewhere '/' is a division: nothing.
        self.assertEqual(complete("SELECT 1 /", "/"), [])

    def test_included_file_takes_the_script_context(self):
        # A section included in a DELIMITER $$ region, after a USE.
        (self.dev / "sections" / "190_helpers.sql").write_text(
            "CREATE PROCEDURE helper()\nBEGIN\n  SELECT 1;\nEND$$\n"
            "CREATE TABLE helped (id INT)$$\n")
        script = self.dev / "helpers_next.sql"
        script.write_text("USE shop;\nDELIMITER $$\nSOURCE './sections/190_helpers.sql';\n"
                          "DELIMITER ;\n")
        helpers_uri = (self.dev / "sections" / "190_helpers.sql").as_uri()
        helpers = (self.dev / "sections" / "190_helpers.sql").read_text()

        # On its own: ';' ends the procedure early.
        mark = self.client.mark()
        self.client.open(helpers_uri, helpers)
        diags = self.client.diagnostics(helpers_uri, after=mark)
        self.assertTrue(any(d.get("code") == "syntax-error" for d in diags))

        # With the script open: '$$' and the schema shop.
        mark = self.client.mark()
        self.client.open(script.as_uri(), script.read_text())
        self.client.wait_notification(
            "textDocument/publishDiagnostics",
            lambda p: p["uri"] == helpers_uri and not p["diagnostics"], 20, mark)
        deadline = time.monotonic() + 20
        while True:
            symbols = self.symbols(helpers_uri)
            if find_symbol(symbols, "shop") or time.monotonic() > deadline:
                break
            time.sleep(0.1)
        shop = find_symbol(symbols, "shop")
        self.assertIsNotNone(shop)
        self.assertEqual(sorted(c["name"] for c in shop["children"]), ["helped", "helper"])

        # The script closed: its own context again.
        mark = self.client.mark()
        self.client.notify("textDocument/didClose", {"textDocument": {"uri": script.as_uri()}})
        self.client.wait_notification(
            "textDocument/publishDiagnostics",
            lambda p: p["uri"] == helpers_uri and any(
                d.get("code") == "syntax-error" for d in p["diagnostics"]), 20, mark)

    def test_navigation_from_an_included_file(self):
        tables = self.dev / "sections" / "140_tables.sql"
        tables_uri = tables.as_uri()
        text = tables.read_text()
        self.client.open(tables_uri, text)
        at = {"textDocument": {"uri": tables_uri},
              "position": position_of(text, "customer (", 2)}

        def files_of(refs):
            out = {}
            for r in refs:
                out.setdefault(pathlib.Path(r["uri"][len("file://"):]).name, []).append(
                    r["range"]["start"]["line"])
            return out

        # On its own: this file only.
        refs = self.client.request("textDocument/references",
                                   {**at, "context": {"includeDeclaration": True}})
        self.assertEqual(files_of(refs), {"140_tables.sql": [2, 6, 10]})

        # With the script that includes it open: the script and its files.
        self.client.open(self.uri, self.text)
        deadline = time.monotonic() + 20
        while True:
            refs = self.client.request("textDocument/references",
                                       {**at, "context": {"includeDeclaration": True}})
            if len(files_of(refs)) > 1 or time.monotonic() > deadline:
                break
            time.sleep(0.1)
        self.assertEqual(files_of(refs), {"140_tables.sql": [2, 6, 10], "150_routines.sql": [9],
                                          "170_roles.sql": [0], "shop_next.sql": [16]})
        # This file's locations under the URI the client gave it.
        self.assertIn(tables_uri, [r["uri"] for r in refs])

        prepared = self.client.request("textDocument/prepareRename", at)
        self.assertEqual(prepared["range"]["start"], {"line": 2, "character": 13})
        edits = self.client.request("textDocument/rename", {**at, "newName": "client"})["changes"]
        # The requesting file under the URI its client gave, as is.
        self.assertEqual(len(edits[tables_uri]), 3)
        edits = by_path(edits)
        self.assertEqual(len(edits[uri_path(self.uri)]), 1)
        self.assertEqual(
            len(edits[uri_path((self.dev / "sections" / "150_routines.sql").as_uri())]), 1)

    def test_document_links(self):
        self.client.open(self.uri, self.text)
        links = self.client.request("textDocument/documentLink",
                                    {"textDocument": {"uri": self.uri}})
        self.assertEqual([pathlib.Path(link["target"][len("file://"):]).name for link in links],
                         ["140_tables.sql", "150_routines.sql", "170_roles.sql", "180_extra.sql"])
        self.assertEqual(links[0]["range"]["start"], position_of(self.text, "./sections/140"))

    def test_open_included_file_is_read_from_the_editor(self):
        self.client.open(self.uri, self.text)
        self.assertIsNotNone(find_symbol(self.symbols(), "customer"))
        tables = self.dev / "sections" / "140_tables.sql"
        tables_uri = tables.as_uri()
        text = tables.read_text()
        self.client.open(tables_uri, text)
        # Unsaved: a table added in the editor's text shows in the script.
        edited = text.replace("ALTER TABLE missing", "CREATE TABLE added (a INT);\nALTER TABLE missing")
        self.client.change(tables_uri, 2, [{"text": edited}])
        self.assertIsNotNone(find_symbol(self.symbols_until("added"), "added"))
        # Closed without saving: the disk's text again.
        self.client.notify("textDocument/didClose", {"textDocument": {"uri": tables_uri}})
        self.assertIsNone(find_symbol(self.symbols_until("added", present=False), "added"))

    def test_changed_file_on_disk(self):
        self.client.open(self.uri, self.text)
        self.assertIsNone(find_symbol(self.symbols(), "audit"))
        extra = self.dev / "sections" / "180_extra.sql"
        extra.write_text(extra.read_text() + "CREATE TABLE audit (id INT);\n")
        self.client.notify("workspace/didChangeWatchedFiles",
                           {"changes": [{"uri": extra.as_uri(), "type": 2}]})
        self.assertIsNotNone(find_symbol(self.symbols_until("audit"), "audit"))


class Source_dir_cli(unittest.TestCase):
    """mariadb-lsp check, outline and format with --source-dir: the folder
    relative SOURCE paths are taken from, for stdin above all."""

    DEV = FIXTURES / "msm" / "development"
    SCRIPT = DEV / "shop_next.sql"
    TEXT = SCRIPT.read_text()

    def run_cli(self, *args, stdin=None):
        return subprocess.run([BINARY, *args], input=stdin, capture_output=True, text=True,
                              timeout=60)

    def test_check_from_stdin(self):
        # Without it, stdin has no folder: every relative path is refused.
        r = self.run_cli("check", "-", stdin=self.TEXT)
        self.assertEqual(r.returncode, 1)
        self.assertEqual(r.stdout.count("the path is relative, and the document is not in a folder"), 4)
        # With it, the includes are read: the nested directive's warning names
        # the file and line, as for a file.
        r = self.run_cli("check", "--source-dir", str(self.DEV), "-", stdin=self.TEXT)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(r.stdout.splitlines(),
                         ["-:16:1: warning: ./sections/180_extra.sql line 2 has SOURCE "
                          "'./deeper.sql', which msm does not expand: it expands one level only"])
        # --no-source still wins: the directives are SQL, and errors.
        r = self.run_cli("check", "--no-source", "--source-dir", str(self.DEV), "-", stdin=self.TEXT)
        self.assertEqual(r.returncode, 1)
        self.assertIn("syntax error near 'SOURCE'", r.stdout)

    def test_outline_from_stdin(self):
        # The same outline as the file's (includes, sources, notes with
        # file:line), but for the input's name.
        r = self.run_cli("outline", "--source-dir", str(self.DEV), "-", stdin=self.TEXT)
        self.assertEqual(r.returncode, 0, r.stderr)
        stdin = json.loads(r.stdout)
        self.assertEqual([pathlib.Path(i["file"]).name for i in stdin["includes"]],
                         ["140_tables.sql", "150_routines.sql", "170_roles.sql", "180_extra.sql"])
        file = json.loads(self.run_cli("outline", str(self.SCRIPT)).stdout)
        del stdin["file"], file["file"]
        self.assertEqual(stdin, file)

    def test_it_replaces_a_files_folder(self):
        r = self.run_cli("check", "--source-dir", str(FIXTURES), str(self.SCRIPT))
        self.assertEqual(r.returncode, 1)
        self.assertEqual(r.stdout.count("cannot read"), 4)

    def test_format_takes_it_and_reads_nothing(self):
        r = self.run_cli("format", "--source-dir", str(self.DEV), "-", stdin="select 1;\n")
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout, "SELECT 1;\n")

    def test_usage_errors(self):
        r = self.run_cli("check", "--source-dir", str(self.DEV / "no-such-folder"), "-", stdin="")
        self.assertEqual(r.returncode, 2)
        self.assertIn("not a folder", r.stderr)
        r = self.run_cli("check", "--source-dir", str(self.SCRIPT), "-", stdin="")
        self.assertEqual(r.returncode, 2)
        r = self.run_cli("split", "--source-dir", str(self.DEV), "-", stdin="")
        self.assertEqual(r.returncode, 2)
        self.assertIn("goes with check, outline and format", r.stderr)

if __name__ == "__main__":
    unittest.main()
