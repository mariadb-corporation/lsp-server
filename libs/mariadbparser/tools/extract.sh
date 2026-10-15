#!/usr/bin/env bash
# tools/extract.sh — the extraction pipeline, and the ONLY way anything
# in build/ comes into existence. No step of it is manual, and no artefact it
# produces is ever hand-edited: a parser that was hand-patched once cannot be
# regenerated for the next MariaDB release, which is the entire point of the
# experiment. Anything the transform cannot do mechanically goes in
# overrides/*.toml, whose entry count this script records as the project's
# regeneration debt.
#
# THIS IS THE SUPPORTED ENTRY POINT (task 9.4): `tools/extract.sh
# --source <tree> --variant MARIADB` is the one way to regenerate this
# project against a MariaDB tree. When MariaDB moves, `docs/REGENERATION.md`
# is the procedure — the exact commands, what evidence shows each step
# has actually been exercised, and the honest inventory of every place a
# human judgement is baked into this pipeline. Read it before changing
# anything here.
#
#   tools/extract.sh [--source <tree>] [--variant MARIADB|ORACLE]
#                         [--out <dir>] [--no-provenance-check]
#                         [--no-compile | --compile-only]
#                         [--model <grammar-model.json>]
#
# --source defaults to $MARIADB_SOURCE_DIR; with neither, the script exits 2.
# --out defaults to $MDBP_BUILD_DIR, else build/; --model defaults to
# $MDBP_CORPUS_DIR/grammar-model.json, else corpus/grammar-model.json.
#
# Steps, in order:
#   1. resolve the source tree (--source, else $MARIADB_SOURCE_DIR)
#   2. provenance.py --check      (fail-closed: a changed source tree
#                                  invalidates every number computed from it)
#   3. MariaDB's own sql/gen_yy_files.cmake — its %ifdef MARIADB / %ifdef
#      ORACLE preprocessor, and NO other part of MariaDB's build: no server
#      cmake configure, no mariadb.h, no generated lex_token.h, no sql_yacc.cc
#   4. assertions on the selected variant's .yy
#   5. stub.py            build/yy_<variant>.yy -> build/yy_stub.yy
#   6. bison 3.8.2, stderr captured; a non-zero exit OR a non-empty stderr is
#      a failure (K2 is zero warnings; nothing here filters or whitelists —
#      see the measured note on BISON_REPORT below for why --report is
#      `all` minus `counterexamples`)
#   7. stub.py --rule-map build/parser.c        -> build/rule-map.json
#   7a. lexer generation (task 8.1): tools/lexslice.py locates
#      Lex_input_stream::lex_token/lex_one_token and their transitive
#      closure BY SYMBOL in $source_tree and writes lexer_extracted.cc,
#      keywords.h, lexstates.h, state_maps_gen.cc and lexslice.json into
#      $out -- this ALWAYS runs (it is extraction, not compilation), because
#      build/parser.h (step 6's) exists by now for the keyword slice's
#      SYM() cross-check. Compiling-and-running state_maps_gen.cc (charset
#      route (b), emitting state_maps.h) is the one piece --no-compile DOES
#      skip, mirroring step 9's own gcc build; state_maps.h is then simply
#      absent, not an error.
#   8. evidence/P1/extract.json (now 15 artefacts: the original nine plus
#      the six task 8.1 adds) and evidence/P5b/lexgen.json.
#   9. gen_tables.py (against --model, corpus/grammar-model.json by default)
#      -> build/mdbp_tables.h; then g++ -fPIC over src/mdbp_lex.cc
#      (which #includes the sliced <out>/lexer_extracted.cc) -> mdbp_lex.o;
#      then gcc -fPIC -O2 -Wall -Wextra -Werror over src/parser_glue.c (which
#      #includes the generated parser), src/mdbp_runtime.c and src/mdbp_api.c
#      -> three objects, archived with mdbp_lex.o into libmariadbparser.a and
#      linked into libmariadbparser.so.<MAJOR>.<MINOR>.<PATCH> (soname
#      libmariadbparser.so.<MAJOR>, both from include/mdbparse.h's MDBP_VERSION_*,
#      exports controlled by src/libmariadbparser.map -- task 9.2) plus its
#      two symlinks and build/mariadbparser.pc; then gcc -O2 -Wall -Wextra
#      -Werror over src/mdbparse.c (the CLI's own main) LINKED AGAINST
#      libmariadbparser.a -> build/mdbparse, and evidence/P2/build.json.
#      --no-compile skips this stage (the drift drill has no use for a
#      binary); --compile-only runs ONLY this stage
#      over an existing <out>/parser.{c,h} + rule-map.json + grammar.xml +
#      the lexer artefacts, which is what `make build` calls, so iterating on
#      the C does not re-run bison. --compile-only never runs steps 2-7a
#      either, so it never touches the lexer artefacts (already on disk from
#      the last full run).
#
# The run record (step 8) goes to evidence/P1/extract.json for the default
# --out build and to <dir>/extract.json for any other --out, so a run into a
# scratch directory cannot clobber the phase's evidence; step 9's record and
# evidence/P5b/lexgen.json follow the same rule.  Either way the path is
# printed.
#
# build/yy_oracle.yy is written by step 3 and ignored: no target builds it, no
# test touches it. --variant ORACLE is accepted and untested; it exists so an
# Oracle build is not foreclosed.
#
# --model (task 9.3) names the grammar model step 9's gen_tables.py checks its
# generated tables against; the default is corpus/grammar-model.json, exactly
# as it always was, and nothing else about step 9 changes. Regenerating
# against a MariaDB tree that is not the pinned one is what it is for; that
# procedure is documented outside this script.
#
# Exit codes: 0 ok · 1 internal error (bison) · 2 usage (including no source
# tree) · 3 unsupported input or a failed assertion.
set -uo pipefail

BISON_PINNED=3.8.2
# Recorded, not asserted -- see compile_stage() below for why.
GCC_PINNED=13.3.0

# --report is `all` MINUS `counterexamples`, which is a deviation from P1's
# deliverable 3 (`--report=all`) and the only one in this script. It is not
# argued here, it is measured: `make report-probe`
# (tools/bison_report_probe.sh) writes evidence/P1/bison-report-probe.json,
# which holds two separate measurements. Read it for the numbers; the shape:
#
#  1. `runs` — both flag sets over the very build/yy_stub.yy this pipeline
#     produced. `--report=all` does not complete on this grammar on this host
#     at all: bison is SIGKILLed while generating counterexamples, and NOT by
#     the probe's own timeout (it dies with hundreds of seconds of budget left,
#     so raising the limit would not help), having written a fraction of
#     grammar.output and NONE of grammar.xml, parser.c or parser.h. The pinned
#     flag would not produce this pipeline's artefacts; it would produce
#     nothing. That is the whole reason for the deviation.
#
#  2. `control` — what the dropped sub-report costs, measured where it can be.
#     A run that dies writes nothing to compare, so the comparison has to come
#     from a grammar on which BOTH flag sets finish: the probe runs both over
#     tools/report_probe_control.yy, a small grammar with real
#     shift/reduce conflicts so that `counterexamples` has something to say.
#     Both runs write into the SAME output path (bison embeds that path in
#     parser.c and, uppercased, in parser.h's include guard, so two output
#     directories would make those hashes unequal whatever the flags did).
#     Result there: grammar.xml, parser.c and parser.h are byte-identical
#     between the two flag sets and only grammar.output differs, i.e. this
#     sub-report adds report text and touches no generated table. The JSON's
#     `control.informative` records that the control's grammar.output really
#     did differ, so a comparison with nothing in it cannot pass for a clean
#     one.
#
# What is NOT claimed, because it cannot be measured here: that the two flag
# sets would produce identical artefacts on the MariaDB grammar itself. That
# run does not finish, so there are no outputs of it to hash.
#
# Nothing here is filtered and no warning is whitelisted: with counterexample
# generation off, bison accepts MariaDB's rules — unaltered — with an exit of 0
# and an empty stderr, which is what K2 asks for. Whether the automaton is the
# right one is not this flag's business either way; that is what P1's
# isomorphism check against corpus/grammar-model.json settles.
#
# The flags a given run actually used are recorded beside that run's hashes as
# `bison_report_flags` in evidence/P1/extract.json, so the two can never drift
# apart, and tests/test_extract.py ties all three together.
BISON_REPORT=states,itemsets,lookaheads,solved

# The `-D name=value` definitions this pipeline hands bison, which are exactly
# `%define name value` written on the command line instead of in the grammar
# file (bison 3.8.2 --help: "-D, --define=NAME[=VALUE]  similar to '%define
# NAME "VALUE"'").
#
# parse.lac=full is task 8.3's, and it is what makes yy_lac() -- bison's own
# "can this terminal eventually be shifted from the current stack" oracle --
# exist in build/parser.c at all. src/parser_glue.c calls it to answer
# valid(T) for the lexer's five parsing_place sites (PLAN.md S1.8), and
# evidence/P5a/valid-symbol.json is the measurement that chose it over the
# action-row walk: at an INCONSISTENT state carrying a default reduction the
# row walk answers a strict SUBSET of the truth, and bison's own verbose
# error message names LAC's set.
#
# WHY THE COMMAND LINE AND NOT tools/stub.py.  Both routes are inside the
# pipeline (the house rule that matters is that no artefact is hand-edited),
# and both are a `%define`, which K2 explicitly allows -- but only this one
# leaves build/yy_stub.yy BYTE-IDENTICAL to what it was before the change.
# The isomorphism check that guards K1/K2 compares an automaton built from
# that file; keeping its bytes fixed makes "the automaton did not move" a
# fact about an unchanged input rather than a claim about a changed one.
# (stub.py's transform is also line-for-line length-preserving -- it asserts
# `len(out) == len(lines)` -- so emitting a new directive line there would
# have meant either appending to the %union replacement line or relaxing that
# invariant, both of which cost more than they buy.)
#
# MEASURED, on build/yy_stub.yy, bison 3.8.2, same output directory, with and
# without this flag: build/grammar.xml and build/grammar.output are BYTE
# IDENTICAL, build/parser.h is byte identical, and only build/parser.c differs
# (LAC adds parser code; it does not move the automaton). bison's stderr stays
# empty and `%expect 72` stays satisfied.
#
# Recorded beside the hashes it produced as `bison_defines` in
# evidence/P1/extract.json, the same way BISON_REPORT is, so the two can never
# drift apart.
BISON_DEFINES="-Dparse.lac=full"

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

source_tree=${MARIADB_SOURCE_DIR:-}
variant=MARIADB
out=${MDBP_BUILD_DIR:-build}
provenance_check=1
compile=1
compile_only=0
model=${MDBP_CORPUS_DIR:-corpus}/grammar-model.json

while [ $# -gt 0 ]; do
    case "$1" in
        --source) source_tree=${2:?--source needs a value}; shift 2 ;;
        --variant) variant=${2:?--variant needs a value}; shift 2 ;;
        --out) out=${2:?--out needs a value}; shift 2 ;;
        --model) model=${2:?--model needs a value}; shift 2 ;;
        --no-provenance-check) provenance_check=0; shift ;;
        --no-compile) compile=0; shift ;;
        --compile-only) compile_only=1; shift ;;
        -h | --help)
            # The WHOLE leading comment block, derived from the file instead
            # of from a second hardcoded line number. The old `sed -n '2,32p'`
            # was that second number, and it had already drifted: the block
            # now runs to line 48, so the help stopped mid-sentence inside
            # step 9 and dropped the exit-code table entirely. Line 1 is the
            # shebang (skipped); the block ends at the first line after it
            # that does not begin with '#', which is `set -uo pipefail`.
            awk 'NR > 1 { if ($0 !~ /^#/) exit; print }' "${BASH_SOURCE[0]}"
            exit 0
            ;;
        *)
            echo "extract.sh: unknown argument '$1'" >&2
            exit 2
            ;;
    esac
done

if [ "$compile_only" = 1 ] && [ "$compile" = 0 ]; then
    echo "extract.sh: --compile-only and --no-compile contradict each other" >&2
    exit 2
fi

case "$variant" in
    MARIADB | ORACLE) ;;
    *)
        echo "extract.sh: --variant must be MARIADB or ORACLE, not '$variant'" >&2
        exit 2
        ;;
esac

# No default source tree: the MariaDB tree is always named, by --source or by
# $MARIADB_SOURCE_DIR (the CMake build passes -DMARIADB_SOURCE_DIR through).
if [ -z "$source_tree" ]; then
    echo "extract.sh: MARIADB_SOURCE_DIR is not set (and no --source given)" >&2
    exit 2
fi

cd "$root" || exit 2

# Every child (lexslice.py, the record writers below, build_record.py) spells
# paths through tools/common.py's display_path(), which recognises the MariaDB
# tree and the build tree only by $MARIADB_SOURCE_DIR and $MDBP_BUILD_DIR.
# Both are exported here, resolved once (a relative path is relative to this
# directory, as it always was), so a record's `source_tree` reads
# `<mariadb-source>` and its artefact keys read `build/<name>` however this
# script was invoked: from CMake, or by hand with any --source and --out.
abs_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s\n' "$root/$1" ;;
    esac
}
MARIADB_SOURCE_DIR=$(abs_path "$source_tree")
MDBP_BUILD_DIR=$(abs_path "$out")
export MARIADB_SOURCE_DIR MDBP_BUILD_DIR

if [ "$compile_only" != 1 ] && [ ! -f "$source_tree/sql/sql_yacc.yy" ]; then
    echo "extract.sh: no $source_tree/sql/sql_yacc.yy" >&2
    exit 2
fi

py() { uv run --frozen -- python "$@"; }

# --- 9. the compile stage ---------------------------------------------------
# Defined here as a function because it has two callers: `--compile-only`
# (which is what `make build` runs, so iterating on the C does not re-run
# bison) and the tail of the full pipeline below.
#
# gcc is pinned at 13.3.0 in PINS.toml. Unlike the bison pin this is RECORDED
# rather than asserted: `make doctor` is this repo's pin checker and already
# reports gcc, the compiler version cannot move the automaton (which is what
# P1's assertions protect), and the binary's own sha256 lands in
# evidence/P2/build.json, so a toolchain change is visible in the evidence
# either way. A hard assertion here would only turn a routine gcc upgrade
# into an unbuildable tree.
#
# `-Wno-*` flags: NONE today. If bison's generated translation unit ever needs
# one, add it to MDBP_WNO_FLAGS *with* a reason on the same index of
# MDBP_WNO_REASONS -- both land in evidence/P2/build.json, which is the only
# form in which silencing a warning is permitted. A warning in our own src/*.c
# is fixed, never silenced.
MDBP_WNO_FLAGS=()
MDBP_WNO_REASONS=()

# --- the C++ translation unit (task 8.2) ------------------------------------
# src/mdbp_lex.cc #includes the sliced build/lexer_extracted.cc, so it is the
# ONE object file that carries MariaDB's lexer. It cannot share the C command
# line above: it is C++ (MariaDB's own sources are), and its YYSTYPE is
# MariaDB's union while the parser's is `int` (src/stub.h), which is exactly
# why the two live in different translation units with src/mdbp_lex.h as the
# only thing that crosses.
#
# -I "$source_tree/strings" is a READ-ONLY include of the pinned tree, for
# MariaDB's own strings/ctype-utf8.h -- the header my_charlen_utf8mb4, which
# tools/lexslice.py slices by symbol, is written against.
#
# -fno-exceptions -fno-rtti: nothing here throws or downcasts, and without
# them g++ emits references to libstdc++'s personality routine, which would
# change what the binary links for no behaviour. With them the object file's
# only undefined symbols are libc's plus our own (`nm -u` is the check), so
# the link below stays a plain gcc link and evidence/P2/build.json's
# `binary_links_only_libc` stays true of the lexer-carrying binary too.
#
# NO -Werror, and no -Wno-* SUPPRESSION EITHER, and both halves of that are
# deliberate. The slice compiles with exactly one warning -- MariaDB's own
# `UNINIT_VAR(c)` in lex_one_token, which src/lexshim.h spells `x= x` the way
# include/my_global.h does -- and silencing it would need -Wno-uninitialized,
# which in this single translation unit would also silence our own
# src/mdbp_lex.cc. A warning in OUR code is fixed, never silenced, so the
# warning stands, the whole stderr goes to $out/gxx.stderr, and the count
# lands in evidence/P2/build.json as `lexer_warnings`.
MDBP_CXX_WNO_FLAGS=()
MDBP_CXX_WNO_REASONS=()

# mdbp_header_version() -- the ONE place that parses include/mdbparse.h's
# MDBP_VERSION_MAJOR/MINOR/PATCH macros. Echoes "MAJOR MINOR PATCH"
# space-separated on success; prints nothing and returns 1 if any macro is
# missing. write_pkgconfig() (the .pc Version:) and compile_stage()'s
# shared-library block (the soname, the real-name filename and the two
# symlink targets) both call this instead of hand-typing the version or
# re-deriving it with their own grep, so there is exactly one source of
# truth and bumping a macro here moves every one of those strings together.
mdbp_header_version() {
    local major minor patch
    major=$(grep -oE '#define[[:space:]]+MDBP_VERSION_MAJOR[[:space:]]+[0-9]+' include/mdbparse.h | grep -oE '[0-9]+$')
    minor=$(grep -oE '#define[[:space:]]+MDBP_VERSION_MINOR[[:space:]]+[0-9]+' include/mdbparse.h | grep -oE '[0-9]+$')
    patch=$(grep -oE '#define[[:space:]]+MDBP_VERSION_PATCH[[:space:]]+[0-9]+' include/mdbparse.h | grep -oE '[0-9]+$')
    if [ -z "$major" ] || [ -z "$minor" ] || [ -z "$patch" ]; then
        return 1
    fi
    printf '%s %s %s\n' "$major" "$minor" "$patch"
}

# --- build/mariadbparser.pc (task 9.2 deliverable 4) ------------------------
# Generated, never hand-written: MAJOR/MINOR/PATCH come from
# mdbp_header_version() above, so the .pc's Version and mdbp_version_string()
# cannot drift apart (deliverable 2 -- ONE source of truth for the version).
# Cflags/Libs point at the BUILD TREE ($out), made absolute so
# `PKG_CONFIG_PATH=build pkg-config --cflags --libs mariadbparser` works from
# any cwd; nothing here is installed (task 9.4/distribution packaging is out
# of scope). Libs also carries an rpath into $out: this is an uninstalled
# build-tree .pc, so a program linked against libmariadbparser.so through it
# has to find that .so without an install step or a hand-set
# LD_LIBRARY_PATH -- "pkg-config --cflags --libs ... is enough" is
# deliverable 4's own wording, and an rpath is what makes that literally true
# for the shared form too.
write_pkgconfig() {
    local out_dir=$1
    local out_abs
    local major minor patch version
    local pc

    out_abs=$(cd "$out_dir" && pwd)
    if ! read -r major minor patch < <(mdbp_header_version); then
        echo "extract.sh: could not read MDBP_VERSION_* from include/mdbparse.h" >&2
        return 1
    fi
    version="$major.$minor.$patch"
    pc="$out_dir/mariadbparser.pc"
    cat > "$pc" <<PCEOF
libdir=$out_abs
includedir=$root/include

Name: mariadbparser
Description: MariaDB's own LALR grammar, extracted from sql_yacc.yy and sql_lex.cc by script; accepts a superset of what mariadbd accepts; MariaDB grammar variant only.
Version: $version
Cflags: -I\${includedir}
Libs: -L\${libdir} -Wl,-rpath,\${libdir} -lmariadbparser
PCEOF
    return 0
}

compile_stage() {
    local started
    local finished
    # The shared object's soname and real name, or empty when this src/ tree
    # builds no library at all (see the build_record.py call at the end).
    local record_soname=""
    local record_realname=""
    local rc
    local gcc_version
    local gcc_exit
    local gxx_version
    local gxx_exit
    local gxx_warnings
    local table_summary

    started=$(date +%s%N)

    # The token-name -> token-number table over all 800 terminals and the
    # serial -> rule-number table, generated from build/parser.h +
    # build/grammar.xml + build/rule-map.json and checked against $model
    # (corpus/grammar-model.json unless --model said otherwise). It exits 3
    # having written nothing if they disagree: a wrong table would poison
    # every number in P2, P3 and P4 invisibly.
    table_summary=$(py tools/gen_tables.py \
        --parser-h "$out/parser.h" \
        --grammar-xml "$out/grammar.xml" \
        --rule-map "$out/rule-map.json" \
        --model "$model" \
        --out "$out/mdbp_tables.h")
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "extract.sh: gen_tables.py failed (rc=$rc)" >&2
        return $rc
    fi

    gcc_version=$(gcc -dumpfullversion 2>/dev/null || gcc -dumpversion)
    if [ "$gcc_version" != "$GCC_PINNED" ]; then
        echo "extract.sh: note: gcc is $gcc_version, PINS.toml pins $GCC_PINNED" >&2
    fi

    # The C++ object first: a link that has no lexer to link is not this
    # pipeline's binary.
    if [ ! -f "$out/lexer_extracted.cc" ]; then
        echo "extract.sh: $out/lexer_extracted.cc does not exist: run the full pipeline (tools/extract.sh without --compile-only) first" >&2
        return 2
    fi
    # -fPIC (task 9.2): this object joins BOTH libmariadbparser.a and
    # the shared object below -- one build, not two -- so it has to be
    # position-independent for the shared form. PIC objects link into a
    # static archive or a non-PIE executable without cost to correctness.
    local gxx_argv=(g++ -std=c++17 -O2 -Wall -Wextra "${MDBP_CXX_WNO_FLAGS[@]}"
                    -fno-exceptions -fno-rtti -fPIC -fdiagnostics-plain-output
                    -I include -I src -I "$out" -I "$source_tree/strings"
                    -c -o "$out/mdbp_lex.o" src/mdbp_lex.cc)

    printf '%s\n' "${gxx_argv[@]}" > "$out/gxx.argv"
    "${gxx_argv[@]}" 2> "$out/gxx.stderr"
    gxx_exit=$?
    if [ $gxx_exit -ne 0 ]; then
        echo "extract.sh: g++ exited $gxx_exit" >&2
        sed -n '1,60p' "$out/gxx.stderr" >&2
        return 1
    fi
    gxx_version=$(g++ -dumpfullversion 2>/dev/null || g++ -dumpversion)
    gxx_warnings=$(grep -c 'warning:' "$out/gxx.stderr" || true)
    # Recorded, not asserted: the object file's undefined symbols. It is how a
    # reviewer sees that the lexer needs libc and our own bridge and nothing
    # else -- in particular no libstdc++ -- which is what keeps the link below
    # a plain gcc link.
    nm -u "$out/mdbp_lex.o" 2>/dev/null | awk '{print $NF}' | sort -u \
        > "$out/lexer.undefined"
    printf '%s\n' "${MDBP_CXX_WNO_FLAGS[@]}" > "$out/cxx-wno.flags"
    printf '%s\n' "${MDBP_CXX_WNO_REASONS[@]}" > "$out/cxx-wno.reasons"

    : > "$out/gcc.stderr"

    # The library (task 9.2) is built only when this src/ tree HAS
    # src/mdbp_api.c -- the one file task 9.1 introduced that splits the
    # implementation out of src/mdbparse.c. A --out driven against an older
    # src/ tree that predates that split (exercised only by
    # tests/test_head1_vs_head_cli_blackbox.py's pre-9.1 fixture, a
    # byte-for-byte copy of src/ as it stood before task 9.1, used to prove
    # that refactor left the CLI's behaviour unchanged) has no library to
    # build -- there is nothing task 9.1 split out of yet -- so it falls back
    # to the single monolithic command this pipeline used before task 9.2,
    # unmodified, rather than a later infrastructure task retroactively
    # breaking an earlier one's regression proof. build_record.py's `library`
    # field is `None` for a build produced this way -- decided by whether
    # this branch exported MDBP_LIB_SONAME/MDBP_LIB_REALNAME, not by whether
    # some libmariadbparser.a happens to be on disk, because `--out` may
    # point at a COPY of a real build whose archive is someone else's.
    if [ -f src/mdbp_api.c ]; then
        # --- the library's three C objects (task 9.2 deliverable 1) --------
        # src/parser_glue.c is the ONE translation unit that contains bison's
        # generated parser (it #includes "parser.c", which -I "$out" finds),
        # so build/parser.c is deliberately NOT on any of these command
        # lines. src/mdbparse.c (the CLI's own main) is deliberately NOT one
        # of these three: the library carries the parser, the runtime, the
        # API and the lexer object, never the CLI. -fPIC for the same reason
        # mdbp_lex.o has it: these objects join both the static archive and
        # the shared object, one build of the code for both forms.
        : > "$out/gcc-lib.argv"
        local lib_c_sources=(src/parser_glue.c src/mdbp_runtime.c src/mdbp_api.c)
        local lib_objects=("$out/parser_glue.o" "$out/mdbp_runtime.o" "$out/mdbp_api.o")
        local lib_i lib_src lib_obj lib_argv
        for lib_i in "${!lib_c_sources[@]}"; do
            lib_src=${lib_c_sources[$lib_i]}
            lib_obj=${lib_objects[$lib_i]}
            lib_argv=(gcc -O2 -Wall -Wextra -Werror "${MDBP_WNO_FLAGS[@]}"
                      -fPIC -I include -I src -I "$out" -c -o "$lib_obj" "$lib_src")
            printf '%s\n' "${lib_argv[@]}" >> "$out/gcc-lib.argv"
            "${lib_argv[@]}" 2>> "$out/gcc.stderr"
            gcc_exit=$?
            if [ $gcc_exit -ne 0 ]; then
                echo "extract.sh: gcc exited $gcc_exit compiling $lib_src" >&2
                sed -n '1,60p' "$out/gcc.stderr" >&2
                return 1
            fi
        done
        lib_objects+=("$out/mdbp_lex.o")

        # --- libmariadbparser.a (deliverable 1) -----------------------------
        rm -f "$out/libmariadbparser.a"
        printf '%s\n' "ar rcs $out/libmariadbparser.a ${lib_objects[*]}" \
            > "$out/ar.argv"
        ar rcs "$out/libmariadbparser.a" "${lib_objects[@]}"
        rc=$?
        if [ $rc -ne 0 ]; then
            echo "extract.sh: ar exited $rc" >&2
            return 1
        fi

        # --- libmariadbparser.so.<major>.<minor>.<patch>, its soname and its
        # symlinks (deliverables 1-3) -----------------------------------------
        # The soname and the real name are BUILT from mdbp_header_version()
        # above (the same call write_pkgconfig makes) -- MAJOR alone for the
        # soname, all three for the real name -- never hand-typed as separate
        # literals disconnected from include/mdbparse.h's MDBP_VERSION_* macros,
        # so bumping a macro moves the linker's -soname, the real filename and
        # both symlink targets together instead of leaving a stale one behind
        # (deliverable 2). The exported dynamic symbol table is controlled by
        # src/libmariadbparser.map (deliverable 3): only the mdbp_* functions
        # the header declares, which are the export list, nothing bison or
        # MariaDB's own sliced lexer defines.
        local lib_major lib_minor lib_patch lib_soname lib_realname
        if ! read -r lib_major lib_minor lib_patch < <(mdbp_header_version); then
            echo "extract.sh: could not read MDBP_VERSION_* from include/mdbparse.h" >&2
            return 1
        fi
        lib_soname="libmariadbparser.so.$lib_major"
        lib_realname="libmariadbparser.so.$lib_major.$lib_minor.$lib_patch"
        # A real name left behind by an earlier version (plan
        # `tokeniser-entry-points.md` P1.1): once MINOR or PATCH moves, the old
        # libmariadbparser.so.X.Y.Z would stay in $out beside the new one, and
        # a reader globbing for the real name, or a tool loading it by that
        # name, would find the wrong library. Every real name in $out other
        # than the one about to be linked is removed first. The soname link
        # (.so.MAJOR) and the dev link (.so) do not match this pattern and
        # are re-pointed by the `ln -sf` below.
        local stale_realname
        for stale_realname in "$out"/libmariadbparser.so.*.*.*; do
            if [ ! -e "$stale_realname" ] && [ ! -L "$stale_realname" ]; then
                continue
            fi
            if [ "${stale_realname##*/}" != "$lib_realname" ]; then
                rm -f -- "$stale_realname"
            fi
        done
        local shared_argv=(gcc -shared -O2
                            -Wl,-soname,"$lib_soname"
                            -Wl,--version-script=src/libmariadbparser.map
                            -o "$out/$lib_realname"
                            "${lib_objects[@]}")
        printf '%s\n' "${shared_argv[@]}" > "$out/gcc-shared.argv"
        "${shared_argv[@]}" 2>> "$out/gcc.stderr"
        gcc_exit=$?
        if [ $gcc_exit -ne 0 ]; then
            echo "extract.sh: gcc -shared exited $gcc_exit" >&2
            sed -n '1,60p' "$out/gcc.stderr" >&2
            return 1
        fi
        record_soname=$lib_soname
        record_realname=$lib_realname
        ln -sf "$lib_realname" "$out/$lib_soname"
        ln -sf "$lib_soname" "$out/libmariadbparser.so"
        ldd "$out/$lib_realname" > "$out/ldd-lib.txt" 2>&1

        write_pkgconfig "$out"
        rc=$?
        if [ $rc -ne 0 ]; then
            return $rc
        fi

        # --- build/mdbparse: links the static library, not a sixth compile
        # of the same three sources (deliverable 1: "there is exactly one
        # build of the code every measurement in this repository is about").
        # The archive is a plain linker INPUT here, never `-lmariadbparser`
        # off `-L$out`: with both libmariadbparser.a and libmariadbparser.so
        # sitting in the same directory, `-l` would prefer the .so and turn
        # this statically-linked CLI into one with a brand-new
        # shared-library runtime dependency -- exactly the regression
        # `binary_links_only_libc` exists to catch.
        local gcc_argv=(gcc -O2 -Wall -Wextra -Werror "${MDBP_WNO_FLAGS[@]}"
                        -I include -I src -I "$out" -o "$out/mdbparse" src/mdbparse.c
                        "$out/libmariadbparser.a")
    else
        local gcc_argv=(gcc -O2 -Wall -Wextra -Werror "${MDBP_WNO_FLAGS[@]}"
                        -I include -I src -I "$out" -o "$out/mdbparse" src/*.c
                        "$out/mdbp_lex.o")
    fi

    printf '%s\n' "${gcc_argv[@]}" > "$out/gcc.argv"
    "${gcc_argv[@]}" 2>> "$out/gcc.stderr"
    gcc_exit=$?
    if [ $gcc_exit -ne 0 ]; then
        echo "extract.sh: gcc exited $gcc_exit" >&2
        sed -n '1,60p' "$out/gcc.stderr" >&2
        return 1
    fi
    if [ ! -x "$out/mdbparse" ]; then
        echo "extract.sh: gcc exited 0 but $out/mdbparse is not there" >&2
        return 1
    fi

    ldd "$out/mdbparse" > "$out/ldd.txt" 2>&1

    finished=$(date +%s%N)

    printf '%s\n' "${MDBP_WNO_FLAGS[@]}" > "$out/wno.flags"
    printf '%s\n' "${MDBP_WNO_REASONS[@]}" > "$out/wno.reasons"

    # P4.1 deliverable 5: build_record.py used to hardcode the soname and the
    # real name as four `libmariadbparser.so.1[.0.0]` literals, which were
    # correct only while MDBP_VERSION was 1.0.0 and all became wrong at the
    # 1 -> 2 bump. It is handed the SAME two strings the shared-library block
    # above passed to -Wl,-soname and -o, so there is one source of truth for
    # them (include/mdbparse.h, through mdbp_header_version()) and no copy to go
    # stale. BOTH ARE EMPTY when no library was built -- a src/ tree that
    # predates task 9.1 has no src/mdbp_api.c and, before that, no
    # include/mdbparse.h to read a version out of at all
    # (tests/test_head1_vs_head_cli_blackbox.py builds exactly such a tree);
    # build_record.py's `library` block is null for those runs and never
    # looks at either name.
    MDBP_OUT=$out \
    MDBP_GCC_VERSION=$gcc_version \
    MDBP_GXX_VERSION=$gxx_version \
    MDBP_GXX_WARNINGS=$gxx_warnings \
    MDBP_TABLE_SUMMARY=$table_summary \
    MDBP_STARTED_NS=$started \
    MDBP_FINISHED_NS=$finished \
    MDBP_LIB_SONAME="$record_soname" \
    MDBP_LIB_REALNAME="$record_realname" \
    py tools/build_record.py
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "extract.sh: writing the build record failed (rc=$rc)" >&2
        return 1
    fi
    return 0
}

# --- 7a. lexer generation (task 8.1) ----------------------------------------
# tools/lexslice.py locates Lex_input_stream::lex_token/lex_one_token
# and their transitive closure BY SYMBOL in $source_tree, and writes
# lexer_extracted.cc, keywords.h, lexstates.h, state_maps_gen.cc and
# lexslice.json (the manifest) into $out. This is a defined function (like
# compile_stage above) purely so it sits next to compile_stage in the file;
# it has exactly one caller, the tail of the full pipeline below --
# --compile-only never reaches it.
#
# --parser-h is passed explicitly (rather than lexslice.py's own
# out/parser.h default) so a scratch --out is self-contained: the keyword
# slice's SYM() cross-check reads THIS run's own build/parser.h, never a
# stale one from a previous --out.
#
# --compile-state-maps is passed iff $compile=1: state_maps_gen.cc's g++
# build (charset route (b), emitting state_maps.h) mirrors step 9's own gcc
# build, so --no-compile (the drift drill's flag) skips it the same way it
# skips step 9 -- generation itself (this function's first four files, plus
# the manifest) is not compilation and always runs.
lexer_stage() {
    local rc
    local -a lexslice_argv=(
        tools/lexslice.py
        --source "$source_tree" --out "$out" --parser-h "$out/parser.h"
    )
    if [ "$compile" = 1 ]; then
        lexslice_argv+=(--compile-state-maps)
    fi
    py "${lexslice_argv[@]}"
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "extract.sh: lexslice.py failed (rc=$rc)" >&2
        return $rc
    fi
    return 0
}

if [ "$compile_only" = 1 ]; then
    for required in parser.c parser.h rule-map.json grammar.xml \
                    lexer_extracted.cc keywords.h lexstates.h state_maps.h; do
        if [ ! -f "$out/$required" ]; then
            echo "extract.sh: $out/$required does not exist: run the full pipeline (tools/extract.sh without --compile-only) first" >&2
            exit 2
        fi
    done
    compile_stage
    rc=$?
    if [ $rc -ne 0 ]; then
        exit $rc
    fi
    echo "extract.sh: ok (compile-only, gcc $(gcc -dumpfullversion 2>/dev/null || gcc -dumpversion))"
    exit 0
fi

# --- 2. provenance ----------------------------------------------------------
provenance_checked=false
if [ "$provenance_check" = 1 ]; then
    MARIADB_SOURCE_DIR="$source_tree" py tools/provenance.py --check
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "extract.sh: provenance check failed (rc=$rc)" >&2
        exit $rc
    fi
    provenance_checked=true
fi

mkdir -p "$out" || exit 1

# --- 3. MariaDB's own %ifdef preprocessor -----------------------------------
# It BLANKS the lines of the non-selected arm rather than deleting them, so
# yy_mariadb.yy has exactly sql_yacc.yy's line count and line numbering. That
# property is load-bearing: every sql_yacc.yy:LINE citation recorded anywhere
# in this project stays valid, and stub.py preserves it in turn.
cmake -DIN="$source_tree/sql/sql_yacc.yy" \
      -DOUT1="$out/yy_mariadb.yy" \
      -DOUT2="$out/yy_oracle.yy" \
      -DVAL1=MARIADB \
      -DVAL2=ORACLE \
      -DBISON_VERSION="$BISON_PINNED" \
      -P "$source_tree/sql/gen_yy_files.cmake" || {
    echo "extract.sh: gen_yy_files.cmake failed" >&2
    exit 1
}

case "$variant" in
    MARIADB) yy=$out/yy_mariadb.yy; expect_present='%expect 72'; expect_absent='%expect 73' ;;
    ORACLE) yy=$out/yy_oracle.yy; expect_present='%expect 73'; expect_absent='%expect 72' ;;
esac

# --- 4. assertions ----------------------------------------------------------
# The expected line count is read from source/PROVENANCE.json, never
# hard-coded: task 2.3's drift drill runs this pipeline against a tree with
# one line added, and it must still pass once the pin is updated.
pinned_lines=$(py - <<'PY'
import json
import sys
from pathlib import Path

sys.path.insert(0, "tools")
import common

record = json.loads((common.REPO_ROOT / "source" / "PROVENANCE.json").read_text())
print(record["files"]["sql/sql_yacc.yy"]["lines"])
PY
)
if [ -z "$pinned_lines" ]; then
    echo "extract.sh: assertion failed: source/PROVENANCE.json has no line count for sql/sql_yacc.yy" >&2
    exit 3
fi

yy_variant_lines=$(wc -l < "$yy" | tr -d ' ')
if [ "$yy_variant_lines" != "$pinned_lines" ]; then
    echo "extract.sh: assertion failed: $yy has $yy_variant_lines lines, source/PROVENANCE.json pins $pinned_lines" >&2
    exit 3
fi

if grep -qE '^%ifdef|^%else|^%endif' "$yy"; then
    echo "extract.sh: assertion failed: %ifdef/%else/%endif survived into $yy" >&2
    exit 3
fi

if ! grep -qF "$expect_present" "$yy"; then
    echo "extract.sh: assertion failed: '$expect_present' missing from $yy" >&2
    exit 3
fi
if grep -qF "$expect_absent" "$yy"; then
    echo "extract.sh: assertion failed: '$expect_absent' survived into $yy (wrong variant built)" >&2
    exit 3
fi

# --- 5. the stub transform --------------------------------------------------
py tools/stub.py --in "$yy" --out "$out/yy_stub.yy" --map "$out/stub-map.json" \
    --overrides overrides/stub.toml
rc=$?
if [ $rc -ne 0 ]; then
    echo "extract.sh: stub.py failed (rc=$rc)" >&2
    exit $rc
fi

# --- 6. bison ---------------------------------------------------------------
bison_version=$(bison --version | head -n 1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)*' | tail -n 1)
if [ "$bison_version" != "$BISON_PINNED" ]; then
    echo "extract.sh: assertion failed: bison is $bison_version, not $BISON_PINNED" >&2
    exit 3
fi

# bison embeds the file names it is given: grammar.xml's <filename>, the
# #line directives in parser.c and parser.h's include guard. It is therefore
# always run on the names build/yy_stub.yy, build/parser.c, ..., from a
# scratch directory in which `build` is a symlink to $out, so those three
# artefacts have the same bytes wherever $out is (an absolute CMake build
# directory, or the default build/).
bison_cwd="$out/.bison-cwd"
rm -rf "$bison_cwd" && mkdir -p "$bison_cwd" \
    && ln -s "$(cd "$out" && pwd)" "$bison_cwd/build" || exit 1
(
    cd "$bison_cwd" || exit 1
    bison -d --xml=build/grammar.xml \
          --report="$BISON_REPORT" \
          --report-file=build/grammar.output \
          $BISON_DEFINES \
          -o build/parser.c build/yy_stub.yy
) 2> "$out/bison.stderr"
bison_exit=$?
rm -rf "$bison_cwd"
bison_stderr_bytes=$(wc -c < "$out/bison.stderr" | tr -d ' ')
if [ "$bison_exit" -ne 0 ] || [ "$bison_stderr_bytes" -ne 0 ]; then
    echo "extract.sh: bison exit=$bison_exit, stderr=$bison_stderr_bytes bytes" >&2
    sed -n '1,40p' "$out/bison.stderr" >&2
    exit 1
fi

# --- 7. the serial <-> bison-rule-number mapping ----------------------------
py tools/stub.py --rule-map --parser-c "$out/parser.c" \
    --map "$out/stub-map.json" --out "$out/rule-map.json"
rc=$?
if [ $rc -ne 0 ]; then
    echo "extract.sh: stub.py --rule-map failed (rc=$rc)" >&2
    exit $rc
fi

lexer_stage
rc=$?
if [ $rc -ne 0 ]; then
    exit $rc
fi

# --- 8. evidence ------------------------------------------------------------
# build/ exists only on the test host and is excluded from both directions of
# <remote-runner>'s sync, so nothing written there ever reaches the dev machine.
# evidence/P1/ IS reverse-pulled, which is the only way this run is
# reviewable at all. The `artefacts` map is compared byte for byte between
# two runs by task 2.3's regen-check: do not rename these keys.
MDBP_VARIANT=$variant \
MDBP_SOURCE_TREE=$source_tree \
MDBP_OUT=$out \
MDBP_YY_VARIANT_LINES=$yy_variant_lines \
MDBP_PROVENANCE_CHECKED=$provenance_checked \
MDBP_BISON_VERSION=$bison_version \
MDBP_BISON_REPORT=$BISON_REPORT \
MDBP_BISON_DEFINES="$BISON_DEFINES" \
MDBP_BISON_EXIT=$bison_exit \
MDBP_BISON_STDERR_BYTES=$bison_stderr_bytes \
py - <<'PY'
import json
import os
import socket
import sys
from pathlib import Path

sys.path.insert(0, "tools")
import common
import stub

out = Path(os.environ["MDBP_OUT"])
stub_map = json.loads((out / "stub-map.json").read_text())
rule_map = json.loads((out / "rule-map.json").read_text())

stub_lines = (out / "yy_stub.yy").read_text().count("\n")

# Task 8.1's six lexer-generation artefacts. state_maps.h joins the list only
# when it exists: --compile-state-maps (passed by lexer_stage() iff
# $compile=1) is the one thing --no-compile skips, so a --no-compile run (the
# drift drill) leaves it absent -- handled here, not crashed on.
LEXER_ARTEFACT_NAMES = [
    "lexer_extracted.cc", "keywords.h", "lexstates.h",
    "state_maps_gen.cc", "lexslice.json",
]
if (out / "state_maps.h").is_file():
    LEXER_ARTEFACT_NAMES.append("state_maps.h")

artefacts = {}
for name in (
    "yy_mariadb.yy",
    "yy_oracle.yy",
    "yy_stub.yy",
    "stub-map.json",
    "grammar.xml",
    "grammar.output",
    "parser.c",
    "parser.h",
    "rule-map.json",
    *LEXER_ARTEFACT_NAMES,
):
    path = out / name
    artefacts[common.display_path(path)] = {
        "sha256": common.sha256_file(path),
        "size": path.stat().st_size,
    }

record = {
    "host": common.record_host(),
    "utc": common.utc_now(),
    "git_sha": os.environ.get("GIT_SHA", "uncommitted"),
    "variant": os.environ["MDBP_VARIANT"],
    "source_tree": common.display_path(os.environ["MDBP_SOURCE_TREE"]),
    "out": common.display_path(out),
    "yy_variant_lines": int(os.environ["MDBP_YY_VARIANT_LINES"]),
    "yy_stub_lines": stub_lines,
    "provenance_checked": os.environ["MDBP_PROVENANCE_CHECKED"] == "true",
    "bison_version": os.environ["MDBP_BISON_VERSION"],
    # The `--report=` flags THESE hashes were produced with. See
    # BISON_REPORT in extract.sh and evidence/P1/bison-report-probe.json.
    "bison_report_flags": os.environ["MDBP_BISON_REPORT"],
    # The `-D` definitions THESE hashes were produced with. See BISON_DEFINES
    # in extract.sh: `-Dparse.lac=full` is what puts yy_lac() in
    # build/parser.c, which src/parser_glue.c calls to answer valid(T).
    "bison_defines": os.environ["MDBP_BISON_DEFINES"],
    "bison_exit": int(os.environ["MDBP_BISON_EXIT"]),
    "bison_stderr_bytes": int(os.environ["MDBP_BISON_STDERR_BYTES"]),
    "stub_map_counts": stub_map["counts"],
    # Task P1.1.  build/ never leaves the test host, so this is the only way a reviewer
    # sees what tools/stub.py's named preserve rules did: the counts, the
    # sql_yacc.yy line of every statement lifted, and every parsing_place site
    # NOT lifted with the reason.  The full manifest (each statement's text
    # and its serial) stays in build/stub-map.json's `preserve` block.
    "preserve": {
        "rule": stub_map["preserve"]["rule"],
        "counts": stub_map["preserve"]["counts"],
        "macro_lines": stub_map["preserve"]["macro_lines"],
        "comment_only_lines": stub_map["preserve"]["comment_only_lines"],
        "lifted_lines": [a["yy_line"] for a in stub_map["preserve"]["applied"]],
        "skipped": stub_map["preserve"]["skipped"],
    },
    "rule_map": {
        "serials_mapped": len(rule_map["serial_to_rule"]),
        "unmapped_serials": len(rule_map["unmapped_serials"]),
    },
    "override_entries": stub.count_override_entries(common.REPO_ROOT / "overrides"),
    "artefacts": artefacts,
}

# The run record goes to evidence/P1/extract.json ONLY for the default
# --out build. Any other --out is a scratch run (task 2.4's drift drill, a
# regeneration probe) and writes its record next to its own artefacts, so it
# cannot clobber the phase's evidence -- which is exactly what used to
# happen, and what tests/test_drift_drill.py had to work around.
if out.resolve() == (common.REPO_ROOT / "build").resolve():
    dest = common.REPO_ROOT / "evidence" / "P1" / "extract.json"
else:
    dest = out / "extract.json"
common.write_json(dest, record)
print(f"wrote {dest}")

# evidence/P5b/lexgen.json: build/ never leaves the test host (excluded from both
# directions of <remote-runner>'s sync), so this is the only way a reviewer sees
# what tools/lexslice.py generated. Its `artefacts` sub-map is exactly
# the six entries above, keyed the same way as evidence/P1/extract.json's
# own `artefacts`, on purpose: tests/test_extract.py checks the two agree,
# so they cannot drift apart.
lexslice_manifest = json.loads((out / "lexslice.json").read_text())
state_maps_manifest = lexslice_manifest.get("state_maps") or {}
lexgen_record = {
    "phase": "P5b",
    "host": common.record_host(),
    "utc": common.utc_now(),
    "git_sha": os.environ.get("GIT_SHA", "uncommitted"),
    "source_tree": common.display_path(os.environ["MDBP_SOURCE_TREE"]),
    "artefacts": {
        common.display_path(out / name): artefacts[common.display_path(out / name)]
        for name in LEXER_ARTEFACT_NAMES
    },
    "symbols": (
        lexslice_manifest["types"] + lexslice_manifest["functions"]
        + lexslice_manifest["charset"]["symbols"]
    ),
    "rewrites": lexslice_manifest["rewrites"],
    "unknown_tokens": lexslice_manifest["keywords"]["unknown_tokens"],
    "declared_not_sliced": lexslice_manifest["declared_not_sliced"],
    "state_maps": {
        "bytes": state_maps_manifest.get("bytes"),
        "entries": state_maps_manifest.get("entries"),
        "compiled": bool(state_maps_manifest.get("compiles")),
        "ran": bool(state_maps_manifest.get("ran")),
    },
}
if out.resolve() == (common.REPO_ROOT / "build").resolve():
    lexgen_dest = common.REPO_ROOT / "evidence" / "P5b" / "lexgen.json"
else:
    lexgen_dest = out / "lexgen.json"
common.write_json(lexgen_dest, lexgen_record)
print(f"wrote {lexgen_dest}")
PY
rc=$?
if [ $rc -ne 0 ]; then
    echo "extract.sh: writing the run record failed (rc=$rc)" >&2
    exit 1
fi

if [ "$compile" = 1 ]; then
    compile_stage
    rc=$?
    if [ $rc -ne 0 ]; then
        exit $rc
    fi
fi

echo "extract.sh: ok ($variant, $yy_variant_lines lines, bison $bison_version, 0 stderr bytes)"
exit 0
