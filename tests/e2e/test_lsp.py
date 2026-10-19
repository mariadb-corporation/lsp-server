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

"""End-to-end tests: start mariadb-lsp, speak LSP to it, check the answers.

Run by ctest; by hand: MARIADB_LSP=build/bin/mariadb-lsp python3 -m unittest
discover -s tests/e2e"""

import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from lsp_client import Client, Lsp_error  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]
FIXTURES = ROOT / "tests" / "fixtures" / "sql"
BINARY = os.environ.get("MARIADB_LSP", str(ROOT / "build" / "bin" / "mariadb-lsp"))
URI = "file:///test.sql"

TOKEN_TYPES = ["keyword", "string", "number", "comment", "variable", "operator", "function",
               "parameter"]


def decode_tokens(text, data):
    """(line, character, text, type) for LSP-encoded semantic tokens."""
    lines = text.split("\n")
    out = []
    line = char = 0
    for i in range(0, len(data), 5):
        dl, dc, length, ttype, _ = data[i:i + 5]
        line += dl
        char = dc if dl else char + dc
        utf16 = lines[line].encode("utf-16-le")
        out.append((line, char, utf16[2 * char:2 * (char + length)].decode("utf-16-le"),
                    TOKEN_TYPES[ttype]))
    return out


class Lsp_test(unittest.TestCase):
    def setUp(self):
        self.client = Client(BINARY)

    def tearDown(self):
        code = self.client.close()
        self.assertEqual(code, 0)


class Lifecycle(unittest.TestCase):
    def test_initialize_shutdown_exit(self):
        c = Client(BINARY)
        with self.assertRaises(Lsp_error) as e:
            c.request("textDocument/documentSymbol", {"textDocument": {"uri": URI}})
        self.assertEqual(e.exception.code, -32002)
        caps = c.initialize()["capabilities"]
        self.assertEqual(caps["textDocumentSync"]["change"], 2)
        self.assertTrue(caps["semanticTokensProvider"]["range"])
        self.assertEqual(caps["semanticTokensProvider"]["legend"]["tokenTypes"], TOKEN_TYPES)
        with self.assertRaises(Lsp_error) as e:
            c.request("no/such/method")
        self.assertEqual(e.exception.code, -32601)
        self.assertEqual(c.close(), 0)

    def test_exit_without_shutdown_is_an_error(self):
        c = Client(BINARY)
        c.initialize()
        c.notify("exit")
        self.assertEqual(c.proc.wait(timeout=10), 1)
        c.close()

    def test_end_of_input_stops_the_server(self):
        c = Client(BINARY)
        c.initialize()
        c.proc.stdin.close()
        self.assertEqual(c.proc.wait(timeout=10), 1)
        c.close()

    def test_garbage_gets_a_parse_error(self):
        c = Client(BINARY)
        c.initialize()
        c.send_raw(b"Content-Length: 5\r\n\r\n{nope")
        self.assertEqual(c.request("shutdown"), None)
        c.notify("exit")
        self.assertEqual(c.proc.wait(timeout=10), 0)
        c.close()


class Diagnostics(Lsp_test):
    def test_errors_follow_edits(self):
        c = self.client
        c.initialize()
        c.open(URI, "SELECT 1;\nSELECT FROM t;\nSELECT 3;\n")
        diags = c.diagnostics(URI, 1)
        self.assertEqual(len(diags), 1)
        d = diags[0]
        self.assertEqual(d["range"]["start"], {"line": 1, "character": 7})
        self.assertEqual(d["range"]["end"], {"line": 1, "character": 11})
        self.assertTrue(d["message"].startswith("syntax error near 'FROM'"), d["message"])
        self.assertEqual(d["severity"], 1)

        # Fix it: SELECT a FROM t.
        c.change(URI, 2, [{"range": {"start": {"line": 1, "character": 7},
                                     "end": {"line": 1, "character": 7}}, "text": "a "}])
        self.assertEqual(c.diagnostics(URI, 2), [])

        # Break the first statement; the third keeps its verdict.
        c.change(URI, 3, [{"range": {"start": {"line": 0, "character": 0},
                                     "end": {"line": 0, "character": 6}}, "text": "SELEC"}])
        diags = c.diagnostics(URI, 3)
        self.assertEqual([x["range"]["start"]["line"] for x in diags], [0])

        # Replace everything.
        c.change(URI, 4, [{"text": "SELECT 'open"}])
        diags = c.diagnostics(URI, 4)
        self.assertTrue(any("unterminated single-quoted string" in x["message"] for x in diags),
                        diags)

    def test_document_context_sets_the_sql_mode(self):
        c = self.client
        c.initialize()
        c.open(URI, 'CREATE TABLE "t" (a INT);\n')
        self.assertEqual(len(c.diagnostics(URI, 1)), 1)
        mark = c.mark()
        c.notify("mariadb/documentContext",
                 {"textDocument": {"uri": URI}, "sqlMode": "ANSI_QUOTES"})
        self.assertEqual(c.diagnostics(URI, after=mark), [])

    def test_unsupported_sql_mode_is_noted(self):
        c = self.client
        c.initialize({"sqlMode": "ORACLE"})
        c.open(URI, "SELECT 1;\n")
        diags = c.diagnostics(URI, 1)
        self.assertEqual(len(diags), 1)
        self.assertIn("sql_mode ORACLE is not supported", diags[0]["message"])
        self.assertEqual(diags[0]["severity"], 3)

    def test_close_clears_diagnostics(self):
        c = self.client
        c.initialize()
        c.open(URI, "SELEC 1;")
        self.assertEqual(len(c.diagnostics(URI, 1)), 1)
        mark = c.mark()
        c.notify("textDocument/didClose", {"textDocument": {"uri": URI}})
        self.assertEqual(c.diagnostics(URI, after=mark), [])

    def test_a_new_delimiter_reaches_open_documents(self):
        c = self.client
        c.initialize()
        own = "file:///own.sql"
        c.open(URI, "SELECT 1$$\nSELECT 2$$\n")
        self.assertNotEqual(c.diagnostics(URI, 1), [])
        # A document whose delimiter the client set keeps it.
        c.open(own, "SELECT 1;\n")
        c.notify("mariadb/documentContext", {"textDocument": {"uri": own}, "delimiter": ";"})
        c.diagnostics(own, 1)
        mark = c.mark()
        c.notify("workspace/didChangeConfiguration", {"settings": {"mariadb": {"delimiter": "$$"}}})
        c.wait_notification("textDocument/publishDiagnostics",
                            lambda p: p["uri"] == URI and p["diagnostics"] == [], 30, mark)
        mark = c.mark()
        c.change(own, 2, [{"text": "SELECT 1;\nSELECT 2;\n"}])
        self.assertEqual(c.diagnostics(own, 2, after=mark), [])

    def test_reopening_a_document_ends_its_progress(self):
        c = self.client
        c.initialize({"progressThreshold": 1}, capabilities={"window": {"workDoneProgress": True}})
        text = SAKILA * 100
        c.open(URI, text)
        c.wait_notification("$/progress", lambda p: p["value"]["kind"] == "begin")
        c.open(URI, text, version=2)
        c.wait_notification("mariadb/parseProgress", lambda p: p["version"] == 2 and p["done"], 120)
        with c.cond:
            begun = [m["params"]["token"] for _, m in c.notifications
                     if m["method"] == "$/progress" and m["params"]["value"]["kind"] == "begin"]
        self.assertEqual(len(begun), 2)
        for token in begun:
            c.wait_notification("$/progress",
                                lambda p, t=token: p["token"] == t and p["value"]["kind"] == "end")

    def test_many_quick_edits(self):
        c = self.client
        c.initialize({"debounceMs": 20})
        text = "".join(f"SELECT {i};\n" for i in range(200))
        c.open(URI, text)
        c.diagnostics(URI, 1)
        # Type "SELECT x FROM" at the end, one character per change.
        line = 200
        version = 1
        for ch in "SELECT x FROM":
            version += 1
            c.change(URI, version, [{"range": {"start": {"line": line, "character": version - 2},
                                               "end": {"line": line, "character": version - 2}},
                                     "text": ch}])
        diags = c.diagnostics(URI, version)
        self.assertEqual(len(diags), 1)
        self.assertIn("end of statement", diags[0]["message"])


SAKILA = (FIXTURES / "sakila-schema.sql").read_text()


class Features(Lsp_test):
    def test_document_symbols(self):
        c = self.client
        c.initialize()
        c.open(URI, SAKILA)
        symbols = c.request("textDocument/documentSymbol", {"textDocument": {"uri": URI}})
        self.assertEqual([s["name"] for s in symbols], ["sakila"])
        tables = {s["name"]: s for s in symbols[0]["children"]}
        self.assertIn("actor", tables)
        actor = tables["actor"]
        self.assertEqual(actor["kind"], 5)
        cols = [x["name"] for x in actor["children"] if x["kind"] == 8]
        self.assertEqual(cols, ["actor_id", "first_name", "last_name", "last_update"])
        line = SAKILA.split("\n")[actor["selectionRange"]["start"]["line"]]
        self.assertIn("CREATE TABLE actor", line)

    def test_outline_matches_the_cli(self):
        c = self.client
        c.initialize()
        c.open(URI, SAKILA)
        got = c.request("mariadb/outline", {"textDocument": {"uri": URI}})
        cli = json.loads(subprocess.run([BINARY, "outline", str(FIXTURES / "sakila-schema.sql")],
                                        capture_output=True, check=True).stdout)
        self.assertEqual(got["file"], URI)
        got.pop("file")
        cli.pop("file")
        self.assertEqual(got, cli)

    def test_folding_ranges(self):
        c = self.client
        c.initialize({"delimiter": "$$"})
        text = ("-- a\n-- b\nCREATE PROCEDURE p()\nBEGIN\n  IF 1 THEN\n    SELECT 1;\n"
                "  END IF;\nEND$$\nSELECT 1$$\n")
        c.open(URI, text)
        got = c.request("textDocument/foldingRange", {"textDocument": {"uri": URI}})
        got = sorted((r["startLine"], r["endLine"], r.get("kind")) for r in got)
        self.assertEqual(got, [(0, 1, "comment"), (2, 7, None), (3, 6, None), (4, 5, None)])

    def test_selection_ranges(self):
        c = self.client
        c.initialize()
        text = "SELECT 1;\nSELECT a + b FROM t WHERE x = 1;\n"
        c.open(URI, text)
        got = c.request("textDocument/selectionRange", {
            "textDocument": {"uri": URI}, "positions": [{"line": 1, "character": 11}]})
        self.assertEqual(len(got), 1)
        lines = text.split("\n")
        chain = []
        r = got[0]
        while r:
            a, b = r["range"]["start"], r["range"]["end"]
            if a["line"] == b["line"]:
                chain.append(lines[a["line"]][a["character"]:b["character"]])
            else:
                chain.append((a["line"], b["line"]))
            r = r.get("parent")
        self.assertEqual(chain[0], "b")
        self.assertIn("a + b", chain)
        self.assertIn("SELECT a + b FROM t WHERE x = 1", chain)
        self.assertEqual(chain[-1], (0, 2))

    def test_semantic_tokens_full_delta_range(self):
        c = self.client
        c.initialize()
        text = "SELECT 'a', 1 -- c\nFROM t;\n"
        c.open(URI, text)
        full = c.request("textDocument/semanticTokens/full", {"textDocument": {"uri": URI}})
        self.assertEqual(decode_tokens(text, full["data"]), [
            (0, 0, "SELECT", "keyword"), (0, 7, "'a'", "string"), (0, 12, "1", "number"),
            (0, 14, "-- c", "comment"), (1, 0, "FROM", "keyword")])

        c.change(URI, 2, [{"range": {"start": {"line": 0, "character": 12},
                                     "end": {"line": 0, "character": 13}}, "text": "@v"}])
        text2 = "SELECT 'a', @v -- c\nFROM t;\n"
        delta = c.request("textDocument/semanticTokens/full/delta",
                          {"textDocument": {"uri": URI}, "previousResultId": full["resultId"]})
        self.assertIn("edits", delta)
        data = list(full["data"])
        for e in delta["edits"]:
            data[e["start"]:e["start"] + e["deleteCount"]] = e.get("data", [])
        self.assertEqual(decode_tokens(text2, data)[2], (0, 12, "@v", "variable"))

        rng = c.request("textDocument/semanticTokens/range", {
            "textDocument": {"uri": URI},
            "range": {"start": {"line": 1, "character": 0}, "end": {"line": 1, "character": 4}}})
        self.assertIn((1, 0, "FROM", "keyword"), decode_tokens(text2, rng["data"]))

    def test_cancel(self):
        c = self.client
        c.initialize()
        c.open(URI, SAKILA)
        rid = c.request_async("textDocument/semanticTokens/full", {"textDocument": {"uri": URI}})
        c.notify("$/cancelRequest", {"id": rid})
        try:
            c.wait_response(rid)
        except Lsp_error as e:
            self.assertEqual(e.code, -32800)


class Large_file(Lsp_test):
    """A large dump: tokens for a range come before the full check is done,
    and an edit in the middle gets its diagnostics quickly."""

    SIZE_MB = float(os.environ.get("MDBLS_E2E_LARGE_MB", "64"))

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        path = pathlib.Path(cls.tmp.name) / "dump.sql"
        subprocess.run([sys.executable, str(ROOT / "scripts" / "gen_fixture_dump.py"),
                        str(cls.SIZE_MB), str(path)], check=True, capture_output=True)
        cls.text = path.read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_range_tokens_before_the_check_finishes(self):
        c = self.client
        c.initialize({"debounceMs": 0},
                     capabilities={"window": {"workDoneProgress": True}})
        lines = self.text.split("\n")
        middle = len(lines) // 2
        t0 = time.monotonic()
        c.open(URI, self.text)
        rid = c.request_async("textDocument/semanticTokens/range", {
            "textDocument": {"uri": URI},
            "range": {"start": {"line": middle, "character": 0},
                      "end": {"line": middle + 100, "character": 0}}})
        result = c.wait_response(rid, timeout=60)
        answered, answered_at = c.last_response_seq, time.monotonic()
        self.assertGreater(len(result["data"]), 0)
        done, done_seq = c.wait_notification("mariadb/parseProgress", lambda p: p["done"],
                                             timeout=120)
        finished = time.monotonic()
        self.assertEqual(done["checked"], done["total"])
        print(f"\n  {self.SIZE_MB:g} MB: range tokens after {answered_at - t0:.3f}s, "
              f"check done after {finished - t0:.3f}s (from sending didOpen)", file=sys.stderr)
        if self.SIZE_MB >= 64:
            self.assertLess(answered, done_seq)
        # workDoneProgress was offered, created, begun and ended.
        self.assertTrue(any(r["method"] == "window/workDoneProgress/create"
                            for r in c.server_requests))
        kinds = [m["params"]["value"]["kind"] for _, m in c.notifications
                 if m["method"] == "$/progress"]
        self.assertEqual(kinds[0], "begin")
        self.assertEqual(kinds[-1], "end")

    def test_edit_in_the_middle(self):
        c = self.client
        c.initialize({"debounceMs": 0})
        c.open(URI, self.text)
        c.wait_notification("mariadb/parseProgress", lambda p: p["done"], timeout=120)
        c.diagnostics(URI, 1)
        lines = self.text.split("\n")
        middle = next(i for i in range(len(lines) // 2, len(lines))
                      if lines[i].startswith("INSERT INTO"))
        times = []
        for k in range(10):
            version = 2 + 2 * k
            mark = c.mark()
            t0 = time.monotonic()
            # Break the INSERT ("INSERT INTO" -> "INSERT INT O"), then repair it.
            c.change(URI, version, [{"range": {"start": {"line": middle, "character": 10},
                                               "end": {"line": middle, "character": 10}},
                                     "text": " "}])
            diags = c.diagnostics(URI, version, after=mark)
            times.append(time.monotonic() - t0)
            self.assertEqual([d["range"]["start"]["line"] for d in diags], [middle])
            mark = c.mark()
            c.change(URI, version + 1, [{"range": {"start": {"line": middle, "character": 10},
                                                   "end": {"line": middle, "character": 11}},
                                         "text": ""}])
            self.assertEqual(c.diagnostics(URI, version + 1, after=mark), [])
        times.sort()
        print(f"\n  {self.SIZE_MB:g} MB: edit to diagnostics median {times[len(times) // 2] * 1000:.1f} ms, "
              f"max {times[-1] * 1000:.1f} ms", file=sys.stderr)


if __name__ == "__main__":
    unittest.main()
