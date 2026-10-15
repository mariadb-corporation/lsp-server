#!/usr/bin/env python3
"""tools/build_record.py -- evidence/P2/build.json.

Called by tools/extract.sh's compile stage (and by nothing else) with its
inputs in the environment.  It is a module rather than another heredoc inside
extract.sh so that `binary_links_only_libc` -- the P2 convergence secondary,
i.e. "the tokens-in binary is self-contained" -- is a function a test can feed
a doctored `ldd` output to, instead of a claim only a real build can check.

A sibling task (3.2) reads this file for P2's convergence secondary: do not
rename these keys.

Environment: MDBP_OUT, MDBP_GCC_VERSION, MDBP_GXX_VERSION,
MDBP_GXX_WARNINGS, MDBP_TABLE_SUMMARY (gen_tables.py's own JSON line),
MDBP_STARTED_NS, MDBP_FINISHED_NS, MDBP_LIB_SONAME, MDBP_LIB_REALNAME,
GIT_SHA.

PLAN.md P4.1 deliverable 5: `MDBP_LIB_SONAME`/`MDBP_LIB_REALNAME` are the
soname and the real name `extract.sh`'s own `mdbp_header_version()` computed
from `include/mdbparse.h` -- the SAME two strings it passed to `-Wl,-soname` and
`-o` a moment earlier.  They used to be four hardcoded
`libmariadbparser.so.1[.0.0]` literals here, correct only while
`MDBP_VERSION` was 1.0.0; at the 1 -> 2 bump all four became wrong at once,
which is why they are now read rather than typed.

Task 8.2 added the `lexer` block: the C++ translation unit that carries the
sliced lexer is compiled separately (different YYSTYPE, different language),
so its argv, its warning count and its own -Wno- table are recorded beside the
C one rather than folded into it.  `binary_links_only_libc` is now measured of
a binary that DOES carry the lexer; see README.md for the restatement.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402

#: An `ldd` line resolves to one of these iff the binary is self-contained:
#: the C library, the dynamic loader, or the kernel's vDSO.
LIBC_PREFIXES = ("libc.so", "libc-", "ld-linux", "ld.so", "linux-vdso", "linux-gate")


def ldd_lines(text: str) -> list[str]:
    return [line.strip() for line in text.splitlines() if line.strip()]


def _soname(line: str) -> str:
    """The library name an `ldd` line is about: the part before ` => `, or the
    whole line when there is none (`statically linked`, a bare path)."""
    name = line.split(" => ", 1)[0].strip()
    if name.startswith("/"):
        name = name.rsplit("/", 1)[-1]
    return name.split(" (")[0].strip()


def links_only_libc(lines: list[str]) -> bool:
    """True iff every `ldd` line resolves to libc, the dynamic loader or the
    vDSO -- and nothing else.  A statically linked binary (no lines but the
    `statically linked` note) counts as self-contained too."""
    for line in lines:
        if line.startswith("statically linked"):
            continue
        name = _soname(line)
        if not any(name.startswith(prefix) for prefix in LIBC_PREFIXES):
            return False
    return True


def _read_argv(path: Path) -> list[str]:
    """An argv file's lines as the record carries them: every absolute path
    through common.display_arg(), so no path of this machine is recorded."""
    return [common.display_argv_line(line) for line in _read_lines(path)]


def _read_lines(path: Path) -> list[str]:
    if not path.is_file():
        return []
    return [line for line in path.read_text().splitlines() if line.strip()]


def _library_facts(out: Path, soname: str, realname: str) -> dict | None:
    """Task 9.2's library block: the static archive, the shared object, its
    soname and dev symlinks, and the same `links_only_libc` measurement
    `binary_links_only_libc` is, taken of the .so instead of the CLI.

    `None` when this run built no library at all -- a src/ tree from before
    task 9.2, or a `--no-compile` run -- rather than a half-filled dict a
    reader could mistake for a real measurement.

    `soname`/`realname` come from the caller (extract.sh's
    mdbp_header_version(), through the environment) -- never a literal here,
    see this module's docstring."""
    if not soname or not realname:
        # THIS RUN built no library, so extract.sh had no name to pass: a src/
        # tree that predates task 9.1 has no src/mdbp_api.c to split out and,
        # before that, no include/mdbparse.h to read a version from. Deciding it
        # from the names rather than from whether libmariadbparser.a happens
        # to be on disk matters, because `--out` may point at a COPY of a real
        # build (tests/test_head1_vs_head_cli_blackbox.py's scratch tree does
        # exactly that): the archive is then someone else's, and describing it
        # here would put another build's sha256 in this build's record.
        return None
    static_path = out / "libmariadbparser.a"
    if not static_path.is_file():
        return None
    shared_real = out / realname
    shared_soname = out / soname
    shared_dev = out / "libmariadbparser.so"
    pc = out / "mariadbparser.pc"

    def _link_target(path: Path) -> str | None:
        return os.readlink(path) if path.is_symlink() else None

    lib_ldd = ldd_lines(
        (out / "ldd-lib.txt").read_text() if (out / "ldd-lib.txt").is_file() else "")

    return {
        "map_file": "src/libmariadbparser.map",
        "objects_argv": _read_argv(out / "gcc-lib.argv"),
        "ar_argv": _read_argv(out / "ar.argv"),
        "shared_argv": _read_argv(out / "gcc-shared.argv"),
        "static": {
            "path": common.display_path(static_path),
            "sha256": common.sha256_file(static_path),
            "size": static_path.stat().st_size,
        },
        "shared": {
            "real_name": {
                "path": common.display_path(shared_real),
                "sha256": common.sha256_file(shared_real),
                "size": shared_real.stat().st_size,
            } if shared_real.is_file() else None,
            "soname": soname,
            "soname_symlink_target": _link_target(shared_soname),
            "dev_symlink_target": _link_target(shared_dev),
        },
        "ldd": lib_ldd,
        "links_only_libc": links_only_libc(lib_ldd),
        "pkgconfig": {
            "path": common.display_path(pc),
            "sha256": common.sha256_file(pc),
            "size": pc.stat().st_size,
        } if pc.is_file() else None,
    }


def main() -> int:
    out = Path(os.environ["MDBP_OUT"])
    binary = out / "mdbparse"
    lexer_object = out / "mdbp_lex.o"

    gcc_stderr = (out / "gcc.stderr").read_text() if (out / "gcc.stderr").is_file() else ""
    warnings = [common.display_text(line)
                for line in gcc_stderr.splitlines() if "warning:" in line]

    flags = _read_lines(out / "wno.flags")
    reasons = _read_lines(out / "wno.reasons")
    wno = [
        {"flag": flag, "reason": reasons[i] if i < len(reasons) else ""}
        for i, flag in enumerate(flags)
    ]

    gxx_stderr = (out / "gxx.stderr").read_text() if (out / "gxx.stderr").is_file() else ""
    gxx_warnings = [common.display_text(line)
                    for line in gxx_stderr.splitlines() if "warning:" in line]
    cxx_flags = _read_lines(out / "cxx-wno.flags")
    cxx_reasons = _read_lines(out / "cxx-wno.reasons")
    cxx_wno = [
        {"flag": flag, "reason": cxx_reasons[i] if i < len(cxx_reasons) else ""}
        for i, flag in enumerate(cxx_flags)
    ]

    lines = ldd_lines((out / "ldd.txt").read_text() if (out / "ldd.txt").is_file() else "")
    tables = json.loads(os.environ["MDBP_TABLE_SUMMARY"])
    started = int(os.environ["MDBP_STARTED_NS"])
    finished = int(os.environ["MDBP_FINISHED_NS"])

    # `.get`, not `[...]`: a src/ tree that predates task 9.1 builds no
    # library, so extract.sh has no name to pass and `_library_facts` returns
    # None before it would look at the archive at all. Absent names mean "no
    # library block", full stop -- extract.sh exports both or neither, and
    # they are exported from the same header read that decides what the
    # library is CALLED, so a real build cannot reach here without them.
    library = _library_facts(out, os.environ.get("MDBP_LIB_SONAME", ""),
                             os.environ.get("MDBP_LIB_REALNAME", ""))

    record = {
        "host": common.record_host(),
        "utc": common.utc_now(),
        "git_sha": os.environ.get("GIT_SHA", "uncommitted"),
        "gcc_version": os.environ["MDBP_GCC_VERSION"],
        "argv": _read_argv(out / "gcc.argv"),
        "warnings": warnings,
        "build_warnings": len(warnings),
        "wno_flags": wno,
        "binary": {
            "sha256": common.sha256_file(binary),
            "size": binary.stat().st_size,
        },
        "ldd": lines,
        "binary_links_only_libc": links_only_libc(lines),
        "tables": {"terminals": tables["terminals"], "serials": tables["serials"]},
        # The lexer's own translation unit (task 8.2, P5b).  It is compiled
        # WITHOUT -Werror and WITHOUT any -Wno- flag, both on purpose: the
        # slice carries MariaDB's own UNINIT_VAR(c) in lex_one_token, and the
        # only flag that silences it (-Wno-uninitialized) would also silence
        # our own src/mdbp_lex.cc, which shares this translation unit.  So the
        # warning is REPORTED here instead, and `lexer_warnings` is the number
        # a reviewer checks.
        "lexer": {
            "gxx_version": os.environ.get("MDBP_GXX_VERSION", ""),
            "argv": _read_argv(out / "gxx.argv"),
            "warnings": gxx_warnings,
            "lexer_warnings": len(gxx_warnings),
            "wno_flags": cxx_wno,
            "object": {
                "sha256": common.sha256_file(lexer_object),
                "size": lexer_object.stat().st_size,
            } if lexer_object.is_file() else None,
            "undefined_symbols": _read_lines(out / "lexer.undefined"),
        },
        # How error.state and error.expected_sample are reached, recorded
        # because PLAN.md task 3.1 requires the route (or the shortfall) to be
        # in this file rather than in someone's memory.
        "introspection": {
            "route": "glue-tu",
            "detail": (
                "src/parser_glue.c #includes build/parser.c, so bison's static "
                "yypact/yycheck/yytable are in scope, and intercepts the "
                "yyerror CALL SITE inside yyparse with a function-like macro "
                "so the hook also receives *yyssp, the state on top of the "
                "parser's own stack. No change to the bison invocation, hence "
                "no risk to P1's automaton_differences == 0."
            ),
            "state": "available",
            "expected_sample": "available",
            # Plan `expected-set-from-lac.md` P1.1 replaced what this field
            # describes: the hook no longer walks the failing state's action
            # row, it asks bison's own yy_lac() per terminal kind. The two
            # answers are NOT the same set, and this string is the published
            # semantics of the CLI's `expected_sample`, so it says the new
            # contract -- pointing at the phase's own evidence for the
            # measured agreement/divergence counts rather than restating
            # numbers that move with the corpus.
            "expected_sample_semantics": (
                "every terminal that can still be SHIFTED from the parser "
                "stack as it stood when the error was reported, after any "
                "number of reductions (bison's own yy_lac() under `%define "
                "parse.lac full`), sorted by name. NOT the failing state's "
                "action row: at a state carrying a default reduction that row "
                "is a strict SUBSET of this set, so replay.py's "
                "Replay.expected(), which computes that row from the model, "
                "agrees only where the two coincide -- see "
                "evidence/P8/expected-after.json for the measured "
                "action-row-versus-LAC counts. null and [] are DIFFERENT "
                "answers: null means the set could not be computed (no "
                "automaton ran on the record, or the shiftability check ran "
                "out of memory -- a short set is never reported in its "
                "place); [] means computed, and nothing can be shifted here"
            ),
            "rejected_routes": [
                "%define parse.error custom: needs a different bison "
                "invocation, which drags P1's evidence set along with it"
            ],
        },
        # Task 9.2's library facts: the static archive and the shared object
        # `tools/extract.sh`'s compile stage now produces beside
        # build/mdbparse, from the SAME three objects (plus mdbp_lex.o) --
        # never a second compile of the same sources. `library` is None when
        # this run predates task 9.2 (a scratch --out from an old checkout);
        # nothing here re-derives what extract.sh already computed.
        "library": library,
        "wall_s": round((finished - started) / 1e9, 3),
    }

    if out.resolve() == (common.REPO_ROOT / "build").resolve():
        dest = common.REPO_ROOT / "evidence" / "P2" / "build.json"
    else:
        dest = out / "build.json"
    common.write_json(dest, record)
    print(f"wrote {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
