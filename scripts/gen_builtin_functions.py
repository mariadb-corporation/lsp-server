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

"""Writes libs/mariadblanguageserver/src/builtin_functions.inc.

A one-shot tool like libs/mariadbparser/tools/: run it by hand against the
MariaDB server checkout at the parser's pin, commit the output, and never from
the build.

    scripts/gen_builtin_functions.py ../mariadb-server-lsp

The function NAMES come from the server's own registries, as the VS Code
extension's tools/grammar.mts reads them: sql_functions[] in sql/lex.h (the
functions with a grammar rule of their own), the function_call_* rules of
sql/sql_yacc.yy that start with a keyword and '(' (LEFT(, CHAR(, DATE(, ...),
and the Native_func_registry arrays of sql/item_create.cc and
sql/item_geofunc.cc.  Their SYNTAX and a one-paragraph summary come from the
help tables, scripts/fill_help_tables.sql, which the server generates from its
documentation.  A function the help tables do not describe is listed with its
name only.
"""

import os
import re
import sys

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "libs",
                   "mariadblanguageserver", "src", "builtin_functions.inc")

# Help categories whose topics are functions (fill_help_tables.sql ids differ
# between releases, so they are matched by name).
FUNCTION_CATEGORY = re.compile(r"Functions|Modifiers for Use with GROUP BY|Comparison Operators|"
                               r"Geometry Constructors|Properties|WKB|WKT|MBR|"
                               r"Geometry Relations|GeoJSON")


def read(*parts):
    with open(os.path.join(*parts), encoding="utf-8", errors="replace") as f:
        return f.read()


def array_body(text, start):
    i = text.index(start)
    j = text.index("};", i)
    return text[i:j]


def symbol_table(lex, name):
    """{spelling: token} of one SYMBOL array of sql/lex.h."""
    out = {}
    for m in re.finditer(r'\{\s*"([A-Z0-9_]+)"\s*,\s*SYM(?:_FN)?\(([A-Z0-9_]+)',
                         array_body(lex, "SYMBOL %s[]" % name)):
        out[m.group(1)] = m.group(2)
    return out


def rule_tokens(yacc, rule_prefix, alternative):
    """Tokens matched by `alternative` in the rules whose name starts with
    `rule_prefix`."""
    tokens = set()
    current = None
    for line in yacc.splitlines():
        m = re.match(r"^([a-z_][a-z0-9_]*)\s*:", line)
        if m:
            current = m.group(1)
            line = line[m.end():]
        elif re.match(r"^\s*;\s*$", line):
            current = None
            continue
        if current and current.startswith(rule_prefix):
            a = re.match(alternative, line)
            if a:
                tokens.add(a.group(1))
    return tokens


def native_functions(text, name):
    return re.findall(r'STRING_WITH_LEN\("([A-Z0-9_]+)"\)', array_body(text, name + "[] ="))


def sql_strings(values):
    """The fields of one VALUES (...) tuple: ints and quoted strings."""
    out = []
    i = 0
    n = len(values)
    while i < n:
        c = values[i]
        if c == "'":
            buf = []
            i += 1
            while i < n:
                c = values[i]
                if c == "\\" and i + 1 < n:
                    e = values[i + 1]
                    buf.append({"n": "\n", "t": "\t", "r": "\r", "0": "\0"}.get(e, e))
                    i += 2
                elif c == "'" and i + 1 < n and values[i + 1] == "'":
                    buf.append("'")
                    i += 2
                elif c == "'":
                    i += 1
                    break
                else:
                    buf.append(c)
                    i += 1
            out.append("".join(buf))
        elif c.isdigit() or c == "-":
            j = i
            while j < n and (values[j].isdigit() or values[j] == "-"):
                j += 1
            out.append(int(values[i:j]))
            i = j
        else:
            i += 1
    return out


def help_topics(help_sql):
    categories = {}
    for m in re.finditer(r"insert into help_category \([^)]*\) values \((.*?)\);\n", help_sql):
        f = sql_strings(m.group(1))
        categories[f[0]] = f[1]
    topics = {}
    for m in re.finditer(r"insert into help_topic \([^)]*\) values \((.*?)\);\n", help_sql):
        f = sql_strings(m.group(1))
        topic_id, category, name, description, _example, url = f[:6]
        if not FUNCTION_CATEGORY.search(categories.get(category, "")):
            continue
        # "REPLACE Function" and "IF Function" share their name with a statement.
        key = re.sub(r"(\s+FUNCTION|\(\))$", "", name.upper().strip())
        topics.setdefault(key, (description, url))
    return topics


def section(description, title):
    """The text under a "Title\\n-----" heading, up to the next heading."""
    m = re.search(r"(?:^|\n)%s\n-+\n(.*?)(?=\n[A-Z][A-Za-z ]+\n-+\n|\Z)" % re.escape(title),
                  description, re.S)
    return m.group(1).strip() if m else ""


def syntax_lines(name, description):
    """The call forms the Syntax section gives for `name`."""
    lines = []
    for line in section(description, "Syntax").splitlines():
        line = line.strip()
        if not line.upper().startswith(name + "(") and not line.upper().startswith(name + " ("):
            continue
        line = re.sub(r"\s+", " ", line)
        if line not in lines:
            lines.append(line)
    return lines


def summary(description):
    text = section(description, "Description")
    para = text.split("\n\n")[0] if text else ""
    para = re.sub(r"\s+", " ", para).strip()
    if len(para) > 400:
        cut = para.rfind(". ", 0, 400)
        para = para[:cut + 1] if cut > 100 else para[:400] + "..."
    return para


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    server = sys.argv[1]
    lex = read(server, "sql", "lex.h")
    yacc = read(server, "sql", "sql_yacc.yy")
    symbols = symbol_table(lex, "symbols")
    syntactic = symbol_table(lex, "sql_functions")
    call_tokens = rule_tokens(yacc, "function_call_", r"^\s*\|?\s*([A-Z][A-Z0-9_]*)\s*'\('")
    keyword_functions = [s for s, tok in list(symbols.items()) + list(syntactic.items())
                         if tok in call_tokens]
    names = set(syntactic) | set(keyword_functions)
    names |= set(native_functions(read(server, "sql", "item_create.cc"), "func_array"))
    names |= set(native_functions(read(server, "sql", "item_geofunc.cc"), "func_array_geom"))
    # Interval units and plural spellings share a token with a function but
    # are not called.
    names = sorted(n for n in names if not n.endswith("_ORACLE") and not n.startswith("SQL_TSI_")
                   and n != "SCHEMAS")

    topics = help_topics(read(server, "scripts", "fill_help_tables.sql"))
    commit = os.popen("git -C '%s' rev-parse --short=8 HEAD" % server).read().strip()

    described = 0
    with open(OUT, "w", encoding="utf-8") as out:
        out.write("// Generated by scripts/gen_builtin_functions.py from MariaDB server %s:\n"
                  "// names from sql/lex.h, sql/sql_yacc.yy, sql/item_create.cc and\n"
                  "// sql/item_geofunc.cc; syntax and summaries from\n"
                  "// scripts/fill_help_tables.sql.  Do not edit; run the script again.\n"
                  "//\n"
                  "// FN(name, call forms separated by \\n, summary, url)\n" % commit)
        for name in names:
            description, url = topics.get(name, ("", ""))
            forms = syntax_lines(name, description)
            text = summary(description) if forms else ""
            if forms:
                described += 1
            out.write("FN(%s, %s, %s, %s)\n" % (c_string(name), c_string("\n".join(forms)),
                                                c_string(text), c_string(url if forms else "")))
    print("%s: %d functions, %d with syntax from the help tables" % (OUT, len(names), described))


if __name__ == "__main__":
    main()
