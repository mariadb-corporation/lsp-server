#!/usr/bin/env python3
"""tools/stub.py — turn MariaDB's preprocessed bison superset into a
plain-bison, C-only grammar whose every action body is a one-line stub.

Run by hand, once per full re-extraction (see UPSTREAM.md); the build never
runs it.  Its output is committed as generated/yy_stub.yy.

The transform is *line-count preserving*, exactly like MariaDB's own
`sql/gen_yy_files.cmake` step that runs before it: every removed construct is
blanked line by line, and where a replacement is needed it occupies the
construct's **first** line.  So every `sql_yacc.yy:LINE` citation stays valid
through both stages, and bison's own `yyrline[]` points at real
`sql_yacc.yy` lines.

What the declarations section becomes
-------------------------------------
=======================================  ====================================
construct                                becomes
=======================================  ====================================
the first ``%{ … %}`` prologue           ``%{ #include "stub.h" %}`` on its
                                         first line, the rest blank
any further ``%{ … %}`` block            blank
``%code … { … }``                        blank
``%union { … }``                         ``%define api.value.type {int}`` on
                                         its first line, the rest blank
``%parse-param { THD *thd }``            ``%parse-param { mdbp_ctx *ctx }``
``%lex-param { THD *thd }``              ``%lex-param { mdbp_ctx *ctx }``
``%type`` + its continuation lines       blank -- **except** that when the
                                         declaration's symbol list contains
                                         character literals, a ``%token``
                                         naming those literals not already
                                         mentioned by an earlier surviving
                                         declaration line takes the
                                         construct's first line (see below)
``%destructor { … } <tag>``              blank
``<tag>`` on ``%token``/``%nterm``       deleted in place, rest of line kept
``%define`` ``%expect`` ``%left``
``%right`` ``%nonassoc`` ``%precedence`` unchanged
=======================================  ====================================

Any other ``%``-directive is *unrecognised* and exits 3 rather than being
guessed at: a silent mis-transformation would change the automaton
invisibly.

The ``%type`` exception, and why it is a rule of the transform
--------------------------------------------------------------
``sql_yacc.yy:2018``–``:2020`` is a ``%type <kwd>`` whose continuation lines
list **character-literal terminals**, not nonterminals.  Bison accepts
``%type`` on a terminal (it only sets the tag) and **assigns symbol numbers in
order of first mention**, so for a literal whose *only* mention in the
declarations section is that ``%type``, blanking the declaration moves its
first mention into the rules and renumbers it — and because bison builds each
state's transitions in symbol-number order, that renumbering propagates
through the automaton, renumbering the states downstream of every affected
symbol.  Without this rule, six terminals of the pinned grammar got permuted
symbol numbers and the automaton's state numbering diverged from the
original's.

So a ``%type`` whose list contains character literals emits, **on the
construct's first line**, a ``%token`` naming exactly the literals that have
not appeared earlier in the *output's* declarations section (in surviving
precedence/``%token`` lines, or in a ``%token`` this rule already emitted), in
their original order; every other line of the construct is still blanked and
the file's line count is unchanged.  Literals already mentioned earlier are
filtered out because ``%token``-redeclaring a symbol a previous ``%token``
declared makes bison warn (measured: ``symbol ',' redeclared [-Wother]``),
and bison must run over the output without warnings; a symbol first seen in
``%left``/``%right``/``%nonassoc`` may be ``%token``-ed afterwards in silence
(also measured).  If nothing is left after the filter, nothing is emitted —
an empty ``%token`` line is a bison syntax error.  **Named** symbols in a
``%type`` list are still blanked: a ``%token`` on a nonterminal would be
catastrophic.  A future revision that first-mentions a named terminal in a
``%type`` would need this rule extended; compare bison's symbol numbering of
the output against the original grammar's after a re-extraction.

What the rules section becomes
------------------------------
Every action body — final and mid-rule alike — is replaced by
``{ MDBP_R(<serial>); }`` on the body's first line, the remaining lines of the
body blanked.  **And every alternative that has no final action gains one**,
appended at the end of the alternative's last token (so after a trailing
``%prec X``, and before the ``|``/``;`` that terminates the alternative).

That second half is not cosmetic.  The runtime's derivation is *every*
reduction, recorded by ``MDBP_R`` (which records bison's rule number,
``yyn - 1``; the serial itself is only for readability), so a rule left
actionless would be missing from the derivation and from the syntax tree
built from it.  Appending a *final* action is automaton-neutral:
bison synthesises an extra rule only for an action that is not last in its
alternative, so rule count, rule numbering and state count are untouched, and
`yyrline[n]` records where a rule *starts*, not where its action sits.

A mid-rule action whose **own value** the original grammar used — its
``$$``/``$<tag>$`` assigned, or its RHS position referred to as ``$k`` /
``$<tag>k`` by a later action of the same alternative — is stubbed
``{ $$ = 0; MDBP_R(<serial>); }`` instead.  That is not cosmetic either:
bison names a mid-rule's synthesised placeholder nonterminal ``@N`` when the
mid-rule's value is used and ``$@N`` when it is not (measured on bison 3.8.2),
and the placeholder's *name* is a goto key in the automaton, so a stub that
never touches ``$$`` renames three of this grammar's 284 placeholders and the
automaton no longer matches the original grammar's.  The criterion is derived
from the **original action bodies** only, so it carries over to a new server
version.  Both forms are automaton-neutral and neither changes the serial
assignment: there is still exactly one ``MDBP_R`` per rule.

What the rules section KEEPS: the named preserve rules
------------------------------------------------------
One exception to "every action body is a one-line stub", and it is a **named
rule with a manifest**, in `tools/lexslice.py`'s ``REWRITE_RULES`` style:
``preserve_parsing_place``.

MariaDB's lexer reads ``curr_sel->parsing_place`` to decide what a few tokens
mean (``sql_lex.cc``'s five feedback sites).  The whole read-and-spend
machinery is in the sliced lexer, verbatim (``lexer_extracted.cc``); the
WRITES are grammar actions, and blanking them would leave the field
permanently ``NO_MATTER``.  So this rule preserves, inside an otherwise-blanked action
body, whole STATEMENTS that touch only ``parsing_place`` /
``save_parsing_place`` on one of the five paths ``sql_yacc.yy`` spells them
with -- ``Select->``, ``sel->``, ``Lex->current_select->``,
``lex->current_select->``, ``thd->lex->current_select->``.

**It reports rather than guesses.**  A statement is lifted only when every
identifier it mentions is one of those path words, one of the two fields, or an
``enum_parsing_place`` constant; only when it carries no punctuation beyond
``-> = == != && || ( ) ;``; and only when it occupies WHOLE LINES strictly
inside the body.  Anything else is recorded in the manifest with the reason,
printed as a count, and left blanked -- half a statement is never lifted, and
a statement the rule cannot prove self-contained is never guessed at.  The
run also checks its own completeness: every ``parsing_place`` line of the
input is lifted, reported or comment-only, and the output carries exactly the
occurrences that were lifted, or the run is an exit 1.  Which of the three a
line is is decided by
masking THE WHOLE FILE, never by whether an action body happened to be scanned
over it -- so a write OUTSIDE every action body, which no rule here can ever
see, is an exit 1 and not a line quietly called a comment (see
``_finish_preserve``).

**Line-count preservation is unchanged.**  The action's stub opener
(``{ MDBP_R(n);``) takes the body's first line, the preserved statements stay
on THEIR OWN ORIGINAL LINES, the closing ``}`` takes the body's last line and
every other line of the body is blanked -- so ``sql_yacc.yy:LINE`` still
resolves, for the lifted lines most of all.

The rule also writes a macro block into the grammar's first ``%{ … %}``
prologue, on lines that were being blanked anyway, which is what makes those
statements compile UNMODIFIED against ``src/stub.h``'s ``mdbp_ctx``.  See
``PARSING_PLACE_MACROS`` for why the macros live there and not in the header.

Finding an action's end requires lexing, not brace counting: `'{'` and `'}'`
are terminals of this grammar (``| '{' ident table_ref '}' { $$=$3; }``), as
are `';'` and `'|'`, and action bodies hold ``/* … */`` comments, ``//``
comments and string literals that may contain braces.

Modes
-----
::

    stub.py --in <yy> --out <yy_stub> [--map <stub-map.json>]
    stub.py --check --in <yy>

``--map`` writes an optional debugging manifest: each serial's ``sql_yacc.yy``
line and rule, and the ``preserve`` block (every lifted statement, every
``parsing_place`` site not lifted with the reason).

Exit codes: 0 ok · 1 internal error · 2 usage or unreadable input ·
3 unsupported/unrecognised input.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402

EXIT_OK = 0
EXIT_INTERNAL = 1
EXIT_USAGE = 2
EXIT_UNSUPPORTED = 3

STUB_MAP_SCHEMA_VERSION = 1

PROLOGUE_REPLACEMENT = '%{ #include "stub.h" %}'
PROLOGUE_OPEN = '%{ #include "stub.h"'
PROLOGUE_CLOSE = "%}"
UNION_REPLACEMENT = "%define api.value.type {int}"
PARAM_CTX = "mdbp_ctx *ctx"

# `%parse-param`/`%lex-param` are rewritten only when their body is exactly
# MariaDB's `THD *thd`; anything else is an unrecognised construct, because a
# second parameter (or a different type) would silently change the generated
# `yyparse` signature src/parser_glue.c calls.
PARAM_RE = re.compile(r"^%(parse|lex)-param\s*\{\s*THD\s*\*\s*thd\s*\}\s*$")

# Directives that survive the transform untouched.
PASSTHROUGH = frozenset(
    {"%define", "%expect", "%left", "%right", "%nonassoc", "%precedence"}
)
# Directives whose `<tag>` is deleted in place, the rest of the line kept.
TAGGED = frozenset({"%token", "%nterm"})

DIRECTIVE_RE = re.compile(r"%[A-Za-z_%{}-]*")
IDENT_START_RE = re.compile(r"[A-Za-z_.]")
IDENT_RE = re.compile(r"[A-Za-z_.][A-Za-z_.0-9]*")
# A `%type` continuation line carries symbol names only (or is blank, or is
# entirely comment / char-literal terminals, both of which mask to blanks).
TYPE_CONT_RE = re.compile(r"^[A-Za-z0-9_. \t;]*$")


class Unsupported(Exception):
    """An input construct the transform refuses to guess at (exit 3)."""


class Internal(Exception):
    """A defect in this script or in its own output (exit 1)."""


# --------------------------------------------------------------------------
# masking: comments and literals blanked out, columns and lengths preserved
# --------------------------------------------------------------------------


def mask_declarations(lines: list[str]) -> tuple[list[str], int, list[list[str]]]:
    """Mask lines from the top of the file until (and including) the `%%` that
    ends the declarations section.

    Returns `(masked, pct_index, literals)`, where `masked[i]` is `lines[i]`
    with the body of every `/* … */` comment, `//` comment, `"…"` string and
    `'…'` character literal replaced by spaces — same length, same columns, so
    a position found in the masked text indexes the original line directly.
    `pct_index` is the 0-based index of the `%%` line.

    Masking matters here for more than tidiness: `%type <kwd>` continuation
    lines carry `'/'`, `'%'`, `'{'` and `'}'` as char-literal terminals, and a
    scanner that did not treat them as literals would see comment delimiters
    and `%`-directives that are not there.

    Which is also why `literals[i]` exists: it is the list of `'…'` character
    literals line `i` carries **outside** comments and strings, in column
    order and with their quotes, taken from the original text.  The `%type`
    handler needs them (the masked text has already blanked them), and it must
    not find the `'` in a comment's `don't` or in a `"…'…"` string — which is
    exactly what this one scan, and only this one scan, gets right.
    """
    masked: list[str] = []
    literals: list[list[str]] = []
    in_block = False
    for index, text in enumerate(lines):
        out, in_block, found = _mask_line(text, in_block, index)
        masked.append(out)
        literals.append(found)
        if not in_block and out.strip() == "%%":
            return masked, index, literals
    raise Unsupported("no %% separator: the file has no rules section")


def _mask_line(text: str, in_block: bool, index: int) -> tuple[str, bool, list[str]]:
    found: list[str] = []
    buf = list(text)
    n = len(text)
    i = 0
    while i < n:
        c = text[i]
        if in_block:
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                buf[i] = buf[i + 1] = " "
                i += 2
                in_block = False
            else:
                buf[i] = " "
                i += 1
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            buf[i] = buf[i + 1] = " "
            i += 2
            in_block = True
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            for k in range(i, n):
                buf[k] = " "
            return "".join(buf), in_block, found
        if c in "\"'":
            j = i + 1
            while j < n:
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == c:
                    j += 1
                    break
                j += 1
            else:
                raise Unsupported(
                    f"line {index + 1}: unterminated {c!r} literal in a declaration"
                )
            if c == "'":
                found.append(text[i:j])
            for k in range(i, min(j, n)):
                buf[k] = " "
            i = j
            continue
        i += 1
    return "".join(buf), in_block, found


def mask_c_text(text: str) -> str:
    """Blank the contents of C comments, string literals and character
    literals in a multi-line chunk of C, preserving every character's position
    (newlines stay newlines, everything masked becomes a space).

    Unlike `_mask_line` this never raises.  It is used to *read* an action
    body — to decide whether the body uses its own `$$` — and never to rewrite
    one, so an oddity inside a body (an apostrophe the C compiler is happy
    with but this scanner is not) must not turn the run into an exit 3.
    A quote with no closing quote before the end of its line is therefore
    treated as an ordinary character rather than as the start of a literal.
    """
    buf = list(text)
    n = len(text)
    i = 0
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for k in range(i, end):
                if buf[k] != "\n":
                    buf[k] = " "
            i = end
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            end = text.find("\n", i)
            end = n if end < 0 else end
            for k in range(i, end):
                buf[k] = " "
            i = end
            continue
        if c in "\"'":
            j = i + 1
            closed = False
            while j < n and text[j] != "\n":
                if text[j] == "\\" and j + 1 < n and text[j + 1] != "\n":
                    j += 2
                    continue
                if text[j] == c:
                    j += 1
                    closed = True
                    break
                j += 1
            if not closed:
                i += 1
                continue
            for k in range(i, j):
                buf[k] = " "
            i = j
            continue
        i += 1
    return "".join(buf)


# --------------------------------------------------------------------------
# line edits
# --------------------------------------------------------------------------


class Edits:
    """Column-span replacements, collected per line and applied right-to-left.

    Every construct this transform touches is expressed as one or more spans,
    so several edits can land on the same line (an action and the appended
    action of the alternative that follows it on that line, say) without
    either having to know about the other.  Nothing here ever adds or removes
    a line: that is what keeps the output's line numbering identical to
    `sql_yacc.yy`'s.
    """

    def __init__(self, lines: list[str]) -> None:
        self.lines = lines
        self.spans: dict[int, list[tuple[int, int, str]]] = {}

    def replace(self, index: int, start: int, end: int, text: str) -> None:
        if "\n" in text:
            raise Internal("an edit replacement may not contain a newline")
        self.spans.setdefault(index, []).append((start, end, text))

    def blank(self, index: int) -> None:
        self.replace(index, 0, len(self.lines[index]), "")

    def insert(self, index: int, at: int, text: str) -> None:
        self.replace(index, at, at, text)

    def blank_span(self, l1: int, c1: int, l2: int, c2: int, first: str) -> None:
        """Blank the inclusive span (l1,c1)–(l2,c2), leaving `first` at its
        start and keeping whatever follows the span on line l2."""
        if l1 == l2:
            self.replace(l1, c1, c2 + 1, first)
            return
        self.replace(l1, c1, len(self.lines[l1]), first)
        for index in range(l1 + 1, l2):
            self.blank(index)
        self.replace(l2, 0, c2 + 1, "")

    def blank_span_keeping(self, l1: int, c1: int, l2: int, c2: int,
                           first: str, keep: set[int]) -> None:
        """`blank_span`, except that the lines in `keep` are left untouched
        and the span's closing `}` is written at the start of line l2.

        This is what makes the `preserve_parsing_place` rule line-count
        preserving: nothing is inserted and nothing is removed, the action's
        opening text moves to its own first line, the preserved statements
        stay on THEIR OWN ORIGINAL LINES (so every `sql_yacc.yy:LINE` citation
        still resolves), and every other line of the body is blanked exactly
        as before.  `first` must therefore be an UNCLOSED action opener --
        `{ MDBP_R(n);` -- since the `}` is written here.
        """
        if l1 == l2 or not keep:
            raise Internal("blank_span_keeping needs a multi-line span and "
                           "at least one preserved line")
        self.replace(l1, c1, len(self.lines[l1]), first)
        for index in range(l1 + 1, l2):
            if index not in keep:
                self.blank(index)
        self.replace(l2, 0, c2 + 1, "}")

    def apply(self) -> list[str]:
        out = list(self.lines)
        for index, spans in self.spans.items():
            spans = sorted(spans, key=lambda s: (s[0], s[1]))
            for prev, nxt in zip(spans, spans[1:]):
                if nxt[0] < prev[1]:
                    raise Internal(f"overlapping edits on line {index + 1}")
            text = out[index]
            for start, end, repl in reversed(spans):
                text = text[:start] + repl + text[end:]
            out[index] = text
        return out


# --------------------------------------------------------------------------
# rules-section lexer
# --------------------------------------------------------------------------


class Token:
    __slots__ = ("kind", "text", "l1", "c1", "l2", "c2")

    def __init__(self, kind: str, text: str, l1: int, c1: int, l2: int, c2: int):
        self.kind = kind
        self.text = text
        self.l1 = l1
        self.c1 = c1
        self.l2 = l2
        self.c2 = c2

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return f"<{self.kind} {self.text!r} @{self.l1 + 1}:{self.c1}>"


class RulesLexer:
    """Tokenises the bison rules section.

    Grammar-level literals are tokens in their own right, which is the whole
    point: `'{'`, `'}'`, `';'` and `'|'` appear as terminals in this grammar's
    right-hand sides and must never be read as an action delimiter, a rule
    terminator or an alternative separator.
    """

    def __init__(self, lines: list[str], start: int) -> None:
        self.lines = lines
        self.li = start
        self.ci = 0

    # -- character-level helpers -------------------------------------------
    def _char(self) -> str | None:
        if self.li >= len(self.lines):
            return None
        line = self.lines[self.li]
        return "\n" if self.ci >= len(line) else line[self.ci]

    def _peek(self) -> str | None:
        """The character after the cursor, within the current line only."""
        if self.li >= len(self.lines):
            return None
        line = self.lines[self.li]
        return line[self.ci + 1] if self.ci + 1 < len(line) else None

    def _advance(self) -> None:
        if self.li >= len(self.lines):
            return
        if self.ci >= len(self.lines[self.li]):
            self.li += 1
            self.ci = 0
        else:
            self.ci += 1

    def _here(self) -> str:
        return f"line {self.li + 1}"

    # -- skipping -----------------------------------------------------------
    def _skip_trivia(self) -> None:
        while True:
            c = self._char()
            if c is None:
                return
            if c.isspace():
                self._advance()
                continue
            if c == "/" and self._peek() == "*":
                self._skip_block_comment()
                continue
            if c == "/" and self._peek() == "/":
                self.li += 1
                self.ci = 0
                continue
            return

    def _skip_block_comment(self) -> None:
        start = self._here()
        self._advance()
        self._advance()
        while True:
            c = self._char()
            if c is None:
                raise Unsupported(f"{start}: unterminated /* comment")
            if c == "*" and self._peek() == "/":
                self._advance()
                self._advance()
                return
            self._advance()

    def _consume_literal(self, quote: str) -> None:
        """Consume a `'…'` or `"…"` literal; the cursor lands past its close."""
        start = self._here()
        self._advance()
        while True:
            c = self._char()
            if c is None or c == "\n":
                raise Unsupported(f"{start}: unterminated {quote!r} literal")
            if c == "\\":
                self._advance()
                self._advance()
                continue
            self._advance()
            if c == quote:
                return

    # -- tokens -------------------------------------------------------------
    def tokens(self) -> list[Token]:
        out: list[Token] = []
        while True:
            self._skip_trivia()
            c = self._char()
            if c is None:
                return out
            l1, c1 = self.li, self.ci
            if c == "{":
                l2, c2 = self._consume_action()
                out.append(Token("action", "{}", l1, c1, l2, c2))
                continue
            if c in "\"'":
                self._consume_literal(c)
                out.append(Token("sym", c, l1, c1, self.li, self.ci - 1))
                continue
            if c == ":":
                self._advance()
                out.append(Token("colon", ":", l1, c1, l1, c1))
                continue
            if c == ";":
                self._advance()
                out.append(Token("semi", ";", l1, c1, l1, c1))
                continue
            if c == "|":
                self._advance()
                out.append(Token("pipe", "|", l1, c1, l1, c1))
                continue
            if c == "%":
                word = DIRECTIVE_RE.match(self.lines[self.li][self.ci :])
                name = word.group(0) if word else "%"
                if name != "%prec":
                    raise Unsupported(
                        f"{self._here()}: unrecognised directive {name!r} in the rules section"
                    )
                for _ in range(len(name)):
                    self._advance()
                out.append(Token("prec", name, l1, c1, l1, c1 + len(name) - 1))
                continue
            if IDENT_START_RE.match(c):
                text = IDENT_RE.match(self.lines[self.li][self.ci :]).group(0)
                for _ in range(len(text)):
                    self._advance()
                out.append(Token("sym", text, l1, c1, l1, c1 + len(text) - 1))
                continue
            raise Unsupported(
                f"{self._here()}: unrecognised character {c!r} in the rules section"
            )

    def _consume_action(self) -> tuple[int, int]:
        """Consume `{ … }` as C, returning the (line, col) of its closing `}`.

        C comments and literals are lexed, not scanned for braces: an action
        body in this file contains `/* } */`, `// }` and `"}"` — and a plain
        brace counter would end the action in the wrong place and still
        produce a grammar bison compiles.
        """
        start = self._here()
        depth = 0
        while True:
            c = self._char()
            if c is None:
                raise Unsupported(f"{start}: unterminated action body")
            if c == "/" and self._peek() == "*":
                self._skip_block_comment()
                continue
            if c == "/" and self._peek() == "/":
                self.li += 1
                self.ci = 0
                continue
            if c in "\"'":
                self._consume_literal(c)
                continue
            if c == "{":
                depth += 1
                self._advance()
                continue
            if c == "}":
                depth -= 1
                here = (self.li, self.ci)
                self._advance()
                if depth == 0:
                    return here
                continue
            self._advance()


# --------------------------------------------------------------------------
# the transform
# --------------------------------------------------------------------------

# `$$ = …` / `$<tag>$ = …`, but not `$$ ==`.
SELF_VALUE_ASSIGN_RE = re.compile(r"\$(?:<[^>]*>)?\$\s*=(?!=)")
# `$k` / `$<tag>k` — a reference to RHS position k.
POSITION_REF_RE = re.compile(r"\$(?:<[^>]*>)?(\d+)")


def _rhs_positions(alt: list[Token]) -> dict[int, int]:
    """`id(token) -> 1-based RHS position` for one alternative.

    Bison counts a mid-rule action as an RHS position of its own — that is why
    `a: X { m } Y { $<t>2 }` refers to the mid-rule as `$2` — so actions are
    numbered alongside symbols.  A `%prec` and the symbol naming its
    precedence are not RHS symbols and are skipped, wherever in the
    alternative they sit.
    """
    positions: dict[int, int] = {}
    k = 0
    skip = False
    for tok in alt:
        if tok.kind == "prec":
            skip = True
            continue
        if skip:
            skip = False
            continue
        k += 1
        positions[id(tok)] = k
    return positions


# --------------------------------------------------------------------------
# the named preserve rules
#
# One entry per rule, in `tools/lexslice.py`'s `REWRITE_RULES` style: the
# rule is a function, it is applied by this script and nowhere else, and EVERY
# application lands in the manifest (stub-map.json's `preserve` block) with
# the `sql_yacc.yy` line it was applied at.  Every site the rule does NOT lift
# lands there too, with the reason.
# --------------------------------------------------------------------------

RULE_PRESERVE_PARSING_PLACE = "preserve_parsing_place"

#: The macro block the rule writes into the grammar's FIRST `%{ … %}`
#: prologue, on lines the transform would otherwise blank, so the file's line
#: count is unchanged.  It resolves the five spellings `sql_yacc.yy` uses to
#: reach the `SELECT_LEX` whose `parsing_place` MariaDB's lexer reads --
#: `Select->`, `sel->`, `Lex->current_select->`, `lex->current_select->` and
#: `thd->lex->current_select->` -- onto `src/stub.h`'s `mdbp_ctx`, so that the
#: preserved statements below compile UNMODIFIED.  `Lex` and `Select` keep
#: MariaDB's own shape (`sql_yacc.yy:33` and `:35`); `thd` and `lex` are ours,
#: because this grammar's `%parse-param` is `mdbp_ctx *ctx`, not `THD *thd`,
#: and `mdbp_ctx` therefore carries a self-pointer named `ctx` so that
#: `thd->lex` reads as `ctx->ctx` (see src/stub.h).
#:
#: They are emitted HERE and not in src/stub.h on purpose: src/stub.h is also
#: read by the C++ lexer translation unit (src/mdbp_lex.cc, through
#: src/lexshim.h and src/mdbp_lex.h), which uses `lex`, `sel` and `thd` as
#: ORDINARY IDENTIFIERS.  A macro on those names in a shared header would
#: rewrite that file's own declarations.  In the grammar's prologue they can
#: reach nothing but bison's generated parser, which declares no identifier of
#: any of these five names (checked against bison 3.8.2's parser.c/parser.h).
#:
#: The block is emitted into yy_stub.yy, so its text is frozen: change it only
#: together with the committed generated/yy_stub.yy.
PARSING_PLACE_MACROS = [
    "/* stub.py rule preserve_parsing_place: the five spellings sql_yacc.yy",
    "   uses to reach the SELECT_LEX whose parsing_place MariaDB's lexer",
    "   reads, on src/stub.h's mdbp_ctx.  Lex and Select keep sql_yacc.yy:33",
    "   and :35's shape; thd and lex are ours (the %parse-param is",
    "   `mdbp_ctx *ctx`, and mdbp_ctx carries a self-pointer named ctx). */",
    "#define thd ctx",
    "#define lex ctx",
    "#define Lex (thd->lex)",
    "#define Select Lex->current_select",
    "#define sel Select",
]

#: The two fields a preserved statement may touch.
PARSING_PLACE_FIELDS = ("parsing_place", "save_parsing_place")

#: Every identifier a preserved statement may mention besides an
#: `enum_parsing_place` constant: the five paths' words, the two fields, and
#: the one control keyword `sql_yacc.yy:16367` needs.  `else` is deliberately
#: absent -- see `_chunk_reason`.
PARSING_PLACE_WORDS = frozenset({
    "Select", "sel", "Lex", "lex", "thd", "current_select",
    "parsing_place", "save_parsing_place", "if",
})

#: `enum enum_parsing_place`, sql/sql_priv.h:327.  src/stub.h carries the same
#: list; keep the two in step.  A value outside it is NOT lifted, it is
#: reported.
PARSING_PLACE_VALUES = frozenset({
    "NO_MATTER", "IN_HAVING", "SELECT_LIST", "IN_WHERE", "IN_ON",
    "IN_GROUP_BY", "IN_ORDER_BY", "IN_UPDATE_ON_DUP_KEY", "IN_PART_FUNC",
    "BEFORE_OPT_LIST", "AFTER_LIST", "FOR_LOOP_BOUND", "IN_RETURNING",
})

#: Everything a preserved statement may carry that is not an identifier or
#: whitespace: the arrow, assignment, the two equality operators and the two
#: logical connectives, parentheses, and the terminating semicolon.  Note what
#: is NOT here: `$` (a bison value reference), `,`, `{`, `}` and every
#: arithmetic operator.  A statement carrying any of them is reported, never
#: lifted.
PARSING_PLACE_PUNCT = frozenset("-<>=!&|();")

_IDENT_TOKEN_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_PARSING_PLACE_RE = re.compile(r"parsing_place")


def _body_masked(lines: list[str], l1: int, c1: int, l2: int, c2: int) -> list[str]:
    """The action body's lines, COMMENT- AND LITERAL-MASKED, every
    character still at its original column, and everything outside the `{ … }` blanked.

    Returned as one entry per file line l1..l2, so `out[k]` is line `l1 + k`
    and a column found in it indexes `lines[l1 + k]` directly.
    """
    body = [lines[i] for i in range(l1, l2 + 1)]
    body[0] = " " * c1 + body[0][c1:]
    body[-1] = body[-1][: c2 + 1]
    return mask_c_text("\n".join(body)).split("\n")


def _chunks(masked: list[str]) -> list[tuple[int, int, int, int]]:
    """The body's `;`-terminated statements, as (l, c, l2, c2) index pairs into
    `masked` (inclusive of the `;`).

    Split at a `;` that is not inside parentheses or braces; a chunk that
    contains a brace at all is rejected downstream, so a compound statement is
    reported rather than half-lifted.  The scan starts just after the opening
    `{` and stops at the closing `}`.
    """
    out: list[tuple[int, int, int, int]] = []
    depth = 0
    start: tuple[int, int] | None = None
    for li, text in enumerate(masked):
        for ci, ch in enumerate(text):
            if li == 0 and ch == "{" and start is None and depth == 0:
                depth = 1
                continue
            if start is None and not ch.isspace():
                start = (li, ci)
            if ch in "({":
                depth += 1
            elif ch in ")}":
                depth -= 1
                if depth == 0:
                    # the action's own closing brace: nothing follows it
                    return out
                if ch == "}" and depth == 1:
                    # A BLOCK also ends a statement in C -- `if (…) { … }`
                    # carries no trailing `;` (sql_yacc.yy:14771-14777 is one,
                    # three statements above a liftable site).  Without this
                    # the run would swallow everything up to the next `;` and
                    # report a perfectly self-contained assignment as
                    # unliftable.  A `}` that does NOT end a statement (an
                    # initialiser's) can only ever split a chunk this rule
                    # then refuses, because the two halves carry `=`-less
                    # punctuation the allow-list rejects.
                    start = None
            elif ch == ";" and depth == 1 and start is not None:
                out.append((start[0], start[1], li, ci))
                start = None
    return out


def _chunk_text(masked: list[str], a: int, b: int, c: int, d: int) -> str:
    if a == c:
        return masked[a][b : d + 1]
    parts = [masked[a][b:]]
    parts.extend(masked[i] for i in range(a + 1, c))
    parts.append(masked[c][: d + 1])
    return "\n".join(parts)


def _chunk_reason(masked: list[str], a: int, b: int, c: int,
                  d: int) -> str | None:
    """None when the chunk may be lifted, else why it may not.

    The tests are deliberately whole-statement tests: a statement is lifted
    only when EVERYTHING it mentions is `parsing_place`/`save_parsing_place`
    on one of the five recognised paths (or an `enum_parsing_place` constant),
    and only when it occupies whole lines strictly inside the action body.
    Anything else is reported and left blanked -- the rule never lifts half a
    statement, and never guesses at one it cannot prove self-contained.
    """
    text = _chunk_text(masked, a, b, c, d)
    if "{" in text or "}" in text:
        return "the statement contains a brace; it is not a simple statement"
    words = _IDENT_TOKEN_RE.findall(text)
    unknown = sorted({w for w in words
                      if w not in PARSING_PLACE_WORDS
                      and w not in PARSING_PLACE_VALUES})
    if unknown:
        return (
            "the statement mentions " + ", ".join(f"`{w}`" for w in unknown)
            + ", which is neither `parsing_place`/`save_parsing_place` on one "
            "of the five recognised paths nor an enum_parsing_place constant"
        )
    stripped = _IDENT_TOKEN_RE.sub("", text)
    bad = sorted({ch for ch in stripped
                  if not ch.isspace() and ch not in PARSING_PLACE_PUNCT})
    if bad:
        return ("the statement carries " + ", ".join(f"`{ch}`" for ch in bad)
                + ", which this rule does not recognise")
    if not (0 < a and c < len(masked) - 1):
        return ("the statement shares a line with the action body's own `{` "
                "or `}`")
    if masked[a][:b].strip() or masked[c][d + 1:].strip():
        return "the statement shares a line with other code"
    # `if (…) x; else y;` would split at the first `;`, so lifting the `if`
    # alone would change behaviour.  Reported, never lifted.
    rest = "\n".join([masked[c][d + 1:]] + masked[c + 1:])
    if re.match(r"(?s)\s*\belse\b", rest):
        return "the statement is the `if` half of an if/else"
    return None


def rule_preserve_parsing_place(lines: list[str], l1: int, c1: int, l2: int,
                                c2: int) -> tuple[list[dict], list[dict]]:
    """Preserve, inside an otherwise-blanked action body, whole STATEMENTS
    touching only `parsing_place` / `save_parsing_place` on one of the five
    paths `sql_yacc.yy` spells them with.

    Returns `(applications, skipped)`.  An application is
    `{"first", "last", "yy_line", "lines", "text"}` -- `first`/`last` are
    0-based file line indices the caller must leave untouched, everything else
    is manifest.  A skipped entry is `{"yy_line", "reason"}`, one per
    `sql_yacc.yy` line carrying a `parsing_place` this rule did not lift.
    """
    masked = _body_masked(lines, l1, c1, l2, c2)
    sites = {l1 + k for k, text in enumerate(masked)
             if _PARSING_PLACE_RE.search(text)}
    if not sites:
        return [], []

    applications: list[dict] = []
    reasons: dict[int, str] = {}
    for a, b, c, d in _chunks(masked):
        if not _PARSING_PLACE_RE.search(_chunk_text(masked, a, b, c, d)):
            continue
        reason = _chunk_reason(masked, a, b, c, d)
        if reason is not None:
            for k in range(a, c + 1):
                if l1 + k in sites:
                    reasons.setdefault(l1 + k, reason)
            continue
        applications.append({
            "rule": RULE_PRESERVE_PARSING_PLACE,
            "first": l1 + a,
            "last": l1 + c,
            "yy_line": l1 + a + 1,
            "lines": [l1 + a + 1, l1 + c + 1],
            "text": "\n".join(lines[l1 + a : l1 + c + 1]).strip(),
        })

    lifted = {i for app in applications for i in range(app["first"], app["last"] + 1)}
    skipped = [
        {"rule": RULE_PRESERVE_PARSING_PLACE, "yy_line": line + 1,
         "reason": reasons.get(
             line, "the `parsing_place` here is not inside a `;`-terminated "
                   "statement of this action body")}
        for line in sorted(sites - lifted)
    ]
    return applications, skipped

#: The registry.  One entry today; the list is the interface, so a second rule
#: is added here and nowhere else.
PRESERVE_RULES = [(RULE_PRESERVE_PARSING_PLACE, rule_preserve_parsing_place)]


class Transform:
    def __init__(self, lines: list[str], literals: list[list[str]]) -> None:
        self.lines = lines
        # `literals[i]`: the `'…'` character literals line i carries outside
        # comments and strings, from `mask_declarations`.  Only the
        # declarations section is populated; the rules section is lexed
        # separately and needs none of this.
        self.literals = literals
        self.edits = Edits(lines)
        self.entries: dict[str, dict] = {}
        self.serial = 0
        # Character literals already mentioned by a declaration line that
        # *survives* into the output, in file order.  Bison numbers symbols by
        # first mention, so this is what decides whether a `%type`'s literals
        # need a `%token` of their own — and re-declaring one that a previous
        # `%token` already declared is a bison warning, which this pipeline
        # treats as a failure.
        self.seen_literals: set[str] = set()
        # The `preserve_parsing_place` manifest (stub-map.json's `preserve`
        # block): every application with its sql_yacc.yy line, and every
        # site the rule did NOT lift with the reason it did not.
        self.preserve: dict = {
            "rule": RULE_PRESERVE_PARSING_PLACE,
            "macro_lines": [],
            # The macro block AS WRITTEN.
            "macro_text": [],
            "applied": [],
            "skipped": [],
            "counts": {},
        }
        # line index -> how many `parsing_place` substrings the line carries
        # OUTSIDE comments and literals, counted PER ACTION BODY.  It is NOT
        # what the completeness check partitions (that is the whole-file
        # census `transform` takes; a per-body census cannot see a write
        # outside every body) -- it is the independent second opinion
        # `_finish_preserve` checks that census against.
        self.pp_code_lines: dict[int, int] = {}

    def _note_literals(self, index: int) -> None:
        """Record the char literals of a line that survives the transform."""
        self.seen_literals.update(self.literals[index])

    # -- declarations -------------------------------------------------------
    def declarations(self, masked: list[str], pct: int) -> None:
        index = 0
        prologues = 0
        while index < pct:
            text = masked[index]
            stripped = text.lstrip()
            if not stripped.startswith("%"):
                # Not a directive: either blank/comment, or a continuation
                # line of the `%token`/`%left`/… above it.  Either way it is
                # copied through untouched, so whatever it mentions is
                # mentioned in the output too.  (A `%type`'s continuation
                # lines never reach here — `_blank_type` consumes them.)
                self._note_literals(index)
                index += 1
                continue
            match = DIRECTIVE_RE.match(stripped)
            name = match.group(0) if match else "%"
            if name == "%{":
                close = self._find_close_prologue(masked, index, pct)
                prologues += 1
                if prologues == 1:
                    self._prologue(index, close)
                else:
                    for i in range(index, close + 1):
                        self.edits.blank(i)
                index = close + 1
                continue
            if name == "%code":
                close = self._blank_braced(masked, index, pct, "")
                index = close + 1
                continue
            if name == "%union":
                close = self._blank_braced(masked, index, pct, UNION_REPLACEMENT)
                index = close + 1
                continue
            if name == "%destructor":
                close = self._blank_braced(masked, index, pct, "", tagged=True)
                index = close + 1
                continue
            if name == "%type":
                index = self._blank_type(masked, index, pct)
                continue
            if name in TAGGED:
                self._strip_tag(masked, index)
                self._note_literals(index)
                index += 1
                continue
            if name in ("%parse-param", "%lex-param"):
                if not PARAM_RE.match(stripped):
                    raise Unsupported(
                        f"line {index + 1}: {name} is not MariaDB's `{{ THD *thd }}`"
                    )
                self.edits.replace(
                    index, 0, len(self.lines[index]), f"{name} {{ {PARAM_CTX} }}"
                )
                index += 1
                continue
            if name in PASSTHROUGH:
                # `%left`/`%right`/`%nonassoc`/`%precedence` survive verbatim,
                # and this file first-mentions nine of its char-literal
                # terminals in them.
                self._note_literals(index)
                index += 1
                continue
            raise Unsupported(f"line {index + 1}: unrecognised directive {name!r}")

    def _prologue(self, index: int, close: int) -> None:
        """The FIRST `%{ … %}` block becomes `%{ #include "stub.h"`, the
        `preserve_parsing_place` macro block, and `%}` -- on the block's own
        first lines, the rest of it blanked.  The line count is unchanged.

        The macros are HERE and not in src/stub.h because src/stub.h is read
        by the C++ lexer translation unit too, and that file uses `lex`, `sel`
        and `thd` as ordinary identifiers (see PARSING_PLACE_MACROS).

        A prologue TOO SHORT to hold the block is not an error by itself:
        the macros only matter to a grammar that has statements to lift, and
        a short prologue is a perfectly ordinary small grammar (a synthetic
        test grammar, say).  Such a prologue gets the one-line
        `%{ #include "stub.h" %}` this transform always wrote, `macro_lines`
        stays empty, and `_finish_preserve` turns "lifted something with no
        macro block" -- the combination that would NOT compile -- into an
        exit 3.  Refusing at the point where it is actually a problem is what
        keeps the refusal true.
        """
        block = [PROLOGUE_OPEN, *PARSING_PLACE_MACROS, PROLOGUE_CLOSE]
        if close - index + 1 < len(block):
            self.edits.replace(index, 0, len(self.lines[index]),
                               PROLOGUE_REPLACEMENT)
            for i in range(index + 1, close + 1):
                self.edits.blank(i)
            self.preserve["macro_prologue_lines"] = close - index + 1
            return
        for offset, text in enumerate(block):
            self.edits.replace(index + offset, 0,
                               len(self.lines[index + offset]), text)
        for i in range(index + len(block), close + 1):
            self.edits.blank(i)
        self.preserve["macro_lines"] = [
            index + 1 + k for k in range(1, len(block) - 1)
        ]
        self.preserve["macro_text"] = list(PARSING_PLACE_MACROS)

    def _find_close_prologue(self, masked: list[str], index: int, pct: int) -> int:
        for i in range(index + 1, pct):
            if masked[i].strip() == "%}":
                return i
        raise Unsupported(f"line {index + 1}: unterminated %{{ prologue block")

    def _blank_braced(
        self, masked: list[str], index: int, pct: int, first: str, tagged: bool = False
    ) -> int:
        """Blank a `%directive … { … }` construct, `first` replacing its first
        line.  Returns the index of the closing brace's line."""
        open_at = self._find_open_brace(masked, index, pct)
        close_line, close_col = self._match_brace(masked, *open_at, pct)
        rest = masked[close_line][close_col + 1 :]
        if tagged:
            if not re.fullmatch(r"\s*<[^>]*>\s*", rest):
                raise Unsupported(
                    f"line {close_line + 1}: %destructor is not followed by a single <tag>"
                )
        elif rest.strip():
            raise Unsupported(
                f"line {close_line + 1}: unexpected text after the closing brace"
            )
        self.edits.replace(index, 0, len(self.lines[index]), first)
        for i in range(index + 1, close_line + 1):
            self.edits.blank(i)
        return close_line

    def _find_open_brace(self, masked: list[str], index: int, pct: int) -> tuple[int, int]:
        for i in range(index, pct):
            start = 0
            if i == index:
                start = masked[i].index("%")
            col = masked[i].find("{", start)
            if col >= 0:
                return i, col
            if i > index and masked[i].strip():
                raise Unsupported(
                    f"line {i + 1}: expected `{{` after the directive on line {index + 1}"
                )
        raise Unsupported(f"line {index + 1}: directive has no `{{` body")

    def _match_brace(
        self, masked: list[str], line: int, col: int, pct: int
    ) -> tuple[int, int]:
        depth = 0
        for i in range(line, pct):
            text = masked[i]
            for j in range(col if i == line else 0, len(text)):
                if text[j] == "{":
                    depth += 1
                elif text[j] == "}":
                    depth -= 1
                    if depth == 0:
                        return i, j
        raise Unsupported(f"line {line + 1}: unbalanced braces in a declaration")

    def _blank_type(self, masked: list[str], index: int, pct: int) -> int:
        """Blank a `%type` declaration and its continuation lines, re-emitting
        its *character-literal* terminals as a `%token` on its first line when
        they would otherwise lose their first mention.

        A `%type` declaration extends from its `%type` line through the
        following lines up to (but not including) the next line whose first
        non-blank character is `%`.  That is only safe because everything
        between declarations here is comment or blank; each swallowed line is
        checked against that claim (masked, so comments and char-literal
        terminals are already blanks) and an unexpected one exits 3 rather
        than being blanked on a guess.

        The `%token` is the module docstring's documented exception, and it is
        narrow on purpose: only `'…'` literals, only the ones no surviving
        earlier declaration line has mentioned, in their original order, on the
        construct's first line (which is numbering-neutral because the rest of
        the construct first-mentions nothing else), with no `<tag>`, and
        nothing at all when the filter leaves nothing — an empty `%token` line
        is a bison syntax error.  Named symbols in the list stay blanked: a
        `%token` on a nonterminal would be catastrophic.
        """
        end = index + 1
        while end < pct and not masked[end].lstrip().startswith("%"):
            if not TYPE_CONT_RE.match(masked[end]):
                raise Unsupported(
                    f"line {end + 1}: unexpected content in the %type declaration "
                    f"starting on line {index + 1}"
                )
            end += 1

        ordered: list[str] = []
        for i in range(index, end):
            for literal in self.literals[i]:
                if literal not in ordered:
                    ordered.append(literal)
        fresh = [lit for lit in ordered if lit not in self.seen_literals]
        first = f"%token {' '.join(fresh)}" if fresh else ""

        self.edits.replace(index, 0, len(self.lines[index]), first)
        for i in range(index + 1, end):
            self.edits.blank(i)
        # Everything the construct named is now mentioned in the output: the
        # already-seen ones by whatever declared them, the rest by the line
        # just emitted.  A later %type listing the same literal must not
        # re-declare it.
        self.seen_literals.update(ordered)
        return end

    def _strip_tag(self, masked: list[str], index: int) -> None:
        match = re.match(r"^(\s*%(?:token|nterm))(\s*)(<[^>]*>)", masked[index])
        if match is None:
            return
        self.edits.replace(index, match.start(3), match.end(3), "")

    # -- rules --------------------------------------------------------------
    def rules(self, pct: int) -> None:
        tokens = RulesLexer(self.lines, pct + 1).tokens()
        i = 0
        n = len(tokens)
        while i < n:
            # A stray `;` between rules is legal bison and occurs in this file
            # (`sql_yacc.yy:10789`–`:10790` terminate one rule twice).  It is a
            # separator, not an alternative, so it must not gain an action.
            if tokens[i].kind == "semi":
                i += 1
                continue
            if not (
                tokens[i].kind == "sym"
                and i + 1 < n
                and tokens[i + 1].kind == "colon"
            ):
                raise Unsupported(
                    f"line {tokens[i].l1 + 1}: expected `<nonterminal> :`, "
                    f"found {tokens[i].text!r}"
                )
            lhs = tokens[i].text
            opener = tokens[i + 1]
            i += 2
            alt: list[Token] = []
            while True:
                if i >= n:
                    self._alternative(lhs, alt, opener)
                    break
                tok = tokens[i]
                # A rule may be left unterminated: bison ends it at the next
                # `<nonterminal> :`.  Handle it rather than swallowing the
                # next rule's head into this alternative.
                if (
                    tok.kind == "sym"
                    and i + 1 < n
                    and tokens[i + 1].kind == "colon"
                    and alt
                ):
                    self._alternative(lhs, alt, opener)
                    break
                if tok.kind in ("pipe", "semi"):
                    self._alternative(lhs, alt, opener)
                    alt = []
                    opener = tok
                    i += 1
                    if tok.kind == "semi":
                        break
                    continue
                # A `:` is never an RHS symbol.  It gets here only when the
                # `<nonterminal> :` test above declined to start a new rule --
                # i.e. an unterminated rule whose last alternative is empty,
                # `rule: FOO | <newline> other: BAR ;`.  Both readings (an
                # empty final alternative of `rule`, or `| other` as an
                # alternative) are guesses, so exit 3 rather than guess:
                # swallowing it silently would attribute `other`'s
                # alternative to `rule` and leave a rule with no MDBP_R.
                if tok.kind == "colon":
                    raise Unsupported(
                        f"line {tok.l1 + 1}: unexpected `:` in an alternative of "
                        f"`{lhs}` (unterminated rule with an empty final "
                        f"alternative?)"
                    )
                alt.append(tok)
                i += 1

    def _alternative(self, lhs: str, alt: list[Token], opener: Token) -> None:
        # A trailing `%prec X` is not an RHS symbol for the purpose of
        # "is this action final": `/* empty */ {} %prec X` is a final action.
        tail = alt
        if len(alt) >= 2 and alt[-2].kind == "prec":
            tail = alt[:-2]
        final = tail[-1] if tail else None
        valued = self._valued_midrule_positions(alt)
        positions = _rhs_positions(alt)
        for tok in alt:
            if tok.kind != "action":
                continue
            midrule = tok is not final
            self._record(tok.l1, lhs, midrule, added=False)
            stub = self._stub(
                self.serial - 1,
                keep_value=midrule and positions.get(id(tok)) in valued,
            )
            keep = self._preserve(lhs, tok)
            if keep:
                self.edits.blank_span_keeping(
                    tok.l1, tok.c1, tok.l2, tok.c2, stub[:-2].rstrip(), keep
                )
            else:
                self.edits.blank_span(tok.l1, tok.c1, tok.l2, tok.c2, stub)
        if final is not None and final.kind == "action":
            return
        anchor = alt[-1] if alt else opener
        self._record(anchor.l2, lhs, midrule=False, added=True)
        self.edits.insert(anchor.l2, anchor.c2 + 1, " " + self._stub(self.serial - 1))

    def _preserve(self, lhs: str, tok: Token) -> set[int]:
        """Apply every preserve rule to one action body.

        Returns the set of 0-based line indices the caller must leave
        UNTOUCHED; the manifest records each application and each site the
        rules declined, with its reason.  Also counts, per line, the
        `parsing_place` substrings the body carries outside comments and
        literals -- the second opinion `_finish_preserve` checks the
        whole-file census against, never the census itself.
        """
        masked = _body_masked(self.lines, tok.l1, tok.c1, tok.l2, tok.c2)
        for offset, text in enumerate(masked):
            hits = len(_PARSING_PLACE_RE.findall(text))
            if hits:
                self.pp_code_lines[tok.l1 + offset] = hits
        keep: set[int] = set()
        for rule_name, rule in PRESERVE_RULES:
            applications, skipped = rule(self.lines, tok.l1, tok.c1, tok.l2,
                                         tok.c2)
            for app in applications:
                keep.update(range(app["first"], app["last"] + 1))
                self.preserve["applied"].append(
                    {"rule": rule_name, "serial": self.serial - 1,
                     "rule_lhs": lhs, "action_line": tok.l1 + 1,
                     "yy_line": app["yy_line"], "lines": app["lines"],
                     "text": app["text"]}
                )
            self.preserve["skipped"].extend(skipped)
        return keep

    def _finish_preserve(self, raw_lines: dict[int, int],
                         code_lines: dict[int, int]) -> None:
        """Close the manifest, and PROVE the enumeration is complete.

        `raw_lines` is every line of the INPUT that carries the substring
        `parsing_place` at all, comments included.  `code_lines` is the subset
        of them that carry it as CODE, decided by masking THE WHOLE FILE
        (`transform` takes it with `mask_c_text`).  Each raw line must be
        either lifted, or reported with a reason, or comment-only, and the
        three must partition `raw_lines` exactly -- anything else is an exit 1
        rather than a manifest that quietly does not add up.

        THE WHOLE-FILE MASK IS WHAT MAKES THAT REFUSAL REAL.  If
        `comment_only` were `raw_lines` minus the lines the PER-ACTION-BODY
        scan had seen (`pp_code_lines`, filled by `_preserve`, which runs once
        per action body), a `parsing_place` write OUTSIDE every action body --
        the one thing no rule here can ever see -- would be bucketed as a
        comment, `accounted` would equal `set(raw_lines)` by construction, and
        this refusal could never fire.  Under the whole-file mask such a line
        is code, is neither lifted nor reported, and is an exit 1.  On the
        pinned revision one site is reported and five lines are comment-only.
        """
        lifted_lines: set[int] = set()
        for app in self.preserve["applied"]:
            lifted_lines.update(range(app["lines"][0] - 1, app["lines"][1]))
        lifted_lines &= set(code_lines)
        skipped_lines = {entry["yy_line"] - 1
                         for entry in self.preserve["skipped"]}
        comment_only = sorted(
            line + 1 for line in set(raw_lines) - set(code_lines)
        )
        # The two censuses are two independent maskings of the same text --
        # one action body at a time (`_body_masked`, which also blanks
        # everything outside the `{ … }`) and the whole file at once -- so each
        # is a check on the other.  Every occurrence a body saw must be one the
        # whole-file mask sees, on the same line and in the same number; a
        # disagreement means one of the two maskings is wrong, and nothing
        # computed from either can be trusted after that.
        disagree = sorted(line + 1 for line, hits in self.pp_code_lines.items()
                          if code_lines.get(line) != hits)
        if disagree:
            raise Internal(
                "the per-action-body and the whole-file `parsing_place` "
                f"censuses disagree at line(s) {disagree}"
            )
        self.preserve["comment_only_lines"] = comment_only
        self.preserve["counts"] = {
            "input_lines": len(raw_lines),
            "input_occurrences": sum(raw_lines.values()),
            "lifted_statements": len(self.preserve["applied"]),
            "lifted_lines": len(lifted_lines),
            "lifted_occurrences": sum(code_lines[line]
                                      for line in lifted_lines),
            "skipped_lines": len(skipped_lines),
            "comment_only_lines": len(comment_only),
        }
        accounted = lifted_lines | skipped_lines | {c - 1 for c in comment_only}
        if accounted != set(raw_lines):
            missing = sorted(line + 1 for line in set(raw_lines) - accounted)
            extra = sorted(line + 1 for line in accounted - set(raw_lines))
            raise Internal(
                f"the {RULE_PRESERVE_PARSING_PLACE} manifest does not account "
                f"for every `parsing_place` line: unaccounted {missing} "
                "(code this rule neither lifted nor reported -- a write "
                "outside every action body is the usual cause, and no rule "
                "here can see one), accounted but absent from the input "
                f"{extra}"
            )
        if self.preserve["applied"] and not self.preserve["macro_lines"]:
            raise Unsupported(
                f"{RULE_PRESERVE_PARSING_PLACE} lifted "
                f"{len(self.preserve['applied'])} statement(s), but the "
                f"grammar's first %{{ prologue is only "
                f"{self.preserve.get('macro_prologue_lines', 0)} lines -- too "
                f"short to carry the "
                f"{len(PARSING_PLACE_MACROS)} macro lines they need in order "
                "to compile, without changing the file's line count"
            )
        if lifted_lines & skipped_lines:
            raise Internal(
                "a `parsing_place` line is both lifted and skipped: "
                f"{sorted(line + 1 for line in lifted_lines & skipped_lines)}"
            )

    def _action_text(self, tok: Token) -> str:
        """The original source text of an action body, `{` through `}`."""
        if tok.l1 == tok.l2:
            return self.lines[tok.l1][tok.c1 : tok.c2 + 1]
        parts = [self.lines[tok.l1][tok.c1 :]]
        parts.extend(self.lines[i] for i in range(tok.l1 + 1, tok.l2))
        parts.append(self.lines[tok.l2][: tok.c2 + 1])
        return "\n".join(parts)

    def _valued_midrule_positions(self, alt: list[Token]) -> set[int]:
        """The RHS positions of this alternative whose *mid-rule* value the
        original grammar used — read out of the original action bodies.

        Two ways a mid-rule's value is used, which are bison's own two:
        the mid-rule action assigns its own `$$` / `$<tag>$`, or a *later*
        action of the same alternative reads that position as `$k` /
        `$<tag>k`.  Bodies are masked first, so a `$$` inside a comment or a
        string does not count.

        Note the positions returned are of every action in the alternative,
        including the final one; the caller only consults the mid-rule ones.
        A `$k` is attributed only when `k` is *before* the referring action —
        a reference to a later position is not a thing bison allows, and
        silently accepting one would over-select.
        """
        positions = _rhs_positions(alt)
        bodies = [
            (positions[id(tok)], mask_c_text(self._action_text(tok)))
            for tok in alt
            if tok.kind == "action" and id(tok) in positions
        ]
        valued: set[int] = set()
        for position, body in bodies:
            if SELF_VALUE_ASSIGN_RE.search(body):
                valued.add(position)
            for match in POSITION_REF_RE.finditer(body):
                referenced = int(match.group(1))
                if referenced < position:
                    valued.add(referenced)
        return valued

    def _stub(self, serial: int, keep_value: bool = False) -> str:
        if keep_value:
            # `$$ = 0;` is what makes bison name this mid-rule's synthesised
            # placeholder `@N` rather than `$@N` (measured on bison 3.8.2), and
            # the placeholder's name is a goto key in the automaton. Valid
            # under `%define api.value.type {int}`, and automaton-neutral.
            return f"{{ $$ = 0; MDBP_R({serial}); }}"
        return f"{{ MDBP_R({serial}); }}"

    def _record(self, line: int, lhs: str, midrule: bool, added: bool) -> None:
        self.entries[str(self.serial)] = {
            "yy_line": line + 1,
            "rule_lhs": lhs,
            "midrule": midrule,
            "added": added,
        }
        self.serial += 1

    # -- counts -------------------------------------------------------------
    def counts(self) -> dict[str, int]:
        """The three kinds of serial, partitioned so that
        `serials == midrule + replaced + added` holds exactly:
        `midrule` counts every mid-rule action (always a replaced body),
        `replaced` counts the *final* actions whose body was replaced, and
        `added` counts the final actions this transform appended."""
        midrule = sum(1 for e in self.entries.values() if e["midrule"])
        added = sum(1 for e in self.entries.values() if e["added"])
        return {
            "serials": len(self.entries),
            "midrule": midrule,
            "replaced": len(self.entries) - midrule - added,
            "added": added,
        }


def transform(
    lines: list[str],
) -> tuple[list[str], dict[str, dict], dict[str, int], dict]:
    masked, pct, literals = mask_declarations(lines)
    job = Transform(lines, literals)
    job.declarations(masked, pct)
    job.rules(pct)
    # The census the manifest is checked against: every line of the INPUT
    # carrying the substring `parsing_place`, comments and all -- the same
    # thing `grep -n parsing_place` reports, so a reader can reproduce it.
    raw = {i: len(_PARSING_PLACE_RE.findall(text))
           for i, text in enumerate(lines) if _PARSING_PLACE_RE.search(text)}
    # ...and which of those lines carry it as CODE, taken over the WHOLE FILE
    # rather than one action body at a time.  That is the difference between a
    # completeness claim that holds by construction and one that can fail: see
    # `_finish_preserve`.  `mask_c_text` keeps every
    # character at its own position and every newline a newline, so line k of
    # the mask is line k of the input.
    masked_file = mask_c_text("\n".join(lines)).split("\n")
    if len(masked_file) != len(lines):
        raise Internal(f"the whole-file mask has {len(masked_file)} lines, "
                       f"the input {len(lines)}")
    code = {i: len(_PARSING_PLACE_RE.findall(text))
            for i, text in enumerate(masked_file)
            if _PARSING_PLACE_RE.search(text)}
    job._finish_preserve(raw, code)
    out = job.edits.apply()
    if len(out) != len(lines):
        raise Internal(f"line count changed: {len(lines)} in, {len(out)} out")
    # The other half of the claim: every occurrence the manifest says was
    # lifted is in the OUTPUT, and nothing else is.
    macros = set(job.preserve["macro_lines"])
    produced = sum(len(_PARSING_PLACE_RE.findall(text))
                   for number, text in enumerate(out, start=1)
                   if number not in macros)
    expected = job.preserve["counts"]["lifted_occurrences"]
    if produced != expected:
        raise Internal(
            f"the output carries {produced} `parsing_place` occurrences; the "
            f"{RULE_PRESERVE_PARSING_PLACE} manifest claims {expected}"
        )
    return out, job.entries, job.counts(), job.preserve


# --------------------------------------------------------------------------
# modes
# --------------------------------------------------------------------------


def read_lines(path: Path) -> list[str]:
    try:
        text = path.read_text()
    except OSError as exc:
        print(f"stub.py: cannot read {path}: {exc}", file=sys.stderr)
        raise SystemExit(EXIT_USAGE) from exc
    return text.split("\n")


def variant_of(path: Path) -> str:
    match = re.fullmatch(r"yy_(.+)\.yy", path.name)
    return match.group(1).upper() if match else "UNKNOWN"


def run_transform(args) -> int:
    src = Path(args.inp)
    lines = read_lines(src)
    out, entries, counts, preserve = transform(lines)
    if args.check:
        print(f"stub.py --check: {src} transforms cleanly ({counts['serials']} serials, "
              f"{preserve['counts']['lifted_statements']} preserved statements, "
              f"{preserve['counts']['skipped_lines']} reported)")
        return EXIT_OK
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text("\n".join(out))
    if args.map:
        common.write_json(
            Path(args.map),
            {
                "schema_version": STUB_MAP_SCHEMA_VERSION,
                "source": str(src),
                "variant": variant_of(src),
                "entries": entries,
                "counts": counts,
                # The named preserve rules' manifest: every application with
                # its sql_yacc.yy line, and every parsing_place site NOT
                # lifted with the reason.
                "preserve": preserve,
            },
        )
    written = len(out) - 1 if out and out[-1] == "" else len(out)
    also = f" and {args.map}" if args.map else ""
    print(
        f"stub.py: wrote {args.out} ({written} lines){also} "
        f"({counts['serials']} serials, {counts['midrule']} midrule, "
        f"{counts['replaced']} replaced, {counts['added']} added; "
        f"{preserve['counts']['lifted_statements']} statements preserved by "
        f"{RULE_PRESERVE_PARSING_PLACE}, {preserve['counts']['skipped_lines']} "
        f"sites reported)"
    )
    return EXIT_OK


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="stub.py", description="stub-transform a bison grammar"
    )
    parser.add_argument("--in", dest="inp", required=True,
                        help="input .yy grammar (gen_yy_files.cmake's output)")
    parser.add_argument("--out", help="output .yy path")
    parser.add_argument("--map", help="optional stub-map.json debugging manifest")
    parser.add_argument(
        "--check",
        action="store_true",
        help="transform in memory, write nothing, exit 3 on any unrecognised construct",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.check:
        if args.out or args.map:
            parser.error("--check writes nothing; drop --out/--map")
    elif not args.out:
        parser.error("--in needs --out (or use --check)")
    try:
        return run_transform(args)
    except Unsupported as exc:
        print(f"stub.py: unsupported: {exc}", file=sys.stderr)
        return EXIT_UNSUPPORTED
    except Internal as exc:
        print(f"stub.py: internal error: {exc}", file=sys.stderr)
        return EXIT_INTERNAL


if __name__ == "__main__":
    sys.exit(main())
