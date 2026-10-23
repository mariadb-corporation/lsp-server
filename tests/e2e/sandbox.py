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

"""A throwaway MariaDB server for the live-connection tests, deployed and
queried through MariaDB Shell (`sandbox deploy`, `--sql -e`): the mariadbd
named by MDBLS_TEST_MARIADBD (a binary or an installation directory) or on
the PATH, a free TCP port on 127.0.0.1, a temporary directory.

And a private MariaDB Shell secret store: the shell's login-path helper,
writing a login file of its own (MYSQL_TEST_LOGIN_FILE), never
~/.mylogin.cnf.

The shell is the one in MDBLS_SHELL_BIN_DIR, else the mariadb-shell on the
PATH."""

import json
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile

HELPER = "login-path"
ROOT_PASSWORD = "sandbox-root"


def program(directory, name):
    """`name` in `directory` (with .exe on Windows), None when it is not there."""
    path = pathlib.Path(directory) / (name + (".exe" if os.name == "nt" else ""))
    return str(path) if path.is_file() else None


def shell_bin_dir():
    """The directory of MariaDB Shell's binaries, None without the shell or
    its login-path helper."""
    d = os.environ.get("MDBLS_SHELL_BIN_DIR")
    if not d or not program(d, "mariadb-shell"):
        found = shutil.which("mariadb-shell")
        if not found:
            return None
        d = str(pathlib.Path(found).resolve().parent)
    return d if program(d, "mariadb-secret-store-" + HELPER) else None


def mariadbd():
    return os.environ.get("MDBLS_TEST_MARIADBD") or shutil.which("mariadbd")


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Sandbox:
    """A running server; root logs in over TCP with ROOT_PASSWORD."""

    def __init__(self, sql_mode="", setup_sql=""):
        server, bindir = mariadbd(), shell_bin_dir()
        if server is None or bindir is None:
            raise RuntimeError("no mariadbd or no MariaDB Shell")
        self.shell = program(bindir, "mariadb-shell")
        # A short path: a socket path must fit in 104 bytes on macOS.
        self.dir = tempfile.mkdtemp(prefix="mdbls-", dir="/tmp" if os.name != "nt" else None)
        # The shell's own files go there too, and it saves no password.
        config = os.path.join(self.dir, "shell")
        os.mkdir(config)
        self.env = dict(os.environ, MARIADB_SHELL_USER_CONFIG_HOME=config,
                        MARIADB_SHELL_CREDENTIAL_STORE_HELPER="<disabled>")
        self.port = free_port()
        options = ["bind-address=127.0.0.1", "skip-log-bin"]
        if sql_mode:
            options.append("sql_mode=" + sql_mode)
        try:
            # As JSON: a plain list would be split at the commas of sql_mode.
            self.run("--", "sandbox", "deploy", str(self.port), "--sandboxDir=" + self.dir,
                     "--mariadbdPath=" + server, "--password=" + ROOT_PASSWORD, "--ssl=false",
                     "--allowRootFrom=", "--mariadbdOptions:json=" + json.dumps(options))
        except RuntimeError:
            shutil.rmtree(self.dir, ignore_errors=True)
            raise
        if setup_sql:
            try:
                self.sql(setup_sql)
            except RuntimeError:
                # setUpClass failed, so tearDownClass will not stop it.
                self.stop()
                raise

    def run(self, *args):
        """The shell's output; its error output in the exception when it fails."""
        r = subprocess.run([self.shell, *args], capture_output=True, text=True, env=self.env)
        if r.returncode != 0:
            raise RuntimeError("mariadb-shell %s: %s" % (" ".join(args[:4]), r.stderr.strip()))
        return r.stdout

    def sql(self, statements, *options):
        return self.run("--sql", "--uri", "root@127.0.0.1:%d" % self.port,
                        "--password=" + ROOT_PASSWORD, *options, "-e", statements)

    def query(self, statement):
        """The last column of the first row, as text."""
        for line in self.sql(statement, "--json=raw").splitlines():
            rows = json.loads(line).get("rows")
            if rows:
                return str(list(rows[0].values())[-1])
        return ""

    def stop(self):
        try:
            self.run("--", "sandbox", "kill", str(self.port), "--sandboxDir=" + self.dir)
        except RuntimeError:
            pass
        shutil.rmtree(self.dir, ignore_errors=True)


class Secret_store:
    """A private secret store; sets the environment the server and the CLI
    inherit."""

    def __init__(self):
        self.bin = shell_bin_dir()
        if self.bin is None:
            raise RuntimeError("no MariaDB Shell with its login-path helper")
        self.dir = tempfile.mkdtemp(prefix="mdbls-secrets-")
        values = {"MARIADB_SHELL_USER_CONFIG_HOME": self.dir,
                  "MARIADB_SHELL_CREDENTIAL_STORE_HELPER": HELPER,
                  "MYSQL_TEST_LOGIN_FILE": os.path.join(self.dir, "login.cnf")}
        self.saved = {k: os.environ.get(k) for k in values}
        os.environ.update(values)

    def store(self, secret_type, secret_id, secret):
        subprocess.run([program(self.bin, "mariadb-secret-store-" + HELPER), "store"],
                       input=json.dumps({"SecretType": secret_type, "SecretID": secret_id,
                                         "Secret": secret}).encode(), check=True)

    def close(self):
        for k, v in self.saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        shutil.rmtree(self.dir, ignore_errors=True)
