# `overrides/`

Nothing in `build/` is ever edited by hand. Not `yy_mariadb.yy`, not
`yy_stub.yy`, not `parser.c`. This is not hygiene, it is the product
requirement: a parser that was hand-patched once cannot be regenerated for the
next MariaDB release, and regenerability is what the whole experiment is for.

So when `tools/stub.py` meets a construct it cannot transform
mechanically, it exits 3 and says so, loudly, rather than guessing. The fix
goes **here**, in a `*.toml` file, where it is versioned, reviewable, and
re-applied automatically by the next run.

## The rule

> **Every entry carries a comment saying why it exists.**

The `why` key is mandatory and must be non-empty; `stub.py` exits 3 on an
entry without one. The *number of entries* across `overrides/*.toml` is this
project's **regeneration debt** — the amount of human judgement that a future
MariaDB release will force somebody to re-examine by hand. It is a reported
metric: `evidence/P1/extract.json` records it as `override_entries`, and the
target is zero.

## Format

`overrides/stub.toml` holds an array of tables. Each entry patches one line of
the transform's **input** (the `%ifdef`-preprocessed `build/yy_<variant>.yy`)
before the transform runs:

```toml
[[override]]
line    = 1234                      # 1-based line number in the input .yy
replace = "%token FOO"              # the literal text that line becomes
why     = "…why the transform cannot do this mechanically…"
```

`replace` may not contain a newline. That restriction is the same one the
transform itself obeys: both stages of the pipeline preserve `sql_yacc.yy`'s
line count and line numbering exactly, so every `sql_yacc.yy:LINE` citation
recorded anywhere in this project — and every `yyrline[]` entry bison
generates — stays valid.

## What is *not* an override

An override is a **judgement call about one line of one revision**. A rule that
a script can apply to any revision is not one, and must never be written down
here — it belongs in `tools/stub.py`, as code, with its reason in the
module docstring's contract table.

The worked example is the transform's `%type` exception. PLAN.md §2.1 says
every `%type` declaration is blanked, and `sql_yacc.yy:2018`–`:2020` is a
`%type` whose continuation lines list **character-literal terminals**. Bison
assigns symbol numbers in order of first mention, so blanking that declaration
moves four literals' first mention into the rules and renumbers them, and the
renumbering propagates through the automaton. `stub.py` therefore re-emits
those literals as a `%token` on the construct's first line. That could have
been four `[[override]]` entries; it is a rule of the script instead, because:

- it is derived from the input, so it applies unchanged to the next MariaDB
  revision — four line-numbered entries would silently stop applying the
  moment `sql_yacc.yy` gained a line above 2018;
- `override_entries` is this project's **regeneration debt**, and the number is
  only worth reporting if it counts human judgement a future release will force
  somebody to re-examine. Spending it on something mechanical would overstate
  the debt and, worse, teach the next reader that the number does not mean
  much.

The test for whether something belongs here: *could a script decide it from the
input alone, for any revision?* If yes, the script decides it. If no — if it
takes a human looking at this revision — it goes here, with its `why`.
