"""Shared helpers for every tools/*.py script.

Usage from a sibling script::

    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import common
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import socket
import sys
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

# The repo root, derived from this file's location: tools/common.py is one
# directory below the root (libs/mariadbparser in the lsp-server tree).
REPO_ROOT: Path = Path(__file__).resolve().parent.parent


def _abs(value: str | os.PathLike) -> Path:
    """An absolute, normalised path WITHOUT resolving symlinks (a symlink's
    own name, e.g. libmariadbparser.so, must survive into a record)."""
    return Path(os.path.abspath(os.path.expanduser(str(value))))


# --- where the build and the unpacked corpus live ---------------------------
# MDBP_BUILD_DIR is extract.sh's --out: every generated source, object,
# library, argv file and build record. MDBP_CORPUS_DIR is the unpacked corpus
# data in the old corpus/ layout (expected/, fixtures/, grammar-model.json,
# ...). Both default to their old places inside this directory; the CMake
# build sets them to <binaryDir>/libs/mariadbparser/{out,corpus}. Tools accept
# either inside or outside REPO_ROOT.
BUILD_DIR: Path = _abs(os.environ["MDBP_BUILD_DIR"]) \
    if os.environ.get("MDBP_BUILD_DIR") else REPO_ROOT / "build"
CORPUS_DIR: Path = _abs(os.environ["MDBP_CORPUS_DIR"]) \
    if os.environ.get("MDBP_CORPUS_DIR") else REPO_ROOT / "corpus"

# The two corpus/ files that are tracked in this directory rather than
# unpacked into CORPUS_DIR, and the committed archives the data comes from.
TRACKED_CORPUS_FILES = ("PROVENANCE.json", "PIN-VERIFY.json")
TRACKED_CORPUS_DIRS = ("archive",)


def corpus_path(rel: str | os.PathLike) -> Path:
    """Where the corpus path `rel` lives on disk.

    `rel` is a corpus path as the records spell it, `corpus/expected/job.jsonl`,
    or the same path without the leading `corpus/`. Corpus data maps into
    CORPUS_DIR; the tracked `corpus/PROVENANCE.json`, `corpus/PIN-VERIFY.json`
    and `corpus/archive/` stay at REPO_ROOT/"corpus".
    """
    parts = PurePosixPath(Path(rel).as_posix()).parts
    if parts and parts[0] == "corpus":
        parts = parts[1:]
    if (len(parts) == 1 and parts[0] in TRACKED_CORPUS_FILES) or (
            parts and parts[0] in TRACKED_CORPUS_DIRS):
        return REPO_ROOT.joinpath("corpus", *parts)
    return CORPUS_DIR.joinpath(*parts)


# --- other checkouts: placeholders, resolved only from the environment -------
# Tracked files name another checkout only by its placeholder. The path behind
# it comes from one environment variable; a tool that needs it while the
# variable is unset exits 2 naming the variable. There is no default path.
PLACEHOLDERS: dict[str, str] = {
    "<mariadb-source>": "MARIADB_SOURCE_DIR",
    "<grammar-corpus>": "MDBP_GRAMMAR_CORPUS_DIR",
    "<mdb-bison-ts>": "MDBP_BISON_TS_DIR",
    "<mdb-bison-ts-corpus>": "MDBP_BISON_TS_CORPUS_DIR",
}

# Record-only placeholders: they stand in for a path that no longer exists
# (`<scratch>`: a scratch directory a content-pinned file was captured from
# once) and have no environment variable, because there is nothing to point
# one at. A record may carry them as provenance; nothing resolves them.
RECORD_ONLY_PLACEHOLDERS: tuple[str, ...] = ("<scratch>",)


def env_dir(name: str) -> Path:
    """The directory environment variable `name` names; exit 2 with
    `<name> is not set` when it is unset or empty."""
    value = os.environ.get(name)
    if not value:
        print(f"{name} is not set", file=sys.stderr)
        raise SystemExit(2)
    return _abs(value)


def mariadb_source_dir() -> Path:
    """`$MARIADB_SOURCE_DIR`, the MariaDB source tree (`<mariadb-source>`)."""
    return env_dir("MARIADB_SOURCE_DIR")


def resolve_placeholder(p: str | os.PathLike) -> Path:
    """`<placeholder>/rest` -> `$VAR/rest`; any other path unchanged.

    Exits 2 naming the variable when the placeholder's variable is unset,
    and exits 2 for a record-only placeholder (`<scratch>/...`), which no
    variable resolves: returning it unchanged would hand back a path
    relative to the working directory.
    """
    text = str(p)
    for placeholder in RECORD_ONLY_PLACEHOLDERS:
        if text == placeholder or text.startswith(placeholder + "/"):
            print(f"{placeholder} is a record-only placeholder (a scratch path "
                  "that no longer exists); no environment variable resolves "
                  "it", file=sys.stderr)
            raise SystemExit(2)
    for placeholder, var in PLACEHOLDERS.items():
        if text == placeholder:
            return env_dir(var)
        if text.startswith(placeholder + "/"):
            return env_dir(var) / text[len(placeholder) + 1:]
    return Path(text)


def _under(path: Path, root: Path) -> str | None:
    """`path` relative to `root` as a posix string ("" for root itself), or
    None when `path` is not under `root`."""
    try:
        rel = path.relative_to(root)
    except ValueError:
        return None
    return rel.as_posix() if rel.parts else ""


def display_path(p: str | os.PathLike) -> str:
    """The form in which a path is written into a record.

    Under MARIADB_SOURCE_DIR -> `<mariadb-source>/...`; under another set
    checkout variable -> its placeholder; under BUILD_DIR -> `build/...`;
    under CORPUS_DIR -> `corpus/...`; under REPO_ROOT -> relative to it;
    anything else (a relative path, a system path) unchanged. So a record
    never carries an absolute path of this machine, wherever the checkouts
    and the build tree live.
    """
    text = str(p)
    if not os.path.isabs(text):
        return Path(text).as_posix() if text else text
    candidates = [_abs(text)]
    resolved_parent = candidates[0].parent.resolve() / candidates[0].name
    if resolved_parent != candidates[0]:
        candidates.append(resolved_parent)
    roots: list[tuple[Path, str]] = []
    for placeholder, var in PLACEHOLDERS.items():
        value = os.environ.get(var)
        if value:
            roots.append((_abs(value), placeholder))
    roots.append((BUILD_DIR, "build"))
    roots.append((CORPUS_DIR, "corpus"))
    roots.append((REPO_ROOT, ""))
    for path in candidates:
        for root, label in roots:
            for root_form in dict.fromkeys((root, root.resolve())):
                rel = _under(path, root_form)
                if rel is None:
                    continue
                if not label:
                    return rel or "."
                return f"{label}/{rel}" if rel else label
    return text


def display_arg(arg: str) -> str:
    """One command-line argument as a record carries it: an absolute path,
    or an absolute path after `-I`, `-L`, `-o`, `=` or `,`, goes through
    display_path(); anything else is unchanged."""
    if os.path.isabs(arg):
        return display_path(arg)
    match = re.match(r"^(-I|-L|-o|.*[=,])(/.*)$", arg)
    if match:
        return match.group(1) + display_path(match.group(2))
    return arg


def display_argv_line(line: str) -> str:
    """A line of an argv file (one argument, or a whole space-separated
    command such as ar.argv's) with every path argument through
    display_arg()."""
    return " ".join(display_arg(part) for part in line.split(" "))


def display_argv(args) -> list[str]:
    """A command's arguments as a record carries them: display_arg() of
    each."""
    return [display_arg(str(a)) for a in args]


def display_text(text: str) -> str:
    """`text` (a compiler diagnostic, a message) with every absolute path
    under a display_path() root replaced the way display_path() spells it.
    Longer roots are replaced first, so a build tree inside REPO_ROOT still
    reads `build/...`."""
    roots: list[tuple[str, str]] = []
    for placeholder, var in PLACEHOLDERS.items():
        value = os.environ.get(var)
        if value:
            roots.append((str(_abs(value)), placeholder))
    roots.append((str(BUILD_DIR), "build"))
    roots.append((str(CORPUS_DIR), "corpus"))
    for root, label in list(roots):
        resolved = str(Path(root).resolve())
        if resolved != root:
            roots.append((resolved, label))
    for root in dict.fromkeys((str(REPO_ROOT), str(REPO_ROOT.resolve()))):
        roots.append((root, ""))
    for root, label in sorted(roots, key=lambda r: len(r[0]), reverse=True):
        if label:
            text = re.sub(re.escape(root) + r"(?![^/\s:'\"`])",
                          lambda _m, label=label: label, text)
        else:
            # Under REPO_ROOT: the relative path, as display_path() writes it.
            text = re.sub(re.escape(root) + r"/", "", text)
            text = re.sub(re.escape(root) + r"(?![^/\s:'\"`])", ".", text)
    return text


def record_host() -> str:
    """The host name a record carries: `$MDBP_RECORD_HOST` when set (the
    CMake build sets it from MDBLS_RECORD_HOST, `dev-host` by default), else
    socket.gethostname()."""
    return os.environ.get("MDBP_RECORD_HOST") or socket.gethostname()


def sha256_file(path: Path) -> str:
    """Return the hex sha256 digest of the file at `path`."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_bytes(data: bytes) -> str:
    """Return the hex sha256 digest of `data`.

    The in-memory twin of sha256_file(), for callers that already hold the
    bytes -- corpus_mirror.py reads a pinned git blob with `git cat-file`
    and must hash exactly those bytes, never a temporary file written from
    them.
    """
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, obj) -> None:
    """Write `obj` as JSON to `path`, stably formatted.

    Every JSON artefact in this repo is written through this function, so
    diffs are stable: sorted keys, 2-space indent, trailing newline, parent
    directories created as needed.
    """
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(obj, sort_keys=True, indent=2) + "\n"
    path.write_text(text)


def utc_now() -> str:
    """Return the current UTC time as ISO-8601 with a trailing Z, second resolution."""
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


# --- the library's version, in ONE place ------------------------------------
# include/mdbparse.h's MDBP_VERSION_MAJOR/MINOR/PATCH macros are the single source
# of truth for the version string, the pkg-config `Version:`, the shared
# object's soname and its real name.  tools/extract.sh has the shell copy
# of this rule (mdbp_header_version(), which the .pc and the -Wl,-soname come
# from); this is the Python one, for the scripts that read a BUILT tree rather
# than produce it.  Neither hand-types a version number, so PLAN.md P4.1's
# bump moves every one of those strings together instead of leaving a literal
# behind -- which is exactly the debt P4.1 deliverable 5 was opened to pay.

HEADER: Path = REPO_ROOT / "include" / "mdbparse.h"


def header_version(header: Path = HEADER) -> tuple[int, int, int]:
    """`(major, minor, patch)` from include/mdbparse.h's MDBP_VERSION_* macros.

    Raises ValueError when any of the three is missing: a caller that cannot
    read the version must fail loudly rather than fall back to a literal,
    since a wrong soname is a silently mis-measured artefact.
    """
    text = Path(header).read_text()
    parts = []
    for name in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(rf"^#define\s+MDBP_VERSION_{name}\s+(\d+)", text,
                          re.MULTILINE)
        if match is None:
            raise ValueError(f"MDBP_VERSION_{name} not found in {header}")
        parts.append(int(match.group(1)))
    return (parts[0], parts[1], parts[2])


def version_string(header: Path = HEADER) -> str:
    """"MAJOR.MINOR.PATCH" -- what mdbp_version_string() returns."""
    return "%d.%d.%d" % header_version(header)


def shared_soname(header: Path = HEADER) -> str:
    """The shared object's soname, e.g. "libmariadbparser.so.2"."""
    return f"libmariadbparser.so.{header_version(header)[0]}"


def shared_realname(header: Path = HEADER) -> str:
    """The shared object's real name: "libmariadbparser.so." followed by
    version_string()."""
    return f"libmariadbparser.so.{version_string(header)}"


# --- the export list, in ONE place ------------------------------------------
# Plan `tokeniser-entry-points.md` P1.1. The public functions are the ones
# include/mdbparse.h declares, and src/libmariadbparser.map is the linker's copy
# of that list, split into version nodes. Tests used to spell the six names
# and the node `MDBPARSER_2.0` as literals, which a seventh function or a
# second node would have left behind. These three readers are how a test
# asks the two files instead. They read text only and never need a build.

MAP_FILE: Path = REPO_ROOT / "src" / "libmariadbparser.map"

_C_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)


def header_functions(header: Path = HEADER) -> list[str]:
    """Every mdbp_* function include/mdbparse.h DECLARES, in declaration order.

    Comments are stripped first, so a name the prose mentions does not count.
    What is left is any mdbp_<word> directly followed by '(': the type names
    (mdbp_parser, mdbp_result, mdbp_options, mdbp_token, mdbp_status,
    mdbp_variant) never stand directly before '(' in this header. A name that
    appears twice is listed once, at its first place.

    Raises ValueError when the header declares no function at all: a caller
    that got an empty list would compare against nothing and pass.
    """
    text = _C_COMMENT.sub("", Path(header).read_text())
    names = re.findall(r"\b(mdbp_[A-Za-z0-9_]+)\s*\(", text)
    if not names:
        raise ValueError(f"no mdbp_* function declared in {header}")
    return list(dict.fromkeys(names))


def _map_text(map_path: Path) -> str:
    """The version script with its comments removed. GNU ld's version-script
    lexer takes both /* ... */ and # to the end of the line."""
    text = _C_COMMENT.sub("", Path(map_path).read_text())
    return re.sub(r"#[^\n]*", "", text)


_MAP_NODE = re.compile(r"([A-Za-z_][A-Za-z0-9_.]*)\s*\{(.*?)\}\s*[A-Za-z0-9_.]*\s*;",
                       re.DOTALL)


def map_exports(map_path: Path = MAP_FILE) -> dict[str, list[str]]:
    """Each version node of src/libmariadbparser.map, in file order, mapped to
    the names its `global:` part lists, in file order.

    A name before any `global:` or `local:` label is global, as it is for
    the linker. The `local:` part (`*;` here) is not an export and is left
    out. Raises ValueError when the file has no node.
    """
    nodes: dict[str, list[str]] = {}
    for match in _MAP_NODE.finditer(_map_text(map_path)):
        names: list[str] = []
        section = "global"
        for part in re.split(r"\b(global|local)\s*:", match.group(2)):
            if part in ("global", "local"):
                section = part
                continue
            if section != "global":
                continue
            names.extend(n.strip() for n in part.split(";") if n.strip())
        nodes[match.group(1)] = names
    if not nodes:
        raise ValueError(f"no version node in {map_path}")
    return nodes


def map_version_nodes(map_path: Path = MAP_FILE) -> list[str]:
    """The version nodes src/libmariadbparser.map declares, in file order.
    Raises ValueError when there is none."""
    return list(map_exports(map_path))
