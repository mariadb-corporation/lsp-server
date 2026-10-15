# Regenerating this parser from a new MariaDB tree

**Task 9.4.** This is the document a future maintainer opens the day MariaDB moves. It is not a
summary of the project — it is an inventory of every place a human judgement is baked into the
extraction pipeline, and the exact procedure for re-running that pipeline against a new source tree.
Nothing here is new behaviour: every command below already exists (tasks 9.1–9.3 and earlier), and
every number is quoted next to the evidence file it came from. None is retyped from memory, from
`PLAN.md`, or from a sibling task's report.

Everything below runs on `test-host` (`hostname` = `test-host`) through the `make` targets, per the house rules;
the first output line of every run is `[the test host] host=test-host root=<repo>`.

## 1. The entry point

**`tools/extract.sh --source <tree> --variant MARIADB [--out <dir>]` is the one supported way
anything in `build/` comes into existence.** Its own header comment says so, and points here for the
procedure below.

```
tools/extract.sh [--source <tree>] [--variant MARIADB|ORACLE]
                       [--out <dir>] [--no-provenance-check]
                       [--no-compile | --compile-only]
                       [--model <grammar-model.json>]
```

What it runs, in order (from `tools/extract.sh`'s own header, "Steps, in order"):

1. Refuses unless this is `test-host` (`hostname` returns `test-host`).
2. `tools/provenance.py --check` — fails closed: a changed source tree invalidates every
   number computed from it.
3. MariaDB's **own** `sql/gen_yy_files.cmake` — its `%ifdef MARIADB` / `%ifdef ORACLE` preprocessor,
   and no other part of MariaDB's build (no server cmake configure, no `mariadb.h`, no generated
   `lex_token.h`, no `sql_yacc.cc`).
4. Assertions on the selected variant's `.yy` (line count, `%ifdef` survival, `%expect`; §3.3 below).
5. `tools/stub.py` — the C-action stub transform, `build/yy_<variant>.yy` → `build/yy_stub.yy`.
6. bison 3.8.2, stderr captured; a non-zero exit **or** non-empty stderr is a failure.
7. `stub.py --rule-map build/parser.c` → `build/rule-map.json`.
8. `tools/lexslice.py` — locates `Lex_input_stream::lex_token`/`lex_one_token` and their
   transitive closure **by symbol** in the source tree and writes `lexer_extracted.cc`, `keywords.h`,
   `lexstates.h`, `state_maps_gen.cc` and `lexslice.json`. This always runs (it is extraction, not
   compilation).
9. The compile stage (`--compile-only` runs *only* this step, which is what `make build` calls):
   `gen_tables.py` (against `--model`, `corpus/grammar-model.json` by default) → `build/mdbp_tables.h`;
   `g++` over `src/mdbp_lex.cc` (which `#include`s the sliced lexer) → `mdbp_lex.o`; `gcc` over
   `src/parser_glue.c`/`mdbp_runtime.c`/`mdbp_api.c` → three objects, archived into
   `libmariadbparser.a` and linked into `libmariadbparser.so.<MAJOR>.<MINOR>.<PATCH>`
   (the three `MDBP_VERSION_*` macros in `include/mdbparse.h`, which is also where the soname
   and the `.pc` `Version:` come from) plus `build/mariadbparser.pc`;
   then `gcc` over `src/mdbparse.c` linked against the library → `build/mdbparse`.

**What it produces:** `evidence/P1/extract.json` (the run record; its `artefacts` map lists 15
generated output files under the default `--out build` — see §5's `regen-check` figure — or
`<dir>/extract.json` for any other `--out`, so a scratch run cannot clobber the phase's evidence —
`tools/extract.sh`'s own comment). This 15 is a count of **output artefacts**, not to be
confused with §3.11's 15 **input** `TRACKED_FILES` — the two lists are unrelated and fifteen only by
coincidence today. It also produces `evidence/P5b/lexgen.json`. The
compile stage additionally writes `evidence/P2/build.json`.

**Its flags:** `--source <tree>` (default `${MARIADB_SOURCE_DIR:-<mariadb-source>}`), `--variant
MARIADB|ORACLE` (default `MARIADB`; only `MARIADB` is built or tested anywhere in this project —
`build/yy_oracle.yy` falls out of the same cmake call and is ignored), `--out <dir>` (default
`build`), `--no-provenance-check`, `--no-compile`, `--compile-only`, `--model <grammar-model.json>`
(task 9.3; the model `gen_tables.py` cross-checks its generated tables against — default
`corpus/grammar-model.json`, which is what makes regenerating against a tree that is *not* the pinned
one need this flag; see §2 step 4).

**Its exit codes**, verbatim from its own header comment: `0` ok · `1` internal error (bison) ·
`2` usage/wrong host · `3` unsupported input or a failed assertion.

**Nothing under `build/` is ever hand-edited, and this is a product requirement, not hygiene** —
`tools/extract.sh`'s own header comment: "no artefact it produces is ever hand-edited: a parser
that was hand-patched once cannot be regenerated for the next MariaDB release, which is the entire
point of the experiment." Anything a script genuinely cannot derive goes in `overrides/*.toml` with a
reason instead (§3.1).

## 2. The procedure when MariaDB moves

Numbered below; each step names the exact `test-host` command and the evidence or test that shows the step
has actually been exercised. Four of the five steps — steps 1–4 — are **rehearsed** — run for real,
against a scratch copy of the pinned tree with one line changed, by `make regen-rehearse`
(`tools/regen_rehearse.py`), with step 2 additionally and independently rehearsed by the drift
drill (`tests/test_drift_drill.py`) — and their results are read back below from
`evidence/P6/regeneration.json`. The fifth step is **documented, not rehearsed**
(`evidence/P6/regeneration.json`'s `sweep_step`: `"documented_not_rehearsed"`), and so is its own
compile prerequisite (`compile_step`: `"documented_not_rehearsed"`); the reason for both is quoted from
that same file rather than paraphrased.

1. **Re-run `extract.sh` against the new tree.**
   ```
   tools/extract.sh --source <new-tree> --variant MARIADB
   ```
   **Rehearsed.** `evidence/P6/regeneration.json`'s `steps[1]` (`step: "fail_closed"`) ran exactly
   this shape of command against a scratch tree built from the pinned one with one comment line
   inserted into its `sql/sql_yacc.yy`.

2. **Provenance fails closed.** Against a drifted tree, step 1's command exits `3` and names the
   drifted file, before anything else in the pipeline runs (`extract.sh`'s own step 2,
   `provenance.py --check`).
   **Rehearsed twice**, independently:
   - `evidence/P6/regeneration.json`: `fail_closed_exit: 3`, `fail_closed_names_file: true`,
     `fail_closed_drifted_file: "sql/sql_yacc.yy"`; `steps[1].outcome`: `"exit 3 (expected 3); the
     drifted file is named in its output"`.
   - `tests/test_drift_drill.py`, independently, against its own scratch copy under `build/drift/`.

3. **Re-pin with `PROVENANCE_UPDATE=1`.**
   ```
   PROVENANCE_UPDATE=1 tools/extract.sh --source <new-tree> --variant MARIADB
   ```
   **Rehearsed.** `evidence/P6/regeneration.json`: `repinned_exit: 0`,
   `provenance_repinned_by_run: true`; `steps[2].outcome`: `"exit 0 (expected 0)"`. The rehearsal then
   restores `source/PROVENANCE.json` byte-for-byte (`provenance_restored: true`) and asserts
   `PINS.toml` and `evidence/P1/extract.json` were never touched (`pins_untouched: true`,
   `p1_evidence_untouched: true`) — see the caveat on that last flag in §3.9.

4. **Re-run the isomorphism check against the new tree's own bison output — never against
   `corpus/grammar-model.json`,** because the automaton legitimately changes on a new revision. This
   is the mechanism task 9.3 built into `tools/isomorphism.py`:
   ```
   uv run --frozen -- python tools/isomorphism.py \
     --build-dir <out> \
     --reference-model <new-tree-reference-model.json> \
     --reference-automaton <new-tree-reference-automaton.json> \
     --evidence-dir <scratch-evidence-dir>
   ```
   where `<new-tree-reference-model.json>`/`-automaton.json` come from running bison and `model.py`
   directly over the new tree's own **unstubbed** `yy_mariadb.yy` — never `build/yy_stub.yy`, and
   never `corpus/`'s model, which describes the *old* grammar.
   **Rehearsed.** `evidence/P6/regeneration.json`: `bison_accepts_unstubbed_grammar: true`,
   `bison_exit: 0`, `bison_stderr_bytes: 0` (bison over the scratch tree's own unstubbed grammar);
   `automaton_differences_vs_self_reference: 0`; `self_reference_automaton_equals_corpus: true`
   (the reference this rehearsal built from the scratch tree agrees with `corpus/`'s, which is
   expected here because the only perturbation was a comment line — on a real revision bump this
   number is not expected to be zero, and that is the point of comparing against the new tree's own
   reference rather than against `corpus/`).
   **The caveat this evidence records, quoted rather than paraphrased** (`fallback` key): *"if bison
   ever refuses the unstubbed `yy_mariadb.yy`, the operator produces the new tree's reference from the
   STUBBED grammar instead — bison over `<out>/yy_stub.yy` into a second output directory, then
   `model.py` over that XML — and reads the isomorphism report as a stub-vs-stub comparison, in which
   the `has_action` and `terminal.type` expected-difference families collapse to nothing and stop being
   informative."*

5. **Re-run the corpus sweep to see what moved.**
   ```
   make build && make sweep && make compare
   ```
   **Documented, not rehearsed.** `evidence/P6/regeneration.json`, `sweep_step:
   "documented_not_rehearsed"`, `sweep_step_reason` (quoted in full): *"a 212,332-record sweep needs a
   parser binary built from the drifted tree, and this rehearsal never builds one: both extract.sh
   invocations run `--no-compile`, so compilation is itself documented_not_rehearsed (see
   compile_step_reason) and no property of a drifted-tree binary was measured here or is claimed. Nor
   would the sweep answer its own question on this tree: the perturbation is a comment line, so the
   grammar did not move, and manufacturing a moved grammar means editing a rule, which is forbidden
   (PLAN.md K2/K7). On a real new revision the operator runs `make build`, then `make sweep` and
   `make compare`, unchanged."*
   **Its own prerequisite is also documented, not rehearsed** — `compile_step:
   "documented_not_rehearsed"`, `compile_step_reason` (quoted): *"gen_tables.py was NOT run against the
   scratch out directory, so nothing here measures the scratch tables against any model. That they
   would agree with the DEFAULT model is a DEDUCTION from two measurements in this same record:
   automaton_differences_vs_self_reference=0 ... and self_reference_automaton_equals_corpus=True ... It
   holds only while those read 0 and true; on any other pair it is void. Given them, extract.sh's
   compile stage would succeed against the default model and demonstrate nothing about the mismatch its
   new `--model` flag exists for."* On a real revision bump, step 9 of §1 must be pointed at the new
   tree's own `<new-tree-reference-model.json>` via `extract.sh --model`, exactly as step 4 above
   points the isomorphism check at the new tree's own reference.

## 3. The inventory — what a future MariaDB revision could break

Derived from the tree, not from this paragraph. One row per item: what it is, where it lives (file
plus symbol/key, never a line number), why a script cannot derive it, and what a new revision would do
to it.

### 3.1 `overrides/*.toml` — the regeneration debt itself

**Today: zero entries.** `overrides/stub.toml` is the only file matching `overrides/*.toml`, and it
holds no `[[override]]` table (`overrides/stub.toml`'s own comment: "This file is deliberately empty
of entries"). The count is a reported metric, not an omission: `evidence/P1/convergence.json`'s
`secondary.override_entries` = `0`, and `evidence/P1/extract.json`'s `override_entries` = `0`. An
empty inventory here is a measured result — `tools/stub.py` handles `sql_yacc.yy` as MariaDB
ships it today, with no hand-patched line.

*Why a script cannot derive it:* by construction — this file exists precisely for the constructs
`tools/stub.py` cannot transform mechanically (it exits 3 rather than guess). *What a new
revision would do:* any construct in the new `sql_yacc.yy` that `stub.py` cannot parse forces a new
`[[override]]` entry, with a `why`; `override_entries` would move off zero for the first time.

### 3.2 `extract.sh`'s C warning suppressions — `MDBP_WNO_FLAGS` / `MDBP_WNO_REASONS`

Where it lives: `tools/extract.sh`, the `MDBP_WNO_FLAGS=()` / `MDBP_WNO_REASONS=()` arrays (the
compile stage's C translation unit — `build/parser.c` plus `src/*.c`). Today both arrays are empty;
`extract.sh`'s own comment: *"NONE today. If bison's generated translation unit ever needs one, add it
to `MDBP_WNO_FLAGS` *with* a reason on the same index of `MDBP_WNO_REASONS` — both land in
`evidence/P2/build.json` ... A warning in our own `src/*.c` is fixed, never silenced."*

*Why a script cannot derive it:* whether a compiler warning in bison-generated code is safe to
silence (versus a real defect the pipeline should refuse) is a judgement call about that specific
warning. *What a new revision would do:* a MariaDB grammar change that makes bison emit differently
shaped C, or a gcc upgrade, could produce a new warning in `build/parser.c` that this pipeline would
then have to classify — fix (if in our own code) or silence-with-a-reason (if in generated code we do
not own).

### 3.3 `extract.sh`'s C++ warning suppressions — `MDBP_CXX_WNO_FLAGS` / `MDBP_CXX_WNO_REASONS`

Where it lives: `tools/extract.sh`, the `MDBP_CXX_WNO_FLAGS=()` / `MDBP_CXX_WNO_REASONS=()` arrays
(the `g++` compile of `src/mdbp_lex.cc`, which `#include`s the sliced `build/lexer_extracted.cc`).
Today both are empty, and deliberately so even though the translation unit is not warning-free:
`extract.sh`'s comment records **one** live warning, MariaDB's own `UNINIT_VAR(c)` idiom in
`lex_one_token`, left **unsilenced** on purpose (`evidence/P2/build.json`'s `lexer.lexer_warnings` =
`1`, `lexer.warnings[0]` = `"build/lexer_extracted.cc:1311:20: warning: 'c' is used uninitialized
[-Wuninitialized]"`) — silencing it would need `-Wno-uninitialized`, which in this one translation unit
would also silence `src/mdbp_lex.cc` itself.

*Why a script cannot derive it:* the choice of which warnings in vendored, sliced MariaDB source are
acceptable to leave visible (versus needing a reasoned suppression) is a per-warning judgement.
*What a new revision would do:* moving or rewriting `lex_one_token` could remove this warning, add a
new one, or change its line; whichever happens, `lexer.warnings`/`lexer.lexer_warnings` in
`evidence/P2/build.json` is where it would show up first.

### 3.4 The `%expect 72` / `%expect 73` literals and the line-count assertion

Where it lives: `tools/extract.sh`, the `case "$variant"` block (`expect_present='%expect 72'` /
`expect_absent='%expect 73'` for `MARIADB`) and the block below it that reads `pinned_lines` out of
`source/PROVENANCE.json`'s `files."sql/sql_yacc.yy".lines` and compares it against `wc -l` on the
generated `.yy`. `evidence/P1/extract.json`'s `yy_stub_lines` / `yy_variant_lines` = `21202` today.

*Why a script cannot derive it:* `72` is this specific grammar revision's disabled-reduction count
(`sql_yacc.yy:391`, `%ifdef MARIADB`), asserted rather than computed, because bison itself is the only
thing that can compute it and the whole point of `%expect` is to catch it moving unexpectedly. The
expected line count is read from the pin (not hardcoded) precisely so the drift drill can move it, but
the literal strings `'%expect 72'` / `'%expect 73'` inside `extract.sh` are not derived from anything —
they are this revision's own vendored numbers, copied by a human from `sql_yacc.yy` (PLAN.md §1.3).

*What a new revision would do:* if MariaDB's grammar gains or loses a genuine shift/reduce conflict,
`%expect 72` in the vendored `sql_yacc.yy` itself changes, and `extract.sh`'s `expect_present` literal
has to be updated to match — a code change in `tools/extract.sh`, not an `overrides/*.toml` entry
(it is a rule that has to track the *current* revision's own number, not one that generalises across
revisions, so it is not eligible for `overrides/` under `overrides/README.md`'s own test: "could a
script decide it from the input alone, for any revision?").

### 3.5 `gen_tables.py`'s `--model` cross-check

Where it lives: `tools/gen_tables.py` (its module docstring: the assembled terminal table "is then
required to reproduce `corpus/grammar-model.json`'s terminal list EXACTLY"), invoked from
`extract.sh`'s compile stage (step 9) as `gen_tables.py --model "$model"`, `model` defaulting to
`corpus/grammar-model.json`. This step produces `build/mdbp_tables.h` — a C header of terminal/token
tables — **not** `grammar-model.json` or `grammar-automaton.json` themselves, which are written by the
separate `model.py` step (§2 step 4) that `extract.sh` never calls; `gen_tables.py` only *reads* a
model file to validate against.

*Why a script cannot derive it:* which model file is the correct reference is exactly the question a
regeneration against a new tree changes the answer to, and `gen_tables.py` cannot know on its own
whether it is being asked to validate against the grammar it was pinned to or a new one — that choice
is `--model`'s reason for existing (task 9.3).

*What a new revision would do:* left at the default, `extract.sh`'s compile stage (step 9) would try to
reproduce the **old** `corpus/grammar-model.json` from the **new** tree's tables and fail (exit 3) —
this is exactly why §2 step 5's `compile_step` is `documented_not_rehearsed` in
`evidence/P6/regeneration.json`: the operator must point `--model` at the new tree's own reference
model before running `make build` against it.

### 3.6 `lexslice.py`'s one named rewrite rule

Where it lives: `tools/lexslice.py`, `REWRITE_RULES = [("isolate_wsrep", rule_isolate_wsrep)]`.
The rule replaces an `#ifdef WITH_WSREP ... #endif` block inside the sliced lexer body with a marker
comment, recording the server symbols the block referenced (`WSREP`, `wsrep_*`,
`CONSISTENCY_CHECK_*`) as a Q7 finding rather than letting the preprocessor silently delete them.

*Why a script cannot derive it:* stripping a guarded block is only safe because a human determined that
`WITH_WSREP` is not modelled and the block's symbols are recorded, not because any script can decide on
its own that an arbitrary `#ifdef` block is droppable — `lexslice.py`'s own module docstring: "K7:
modifying `sql_lex.cc`'s logic ends the experiment. What is allowed is *scripted* rewriting of the
slice, and every such rewrite is a **named rule** in `REWRITE_RULES`."

*What a new revision would do:* a new `#ifdef`-guarded block inside the sliced functions (a different
build flag, not `WITH_WSREP`) would pass through unrewritten — plain `#ifdef X ... #endif` where `X` is
undefined at compile time is silently deleted by the C preprocessor with no record — so a human
auditing a new revision's diff to the sliced functions needs to check for new guards and add a rule if
one appears.

### 3.6b `stub.py`'s one named preserve rule — `preserve_parsing_place`

Where it lives: `tools/stub.py`,
`PRESERVE_RULES = [("preserve_parsing_place", rule_preserve_parsing_place)]`, added by task P1.1.
The rule keeps, inside an otherwise-blanked action body, whole `sql_yacc.yy` statements that touch
only `parsing_place` / `save_parsing_place` on one of five spellings of the path to the current
`SELECT_LEX`, and writes the macro block that resolves those spellings onto `src/stub.h`'s
`mdbp_ctx` into the grammar's first `%{ … %}` prologue. `build/stub-map.json`'s `preserve` block is
the manifest: every statement lifted with its `sql_yacc.yy` line, every `parsing_place` site *not*
lifted with the reason, and a census that must partition every line of the input carrying the
substring (lifted, reported, comment-only) or the run is an exit 1.

*Why a script cannot derive it:* which field the LEXER reads back out of the parser is a fact about
`sql_lex.cc`, not about the grammar — a human read `sql_lex.cc:1961`–`:2022` and
`tools/vendor/thd.md` to decide that `parsing_place` is the one, and that the statements assigning it
are safe to keep because they object to nothing and reject nothing. The rule's *application* is
mechanical and reports rather than guesses, but its *subject* is a judgement.

*What a new revision would do:* three things, all of them loud. A new spelling of the path (a
different local name for the `SELECT_LEX`) makes the rule report that statement instead of lifting
it, with the identifier it did not recognise, and the count in `evidence/P1/extract.json`'s
`preserve.counts` moves. A new `enum_parsing_place` value makes it do the same until the value is
added to `PARSING_PLACE_VALUES` here **and** to `src/stub.h`'s enum
(`tests/test_parsing_place_lift.py` asserts the two agree). And a first `%{ … %}` prologue shorter
than the macro block is an exit 3 the moment anything has to be lifted, rather than a grammar that
quietly stops feeding MariaDB's lexer. `tests/test_parsing_place_lift.py`'s `EXPECTED_CENSUS` and
`EXPECTED_SKIPPED` are the pinned-revision figures a regeneration has to re-justify rather than
silently absorb.

### 3.7 `lexslice.py`'s symbol list — what is sliced, by name, never by line

Where it lives: `tools/lexslice.py`'s `SLICE_TYPES`, `SLICE_FUNCTIONS`, `SLICE_VARIABLES` and
`SLICE_CHARSET` lists (`(source file, kind, symbol)` tuples) and `SLICE_LEX_STATES`,
`SLICE_KEYWORD_ARRAYS`. This is the set of exactly which types, functions, file-scope variables and
keyword tables constitute "the lexer" for extraction purposes; `tests/test_lexslice.py` proves the
property that matters — the same symbols at different line numbers extract to the same text.

*Why a script cannot derive it:* which functions/types/variables *are* the lexer was a human decision,
made once from `tools/vendor/thd.md` and P5a's spike (`PLAN.md` §2.2's escalating-options experiment),
not something a script can infer from `sql_lex.cc` alone.

*What a new revision would do:* MariaDB renaming, splitting or moving one of these symbols raises
`LexSliceError` ("cannot locate ..." or "... is defined N times; ambiguous") — a loud failure, not a
silent drift — and a human has to update the list. A **new** function the lexer's transitive closure
now calls, but that is not in `SLICE_FUNCTIONS`, would instead show up as an unresolved external symbol
at link time.

### 3.8 `lexslice.py`'s `DECLARED_NOT_DEFINED_IS_EXPECTED` set

Where it lives: `tools/lexslice.py`, `DECLARED_NOT_DEFINED_IS_EXPECTED = {"get_escape_func",
"body_utf8_start", "body_utf8_append", "body_utf8_append_ident", "body_utf8_append_escape",
"get_body_utf8_maximum_length", "add_digest_token", "reduce_digest_token"}`. These eight names are
declared in the sliced `Lex_input_stream` class but deliberately given no body (`nm -u` on the object
file is the check that nothing else is missing).

*Why a script cannot derive it:* deciding that a declared-but-unused member is safe to leave
undefined — rather than a finding that the slice's transitive closure is incomplete — requires reading
what calls it (`get_escape_func`'s one caller, `body_utf8_append_escape`, is itself shimmed, per the
file's own comment) and
cross-checking `tools/vendor/thd.md`'s "not modelled" rows (§3.9 below).

*What a new revision would do:* a name dropping out of this set because MariaDB starts calling it from
inside the slice would surface as a link error (an undefined reference), not a silent hole; a name that
MariaDB stops declaring at all would make this set's entry simply unused (harmless, but worth pruning).

### 3.9 `src/lexshim.h`'s shim bodies, and `tools/vendor/thd.md`'s "not modelled" rows

Where it lives: `src/lexshim.h` (the fake `THD`/`LEX`/`SELECT_LEX` surface, one field per row of
`tools/vendor/thd.md`, each tagged `[thd.md ...]` or `[round N]`) and, on the "not modelled" side,
three rows of `tools/vendor/thd.md` itself: `m_echo`/`m_cpp_*` (the pre-processed buffer — "never feeds
back into the token stream"), `add_digest_token`/`m_digest` (performance-schema digest only) and
`thd->alloc`/`body_utf8_*`/`str->set_ident` (semantic values; the lexer reports the raw lexeme span
instead).

*Why a script cannot derive it:* which fields of MariaDB's real `THD`/`LEX`/`Lex_input_stream` the
sliced functions actually touch was established empirically, round by round, by trying to compile and
reading what the compiler demanded (`evidence/P5a/shim-symbols.json`'s `new_symbols` per round) — this
is P5a's whole reason for existing (`PLAN.md` Q3/Q7), and "not modelled" is a judgement that a given
field's real behaviour is safe to leave unfaithful for this project's purposes.

*What a new revision would do:* MariaDB reading a new field inside the sliced functions, or changing
the type/shape of a field `lexshim.h` already fakes, reopens the round process — a fresh compile
against the unchanged shim demands a new symbol, exactly the mechanism `make lexspike` measures and
`evidence/P5a/shim-symbols.json` records. `tools/vendor/thd.md` is vendored (copied from
`<mdb-bison-ts>/ledger/thd.md`, hashed, never patched here), so a widened ledger on the
sibling project would need re-mirroring, not a local edit.

### 3.10 The charset route (b) state-map generator

Where it lives: `tools/lexslice.py`'s `SLICE_CHARSET` list — `("strings/ctype-utf8.c", "array",
"ctype_utf8mb4")`, `("strings/ctype-utf8.c", "function", "my_valid_mbcharlen_utf8mb3")`,
`("strings/ctype-utf8.c", "function", "my_charlen_utf8mb4")`, `("mysys/charset.c", "function",
"is_multi_byte_ident")`, `("mysys/charset.c", "function", "init_state_maps")` — feeding
`build/state_maps_gen.cc`, compiled and *run* once at extraction time to emit `build/state_maps.h`
(the 256-byte `state_map`/`ident_map` tables). `evidence/P1/extract.json`'s `artefacts` entries for
`build/state_maps.h` and `build/state_maps_gen.cc` record their sha256/size for the current run.

*Why a script cannot derive it:* PLAN.md §2.2 named two honest ways to pay this charset dependency —
link a slice of `strings/`+`mysys/`, or generate the tables at build time — as open question Q4
(§7.1), and P5a chose route (b) on measured evidence, not derived it; the five symbols above are
exactly what `init_state_maps` was found to need, a fact established by compiling, not inferred.

*What a new revision would do:* MariaDB restructuring `init_state_maps` or `ctype_utf8mb4`'s
construction (a new dependency, a renamed helper) would move the required symbol set, and
`SLICE_CHARSET` would need updating the same way `SLICE_FUNCTIONS` does (§3.7).

### 3.11 `tools/provenance.py`'s `TRACKED_FILES` — 15 files today

`evidence/P6/regeneration.json`'s `tracked_files` = `15` (task 9.3's own live count; an earlier
docstring in `tests/test_drift_drill.py` and `tests/test_provenance.py` still says "twelve" from when
task 8.1 grew the list from five — that count is stale prose, not evidence, and the live list below is
read from the module itself, not copied from those comments). The 15, in file order:

```
sql/sql_yacc.yy
sql/sql_lex.cc
sql/lex.h
sql/gen_yy_files.cmake
VERSION
sql/sql_lex.h
sql/lex_ident_cli.h
sql/lex_symbol.h
include/m_ctype.h
strings/ctype-utf8.c
strings/ctype-utf8.h
mysys/charset.c
strings/my_strtoll10.c
strings/ctype-latin1.c
sql/share/charsets/Index.xml
```

*Why a script cannot derive it:* this is the enumerated set of upstream files the pipeline's
fail-closed guarantee actually covers — it grew twice already (5 → 12 → 15, tasks 8.1/8.2) as the
lexer-generation stage started reading more of MariaDB's own sources, and each growth was a human
noticing a new dependency, not a script discovering one.

*What a new revision would do:* it is the converse of §3.7/§3.10 — if a future MariaDB revision moves
code that `lexslice.py` or the charset generator depends on into a file **not** in this list, that
dependency's drift would not fail the provenance check closed; it would only be caught (loudly, via
`LexSliceError` or a compile failure) if the symbol itself also moved or changed shape. A revision that
changes a tracked file's *content* without moving any sliced symbol is exactly what this list is
designed to catch.

### 3.12 `PINS.toml`'s bison and gcc pins — one asserted, one recorded

Where it lives: `PINS.toml`'s `[tools]` table, `bison = "3.8.2"` / `gcc = "13.3.0"`, and
`tools/extract.sh`'s `BISON_PINNED=3.8.2` / `GCC_PINNED=13.3.0`.

**Bison is asserted**: `extract.sh` step 6 reads `bison --version`, compares it against
`BISON_PINNED`, and exits 3 on any mismatch — a different bison version can change the automaton, which
is exactly what P1's isomorphism check exists to police, so the pipeline refuses to run under the wrong
one at all.

**Gcc is recorded, not asserted**: `extract.sh`'s own comment: *"gcc is pinned at 13.3.0 in PINS.toml.
Unlike the bison pin this is RECORDED rather than asserted ... the compiler version cannot move the
automaton (which is what P1's assertions protect), and the binary's own sha256 lands in
`evidence/P2/build.json`, so a toolchain change is visible in the evidence either way. A hard assertion
here would only turn a routine gcc upgrade into an unbuildable tree."* `make doctor` is this project's
pin checker and reports the running gcc version regardless.

*Why a script cannot derive it:* the decision that bison's version is load-bearing for the automaton
while gcc's is not is a judgement about what each tool actually does to the artefacts, not something
derivable from either tool's own version string.

*What a new revision would do:* nothing here is about MariaDB moving — it is the trap for whoever
regenerates on a *different host*: a bison upgrade refuses outright (exit 3, loud); a gcc upgrade
proceeds and only shows up as a changed `build/mdbparse` sha256 in `evidence/P2/build.json`.

### 3.13 The `sql_mode` refusal — `ORACLE` / `MSSQL` → exit 3

Where it lives: `src/mdbp_api.c`'s `mdbp_sql_mode_refused()` (declared in `src/mdbp_internal.h`,
called from `src/mdbparse.c` at the `--sql-mode` parse site and from the library's `mdbp_parser_new()`
path), returning the offending mode's canonical spelling. The CLI maps a refusal to `EXIT_UNSUPPORTED`
= `3`.

*Why a script cannot derive it:* which `sql_mode` names change the *language* enough that this project
must refuse rather than silently mis-lex is a judgement recorded in `PLAN.md` §2.2: "`MODE_ORACLE` and
`MODE_MSSQL` are recognised and rejected with exit 3, never silently accepted ... a mode this lexer
cannot honour must not look honoured." No script derives that list from `sql_lex.cc` alone — a human
read `sql_lex.cc`'s `sql_mode` branches and decided which of them this MariaDB-variant-only,
utf8mb4-only extraction cannot honour.

*What a new revision would do:* MariaDB introducing a new `sql_mode` flag that changes lexer or grammar
behaviour the way `MODE_ORACLE`/`MODE_MSSQL` do would need the same human judgement extended to it —
otherwise this library would silently mis-lex under a mode it does not actually implement, exactly the
failure mode PLAN.md §2.2 says must not happen.

## 4. The trend — `override_entries` at P1, P5b and P6

| phase | `override_entries` | evidence |
|---|---|---|
| P1 | **0** | `evidence/P1/convergence.json`, `secondary.override_entries` (also `evidence/P1/extract.json`'s top-level `override_entries`) |
| P5b | **0** | `evidence/P5b/convergence.json`, `secondary.override_entries` |
| P6 | **0** | `evidence/P6/convergence.json`, `secondary.override_entries` (also `secondary.override_entries_p1` = `0` and `secondary.override_entries_p5b` = `0`, cross-checked in the same record so the trend cannot silently diverge from the phases it is reporting on) |

Flat at zero throughout every phase this project has run. **A rising number would mean the pipeline is
accumulating regeneration debt** — human judgement calls baked into `overrides/*.toml` that a future
MariaDB revision will force somebody to re-examine by hand, one entry at a time, rather than the
extraction simply re-running unattended.

## 5. The limitation — recorded, not buried

**Only one MariaDB revision is available on this host** (`bab03b0f`, per `PLAN.md` §1.1/§1.2).
Regenerability is therefore evidenced by:

- **determinism** — `make regen-check` wipes `build/`, re-runs the whole pipeline from scratch, and
  requires byte-identical output (`evidence/P1/regen.json`: `byte_identical: true`,
  `artefacts_compared: 15`);
- **the drift drill** (`tests/test_drift_drill.py`) — a synthetic one-line perturbation of a *copy* of
  the pinned tree, never a second real revision;
- **task 9.3's rehearsal** (`evidence/P6/regeneration.json`, §2 above) — the same synthetic
  perturbation, exercised through the real `extract.sh` end to end for the provenance/re-pin/isomorphism
  steps, with the compile and sweep steps explicitly `documented_not_rehearsed` rather than silently
  assumed.

**Never against a second real MariaDB revision.** `evidence/P1/regen.json`'s own `note` says this
outright: *"only bab03b0f is available on this host, so regenerability is tested by this determinism
check plus the drift drill ..., never by re-running the pipeline against a second real MariaDB
revision."* Nothing in this project closes that gap by assertion — it stays open until a second real
tree is available to run §2 against for real.

## 6. What this phase did not settle

Per `PLAN.md` §7.1, **Q1** (does an empty vs. non-empty mid-rule action body produce an identical
automaton in bison 3.8.2?) and **Q2** (does removing `%type`/`%union`/`%destructor` perturb conflict
counts or resolution?) remain answered by **P1's isomorphism check** — the per-rule digest comparison
against `corpus/grammar-model.json`, unchanged by this task. This task wrote documentation and one
closing measurement (`make api-smoke`); it closed none of the plan's open questions by assertion, and
none of §3's inventory rows above are new findings — every one of them was already true of the tree
before this task started, and this task's only job was to write them down where a future maintainer
will look.
