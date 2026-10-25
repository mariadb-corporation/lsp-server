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

"""Installs the competing language servers, at the versions servers.py
pins, into benchmarks/.servers/ (not in git).  Needs curl, tar, unzip and,
for sql-language-server, npm; sqlfluff (for the sqlfluff VS Code extension
of vscode/run.js) goes into a Python environment of its own.

usage: install.py [NAME ...]   (default: all of them)"""

import json
import shutil
import subprocess
import sys

from servers import SERVERS_DIR, downloads


def run(*cmd, cwd=None):
    print("+ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=cwd)


def install(name, url, kind):
    target = SERVERS_DIR / name
    shutil.rmtree(target, ignore_errors=True)
    target.mkdir(parents=True)
    if kind == "npm":
        # sql-language-server 1.7.1 pins vscode-languageserver 8.0.0-next.8,
        # which wants protocol 3.17.0-next.14; npm would otherwise resolve
        # the protocol to a later release.
        (target / "package.json").write_text(json.dumps({
            "private": True,
            "dependencies": {url.split("@")[0]: url.split("@")[1]},
            "overrides": {"vscode-languageserver-protocol": "3.17.0-next.14"},
        }, indent=2) + "\n")
        run("npm", "install", "--no-audit", "--no-fund", cwd=target)
        return
    if kind == "pip":
        # A Python environment of its own: .servers/<name>/bin/<name>.
        run(sys.executable, "-m", "venv", str(target))
        run(str(target / "bin" / "pip"), "install", "--quiet", url)
        return
    archive = target / url.rsplit("/", 1)[1]
    run("curl", "-fsSL", "-o", str(archive), url)
    if kind == "tar.gz":
        run("tar", "-xzf", str(archive), "-C", str(target))
        archive.unlink()
    elif kind == "zip":
        run("unzip", "-q", "-o", str(archive), "-d", str(target))
        archive.unlink()
        # The zip holds the binary, possibly in a folder of its own.
        found = next(p for p in target.rglob("sqls*") if p.is_file() and p.suffix != ".zip")
        if found != target / name:
            found.rename(target / name)
        (target / name).chmod(0o755)
    else:
        archive.rename(target / name)
        (target / name).chmod(0o755)


def main():
    all_downloads = downloads()
    wanted = sys.argv[1:] or list(all_downloads)
    unknown = [name for name in wanted if name not in all_downloads]
    if unknown:
        sys.exit(f"unknown server {', '.join(unknown)}; known: {', '.join(all_downloads)}")
    for name in wanted:
        install(name, *all_downloads[name])
    print(f"installed into {SERVERS_DIR}")


if __name__ == "__main__":
    main()
