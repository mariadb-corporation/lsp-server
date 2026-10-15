#!/usr/bin/env python3
"""tools/lexslice.py -- slice MariaDB's lexer out of its sources by symbol.

Run by hand, once per full re-extraction (see UPSTREAM.md); the build never
runs it.

WHAT THIS IS
------------
MariaDB's lexer is `Lex_input_stream::lex_token` and its neighbours in
`sql/sql_lex.cc`.  They are not hand-transliterated: they are sliced out of
MariaDB's own sources **by symbol** and compiled against a small shim
(`src/lexshim.h`) plus bison's `parser.h` for the stub grammar, so a new
server version can be re-extracted instead of re-ported by hand.

BY SYMBOL, NEVER BY LINE
------------------------
Every location in this file is a *name*: a function name, a class name, an enum
name, an array name.  There is not one line number anywhere, so the same
symbols at different line numbers extract to the same text.  A slice keyed to
line numbers would break on the next MariaDB revision.

MECHANICAL REWRITES ONLY
------------------------
The slice does not change `sql_lex.cc`'s logic.  What is allowed is *scripted*
rewriting of the slice, and every such rewrite is a named rule in
`REWRITE_RULES` below, applied by this script and listed in the manifest.

OUTPUT (all under --out)
------------------------
  lexer_extracted.cc   the slice, one translation unit
  keywords.h           `sql/lex.h`'s keyword tables + `sql/lex_symbol.h`
  lexstates.h          `enum my_lex_states` from `include/m_ctype.h`
  state_maps_gen.cc    the generator of the two utf8mb4 lexer state maps
  state_maps.h         its output (with --compile-state-maps)
  lexslice.json        a debugging manifest: every symbol, its size, every rule

The committed copies of the first five headers/sources live in
libs/mariadbparser/generated/ (state_maps_gen.cc is not committed).

GENERATED TEXT IS FROZEN
------------------------
The banners, slice markers and comments this script writes INTO the generated
files (BANNER, the `isolate_wsrep` marker, the `#include "ctype-utf8.h"`
comment, the state-map generator's printf) are kept exactly as the committed
files carry them, including their references to `build/` and to the research
project the slice came from, so a re-extraction is byte-identical to
generated/.  Change them only together with the committed files.

Exit codes: 0 ok - 2 usage, or a missing/unlocatable input.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402

EXIT_OK, EXIT_USAGE = 0, 2


class LexSliceError(Exception):
    """A symbol could not be located, or was located more than once."""


# ---------------------------------------------------------------------------
# What to slice.  Names only -- these lists are reviewed as source, not as
# line ranges.
# ---------------------------------------------------------------------------

# Types the sliced code needs that are themselves MariaDB source.  Slicing them
# rather than re-typing them in the shim is the "extract, don't transliterate"
# principle applied to the declarations as well as to the bodies.
# `enum my_lex_states` gets its own generated header because BOTH translation
# units need it: the slice, and the state-map generator.
SLICE_LEX_STATES = ("include/m_ctype.h", "enum", "my_lex_states")

SLICE_TYPES: list[tuple[str, str, str]] = [
    # (source file, kind, symbol)
    ("sql/sql_lex.h", "enum", "enum_comment_state"),
    ("sql/lex_ident_cli.h", "struct", "Lex_string_with_metadata_st"),
    ("sql/lex_ident_cli.h", "struct", "Lex_ident_cli_st"),
    ("sql/sql_lex.h", "class", "Lex_input_stream"),
]

# The functions.  The first seven are the lexer proper; the rest are what the
# transitive closure forces (see `declared_not_sliced` in the manifest, which
# is the mechanical check that nothing is silently missing).  `lex_token` is
# on the list because `MYSQLlex` is a three-line wrapper and all five
# parsing_place feedback sites live in `lex_token`.
SLICE_FUNCTIONS: list[tuple[str | None, str]] = [
    ("Lex_input_stream", "lex_token"),
    ("Lex_input_stream", "lex_one_token"),
    ("Lex_input_stream", "find_keyword"),
    ("Lex_input_stream", "scan_ident_start"),
    ("Lex_input_stream", "scan_ident_middle"),
    ("Lex_input_stream", "scan_ident_common"),
    ("Lex_input_stream", "get_7bit_or_8bit_ident"),
    # transitive closure, all reached from lex_one_token:
    ("Lex_input_stream", "scan_ident_delimited"),
    ("Lex_input_stream", "find_keyword_qualified_special_func"),
    ("Lex_input_stream", "get_token"),
    ("Lex_input_stream", "consume_comment"),
    # The stream setup and the string-literal scanner.  `init`/`reset` are
    # what put a query into the stream at all (reset: sql_lex.cc:840);
    # `get_text` IS lexing -- it honours NO_BACKSLASH_ESCAPES and ''
    # doubling, and returning true from it is how an unterminated literal
    # becomes ABORT_SYM -- and `unescape` is the half of it that rewrites the
    # literal's bytes.  All four are MariaDB source, so they are sliced, not
    # shimmed.
    ("Lex_input_stream", "init"),
    ("Lex_input_stream", "reset"),
    ("Lex_input_stream", "get_text"),
    ("Lex_input_stream", "unescape"),
    # file-static helpers of sql_lex.cc, called by the members above: they are
    # MariaDB source like the rest, so they are sliced, not shimmed.
    (None, "int_token"),
    (None, "my_unescape"),
    (None, "lex_casecmp"),
    # sql_lex.cc's own one-time initialiser: it fills symbols[].length and
    # sql_functions[].length, which the keyword lookup compares against.
    # MariaDB calls it once from init_common_variables(); the bridge calls it
    # once from mdbp_lex_new().
    (None, "lex_init"),
    # the entry points the parser calls:
    (None, "MYSQLlex"),
    (None, "ORAlex"),
]

# File-scope constants of sql_lex.cc that the sliced functions read.  MariaDB
# source like everything else, so they are sliced, not re-typed in the shim --
# `int_token` compares against these exact strings and lengths.
SLICE_VARIABLES: list[tuple[str, str]] = [
    # `empty_clex_str` is what get_text() stores into the semantic value when
    # the allocation for an unescaped literal fails (sql_lex.cc:1695).
    ("sql/sql_lex.cc", "empty_clex_str"),
    ("sql/sql_lex.cc", "long_str"),
    ("sql/sql_lex.cc", "long_len"),
    ("sql/sql_lex.cc", "signed_long_str"),
    ("sql/sql_lex.cc", "longlong_str"),
    ("sql/sql_lex.cc", "longlong_len"),
    ("sql/sql_lex.cc", "signed_longlong_str"),
    ("sql/sql_lex.cc", "signed_longlong_len"),
    ("sql/sql_lex.cc", "unsigned_longlong_str"),
    ("sql/sql_lex.cc", "unsigned_longlong_len"),
]

# Lex_input_stream methods deliberately left as declarations only, in two
# groups:
#
#   given SHIM BODIES in src/mdbp_lex.cc: the pre-processed-buffer
#     (`body_utf8_*`) and digest members, which tools/vendor/thd.md records as
#     "not modelled" (the buffer the binlog sees, and performance-schema
#     digests).  Neither ever feeds back into the token stream, and dragging
#     their bodies in would drag in the server's memory-root and
#     charset-conversion layers for no gain.
#
#   NEVER REFERENCED, so they need no body at all: `get_escape_func` (its one
#     caller is body_utf8_append_escape, which is shimmed),
#     `get_body_utf8_maximum_length` (called by body_utf8_start and
#     body_utf8_append_escape, both shimmed) and `reduce_digest_token` (the
#     slice never calls it).  `nm -u` on the object file is the check.
#
# The members that ARE lexing (init, reset, get_text, unescape) are in
# SLICE_FUNCTIONS above: MariaDB's own code or nothing.
DECLARED_NOT_DEFINED_IS_EXPECTED = {
    "get_escape_func", "body_utf8_start",
    "body_utf8_append", "body_utf8_append_ident", "body_utf8_append_escape",
    "get_body_utf8_maximum_length", "add_digest_token", "reduce_digest_token",
}

# The keyword tables.  `sql/lex.h` is a *definition* file: `SYMBOL symbols[] =
# {...}` built from a `SYM(A)` macro over token names.  Because the slice
# compiles against the stub grammar's parser.h, SYM(A) resolves to ITS token
# values automatically -- and any name in lex.h that parser.h does not declare
# is reported in the manifest as `unknown_tokens` (and printed on exit).
SLICE_KEYWORD_ARRAYS = ["symbols", "sql_functions"]

# The charset side: compile and run `init_state_maps` once against a minimal
# CHARSET_INFO literal and emit state_maps.h, so the two 256-byte lexer maps
# are computed by MariaDB's own code rather than re-typed.  These are the
# symbols the generator needs, located by symbol like everything else.
SLICE_CHARSET: list[tuple[str, str, str]] = [
    ("strings/ctype-utf8.c", "array", "ctype_utf8mb4"),
    ("strings/ctype-utf8.c", "function", "my_valid_mbcharlen_utf8mb3"),
    ("strings/ctype-utf8.c", "function", "my_charlen_utf8mb4"),
    ("mysys/charset.c", "function", "is_multi_byte_ident"),
    ("mysys/charset.c", "function", "init_state_maps"),
]


# What the LEXER translation unit needs beyond sql_lex.cc, all located by
# symbol like everything else.  These are data and library routines the sliced
# lexer reads or calls; re-typing any of them into src/lexshim.h would be a
# transliteration.
#
#   ctype_utf8mb4              cs->ctype: my_isdigit/my_isalpha/... (the
#                              connection charset is utf8mb4, always)
#   my_charlen_utf8mb4         cs->charlen() and my_ismbchar(), sql_lex.cc:2880
#   my_valid_mbcharlen_utf8mb3 what my_charlen_utf8mb4 calls
#   ctype_latin1               my_charset_latin1's ctype, read by my_strtoll10
#   lfactor / my_strtoll10     the version digits of a versioned comment
#                              (sql_lex.cc:2474); the five #defines are that
#                              function's own private constants
#   to_upper_lex               sql_lex.cc:646, the latin1 upper-casing table
#                              lex_casecmp() (and therefore keyword lookup)
#                              compares through
#
# `array`, `function` and `variable` are located by the same three locators the
# rest of this file uses; `define` is find_define() below, added here for
# my_strtoll10's constants -- still a NAME, never a line number.
SLICE_SUPPORT: list[tuple[str, str, str]] = [
    ("strings/ctype-utf8.c", "array", "ctype_utf8mb4"),
    ("strings/ctype-utf8.c", "function", "my_valid_mbcharlen_utf8mb3"),
    ("strings/ctype-utf8.c", "function", "my_charlen_utf8mb4"),
    ("strings/ctype-latin1.c", "array", "ctype_latin1"),
    ("strings/my_strtoll10.c", "define", "MAX_NEGATIVE_NUMBER"),
    ("strings/my_strtoll10.c", "define", "INIT_CNT"),
    ("strings/my_strtoll10.c", "define", "LFACTOR"),
    ("strings/my_strtoll10.c", "define", "LFACTOR1"),
    ("strings/my_strtoll10.c", "define", "LFACTOR2"),
    ("strings/my_strtoll10.c", "array", "lfactor"),
    ("strings/my_strtoll10.c", "function", "my_strtoll10"),
    ("sql/sql_lex.cc", "array", "to_upper_lex"),
]

# The charset-introducer table.  `scan_ident_middle` asks
# `get_charset_by_csname(name, MY_CS_PRIMARY, ...)` whether `_name` in
# `_latin1'x'` names a character set, and returns UNDERSCORE_CHARSET when it
# does (sql_lex.cc:2984-2996).  The server answers from `all_charsets[]`, which
# is filled at runtime from `sql/share/charsets/Index.xml` plus the compiled-in
# charsets -- there is no symbol to slice.  What there IS, is MariaDB's own
# data, so the NAMES are derived from the pinned tree by the three patterns
# in charset_names() below, emitted into the slice as a table, and looked up
# by src/mdbp_lex.cc's
# get_charset_by_csname() shim.  Only non-NULL-ness reaches the token stream:
# the CHARSET_INFO the server would return lands in yylval->charset and feeds
# the utf8 body, which thd.md records as not modelled.
CHARSET_NAME_SOURCES = {
    "index_xml": "sql/share/charsets/Index.xml",
    "strings_dir": "strings",
}


# ---------------------------------------------------------------------------
# lexing helpers: comment/literal blanking, so brace and paren matching cannot
# be fooled by a `{` in a string or a `"` in a comment.
# ---------------------------------------------------------------------------


def blank_comments_and_literals(text: str) -> str:
    """Return `text` with every comment and every string/char literal *body*
    replaced by spaces, preserving length and newlines.

    Length preservation is what lets every offset computed on the blanked text
    be used directly on the original.
    """
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                if i + 1 < n:
                    out[i + 1] = " "
                i += 2
        elif c in "\"'":
            quote = c
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\" and i + 1 < n:
                    out[i] = " "
                    if text[i + 1] != "\n":
                        out[i + 1] = " "
                    i += 2
                    continue
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            i += 1
        else:
            i += 1
    return "".join(out)


def _match_delims(blank: str, start: int, opener: str, closer: str) -> int:
    """Index just past the `closer` matching the `opener` at `blank[start]`."""
    assert blank[start] == opener
    depth = 0
    i = start
    while i < len(blank):
        if blank[i] == opener:
            depth += 1
        elif blank[i] == closer:
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise LexSliceError(f"unbalanced {opener}{closer} from offset {start}")


def _skip_ws(blank: str, i: int) -> int:
    while i < len(blank) and blank[i] in " \t\r\n\\":
        i += 1
    return i


# ---------------------------------------------------------------------------
# locators.  Every one takes a NAME and returns a (start, end) span of the
# original text.  None of them takes, computes or returns a line number.
# ---------------------------------------------------------------------------


def _line_start(text: str, offset: int) -> int:
    nl = text.rfind("\n", 0, offset)
    return 0 if nl < 0 else nl + 1


def _extend_signature_start(text: str, blank: str, line_start: int) -> int:
    """Walk back over the lines that are part of the same declaration.

    MariaDB writes some definitions with the return type on its own line
    (`static int\nmy_charlen_utf8mb4(...)`).  A span that began at the line
    carrying the name would drop the return type, so walk back while the
    previous line is non-blank and does not end a previous construct
    (`;`, `{`, `}`, `:`, `)`) and is not a preprocessor directive.  Comments are
    already blanked, so a doc comment above the signature stops the walk.
    """
    while line_start > 0:
        prev_end = line_start - 1
        prev_start = _line_start(text, prev_end)
        prev = blank[prev_start:prev_end].rstrip()
        if not prev or prev.lstrip().startswith("#") or prev[-1] in ";{}:)":
            break
        line_start = prev_start
    return line_start


def find_function(text: str, name: str, klass: str | None = None) -> tuple[int, int]:
    """Span of the *definition* of `name` (optionally `klass::name`).

    A definition is recognised structurally: a line that starts in column 0
    (MariaDB puts every out-of-line definition there; a *call* is always
    indented), carrying `name (`, whose parameter list is followed by a `{`.
    A candidate followed by `;` is a declaration and is skipped.
    """
    blank = blank_comments_and_literals(text)
    if klass:
        pattern = re.compile(
            r"(?m)^(?![ \t#])[^\n]*?\b" + re.escape(klass) + r"\s*::\s*"
            + re.escape(name) + r"\s*\("
        )
    else:
        pattern = re.compile(
            r"(?m)^(?![ \t#])[^\n]*?(?<![\w:])" + re.escape(name) + r"\s*\("
        )
    spans: list[tuple[int, int]] = []
    for m in pattern.finditer(blank):
        open_paren = blank.index("(", m.end() - 1)
        after = _match_delims(blank, open_paren, "(", ")")
        # trailing const / noexcept / attributes before the body
        j = _skip_ws(blank, after)
        while j < len(blank) and (blank.startswith("const", j)
                                  or blank.startswith("noexcept", j)):
            j = _skip_ws(blank, j + (5 if blank.startswith("const", j) else 8))
        if j >= len(blank) or blank[j] != "{":
            continue  # a declaration, or a prototype: not the definition
        end = _match_delims(blank, j, "{", "}")
        start = _extend_signature_start(text, blank, _line_start(text, m.start()))
        spans.append((start, end))
    return _exactly_one(spans, f"function {klass + '::' if klass else ''}{name}")


def _find_braced(text: str, header: re.Pattern, what: str) -> tuple[int, int]:
    blank = blank_comments_and_literals(text)
    spans = []
    for m in header.finditer(blank):
        j = blank.index("{", m.end() - 1) if "{" in blank[m.end() - 1:m.end() + 400] \
            else -1
        if j < 0:
            continue
        end = _match_delims(blank, j, "{", "}")
        end = _skip_ws(blank, end)
        if end < len(blank) and blank[end] == ";":
            end += 1
        spans.append((_line_start(text, m.start()), end))
    return _exactly_one(spans, what)


def find_class(text: str, name: str, keyword: str = "class") -> tuple[int, int]:
    """Span of `class NAME { ... };` / `struct NAME : Base { ... };`."""
    header = re.compile(r"(?m)^" + keyword + r"\s+" + re.escape(name) + r"\b[^;{\n]*")
    return _find_braced(text, header, f"{keyword} {name}")


def find_enum(text: str, name: str) -> tuple[int, int]:
    """Span of `enum NAME { ... };`."""
    header = re.compile(r"(?m)^enum\s+" + re.escape(name) + r"\b[^;{]*")
    return _find_braced(text, header, f"enum {name}")


def find_array(text: str, name: str) -> tuple[int, int]:
    """Span of a file-scope array definition `T NAME[] = { ... };`.

    The dimension is optional: strings/my_strtoll10.c writes
    `static unsigned long lfactor[9]= {...}`, and a pattern that insisted on
    empty brackets could not find it.
    """
    header = re.compile(
        r"(?m)^(?![ \t#])[^\n]*?\b" + re.escape(name)
        + r"\s*\[\s*[0-9]*\s*\]\s*=\s*"
    )
    return _find_braced(text, header, f"array {name}")


def find_define(text: str, name: str) -> tuple[int, int]:
    """Span of `#define NAME ...`, backslash continuations included.

    A NAME, like every other locator here -- `#define` is how MariaDB spells a
    named constant, and my_strtoll10()'s five constants are private to that
    file, so the alternative to slicing them is re-typing their values into
    src/lexshim.h, i.e. a transliteration.
    """
    pattern = re.compile(
        # `(?:[^\n]*\\\n)*[^\n]*` and NOT `[^\n]*(?:\\\n[^\n]*)*`: the
        # second is greedy on the first line, swallows the trailing backslash
        # and can then never enter the continuation branch, so it stops at the
        # first line of a multi-line macro.
        r"(?m)^[ \t]*#[ \t]*define[ \t]+" + re.escape(name)
        + r"(?![A-Za-z0-9_])(?:[^\n]*\\\n)*[^\n]*"
    )
    spans = [(_line_start(text, m.start()), m.end())
             for m in pattern.finditer(text)]
    return _exactly_one(spans, f"define {name}")


def find_variable(text: str, name: str) -> tuple[int, int]:
    """Span of a file-scope scalar/pointer definition `T NAME= <init>;`."""
    blank = blank_comments_and_literals(text)
    pattern = re.compile(
        r"(?m)^(?![ \t#])[^\n]*?\b" + re.escape(name) + r"\s*=[^;\n]*;"
    )
    spans = [(_line_start(text, m.start()), m.end()) for m in pattern.finditer(blank)]
    return _exactly_one(spans, f"variable {name}")


def _exactly_one(spans: list[tuple[int, int]], what: str) -> tuple[int, int]:
    if not spans:
        raise LexSliceError(f"cannot locate {what}")
    if len(spans) > 1:
        raise LexSliceError(f"{what} is defined {len(spans)} times; ambiguous")
    return spans[0]


# ---------------------------------------------------------------------------
# the mechanical rewrite rules.  One entry per rule; each returns
# (new_text, [applications]) and every application lands in the manifest.
# ---------------------------------------------------------------------------

WSREP_GUARD = "WITH_WSREP"


def rule_isolate_wsrep(body: str) -> tuple[str, list[dict]]:
    """`#ifdef WITH_WSREP ... #endif` inside a sliced body is replaced by a
    marker comment, and the server symbols it referenced are reported.

    Why a rule and not "let the preprocessor drop it": WITH_WSREP is undefined
    here, so cpp would delete the block silently and the server state it
    reads (`WSREP(thd)`, `wsrep_thd_is_local(thd)`,
    `thd->wsrep_consistency_check`) would never be visible.  The rule removes
    it *visibly*, with a marker comment in the slice, and lists the
    identifiers in the manifest's `wsrep_findings`.  (The marker's "Q7
    finding" wording is frozen generated text; see the module docstring.)
    """
    applications: list[dict] = []
    pattern = re.compile(
        r"(?m)^[ \t]*#[ \t]*ifdef[ \t]+" + WSREP_GUARD + r"[ \t]*$.*?"
        r"^[ \t]*#[ \t]*endif[^\n]*$\n?",
        re.S,
    )

    def replace(m: re.Match) -> str:
        block = m.group(0)
        idents = sorted(
            set(re.findall(r"\b(?:WSREP|WSREP_DEBUG|wsrep_[a-z_]+)\b", block))
            | set(re.findall(r"\b(CONSISTENCY_CHECK_[A-Z_]+)\b", block))
            | set(re.findall(r"thd->(wsrep_[a-z_]+)", block))
        )
        applications.append({"guard": WSREP_GUARD, "symbols": idents,
                             "removed_lines": block.count("\n")})
        return ("      /* lexslice.py rule isolate_wsrep: #ifdef "
                + WSREP_GUARD + " block removed; symbols recorded as a "
                "Q7 finding */\n")

    return pattern.sub(replace, body), applications


REWRITE_RULES = [("isolate_wsrep", rule_isolate_wsrep)]


# ---------------------------------------------------------------------------
# the slice
# ---------------------------------------------------------------------------

# Frozen generated text (see the module docstring): the committed
# generated/lexer_extracted.cc carries this banner byte for byte.
BANNER = (
    "/* GENERATED by tools/lexslice.py -- DO NOT EDIT.\n"
    "   Every byte below except this banner and the `slice:` markers is\n"
    "   MariaDB's own source, located by symbol and copied verbatim, except\n"
    "   where a named rewrite rule says otherwise (see build/lexslice.json).\n"
    "   Hand-editing this file voids the experiment (PLAN P5a, K7). */\n"
)


def _marker(kind: str, path: str, name: str) -> str:
    return f"\n/* ---- slice: {kind} {name} <- {path} ---- */\n"


def read_source(root: Path, rel: str) -> str:
    path = root / rel
    if not path.is_file():
        raise LexSliceError(f"{path} is missing")
    return path.read_text(encoding="utf-8", errors="surrogateescape")


def parser_token_names(parser_h: Path) -> set[str]:
    """Every token name parser.h's `enum yytokentype` declares.

    The trailing comma is OPTIONAL: bison does not write one after the LAST
    enumerator (`EMPTY_FROM_CLAUSE = 1033` here), and requiring it dropped that
    token from `declared`, where it would surface as a spurious
    `unknown_tokens` finding.  What it must NOT do is anchor at end of line
    without allowing a comment: bison writes a `/* NAME */` comment after
    every enumerator but the first few, so a bare `$` matches almost nothing.
    The manifest's `keywords.parser_h_tokens` is the count this returns.
    """
    text = parser_h.read_text()
    return set(re.findall(
        r"(?m)^\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*-?\d+\s*,?(?:\s*/\*.*)?\s*$",
        text))


def keyword_symbols(lex_h_text: str) -> list[str]:
    """Every `SYM(NAME)` argument in sql/lex.h, excluding the macro's own
    definition line (`#define SYM(A) ...`), whose `A` is the parameter."""
    body = re.sub(r"(?m)^\s*#\s*define[^\n]*(\\\n[^\n]*)*", "", lex_h_text)
    return sorted(set(re.findall(r"\bSYM\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)", body)))


def build_keywords_header(root: Path) -> tuple[str, dict]:
    """keywords.h: sql/lex_symbol.h's declarations plus sql/lex.h's two
    keyword arrays, sliced by symbol."""
    lex_symbol = read_source(root, "sql/lex_symbol.h")
    lex_h = read_source(root, "sql/lex.h")
    parts = [
        "/* GENERATED by tools/lexslice.py -- DO NOT EDIT. */\n"
        "#ifndef MDBP_KEYWORDS_H\n#define MDBP_KEYWORDS_H\n",
        _marker("header", "sql/lex_symbol.h", "SYMBOL/LEX_SYMBOL/SYM_GROUP"),
        lex_symbol,
        "\nSYM_GROUP sym_group_common= {\"\", \"\"};\n"
        "#define SYM_OR_NULL(A) A\n"
        "#define SYM(A) SYM_OR_NULL(A),0,&sym_group_common\n",
    ]
    manifest = {"arrays": []}
    for name in SLICE_KEYWORD_ARRAYS:
        start, end = find_array(lex_h, name)
        parts.append(_marker("array", "sql/lex.h", name))
        parts.append(lex_h[start:end] + "\n")
        manifest["arrays"].append(
            {"name": name, "bytes": end - start,
             "entries": lex_h[start:end].count("SYM(")}
        )
    parts.append(
        "\nsize_t symbols_length= sizeof(symbols) / sizeof(SYMBOL);\n"
        "size_t sql_functions_length= sizeof(sql_functions) / sizeof(SYMBOL);\n"
        "#endif /* MDBP_KEYWORDS_H */\n"
    )
    return "".join(parts), manifest


def build_lex_states(root: Path) -> tuple[str, dict]:
    """lexstates.h -- `enum my_lex_states`, sliced from include/m_ctype.h.

    Its own header because both translation units need it: the lexer slice and
    the state-map generator (whose `init_state_maps` fills the map with
    exactly these values).
    """
    rel, _kind, name = SLICE_LEX_STATES
    src = read_source(root, rel)
    start, end = find_enum(src, name)
    text = (
        "/* GENERATED by tools/lexslice.py -- DO NOT EDIT. */\n"
        "#ifndef MDBP_LEXSTATES_H\n#define MDBP_LEXSTATES_H\n"
        + _marker("enum", rel, name)
        + src[start:end]
        + "\n#endif /* MDBP_LEXSTATES_H */\n"
    )
    return text, {"file": rel, "kind": "enum", "symbol": name,
                  "bytes": end - start}


def charset_names(root: Path) -> list[str]:
    """Every name `get_charset_by_csname(name, MY_CS_PRIMARY, ...)` resolves in
    the pinned tree, derived from MariaDB's own data files.

    Three patterns, and the reason for each:

    * `sql/share/charsets/Index.xml` -- `charset name="X"`: the 8-bit character
      sets the server loads from XML at startup.
    * `strings/*.c` -- `charset_name_X` (not `..._length`) and
      `STRING_WITH_LEN("x") }, /* cs name`: the compiled-in character sets,
      which name themselves in their own CHARSET_INFO literals.
    * `utf8`, added by hand and only here: it is not a cs_name anywhere in the
      tree, because `get_charset_number()` maps it to utf8mb3/utf8mb4 by hand
      (mysys/charset.c).
    """
    index = root / CHARSET_NAME_SOURCES["index_xml"]
    strings_dir = root / CHARSET_NAME_SOURCES["strings_dir"]
    if not index.is_file():
        raise LexSliceError(f"{index} is missing")
    if not strings_dir.is_dir():
        raise LexSliceError(f"{strings_dir} is missing")
    names = set(re.findall(
        r'charset name="([a-z0-9]+)"',
        index.read_text(encoding="utf-8", errors="surrogateescape")))
    for path in sorted(strings_dir.glob("*.c")):
        text = path.read_text(encoding="utf-8", errors="surrogateescape")
        names |= set(re.findall(r"charset_name_([a-z0-9]+)\b(?!_length)", text))
        names |= set(re.findall(
            r'STRING_WITH_LEN\("([a-z0-9]+)"\)\s*\},?\s*/\* cs name', text))
    names -= {"length"}
    names |= {"utf8"}
    return sorted(names)


def build_charset_names(root: Path) -> tuple[str, dict]:
    """The charset-introducer table, emitted into the slice."""
    names = charset_names(root)
    rows = "".join(f'  "{name}",\n' for name in names)
    text = (
        # No "/*" in the marker: the sources include a glob, and a nested
        # "/*" inside a /* ... */ comment is a -Wcomment warning in OUR
        # generated bytes.
        "\n/* ---- generated: charset introducer names <- "
        + CHARSET_NAME_SOURCES["index_xml"] + " + "
        + CHARSET_NAME_SOURCES["strings_dir"] + " *.c files ---- */\n"
        "static const char *const mdbp_charset_names[]=\n{\n"
        + rows
        + "};\n"
        "static const size_t mdbp_charset_names_count=\n"
        "  sizeof(mdbp_charset_names) / sizeof(mdbp_charset_names[0]);\n"
    )
    return text, {"count": len(names), "names": names,
                  "sources": dict(CHARSET_NAME_SOURCES)}


def build_slice(root: Path) -> tuple[str, dict]:
    """The whole translation unit, plus its manifest."""
    # The shim is included TWICE, on purpose.  Phase 1 is the primitives,
    # THD/LEX and the forward declaration of `union YYSTYPE`; the sliced types
    # come next (Lex_ident_cli_st and friends); phase 2 is the YYSTYPE union
    # itself, whose members ARE those sliced types, plus parser.h.  The order
    # is forced by the sources: MariaDB's %union holds Lex_ident_cli_st by
    # value, and Lex_input_stream only ever sees `union YYSTYPE *`.
    parts = [BANNER, '#include "lexshim.h"\n', '#include "lexstates.h"\n']
    manifest: dict = {
        "source_tree": str(root),
        "types": [],
        "functions": [],
        "rewrites": [],
        "declared_not_sliced": [],
        "wsrep_findings": [],
    }

    texts: dict[str, str] = {}

    def text_of(rel: str) -> str:
        if rel not in texts:
            texts[rel] = read_source(root, rel)
        return texts[rel]

    for rel, kind, name in SLICE_TYPES:
        src = text_of(rel)
        if kind == "enum":
            start, end = find_enum(src, name)
        else:
            start, end = find_class(src, name, keyword=kind)
        parts.append(_marker(kind, rel, name))
        parts.append(src[start:end] + "\n")
        manifest["types"].append(
            {"file": rel, "kind": kind, "symbol": name, "bytes": end - start}
        )

    parts.append("\n#define MDBP_LEXSHIM_PHASE2 1\n"
                 '#include "lexshim.h"\n'
                 '#include "keywords.h"\n'
                 "/* MariaDB's own header, unmodified, via -I $MARIADB_SOURCE_DIR/strings:\n"
                 "   the IS_UTF8MB4_STEP2 / IS_CONTINUATION_BYTE macros\n"
                 "   my_charlen_utf8mb4 below is written against. */\n"
                 '#include "ctype-utf8.h"\n')

    # The support symbols (charset tables, my_strtoll10, to_upper_lex) go
    # BEFORE sql_lex.cc's own file-scope constants and functions, because
    # lex_casecmp reads to_upper_lex and get_text reaches my_charlen_utf8mb4.
    for rel, kind, name in SLICE_SUPPORT:
        src = text_of(rel)
        if kind == "array":
            start, end = find_array(src, name)
        elif kind == "define":
            start, end = find_define(src, name)
        else:
            start, end = find_function(src, name)
        parts.append(_marker(kind, rel, name))
        parts.append(src[start:end] + "\n")
        entry = {"file": rel, "kind": kind, "symbol": name, "bytes": end - start}
        if kind == "function":
            manifest["functions"].append({**entry, "klass": None,
                                          "rules_applied": []})
        else:
            manifest["types"].append(entry)

    charset_table, charset_names_manifest = build_charset_names(root)
    parts.append(charset_table)
    manifest["charset_names"] = charset_names_manifest

    for rel, name in SLICE_VARIABLES:
        src = text_of(rel)
        start, end = find_variable(src, name)
        parts.append(_marker("variable", rel, name))
        parts.append(src[start:end] + "\n")
        manifest["types"].append(
            {"file": rel, "kind": "variable", "symbol": name, "bytes": end - start}
        )

    cc = text_of("sql/sql_lex.cc")
    # Free functions first, then the members: a file-static helper such as
    # int_token is called by lex_one_token, and C++ needs it declared before
    # the call.  Ordering by kind is mechanical; forward-declaring by hand
    # would not be.
    ordered = ([f for f in SLICE_FUNCTIONS if f[0] is None]
               + [f for f in SLICE_FUNCTIONS if f[0] is not None])
    for klass, name in ordered:
        start, end = find_function(cc, name, klass)
        body = cc[start:end]
        applied = []
        for rule_name, rule in REWRITE_RULES:
            body, applications = rule(body)
            for app in applications:
                applied.append(rule_name)
                manifest["rewrites"].append(
                    {"rule": rule_name, "symbol": name, **app}
                )
                if rule_name == "isolate_wsrep":
                    manifest["wsrep_findings"].extend(app["symbols"])
        parts.append(_marker("function", "sql/sql_lex.cc",
                             f"{klass + '::' if klass else ''}{name}"))
        parts.append(body + "\n")
        manifest["functions"].append(
            {"file": "sql/sql_lex.cc", "klass": klass, "symbol": name,
             "bytes": end - start, "rules_applied": sorted(set(applied))}
        )

    manifest["wsrep_findings"] = sorted(set(manifest["wsrep_findings"]))

    # The mechanical closure check: every Lex_input_stream method the sliced
    # class declares that the sliced bodies actually call must be either
    # sliced or on the declared-only list.  Anything else is reported, never
    # silently dropped.
    sliced_names = {name for _, name in SLICE_FUNCTIONS}
    # Every Lex_input_stream method sql_lex.cc defines out of line.  Matching
    # against THIS set (rather than against every name that looks like a call)
    # is what keeps `memcpy` and `my_isspace` out of the report.
    defined_in_cc = set(re.findall(
        r"(?m)^(?![ \t#])[^\n]*?\bLex_input_stream\s*::\s*(\w+)\s*\(", cc))
    called: set[str] = set()
    for chunk in parts:
        called |= set(re.findall(r"(?<![\w:.>])([a-z_][a-z0-9_]*)\s*\(", chunk))
    for name in sorted(defined_in_cc & called):
        if name not in sliced_names and name not in DECLARED_NOT_DEFINED_IS_EXPECTED:
            manifest["declared_not_sliced"].append(name)

    return "".join(parts), manifest


def build_state_map_generator(root: Path) -> tuple[str, dict]:
    """state_maps_gen.cc -- the state-map generator.

    `init_state_maps` (mysys/charset.c) over a minimal CHARSET_INFO literal
    carrying `ctype_utf8mb4` (strings/ctype-utf8.c), sliced by symbol.  The
    generator prints state_maps.h; --compile-state-maps builds and runs it.
    """
    parts = [
        BANNER,
        '#include "lexshim.h"\n',
        '#include "lexstates.h"\n',
        "/* MariaDB's own header, unmodified, via -I $MARIADB_SOURCE_DIR/strings: the\n"
        "   IS_UTF8MB4_STEP2 / IS_CONTINUATION_BYTE macros my_charlen_utf8mb4\n"
        "   below is written against. */\n"
        '#include "ctype-utf8.h"\n',
    ]
    manifest = {"symbols": []}
    for rel, kind, name in SLICE_CHARSET:
        src = read_source(root, rel)
        if kind == "function":
            start, end = find_function(src, name)
        else:
            start, end = find_array(src, name)
        parts.append(_marker(kind, rel, name))
        parts.append(src[start:end] + "\n")
        manifest["symbols"].append(
            {"file": rel, "kind": kind, "symbol": name, "bytes": end - start}
        )
    parts.append(MAIN_STATE_MAP_GEN)
    return "".join(parts), manifest


# Frozen generated text (see the module docstring): its printf writes the
# first line of the committed generated/state_maps.h.
MAIN_STATE_MAP_GEN = """
/* lexslice.py: the generator's own main(), and the one hook src/lexshim.h's
   my_ci_charlen macro needs.  Neither is MariaDB source -- they print what
   init_state_maps() above computed, and nothing else. */
int mdbp_charlen(CHARSET_INFO *cs, const uchar *s, const uchar *e)
{
  return my_charlen_utf8mb4(cs, s, e);
}

int main(void)
{
  struct charset_info_st cs;
  memset(&cs, 0, sizeof(cs));
  cs.ctype= ctype_utf8mb4;
  if (init_state_maps(&cs))
    return 1;
  printf("/* GENERATED by build/state_maps_gen.cc (tools/lexslice.py). */\\n");
  printf("#ifndef MDBP_STATE_MAPS_H\\n#define MDBP_STATE_MAPS_H\\n");
  for (int pass= 0; pass < 2; pass++)
  {
    const unsigned char *map= pass ? cs.ident_map : cs.state_map;
    printf("static const unsigned char mdbp_%s[256]= {",
           pass ? "ident_map" : "state_map");
    for (int i= 0; i < 256; i++)
      printf("%s%u", i ? "," : "", (unsigned) map[i]);
    printf("};\\n");
  }
  printf("#endif\\n");
  return 0;
}
"""


def compile_state_maps(out: Path, root: Path, cxx: str = "g++") -> dict:
    """Compile and run `<out>/state_maps_gen.cc`; its stdout is
    `<out>/state_maps.h`.

    Returns `argv`/`returncode`/`stderr` too, for the caller to report a
    failure; only `state_maps_manifest_entry()`'s subset goes into
    lexslice.json, so the manifest does not embed run-specific paths.
    """
    gen_src = out / "state_maps_gen.cc"
    gen_bin = out / "state_maps_gen"
    gen_bin.unlink(missing_ok=True)
    argv = [
        cxx, "-std=c++17", "-O0", "-Wall", "-Wextra",
        "-I", str(out), "-I", str(common.REPO_ROOT / "include"), "-I", str(common.REPO_ROOT / "src"),
        "-I", str(root / "strings"),
        "-o", str(gen_bin), str(gen_src),
    ]
    proc = subprocess.run(argv, capture_output=True, text=True, check=False)
    info = {
        "argv": argv, "returncode": proc.returncode,
        "compiles": gen_bin.is_file(), "ran": False,
        "bytes": None, "entries": None, "stderr": proc.stderr,
    }
    if gen_bin.is_file():
        run = subprocess.run([str(gen_bin)], capture_output=True, text=True,
                             check=False)
        info["ran"] = run.returncode == 0
        if run.returncode == 0:
            (out / "state_maps.h").write_text(run.stdout)
            info["bytes"] = len(run.stdout)
            info["entries"] = sum(
                len(row.split(",")) for row in re.findall(r"\{([^}]*)\}", run.stdout))
        else:
            info["stderr"] += run.stderr
        gen_bin.unlink(missing_ok=True)
    return info


def state_maps_manifest_entry(info: dict) -> dict:
    """The path-independent subset of `compile_state_maps()`'s return value:
    what lexslice.json records."""
    return {key: info[key] for key in ("compiles", "ran", "bytes", "entries")}


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="lexslice.py",
        description="Slice MariaDB's lexer out of sql_lex.cc by symbol.",
    )
    parser.add_argument(
        "--source", required=True,
        help="the MariaDB server tree at the pin (read only)",
    )
    parser.add_argument(
        "--out", required=True,
        help="output directory",
    )
    parser.add_argument(
        "--parser-h", default=None,
        help="bison's parser.h for the stub grammar, for the SYM() "
             "cross-check (default <out>/parser.h)",
    )
    parser.add_argument(
        "--compile-state-maps", action="store_true",
        help="also compile and run <out>/state_maps_gen.cc, writing "
             "<out>/state_maps.h (the two 256-byte lexer state maps)",
    )
    parser.add_argument("--cxx", default="g++",
                        help="C++ compiler for --compile-state-maps "
                             "(default g++)")
    args = parser.parse_args(argv)

    root = Path(args.source)
    out = Path(args.out)
    parser_h = Path(args.parser_h) if args.parser_h else out / "parser.h"

    try:
        states_text, states_manifest = build_lex_states(root)
        slice_text, manifest = build_slice(root)
        keywords_text, keyword_manifest = build_keywords_header(root)
        gen_text, charset_manifest = build_state_map_generator(root)
    except LexSliceError as exc:
        print(f"lexslice.py: {exc}", file=sys.stderr)
        return EXIT_USAGE

    manifest["keywords"] = keyword_manifest
    manifest["charset"] = charset_manifest
    manifest["types"].insert(0, states_manifest)

    lex_h = read_source(root, "sql/lex.h")
    names = keyword_symbols(lex_h)
    manifest["keywords"]["sym_names"] = len(names)
    if parser_h.is_file():
        declared = parser_token_names(parser_h)
        manifest["keywords"]["unknown_tokens"] = sorted(set(names) - declared)
        manifest["keywords"]["parser_h_tokens"] = len(declared)
    else:
        manifest["keywords"]["unknown_tokens"] = None
        manifest["keywords"]["parser_h_tokens"] = None

    out.mkdir(parents=True, exist_ok=True)
    (out / "lexstates.h").write_text(states_text)
    (out / "lexer_extracted.cc").write_text(slice_text)
    (out / "keywords.h").write_text(keywords_text)
    (out / "state_maps_gen.cc").write_text(gen_text)

    if args.compile_state_maps:
        state_maps_info = compile_state_maps(out, root, cxx=args.cxx)
        if not (state_maps_info["compiles"] and state_maps_info["ran"]):
            print(
                f"lexslice.py: state_maps_gen.cc did not compile and run "
                f"(compiles={state_maps_info['compiles']} "
                f"ran={state_maps_info['ran']}):\n{state_maps_info['stderr']}",
                file=sys.stderr,
            )
            return EXIT_USAGE
        manifest["state_maps"] = state_maps_manifest_entry(state_maps_info)

    common.write_json(out / "lexslice.json", manifest)

    print(
        f"lexslice.py: {len(manifest['functions'])} functions, "
        f"{len(manifest['types'])} types, "
        f"{len(manifest['rewrites'])} rewrites, "
        f"{len(manifest['declared_not_sliced'])} declared-not-sliced, "
        f"unknown_tokens={manifest['keywords']['unknown_tokens']}"
    )
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
