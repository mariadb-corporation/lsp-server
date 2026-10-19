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

"""The SQL the server runs through the VS Code extension is on its allow-list.

In MCP mode (`lsp --mcp`) the server asks the client for `db.*` tool calls
(`mariadb/dbCall`). The VS Code extension runs `db.execute_sql` only for the
statements in `ALLOWED_SQL` (`src/lsp/dbCall.ts` in vscode-extension), and
refuses anything else for good: a statement changed or added here without the
same change there makes every catalog reading through the extension fail.
This test reads the statements from the sources, so it needs no binary.

The extension's file is read from $VSCODE_EXTENSION_DIR, else from a sibling
checkout ../vscode-extension, else downloaded from GitHub at
$VSCODE_EXTENSION_REF (default main). The comparison is skipped when none of
them has an ALLOWED_SQL."""

import os
import pathlib
import re
import unittest
import urllib.error
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE_DIRS = [ROOT / "libs", ROOT / "src"]
SOURCE_SUFFIXES = {".cc", ".cpp", ".h", ".hpp", ".inc"}
DBCALL = pathlib.Path("src") / "lsp" / "dbCall.ts"
RAW_URL = "https://raw.githubusercontent.com/mariadb-corporation/vscode-extension/{ref}/src/lsp/dbCall.ts"
WHAT_TO_DO = ("add it to ALLOWED_SQL in vscode-extension's src/lsp/dbCall.ts, in a release "
              "no later than the server's, or the extension refuses it for good")


def normalized(sql):
    """The extension's comparison form (dbCall.ts `normalized`): runs of
    blanks one space, the ends and a trailing ';' dropped."""
    return re.sub(r"\s*;$", "", re.sub(r"\s+", " ", sql).strip())


# ---- C++

CPP_TOKEN = re.compile(r'''
    (?P<line_comment>//[^\n]*)
  | (?P<block_comment>/\*.*?\*/)
  | (?P<raw>(?:u8|[uUL])?R"(?P<delim>[^()\\\s]{0,16})\((?P<raw_body>.*?)\)(?P=delim)")
  | (?P<string>(?:u8|[uUL])?"(?P<body>(?:[^"\\\n]|\\.)*)")
  | (?P<char>'(?:[^'\\\n]|\\.)*')
''', re.S | re.X)

CPP_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'",
               "?": "?", "a": "\a", "b": "\b", "f": "\f", "v": "\v"}


def unescape_cpp(body):
    return re.sub(r"\\(.)", lambda m: CPP_ESCAPES.get(m.group(1), m.group(1)), body)


def cpp_tokens(code):
    """The C++ text with comments blanked out, and its string literals as
    (start, end, value), in order."""
    out, literals, pos = [], [], 0
    for m in CPP_TOKEN.finditer(code):
        out.append(code[pos:m.start()])
        if m.group("line_comment") or m.group("block_comment"):
            # Blanks keep offsets; line breaks keep line numbers.
            out.append(re.sub(r"[^\n]", " ", m.group(0)))
        else:
            out.append(m.group(0))
            if m.group("raw") is not None:
                literals.append((m.start(), m.end(), m.group("raw_body")))
            elif m.group("string") is not None:
                literals.append((m.start(), m.end(), unescape_cpp(m.group("body"))))
        pos = m.end()
    out.append(code[pos:])
    return "".join(out), literals


def literal_run(code, literals, at):
    """The value of the adjacent string literals starting at `at` (blanks
    between them only), and where they end; None when none starts there."""
    starting = {start: (end, value) for start, end, value in literals}
    pos = at
    while pos < len(code) and code[pos].isspace():
        pos += 1
    if pos not in starting:
        return None
    parts = []
    while pos in starting:
        end, value = starting[pos]
        parts.append(value)
        pos = end
        while pos < len(code) and code[pos].isspace():
            pos += 1
    return "".join(parts), pos


def statement_end(code, literals, at):
    """Where the statement holding offset `at` ends: its ';' at nesting depth
    0, string literals skipped."""
    inside = {start: end for start, end, _ in literals}
    depth, pos = 0, at
    while pos < len(code):
        if pos in inside:
            pos = inside[pos]
            continue
        c = code[pos]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == ";" and depth <= 0:
            return pos
        pos += 1
    return len(code)


def constant_definition(name, files):
    """The string an identifier is defined as (`... name = "a" "b";` or
    `name{"..."}`), searched in `files` ({path: (code, literals)}); None."""
    pattern = re.compile(r"\b" + re.escape(name) + r"\s*(?:=|\{|\()")
    for path, (code, literals) in files.items():
        for m in pattern.finditer(code):
            run = literal_run(code, literals, m.end())
            if run is not None:
                return run[0], path
    return None


def execute_sql_calls(files):
    """Every db.execute_sql call in `files` ({path: text}): a list of
    (path, line, sql or None, how it was found or why not)."""
    parsed = {path: cpp_tokens(text) for path, text in files.items()}
    calls = []
    for path, (code, literals) in parsed.items():
        for start, end, value in literals:
            if value != "db.execute_sql":
                continue
            line = code.count("\n", 0, start) + 1
            stop = statement_end(code, literals, end)
            # The "sql" argument: {"sql", <expr>}.
            key = next((e for s, e, v in literals if end <= s < stop and v == "sql"), None)
            if key is None:
                calls.append((path, line, None, "no \"sql\" argument found in the call"))
                continue
            comma = code.find(",", key, stop)
            if comma < 0:
                calls.append((path, line, None, "no value after \"sql\""))
                continue
            run = literal_run(code, literals, comma + 1)
            if run is not None and code[run[1]] in "})":
                calls.append((path, line, run[0], "a literal"))
                continue
            ident = re.match(r"\s*([A-Za-z_]\w*)\s*[})]", code[comma + 1:stop])
            if ident is not None:
                found = constant_definition(ident.group(1), parsed)
                if found is not None:
                    calls.append((path, line, found[0], f"the constant {ident.group(1)}"))
                    continue
            expr = " ".join(code[comma + 1:stop].split())[:80]
            calls.append((path, line, None, f"the SQL is not a string constant: {expr}"))
    return calls


def server_sources():
    files = {}
    for d in SOURCE_DIRS:
        for p in sorted(d.rglob("*")):
            if p.suffix in SOURCE_SUFFIXES and p.is_file() and "generated" not in p.parts:
                text = p.read_text(encoding="utf-8", errors="replace")
                if "db.execute_sql" in text:
                    files[str(p.relative_to(ROOT))] = text
    return files


# ---- TypeScript

TS_TOKEN = re.compile(r'''
    (?P<comment>//[^\n]*|/\*.*?\*/)
  | (?P<string>"(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*'|`(?:[^`\\]|\\.)*`)
  | (?P<punct>[+,\])])
  | (?P<other>\S)
''', re.S | re.X)

TS_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "0": "\0", "\\": "\\", '"': '"', "'": "'",
              "`": "`", "b": "\b", "f": "\f", "v": "\v"}


def allowed_sql(ts):
    """The statements of ALLOWED_SQL in dbCall.ts: each entry one string
    literal, or literals joined with '+'. None when there is no list."""
    at = re.search(r"\bALLOWED_SQL\b[^=]*=\s*new\s+Set\s*\(\s*\[", ts)
    if at is None:
        return None
    entries, current = [], []
    for m in TS_TOKEN.finditer(ts, at.end()):
        if m.group("comment"):
            continue
        if m.group("string"):
            body = m.group("string")[1:-1]
            if m.group("string")[0] == "`" and "${" in body:
                raise ValueError(f"ALLOWED_SQL has a template with a substitution: {body!r}")
            current.append(re.sub(r"\\(.)", lambda e: TS_ESCAPES.get(e.group(1), e.group(1)),
                                  body))
        elif m.group("punct") == "+":
            continue
        elif m.group("punct") in (",", "]"):
            if current:
                entries.append("".join(current))
                current = []
            if m.group("punct") == "]":
                return entries
        else:
            raise ValueError(f"ALLOWED_SQL has an entry that is not a string: {m.group(0)!r}")
    return entries


def extension_dbcall():
    """(dbCall.ts text or None, the sources tried)."""
    tried = []
    candidates = []
    if os.environ.get("VSCODE_EXTENSION_DIR"):
        candidates.append(pathlib.Path(os.environ["VSCODE_EXTENSION_DIR"]))
    candidates.append(ROOT.parent / "vscode-extension")
    for d in candidates:
        p = d / DBCALL
        tried.append(str(p))
        if p.is_file():
            return p.read_text(encoding="utf-8"), str(p), tried
    url = RAW_URL.format(ref=os.environ.get("VSCODE_EXTENSION_REF", "main"))
    tried.append(url)
    try:
        with urllib.request.urlopen(url, timeout=15) as r:
            return r.read().decode("utf-8"), url, tried
    except (urllib.error.URLError, OSError) as e:
        tried[-1] += f" ({e})"
        return None, None, tried


class Allowed_sql(unittest.TestCase):
    def test_every_db_execute_sql_runs_a_constant(self):
        calls = execute_sql_calls(server_sources())
        self.assertTrue(calls, "no db.execute_sql call found in libs/ or src/: if the server "
                               "still runs SQL through the client, this test no longer finds it")
        for path, line, sql, how in calls:
            self.assertIsNotNone(sql, f"{path}:{line}: {how}. A statement built at run time "
                                      "cannot be on the extension's ALLOWED_SQL: make it a "
                                      "constant")

    def test_server_sql_is_on_the_extensions_allow_list(self):
        calls = [c for c in execute_sql_calls(server_sources()) if c[2] is not None]
        ts, source, tried = extension_dbcall()
        if ts is None:
            self.skipTest("vscode-extension's src/lsp/dbCall.ts not found; tried "
                          + "; ".join(tried))
        allowed = allowed_sql(ts)
        if allowed is None:
            self.skipTest(f"no ALLOWED_SQL in {source} (a ref from before it existed?)")
        allowed = {normalized(s) for s in allowed}
        for path, line, sql, how in calls:
            with self.subTest(file=path, line=line):
                self.assertIn(normalized(sql), allowed,
                              f"{path}:{line} runs, through db.execute_sql ({how}):\n  {sql}\n"
                              f"which is not in ALLOWED_SQL of {source}: {WHAT_TO_DO}")

    def test_the_readers(self):
        # Adjacent literals across lines, a comment between, an escape.
        files = {"a.cc": 'const char *const k_q =\n    "SELECT 1, "  // one\n'
                         '    "\\"x\\"";\n'
                         'void f() { r.run("db.execute_sql", arguments({{"sql", k_q}}), &d); }\n'
                         'void g() { r.run("db.execute_sql", arguments({{"sql", R"(SELECT 2)"}})); }\n'
                         'void h() { r.run("db.execute_sql", arguments({{"sql", make(q)}})); }\n'
                         '// r.run("db.execute_sql", arguments({{"sql", "commented out"}}));\n'
                         'void i() { r.run("db.execute_sql_script", arguments({{"sql", x}})); }\n'}
        calls = execute_sql_calls(files)
        self.assertEqual([(c[1], c[2]) for c in calls],
                         [(4, 'SELECT 1, "x"'), (5, "SELECT 2"), (6, None)])
        self.assertIn("not a string constant: make(q)", calls[2][3])

        ts = ('export const ALLOWED_SQL: ReadonlySet<string> = new Set([\n'
              '    "SELECT 1",  // a comment, with a ]\n'
              "    'SELECT ' + \"2\",\n"
              '    `SELECT 3;`,\n'
              ']);\n')
        self.assertEqual(allowed_sql(ts), ["SELECT 1", "SELECT 2", "SELECT 3;"])
        self.assertIsNone(allowed_sql("export const OTHER = 1;"))
        self.assertEqual(normalized("  SELECT\n  1 ;  "), "SELECT 1")


if __name__ == "__main__":
    unittest.main()
