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

"""A minimal LSP client for the end-to-end tests: starts mariadb-lsp, frames
JSON-RPC messages, and collects what the server sends."""

import json
import os
import queue
import subprocess
import threading
import time


class Lsp_error(Exception):
    def __init__(self, error):
        super().__init__(f"{error.get('code')}: {error.get('message')}")
        self.code = error.get("code")


class Request_error(Exception):
    """What a request handler raises to answer a server request with an error."""

    def __init__(self, message, code=-32000, data=None):
        super().__init__(message)
        self.code = code
        self.data = data


class Client:
    def __init__(self, binary=None, args=(), log=None):
        binary = binary or os.environ["MARIADB_LSP"]
        cmd = [binary, "lsp"] + list(args)
        if log:
            cmd += ["--log", log]
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE)
        self.next_id = 1
        self.received = 0  # messages read so far: their sequence numbers
        self.last_response_seq = None  # sequence number of the last awaited response
        self.responses = {}
        self.notifications = []  # (sequence number, message), in arrival order
        self.server_requests = []
        # Answers the server's requests when set: called with the message,
        # it returns the result, or raises Request_error.  Else they are
        # answered with null.
        self.request_handler = None
        self.cond = threading.Condition()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    # ---- wire

    def _read(self):
        out = self.proc.stdout
        while True:
            length = None
            while True:
                line = out.readline()
                if not line:
                    return
                line = line.strip()
                if not line:
                    break
                name, _, value = line.decode().partition(":")
                if name.lower() == "content-length":
                    length = int(value)
            body = out.read(length)
            msg = json.loads(body)
            with self.cond:
                self.received += 1
                seq = self.received
                if "method" in msg and "id" in msg:
                    self.server_requests.append(msg)
                    self._answer(msg)
                elif "method" in msg:
                    self.notifications.append((seq, msg))
                else:
                    self.responses[msg["id"]] = (seq, msg)
                self.cond.notify_all()

    # What a request handler returns to leave a request unanswered.
    NO_ANSWER = object()

    def _answer(self, msg):
        reply = {"jsonrpc": "2.0", "id": msg["id"]}
        if self.request_handler is None:
            reply["result"] = None
        else:
            try:
                result = self.request_handler(msg)
            except Request_error as e:
                reply["error"] = {"code": e.code, "message": str(e), "data": e.data}
            else:
                if result is Client.NO_ANSWER:
                    return
                reply["result"] = result
        self._send(reply)

    def _send(self, msg):
        body = json.dumps(msg).encode()
        self.proc.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        self.proc.stdin.flush()

    def send_raw(self, data):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    # ---- messages

    def request_async(self, method, params=None):
        with self.cond:
            rid = self.next_id
            self.next_id += 1
            self._send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params or {}})
        return rid

    def wait_response(self, rid, timeout=30):
        deadline = time.monotonic() + timeout
        with self.cond:
            while rid not in self.responses:
                left = deadline - time.monotonic()
                if left <= 0:
                    raise TimeoutError(f"no response to request {rid}")
                self.cond.wait(left)
            self.last_response_seq, msg = self.responses.pop(rid)
        if "error" in msg:
            raise Lsp_error(msg["error"])
        return msg["result"]

    def request(self, method, params=None, timeout=30):
        return self.wait_response(self.request_async(method, params), timeout)

    def notify(self, method, params=None):
        with self.cond:
            self._send({"jsonrpc": "2.0", "method": method, "params": params or {}})

    def wait_notification(self, method, predicate=lambda p: True, timeout=30, after=0):
        """The first notification `method` at index >= `after` whose params
        satisfy `predicate`: returns (params, sequence number)."""
        deadline = time.monotonic() + timeout
        with self.cond:
            while True:
                for i in range(after, len(self.notifications)):
                    msg = self.notifications[i][1]
                    if msg["method"] == method and predicate(msg["params"]):
                        return msg["params"], self.notifications[i][0]
                left = deadline - time.monotonic()
                if left <= 0:
                    raise TimeoutError(f"no {method} notification")
                self.cond.wait(left)

    def mark(self):
        with self.cond:
            return len(self.notifications)

    # ---- lifecycle

    def initialize(self, options=None, capabilities=None, root_uri=None):
        result = self.request("initialize", {
            "processId": os.getpid(),
            "rootUri": root_uri,
            "capabilities": capabilities or {},
            "initializationOptions": {"debounceMs": 0, **(options or {})},
        })
        self.notify("initialized", {})
        return result

    def open(self, uri, text, version=1):
        self.notify("textDocument/didOpen", {"textDocument": {
            "uri": uri, "languageId": "sql", "version": version, "text": text}})

    def change(self, uri, version, changes):
        self.notify("textDocument/didChange", {
            "textDocument": {"uri": uri, "version": version}, "contentChanges": changes})

    def diagnostics(self, uri, version=None, timeout=30, after=0):
        params, _ = self.wait_notification(
            "textDocument/publishDiagnostics",
            lambda p: p["uri"] == uri and (version is None or p.get("version") == version),
            timeout, after)
        return params["diagnostics"]

    def close(self):
        """shutdown and exit; returns the exit code."""
        try:
            self.request("shutdown", timeout=10)
            self.notify("exit")
        except (OSError, ValueError, TimeoutError):
            pass
        try:
            return self.proc.wait(timeout=10)
        finally:
            for f in (self.proc.stdin, self.proc.stdout, self.proc.stderr):
                try:
                    f.close()
                except OSError:
                    pass
