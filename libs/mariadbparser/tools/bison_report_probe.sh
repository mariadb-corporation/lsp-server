#!/usr/bin/env bash
# tools/bison_report_probe.sh — the measurement behind extract.sh's one
# deviation from the phase spec.
#
# P1's deliverable 3 pins `bison ... --report=all`; extract.sh ships
# `--report=$BISON_REPORT` (= all minus `counterexamples`). This script is why,
# and it is re-runnable rather than a comment. It measures two separate things
# and keeps them separate in evidence/P1/bison-report-probe.json:
#
#   1. `runs` / `output_comparison` — both flag sets over the SAME
#      build/yy_stub.yy the pipeline produced. This is what says whether the
#      pinned flag is usable on MariaDB's grammar at all; it is not expected to
#      yield a full output comparison, because a run that dies writes nothing
#      to compare, and the JSON then records "not-comparable" for what it did
#      not get.
#   2. `control` — both flag sets over tools/report_probe_control.yy, a
#      small deliberately-ambiguous grammar on which both DO finish. This is
#      the only place a real grammar.xml/parser.c/parser.h comparison can come
#      from, and it is what backs extract.sh's claim that `--report` selects
#      the content of the .output report file and nothing else.
#
# Both runs of a pair write into the SAME scratch path ($probe/run), one after
# the other, and their outputs are moved aside (never rewritten) once each run
# is done. That matters: bison embeds the -o path in parser.c and, uppercased,
# in parser.h's include guard, so runs writing to two differently-named
# directories could never produce equal hashes no matter what the flags did.
# Only hashes of files produced at one and the same output path are compared
# here. For the same reason, a hash in this file is comparable with the
# pipeline's own only for outputs that carry no path — grammar.xml does not,
# parser.c/parser.h do.
#
# Neither pair touches build/'s own artefacts. Each run's wall time, exit code,
# stderr and output hashes are recorded either way.
#
#   tools/bison_report_probe.sh [--stub <yy>] [--timeout <seconds>]
#                                    [--out <evidence.json>]
#
# `--timeout` (default 900) is applied per run with `timeout -s KILL`. A run
# that is killed is a result, not a failure: this script exits 0 whenever all
# runs were attempted and the JSON was written. `hit_probe_timeout` in the
# JSON says whether the kill came from this script's own limit or from
# outside it (the kernel), which is the difference between "slow" and
# "cannot run here" and must not be blurred.
#
# Exit codes: 0 ok · 1 internal error · 2 usage/no stub grammar.
set -uo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)

# The build tree: $MDBP_BUILD_DIR (the CMake build's extract.sh --out), else
# build/ in this directory.
build_dir=${MDBP_BUILD_DIR:-build}
stub=$build_dir/yy_stub.yy
limit=900
evidence=evidence/P1/bison-report-probe.json
control=tools/report_probe_control.yy
reduced_flags=states,itemsets,lookaheads,solved

while [ $# -gt 0 ]; do
    case "$1" in
        --stub) stub=${2:?--stub needs a value}; shift 2 ;;
        --timeout) limit=${2:?--timeout needs a value}; shift 2 ;;
        --out) evidence=${2:?--out needs a value}; shift 2 ;;
        -h | --help) sed -n '2,45p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *) echo "bison_report_probe.sh: unknown argument '$1'" >&2; exit 2 ;;
    esac
done

cd "$root" || exit 2

if [ ! -f "$stub" ]; then
    echo "bison_report_probe.sh: no $stub (run \`make grammar\` first)" >&2
    exit 2
fi
if [ ! -f "$control" ]; then
    echo "bison_report_probe.sh: no $control" >&2
    exit 2
fi

py() { uv run --frozen -- python "$@"; }

bison_version=$(bison --version | head -n 1 | grep -oE '[0-9]+\.[0-9]+(\.[0-9]+)*' | tail -n 1)

# Scratch lives under build/ (gitignored, and excluded from both directions of
# <remote-runner>'s sync) so a 50-MB-per-run report never lands in /tmp or in the
# tree. The trap removes it on any exit path.
mkdir -p "$build_dir" || exit 1
probe=$(mktemp -d "$build_dir/.report-probe.XXXXXX") || exit 1
trap 'rm -rf "$probe"' EXIT

# The one output path every run writes to, and the archive the finished runs
# are moved into.
shared=$probe/run
mkdir -p "$probe/keep" || exit 1

: > "$probe/results.tsv"

# run_one <pair> <name> <flags> <grammar>
run_one() {
    local pair=$1 name=$2 flags=$3 grammar=$4 started ended rc seconds
    rm -rf "$shared" || return 1
    mkdir -p "$shared" || return 1
    started=$(date +%s.%N)
    timeout -s KILL "$limit" \
        bison -d --xml="$shared/grammar.xml" \
              --report="$flags" \
              --report-file="$shared/grammar.output" \
              -o "$shared/parser.c" "$grammar" 2> "$shared/stderr"
    rc=$?
    ended=$(date +%s.%N)
    seconds=$(awk -v a="$started" -v b="$ended" 'BEGIN { printf "%.1f", b - a }')
    # Moving does not touch content: what is hashed later is byte for byte
    # what bison wrote at $shared.
    mv "$shared" "$probe/keep/$pair-$name" || return 1
    printf '%s\t%s\t%s\t%s\t%s\n' "$pair" "$name" "$flags" "$rc" "$seconds" \
        >> "$probe/results.tsv"
    echo "bison_report_probe.sh: [$pair] --report=$flags -> exit $rc in ${seconds}s"
}

# `all` is the phase spec's flag; `reduced` is what extract.sh actually uses.
# The stub pair runs first: it is the expensive one and the one the deviation
# is about.
run_one stub all "all" "$stub" || exit 1
run_one stub reduced "$reduced_flags" "$stub" || exit 1
run_one control all "all" "$control" || exit 1
run_one control reduced "$reduced_flags" "$control" || exit 1

MDBP_PROBE=$probe MDBP_STUB=$stub MDBP_LIMIT=$limit MDBP_EVIDENCE=$evidence \
MDBP_BISON_VERSION=$bison_version MDBP_CONTROL=$control MDBP_SHARED=$shared \
py - <<'PY'
import os
import socket
import sys
from pathlib import Path

sys.path.insert(0, "tools")
import common

probe = Path(os.environ["MDBP_PROBE"])
limit = int(os.environ["MDBP_LIMIT"])
shared = os.environ["MDBP_SHARED"]
OUTPUTS = ("grammar.xml", "grammar.output", "parser.c", "parser.h")

pairs: dict[str, dict[str, dict]] = {}
for line in (probe / "results.tsv").read_text().splitlines():
    pair, name, flags, rc, seconds = line.split("\t")
    rc = int(rc)
    d = probe / "keep" / f"{pair}-{name}"
    stderr = (d / "stderr").read_bytes()
    entry = {
        "report_flags": flags,
        "exit": rc,
        "completed": rc == 0,
        # 137 is 128+SIGKILL. This script's `timeout -s KILL` sends it at the
        # limit -- but so does the kernel, at a moment of its choosing, and
        # the two mean different things. The clock separates them.
        "sigkilled": rc == 137,
        "hit_probe_timeout": rc == 137 and float(seconds) >= limit,
        "seconds": float(seconds),
        "stderr_bytes": len(stderr),
        "stderr_head": common.display_text(stderr[:400].decode("utf-8", "replace")),
        # Recorded per run, and equal across runs by construction: bison
        # embeds this path in parser.c/parser.h, so hashes are only
        # comparable because both runs wrote here.
        "output_dir": common.display_path(shared),
        "outputs": {},
    }
    for name_ in OUTPUTS:
        path = d / name_
        if path.exists():
            entry["outputs"][name_] = {
                "sha256": common.sha256_file(path),
                "size": path.stat().st_size,
            }
    pairs.setdefault(pair, {})[name] = entry


def compare(runs: dict[str, dict]) -> dict[str, str]:
    """Per output: identical / differs / not-comparable. Only computable when
    both runs produced the file, which a killed run does not; recorded as
    measured either way, never inferred."""
    out = {}
    for name_ in OUTPUTS:
        a = runs["all"]["outputs"].get(name_)
        b = runs["reduced"]["outputs"].get(name_)
        if a is None or b is None:
            out[name_] = "not-comparable"
        else:
            out[name_] = "identical" if a["sha256"] == b["sha256"] else "differs"
    return out


stub_cmp = compare(pairs["stub"])
control_cmp = compare(pairs["control"])

control = {
    "grammar": common.display_path(os.environ["MDBP_CONTROL"]),
    "grammar_sha256": common.sha256_file(Path(os.environ["MDBP_CONTROL"])),
    "runs": pairs["control"],
    "output_comparison": control_cmp,
    # A control whose grammar.output did NOT change would prove nothing: it
    # would mean `counterexamples` had nothing to say here, and the identical
    # hashes elsewhere would be identical for want of any difference at all.
    "informative": control_cmp["grammar.output"] == "differs",
}

record = {
    "host": common.record_host(),
    "utc": common.utc_now(),
    "git_sha": os.environ.get("GIT_SHA", "uncommitted"),
    "stub": common.display_path(os.environ["MDBP_STUB"]),
    "stub_sha256": common.sha256_file(Path(os.environ["MDBP_STUB"])),
    "bison_version": os.environ["MDBP_BISON_VERSION"],
    "timeout_seconds": limit,
    "runs": pairs["stub"],
    "output_comparison": stub_cmp,
    "control": control,
}
dest = common.REPO_ROOT / os.environ["MDBP_EVIDENCE"]
common.write_json(dest, record)
print(f"wrote {dest}")
PY
rc=$?
if [ $rc -ne 0 ]; then
    echo "bison_report_probe.sh: writing $evidence failed (rc=$rc)" >&2
    exit 1
fi
exit 0
