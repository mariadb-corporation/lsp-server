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

"""A throwaway MariaDB server for the live-connection tests, deployed the
way MariaDB Shell's sandboxes are: the mariadbd on the PATH (or
MDBLS_TEST_MARIADBD), a fresh data directory, a free TCP port on 127.0.0.1.

And a private MariaDB Shell secret store: MARIADB_SHELL_USER_CONFIG_HOME
pointing at a temporary directory, written through the shell's
mariadb-secret-store-plaintext helper (MDBLS_SHELL_BIN_DIR, default
../mariadb-shell/build/bin)."""

import json
import os
import pathlib
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]


def shell_bin_dir():
    d = os.environ.get("MDBLS_SHELL_BIN_DIR") or str(ROOT.parent / "mariadb-shell" / "build" / "bin")
    return d if (pathlib.Path(d) / "mariadb-secret-store-plaintext").exists() else None


def mariadbd():
    path = os.environ.get("MDBLS_TEST_MARIADBD") or shutil.which("mariadbd")
    if not path:
        return None
    bindir = pathlib.Path(path).resolve().parent
    install = bindir / "mariadb-install-db"
    if not install.exists():
        install = pathlib.Path(shutil.which("mariadb-install-db") or "")
    client = bindir / "mariadb"
    if not client.exists():
        client = pathlib.Path(shutil.which("mariadb") or "")
    if not install.is_file() or not client.is_file():
        return None
    return str(pathlib.Path(path).resolve()), str(install), str(client)


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Sandbox:
    """A running server; root has no password and talks over the socket."""

    def __init__(self, sql_mode="", setup_sql=""):
        found = mariadbd()
        if found is None:
            raise RuntimeError("no mariadbd")
        self.mariadbd, install, self.client = found
        # A short path: a socket path must fit in 104 bytes on macOS.
        self.dir = tempfile.mkdtemp(prefix="mdbls-", dir="/tmp" if os.name != "nt" else None)
        self.socket = os.path.join(self.dir, "s.sock")
        self.port = free_port()
        data = os.path.join(self.dir, "data")
        subprocess.run([install, "--no-defaults", "--datadir=" + data,
                        "--auth-root-authentication-method=normal", "--skip-test-db"],
                       check=True, capture_output=True)
        args = [self.mariadbd, "--no-defaults", "--datadir=" + data, "--socket=" + self.socket,
                "--port=%d" % self.port, "--bind-address=127.0.0.1", "--skip-log-bin",
                "--pid-file=" + os.path.join(self.dir, "pid")]
        if sql_mode:
            args.append("--sql-mode=" + sql_mode)
        self.log = open(os.path.join(self.dir, "server.log"), "wb")
        self.proc = subprocess.Popen(args, stdout=self.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 30
        while True:
            r = subprocess.run([self.client, "--no-defaults", "-S", self.socket, "-uroot", "-e",
                                "SELECT 1"], capture_output=True)
            if r.returncode == 0:
                break
            if time.monotonic() > deadline or self.proc.poll() is not None:
                self.stop()
                raise RuntimeError("the sandbox server did not start")
            time.sleep(0.2)
        if setup_sql:
            self.sql(setup_sql)

    def sql(self, statements):
        subprocess.run([self.client, "--no-defaults", "-S", self.socket, "-uroot", "-e",
                        statements], check=True, capture_output=True)

    def query(self, statement):
        """The first column of the first row, as text."""
        r = subprocess.run([self.client, "--no-defaults", "-S", self.socket, "-uroot", "-N", "-B",
                            "-e", statement], check=True, capture_output=True, text=True)
        return r.stdout.split("\n")[0].split("\t")[-1]

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(30)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        self.log.close()
        shutil.rmtree(self.dir, ignore_errors=True)


class Secret_store:
    """A private plaintext secret store; sets the environment the server
    and the CLI inherit."""

    def __init__(self):
        self.bin = shell_bin_dir()
        if self.bin is None:
            raise RuntimeError("no mariadb-secret-store-plaintext")
        self.dir = tempfile.mkdtemp(prefix="mdbls-secrets-")
        self.saved = {k: os.environ.get(k) for k in ("MARIADB_SHELL_USER_CONFIG_HOME",
                                                     "MARIADB_SHELL_CREDENTIAL_STORE_HELPER")}
        os.environ["MARIADB_SHELL_USER_CONFIG_HOME"] = self.dir
        os.environ["MARIADB_SHELL_CREDENTIAL_STORE_HELPER"] = "plaintext"

    def store(self, secret_type, secret_id, secret):
        subprocess.run([os.path.join(self.bin, "mariadb-secret-store-plaintext"), "store"],
                       input=json.dumps({"SecretType": secret_type, "SecretID": secret_id,
                                         "Secret": secret}).encode(), check=True)

    def close(self):
        for k, v in self.saved.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        shutil.rmtree(self.dir, ignore_errors=True)
