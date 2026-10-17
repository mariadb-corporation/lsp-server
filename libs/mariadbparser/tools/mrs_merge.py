#!/usr/bin/env python3
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

"""One-shot merge of MariaDB Shell's REST SQL grammar into generated/.

Takes the rules between /* START OF MERGE PART */ and /* END OF MERGE PART */
of the shell's mysqlshdk/libs/mrs/parser.yy and appends them, translated,
to generated/yy_stub.yy; new keywords are appended to generated/keywords.h.
Like tools/stub.py it is run by hand, once per merge, never by the build;
UPSTREAM.md records the shell commit and the result.

The translation:
  * every rule name gets the prefix mrs_ (the shell's names, such as
    opt_if_exists or user_name, also exist in the server grammar);
  * action bodies are dropped and every alternative gets one
    { MDBP_R(n); } action, as tools/stub.py does for the server rules;
  * a keyword token is replaced by the server's token for the same spelling
    (generated/keywords.h); a spelling the server does not know becomes a
    new <WORD>_SYM token, added to the keyword table and to the server's
    keyword_sp_var_and_label, so it stays usable as an identifier;
  * punctuation tokens become the server's character tokens;
  * value tokens become the server's: quoted texts TEXT_STRING, back-ticked
    names IDENT_QUOTED, numbers NUM/LONG_NUM/ULONGLONG_NUM (mrs_int_number),
    DECIMAL_NUM and FLOAT_NUM;
  * the shell's request path token becomes mrs_request_path ('/' and a
    segment that may be any word, since the shell lexes a path as one token),
    and every @ANNOTATION token becomes mrs_annotation ('@' LEX_HOSTNAME):
    the server lexer has neither token, and which annotation is allowed
    where is no longer checked (the parser accepts a superset);
  * the shell's helper rules after the merge part are rebuilt to accept what
    the shell accepts: mrs_identifier is a plain identifier, every server
    keyword the REST rules do not use (the shell lexes those words as
    identifiers), and the shell's five identifier keywords;
    mrs_text_or_identifier adds quoted text and the LEX_HOSTNAME the server
    lexer produces after '@'; text_string_literal becomes TEXT_STRING;
  * %nterm type declarations and %prec PREC_BELOW_NOCHECK are dropped (types
    do not apply to the stubbed grammar; the precedence token does not exist);
  * alternatives that became identical are merged;
  * new keywords that begin a statement (CLONE, CONFIGURE) go into the
    server's keyword_verb_clause instead, as its own statement keywords do;
  * GRANT REST and REVOKE REST go through the server's empty
    clear_privileges, and the REST SHOW statements are hooked into the
    server's show_param without their SHOW, so the server and REST paths
    share the empty steps the server takes right after those words;
  * an alternative ending in '@' (the developer prefix "mike@/svc") gets the
    LEX_HOSTNAME the server lexer always emits after '@';
  * a data-mapping field's mode, options, datatype and CRUD options become
    one list of annotations (they are distinct tokens only in the shell);
  * rules nothing reaches any more are dropped;
  * mrs_statement is added to the server's verb_clause.

usage: mrs_merge.py --parser-yy SHELL/mysqlshdk/libs/mrs/parser.yy
                    [--generated DIR] [--dry-run]
"""

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common  # noqa: E402

PUNCTUATION = {
    "EQUAL_OPERATOR": "'='", "COMMA_SYMBOL": "','", "DOT_SYMBOL": "'.'",
    "OPEN_PAR_SYMBOL": "'('", "CLOSE_PAR_SYMBOL": "')'",
    "OPEN_CURLY_SYMBOL": "'{'", "CLOSE_CURLY_SYMBOL": "'}'",
    "OPEN_SQUARE_SYMBOL": "'['", "CLOSE_SQUARE_SYMBOL": "']'",
    "COLON_SYMBOL": "':'", "PLUS_OPERATOR": "'+'", "MINUS_OPERATOR": "'-'",
    "MULT_OPERATOR": "'*'", "DIV_OPERATOR": "'/'", "SEMICOLON_SYMBOL": "';'",
    "AT_SIGN_SYMBOL": "'@'",
}
VALUES = {
    "SINGLE_QUOTED_TEXT": "TEXT_STRING", "DOUBLE_QUOTED_TEXT": "TEXT_STRING",
    "BACK_TICK_QUOTED_ID": "IDENT_QUOTED", "INT_NUMBER": "mrs_int_number",
    "DECIMAL_NUMBER": "DECIMAL_NUM", "FLOAT_NUMBER": "FLOAT_NUM",
    "REST_REQUEST_PATH": "mrs_request_path",
}
HELPERS = {
    "identifier": "mrs_identifier", "schema_name": "mrs_identifier",
    "unquoted_identifier": "mrs_identifier",
    "qualified_identifier": "mrs_qualified_identifier",
    "text_string_literal": "TEXT_STRING",
    "text_or_identifier": "mrs_text_or_identifier",
}
# New keywords that begin a statement: like the server's own (CLOSE, DO,
# ...), they go into keyword_verb_clause, usable as names but not as labels,
# because a label also stands at the start of a statement.
VERB_KEYWORDS = {"CLONE", "CONFIGURE"}

# The shell's identifier_keyword: keywords it allows as identifiers.
IDENTIFIER_KEYWORDS = ("FILES_SYMBOL", "VENDORS_SYMBOL", "COLUMNS_SYMBOL",
                       "DAEMON_SYMBOL", "DAEMONS_SYMBOL")


def strip_actions_and_comments(text):
    """Removes { ... } action bodies (strings and chars inside respected) and
    /* */ and // comments."""
    out = []
    i, depth = 0, 0
    n = len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            out.append(" ")
            continue
        if text.startswith("//", i) and depth == 0:
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if c in "\"'" and (depth > 0 or c == '"'):
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            if depth == 0:
                out.append(text[i:j + 1])
            i = j + 1
            continue
        if c == "'" and depth == 0:
            # a character literal in the grammar, such as ';'
            out.append(text[i:i + 3])
            i += 3
            continue
        if c == "{":
            depth += 1
        elif c == "}" and depth:
            depth -= 1
        elif depth == 0:
            out.append(c)
        i += 1
    return "".join(out)


def parse_rules(text):
    """[(name, [[symbol, ...], ...]), ...] from rule text without actions."""
    tokens = re.findall(r"'[^']'|%empty|%prec|[A-Za-z_][A-Za-z0-9_.]*|[:|;]", text)
    rules = []
    i = 0
    while i < len(tokens):
        name = tokens[i]
        if i + 1 >= len(tokens) or tokens[i + 1] != ":":
            raise SystemExit(f"mrs_merge.py: expected ':' after {name!r}")
        i += 2
        alts, cur = [], []
        while i < len(tokens) and tokens[i] != ";":
            t = tokens[i]
            if t == "|":
                alts.append(cur)
                cur = []
            else:
                cur.append(t)
            i += 1
        alts.append(cur)
        rules.append((name, alts))
        i += 1
    return rules


def shell_token_spellings(parser_yy):
    """KEYWORD_SYMBOL -> "SPELLING" from the shell grammar's %token lines."""
    spell = {}
    for m in re.finditer(r"([A-Z_][A-Z0-9_]*)\s+\"([^\"]+)\"", parser_yy):
        spell.setdefault(m.group(1), m.group(2))
    return spell


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--parser-yy", required=True, type=Path)
    ap.add_argument("--generated", type=Path,
                    default=common.REPO_ROOT / "generated")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    shell = args.parser_yy.read_text()
    start = shell.index("/* START OF MERGE PART */")
    end = shell.index("/* END OF MERGE PART */")
    part = shell[start + 25:end]
    # %nterm <type> lines between the rules: types do not apply here.
    part = re.sub(r"^%nterm[^;]*;", "", part, flags=re.M)
    rules = parse_rules(strip_actions_and_comments(part))
    spell = shell_token_spellings(shell[:shell.index("%%")])

    stub_path = args.generated / "yy_stub.yy"
    kw_path = args.generated / "keywords.h"
    stub = stub_path.read_text()
    keywords = kw_path.read_text()
    if "mrs_statement" in stub:
        raise SystemExit("mrs_merge.py: generated/yy_stub.yy already holds mrs_statement")

    server_kw = dict(re.findall(r'\{ "([A-Z_0-9]+)",\s*SYM\(([A-Z_0-9]+)\)\}', keywords))
    server_tokens = set(re.findall(r"^%token\s+(?:<\w+>\s+)?([A-Z_0-9]+)", stub, re.M))
    names = {name for name, _ in rules}

    new_keywords = {}  # spelling -> token

    def map_symbol(s):
        if s in names:
            return s if s.startswith("mrs_") else "mrs_" + s
        if s in HELPERS:
            return HELPERS[s]
        if s in PUNCTUATION:
            return PUNCTUATION[s]
        if s in VALUES:
            return VALUES[s]
        if s.startswith("AT_") and s.endswith("_SYMBOL") and s != "AT_SIGN_SYMBOL":
            return "mrs_annotation"
        if s.endswith("_SYMBOL"):
            word = spell.get(s)
            if word is None:
                raise SystemExit(f"mrs_merge.py: no spelling for {s}")
            word = word.upper()
            if word in server_kw:
                return server_kw[word]
            tok = word + "_SYM"
            if tok in server_tokens:
                raise SystemExit(f"mrs_merge.py: {tok} exists but is not the keyword {word}")
            new_keywords[word] = tok
            return tok
        if s in ("%empty",):
            return s
        raise SystemExit(f"mrs_merge.py: no translation for {s!r}")

    merged = []
    for name, alts in rules:
        out_alts = []
        for alt in alts:
            syms = []
            skip = False
            for s in alt:
                if skip:
                    skip = False
                    continue
                if s == "%prec":
                    skip = True  # drop "%prec PREC_BELOW_NOCHECK"
                    continue
                syms.append(map_symbol(s))
            # An empty alternative is written as the server grammar writes it,
            # without %empty (whose use anywhere makes bison warn about every
            # empty rule without it).
            syms = [s for s in syms if s != "%empty"]
            if syms not in out_alts:
                out_alts.append(syms)
        merged.append((map_symbol(name), out_alts))
    rules_by_name = dict(merged)

    # GRANT REST / REVOKE REST: the server reduces the empty clear_privileges
    # right after GRANT and REVOKE; going through it too leaves the choice to
    # the token after REST.
    for name, alts in merged:
        for alt in alts:
            if len(alt) >= 2 and alt[0] in ("GRANT", "REVOKE") and alt[1] == "REST_SYM":
                alt.insert(1, "clear_privileges")

    # After '@' the server lexer always produces LEX_HOSTNAME, empty when no
    # plain word follows ("mike@/svc" lexes as IDENT '@' LEX_HOSTNAME('') '/'
    # IDENT), unless a quoted name follows.  Where '@' ends an alternative
    # (the developer prefix of a service path), that token comes next.
    for name, alts in merged:
        for alt in alts:
            if alt and alt[-1] == "'@'":
                alt.append("LEX_HOSTNAME")

    # SHOW ...: the server reduces an empty mid-rule action right after SHOW
    # (show: SHOW $@ show_param).  The REST SHOW statements are hooked into
    # show_param without their SHOW, so both paths share that step.
    show_rules = [name for name, alts in merged
                  if alts and all(alt and alt[0] == "SHOW" for alt in alts)]
    for name in show_rules:
        rules_by_name[name][:] = [alt[1:] for alt in rules_by_name[name]]
    rules_by_name["mrs_statement"][:] = [
        alt for alt in rules_by_name["mrs_statement"] if alt[0] not in show_rules]

    # The field annotations of a data mapping: the shell's mode, options,
    # datatype and CRUD options are distinct @tokens there; here every
    # annotation is the same '@' LEX_HOSTNAME, so they become one list.
    pair = rules_by_name.get("mrs_graphql_pair")
    seq = ["mrs_opt_graphql_mode", "mrs_graphql_value_options",
           "mrs_opt_graphql_datatype", "mrs_opt_graphql_crud_options"]
    if pair is not None:
        for alt in pair:
            for k in range(len(alt) - len(seq) + 1):
                if alt[k:k + len(seq)] == seq:
                    alt[k:k + len(seq)] = ["mrs_graphql_annotations"]
                    break
        merged.extend([
            ("mrs_graphql_annotations",
             [[], ["mrs_graphql_annotations", "mrs_graphql_annotation"]]),
            ("mrs_graphql_annotation",
             [["mrs_annotation"],
              ["mrs_annotation", "'('", "mrs_graphql_datatype_text", "')'"]]),
        ])

    # The identifier rules, built to accept what the shell's do.  The shell
    # lexer reads every word that is not a REST keyword as an identifier, so
    # mrs_identifier is a plain identifier, every server keyword the REST
    # rules do not use, and the five keywords the shell allows explicitly.
    # A request path is one token in the shell, so its segments accept any
    # word, keywords included.
    used = {sym for _, alts in merged for alt in alts for sym in alt
            if re.fullmatch(r"[A-Z_][A-Z0-9_]*", sym)}
    keyword_tokens = {tok for word, tok in server_kw.items()
                      if re.fullmatch(r"[A-Z_][A-Z0-9_]*", word)}
    keyword_tokens |= set(new_keywords.values())
    allowed = {map_symbol(t) for t in IDENTIFIER_KEYWORDS}
    ident_alts = [["IDENT_sys"]] + [[t] for t in sorted((keyword_tokens - used) | allowed)]
    merged.extend([
        ("mrs_identifier", ident_alts),
        ("mrs_qualified_identifier",
         [["mrs_identifier"], ["mrs_identifier", "'.'", "mrs_identifier"]]),
        ("mrs_text_or_identifier",
         [["mrs_identifier"], ["TEXT_STRING"], ["LEX_HOSTNAME"]]),
        ("mrs_int_number", [["NUM"], ["LONG_NUM"], ["ULONGLONG_NUM"]]),
        ("mrs_request_path",
         [["'/'", "mrs_path_segment"], ["mrs_request_path", "'/'", "mrs_path_segment"]]),
        ("mrs_path_segment", [["IDENT_sys"]] + [[t] for t in sorted(keyword_tokens)]),
        ("mrs_annotation", [["'@'", "LEX_HOSTNAME"]]),
    ])

    # Drop rules nothing reaches any more (bison would warn about them).
    by_name = dict(merged)
    reachable = set()
    todo = ["mrs_statement"] + show_rules
    while todo:
        n = todo.pop()
        if n in reachable or n not in by_name:
            continue
        reachable.add(n)
        todo.extend(sym for alt in by_name[n] for sym in alt if sym in by_name)
    dropped = [n for n, _ in merged if n not in reachable]
    merged = [(n, a) for n, a in merged if n in reachable]

    serial = max(int(n) for n in re.findall(r"MDBP_R\((\d+)\)", stub)) + 1
    lines = ["", "", "/* ---- MariaDB REST Service (REST SQL) statements ---------------------",
             "   Merged from MariaDB Shell's mysqlshdk/libs/mrs/parser.yy by",
             "   tools/mrs_merge.py; see UPSTREAM.md. */", ""]
    for name, alts in merged:
        lines.append(f"{name}:")
        for k, alt in enumerate(alts):
            lead = "          " if k == 0 else "        | "
            body = " ".join(alt) if alt else "/* empty */"
            lines.append(f"{lead}{body} {{ MDBP_R({serial}); }}")
            serial += 1
        lines.append("        ;")
        lines.append("")

    # Tokens and the non-reserved keyword list.
    token_block = "".join(f"%token  {tok}  /* REST SQL keyword */\n"
                          for tok in sorted(new_keywords.values()))
    anchor = "%token     ST_COLLECT_SYM\n"
    if anchor not in stub:
        raise SystemExit("mrs_merge.py: token anchor not found")
    stub = stub.replace(anchor, anchor + token_block, 1)

    def add_alternatives(anchor, symbols, what):
        nonlocal stub, serial
        if anchor not in stub:
            raise SystemExit(f"mrs_merge.py: {what} anchor not found")
        extra = ""
        for sym in symbols:
            extra += f"        | {sym} {{ MDBP_R({serial}); }}\n"
            serial += 1
        stub = stub.replace(anchor, anchor + extra, 1)

    verb_tokens = sorted(new_keywords[w] for w in new_keywords if w in VERB_KEYWORDS)
    plain_tokens = sorted(t for w, t in new_keywords.items() if w not in VERB_KEYWORDS)
    add_alternatives(
        "keyword_sp_var_and_label:\n        keyword_func_sp_var_and_label { MDBP_R(3137); }\n",
        plain_tokens, "keyword_sp_var_and_label")
    add_alternatives(
        "keyword_verb_clause:\n          CLOSE_SYM { MDBP_R(2793); }"
        "             /* Verb clause. Reserved in Oracle */\n",
        verb_tokens, "keyword_verb_clause")
    add_alternatives(
        "show_param:\n           DATABASES wild_and_where\n           { MDBP_R(2324); }\n",
        show_rules, "show_param")

    verb = "verb_clause:\n          alter { MDBP_R(9); }\n"
    if verb not in stub:
        raise SystemExit("mrs_merge.py: verb_clause anchor not found")
    stub = stub.replace(verb, verb + f"        | mrs_statement {{ MDBP_R({serial}); }}\n", 1)
    serial += 1

    stub = stub.rstrip("\n") + "\n" + "\n".join(lines)

    # Keyword table: before the end of symbols[].
    sym_end = keywords.index("};", keywords.index("SYMBOL symbols[] = {"))
    kw_lines = "".join(f'  {{ "{w}",\t\tSYM({t})}},  /* REST SQL */\n'
                       for w, t in sorted(new_keywords.items()))
    head = keywords[:sym_end].rstrip()
    if not head.endswith(","):
        head += ","  # the last original entry has no trailing comma
    keywords = head + "\n" + kw_lines + keywords[sym_end:]

    print(f"mrs_merge.py: {len(merged)} rules, "
          f"{sum(len(a) for _, a in merged)} alternatives, "
          f"{len(new_keywords)} new keywords: {' '.join(sorted(new_keywords))}; "
          f"{len(show_rules)} SHOW statements under show_param; "
          f"unreachable rules dropped: {' '.join(dropped) or 'none'}",
          file=sys.stderr)
    if not args.dry_run:
        stub_path.write_text(stub)
        kw_path.write_text(keywords)


if __name__ == "__main__":
    main()
