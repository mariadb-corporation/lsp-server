#!/usr/bin/env python3
"""tools/provenance.py — pins the MariaDB source tree this experiment
extracts from, into source/PROVENANCE.json.

The tree is `<mariadb-source>`, which `$MARIADB_SOURCE_DIR` names (unset:
exit 2 `MARIADB_SOURCE_DIR is not set`). It is only ever read: no `git`
command is run inside it and nothing is written into it. The record
spells the tree `<mariadb-source>`, never a path of this machine.
`other_tree` is the same placeholder: the record keeps its per-file
`identical: true/false`, which now compares the tree with itself.

Two modes:

  `provenance.py`          measure the five pinned files at $MARIADB_SOURCE_DIR now,
                            unconditionally (re-)write source/PROVENANCE.json.
  `provenance.py --check`  re-hash the same five files and compare against
                            what is currently recorded in
                            source/PROVENANCE.json. Any drift (a missing
                            file, a changed sha256/size) is printed and the
                            script exits 3 — UNLESS the environment variable
                            PROVENANCE_UPDATE=1 is set, in which case it
                            re-pins (identical to plain `provenance.py`) and
                            exits 0. This fail-closed default is the
                            interface a later phase's drift drill exercises:
                            copy sql_yacc.yy into build/, mutate it, point
                            $MARIADB_SOURCE_DIR at that scratch copy, and confirm
                            `--check` refuses before PROVENANCE_UPDATE=1 is
                            set. PROVENANCE_UPDATE must arrive through the
                            *environment*, never through `$(...)`/`make`
                            text-splicing (see <remote-runner>'s own comment on
                            why: only a plain `$$VAR` environment read is
                            faithful to arbitrary values passed at the
                            command line).
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402

OTHER_TREE = "<mariadb-source>"

# Relative to the tree root ($MARIADB_SOURCE_DIR or OTHER_TREE).
#
# The last seven joined task 8.1 (P5b's lexer-generation stage of
# extract.sh): tools/lexslice.py already reads every one of them (cross-
# checked against evidence/P5a/spike.json's `source_files`, whose
# `pinned_in_provenance` was `false` for exactly these seven), so a drift in
# any of them now fails extract.sh's provenance check closed, the same
# guarantee the original five already had.
TRACKED_FILES = [
    "sql/sql_yacc.yy",
    "sql/sql_lex.cc",
    "sql/lex.h",
    "sql/gen_yy_files.cmake",
    "VERSION",
    "sql/sql_lex.h",
    "sql/lex_ident_cli.h",
    "sql/lex_symbol.h",
    "include/m_ctype.h",
    "strings/ctype-utf8.c",
    "strings/ctype-utf8.h",
    "mysys/charset.c",
    # Task 8.2 (P5b), the three files the RUNNING lexer's slice added:
    # strings/my_strtoll10.c (the version digits of a versioned comment,
    # sql_lex.cc:2474), strings/ctype-latin1.c (my_charset_latin1's ctype,
    # which my_strtoll10 reads through my_isspace) and
    # sql/share/charsets/Index.xml (the charset-introducer names
    # get_charset_by_csname resolves, sql_lex.cc:2992). The FOURTH input of
    # that stage -- the `strings/*.c` scan that finds the compiled-in charset
    # names -- is a glob, not a symbol, so it is not pinnable file by file
    # here; what pins it is that its result is recorded in build/lexslice.json
    # and compared byte for byte by `make regen-check`.
    "strings/my_strtoll10.c",
    "strings/ctype-latin1.c",
    "sql/share/charsets/Index.xml",
]


def mariadb_source_dir() -> Path:
    return common.mariadb_source_dir()


def count_lines(path: Path) -> int:
    total = 0
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            total += chunk.count(b"\n")
    return total


def parse_version(tree: Path) -> dict | None:
    """The tree's VERSION fields, or None when VERSION is not there.

    A missing pinned file is drift, reported as such by `--check`; it must
    never be a FileNotFoundError traceback, because `tools/extract.sh`
    depends on this script's fail-closed exit 3 to distinguish "the source
    tree moved" from "this script crashed"."""
    if not (tree / "VERSION").is_file():
        return None
    text = (tree / "VERSION").read_text()
    fields = {}
    for line in text.splitlines():
        if "=" not in line:
            continue
        key, _, value = line.partition("=")
        fields[key.strip()] = value.strip()
    major = int(fields["MYSQL_VERSION_MAJOR"])
    minor = int(fields["MYSQL_VERSION_MINOR"])
    patch = int(fields["MYSQL_VERSION_PATCH"])
    maturity = fields.get("SERVER_MATURITY", "")
    return {
        "major": major,
        "minor": minor,
        "patch": patch,
        "maturity": maturity,
        "id": major * 10000 + minor * 100 + patch,
    }


def measure(tree: str | Path) -> dict:
    tree_path = Path(tree)
    other_path = common.resolve_placeholder(OTHER_TREE)
    files = {}
    for rel in TRACKED_FILES:
        p = tree_path / rel
        if not p.is_file():
            # Recorded rather than raised: diff_files() below turns this into
            # an ordinary drift finding (printed, exit 3, still re-pinnable
            # with PROVENANCE_UPDATE=1).
            files[rel] = {
                "sha256": None,
                "size": None,
                "mtime": None,
                "lines": None,
                "identical": False,
                "missing": True,
            }
            continue
        sha = common.sha256_file(p)
        size = p.stat().st_size
        mtime = int(p.stat().st_mtime)
        lines = count_lines(p)
        other = other_path / rel
        identical = other.is_file() and common.sha256_file(other) == sha
        files[rel] = {
            "sha256": sha,
            "size": size,
            "mtime": mtime,
            "lines": lines,
            "identical": identical,
        }

    return {
        "tree": common.display_path(tree_path),
        "other_tree": OTHER_TREE,
        "version": parse_version(tree_path),
        "host": _hostname(),
        "utc": common.utc_now(),
        "files": files,
    }


def _hostname() -> str:
    return common.record_host()


def diff_files(recorded: dict, measured: dict) -> list[str]:
    """One message per file that drifted (missing from `recorded`, or
    sha256/size changed)."""
    problems = []
    for rel, m in measured.items():
        if m.get("missing"):
            problems.append(f"{rel}: missing from the measured source tree")
            continue
        r = recorded.get(rel)
        if r is None:
            problems.append(f"{rel}: missing from recorded source/PROVENANCE.json")
            continue
        if r.get("sha256") != m["sha256"]:
            problems.append(
                f"{rel}: sha256 mismatch recorded={r.get('sha256')} measured={m['sha256']}"
            )
        elif r.get("size") != m["size"]:
            problems.append(
                f"{rel}: size mismatch recorded={r.get('size')} measured={m['size']}"
            )
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="re-hash and compare against the recorded source/PROVENANCE.json; "
        "exit 3 on drift unless PROVENANCE_UPDATE=1",
    )
    args = parser.parse_args()

    dest = common.REPO_ROOT / "source" / "PROVENANCE.json"
    measured = measure(mariadb_source_dir())

    if not args.check:
        common.write_json(dest, measured)
        print(f"wrote {dest}")
        return 0

    update = os.environ.get("PROVENANCE_UPDATE") == "1"

    if not dest.exists():
        print(f"missing: {dest}", file=sys.stderr)
        if update:
            common.write_json(dest, measured)
            print(f"PROVENANCE_UPDATE=1: wrote {dest}")
            return 0
        return 3

    import json

    recorded = json.loads(dest.read_text())
    problems = diff_files(recorded.get("files", {}), measured["files"])

    if problems:
        for line in problems:
            print(line, file=sys.stderr)
        if update:
            common.write_json(dest, measured)
            print(f"PROVENANCE_UPDATE=1: re-pinned {dest}")
            return 0
        return 3

    print(f"{dest}: no drift")
    return 0


if __name__ == "__main__":
    sys.exit(main())
