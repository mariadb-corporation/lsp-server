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

"""The language servers the benchmark compares: pinned versions, where
install.py gets them, how run.py starts and configures them.

Every server runs without a database connection.  `.servers/` (not in git)
holds the installed ones; MariaDB LSP is this checkout's build
($MARIADB_LSP, else build/bin/mariadb-lsp)."""

import os
import pathlib
import platform

HERE = pathlib.Path(__file__).resolve().parent
SERVERS_DIR = HERE / ".servers"
ROOT = HERE.parent


def _platform():
    """('osx' | 'linux', 'arm64' | 'x64')."""
    system = {"Darwin": "osx", "Linux": "linux"}.get(platform.system())
    machine = {"arm64": "arm64", "aarch64": "arm64", "x86_64": "x64", "AMD64": "x64"}.get(platform.machine())
    if system is None or machine is None:
        raise SystemExit(f"unsupported platform {platform.system()} {platform.machine()}")
    return system, machine


def mariadb_lsp():
    return os.environ.get("MARIADB_LSP") or str(ROOT / "build" / "bin" / "mariadb-lsp")


STS_VERSION = "6.0.20261023.1"
SQL_LANGUAGE_SERVER_VERSION = "1.7.1"
SQLS_VERSION = "0.2.48"
PGLS_VERSION = "0.28.2"
SQRUFF_VERSION = "0.43.0"
SQLFLUFF_VERSION = "4.4.0"  # the command the sqlfluff VS Code extension runs (vscode/run.js)
SQUAWK_VERSION = "2.70.0"


def downloads():
    """name -> (url, kind): what install.py fetches for this platform."""
    system, machine = _platform()
    sts_rid = f"{system}-{machine}"
    pgls_target = {("osx", "arm64"): "aarch64-apple-darwin", ("osx", "x64"): "x86_64-apple-darwin",
                   ("linux", "arm64"): "aarch64-unknown-linux-gnu",
                   ("linux", "x64"): "x86_64-unknown-linux-gnu"}[(system, machine)]
    return {
        "sqltoolsservice": (
            f"https://github.com/microsoft/sqltoolsservice/releases/download/{STS_VERSION}/"
            f"Microsoft.SqlTools.ServiceLayer-{sts_rid}-net10.0.tar.gz", "tar.gz"),
        "sqls": (
            f"https://github.com/sqls-server/sqls/releases/download/v{SQLS_VERSION}/"
            f"sqls-{'darwin' if system == 'osx' else 'linux'}-{SQLS_VERSION}.zip", "zip"),
        "postgres-language-server": (
            f"https://github.com/supabase-community/postgres-language-server/releases/download/"
            f"{PGLS_VERSION}/postgres-language-server_{pgls_target}", "binary"),
        "sql-language-server": (f"sql-language-server@{SQL_LANGUAGE_SERVER_VERSION}", "npm"),
        "sqruff": (
            f"https://github.com/quarylabs/sqruff/releases/download/v{SQRUFF_VERSION}/sqruff-"
            + {("osx", "arm64"): "darwin-aarch64", ("osx", "x64"): "darwin-x86_64",
               ("linux", "arm64"): "linux-aarch64-musl", ("linux", "x64"): "linux-x86_64-musl"}[(system, machine)]
            + ".tar.gz", "tar.gz"),
        "squawk": (
            f"https://github.com/sbdchd/squawk/releases/download/v{SQUAWK_VERSION}/squawk-"
            f"{'darwin' if system == 'osx' else 'linux'}-{machine}", "binary"),
        "sqlfluff": (f"sqlfluff=={SQLFLUFF_VERSION}", "pip"),
    }


# Each server: `label` for tables and slides, `version`, `cmd` (run in a
# fresh workspace directory), `language_id`, `init_options`, `settings`
# (sent as workspace/didChangeConfiguration after initialize),
# `workspace_files` (written into the workspace first), `after_open`
# (notifications sent after didOpen), `diagnostics` ("push"; "pull" for
# textDocument/diagnostic; "lint" for lints without syntax errors; None
# when the server reports none without a database), `max_size` (the
# largest workload size it is run at, for a server that cannot cope with
# more, and `max_size_reason`, why), `stop` (run after the server exits,
# for a server that leaves a daemon behind) and
# `process_match` (a substring of the command lines of every process that
# belongs to the server, for its memory).
SERVERS = {
    "mariadb-lsp": {
        "label": "MariaDB LSP",
        "version": None,  # mariadb-lsp --version
        "cmd": lambda: [mariadb_lsp(), "lsp"],
        # The check alone; as shipped it first waits debounceMs (150) for typing to pause.
        "init_options": {"debounceMs": 0},
        "diagnostics": "push",
    },
    "sqltoolsservice": {
        "label": "SQL Tools Service (mssql)",
        "version": STS_VERSION,
        "cmd": lambda: [str(SERVERS_DIR / "sqltoolsservice" / "MicrosoftSqlToolsServiceLayer")],
        "settings": {"mssql": {"intelliSense": {"enableIntellisense": True, "enableErrorChecking": True,
                                                "enableSuggestions": True, "enableQuickInfo": True}}},
        "after_open": lambda uri: [("connection/languageflavorchanged",
                                    {"uri": uri, "language": "sql", "flavor": "MSSQL"})],
        "diagnostics": "push",
    },
    "sql-language-server": {
        "label": "sql-language-server",
        "version": SQL_LANGUAGE_SERVER_VERSION,
        "cmd": lambda: ["node", str(SERVERS_DIR / "sql-language-server" / "node_modules" /
                                    "sql-language-server" / "npm_bin" / "cli.js"),
                        "up", "--method", "stdio"],
        "diagnostics": "push",
    },
    "sqls": {
        "label": "sqls",
        "version": SQLS_VERSION,
        "cmd": lambda: [str(SERVERS_DIR / "sqls" / "sqls")],
        "diagnostics": None,
        "max_size": "100K",
        "max_size_reason": "at 1 MB it answers neither completion nor formatting, "
                           "and at 10 MB it was still busy after 35 minutes",
    },
    "postgres-language-server": {
        "label": "postgres-language-server",
        "version": PGLS_VERSION,
        "cmd": lambda: [str(SERVERS_DIR / "postgres-language-server" / "postgres-language-server"),
                        "lsp-proxy"],
        # Its formatter is off by default; completion needs a database.
        "workspace_files": {"postgres-language-server.jsonc":
                            '{ "db": { "disableConnection": true }, "format": { "enabled": true } }\n'},
        "diagnostics": "push",
        # lsp-proxy forwards to a daemon it starts; both count, and the
        # daemon is stopped after each run.
        "process_match": str(SERVERS_DIR / "postgres-language-server" / "postgres-language-server"),
        "stop": lambda: [str(SERVERS_DIR / "postgres-language-server" / "postgres-language-server"), "stop"],
    },
    "sqruff": {
        "label": "sqruff",
        "version": SQRUFF_VERSION,
        "cmd": lambda: [str(SERVERS_DIR / "sqruff" / "sqruff"), "lsp"],
        # Its default rules, in the ANSI dialect (the workload is plain SQL):
        # it lints every statement besides parsing it.
        "workspace_files": {".sqruff": "[sqruff]\ndialect = ansi\n"},
        "max_size": "100K",
        "max_size_reason": "from 1 MB on it stops with one warning, "
                           "\"Maximum parse node count exceeded (limit 100000)\"",
        # Style lints only: it reports no syntax errors, so "open" is the
        # time to its first lint results, and the edits are not measured.
        "diagnostics": "lint",
    },
    "squawk": {
        "label": "squawk",
        "version": SQUAWK_VERSION,
        "cmd": lambda: [str(SERVERS_DIR / "squawk" / "squawk"), "server"],
        # Pull diagnostics (textDocument/diagnostic), asked for after each change.
        "diagnostics": "pull",
    },
}
