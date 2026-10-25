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

"""Measures language servers over LSP on stdio, on the workload of
workload.py, without a database connection.

Per server and size, in fresh server processes (startup and open are the
median of --repeat runs; the rest is measured in the first run):
- startup: spawn to the initialize reply;
- open: didOpen to the first publishDiagnostics holding the error on the
  last line (a full parse);
- edit: a SELECT in the middle broken (SELECT -> SELEC) and fixed again,
  --edit-rounds times: didChange to the diagnostics that show, and that
  drop, its error;
- completion: after `FROM ` and `WHERE ` of SELECTs spread over the file,
  after one completion and a 2 s pause (a file open a moment, as typed in);
- formatting: the whole document;
- memory: resident set size of all the server's processes after the open,
  and after everything else.

usage: run.py [--servers a,b] [--sizes 10K,100K,1M,10M] [--out FILE.json]
Writes FILE.json (default results/<date>-<host>.json) and FILE.md."""

import argparse
import datetime
import json
import os
import pathlib
import platform
import shutil
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tests" / "e2e"))

from lsp_client import Client, Lsp_error  # noqa: E402
from servers import SERVERS  # noqa: E402
import workload  # noqa: E402


# ---- the step log: benchmarks/.runs/<time>-<kind>.log, latest.log points at it

RUNS = HERE / ".runs"
_log_file = None


def open_log(kind):
    global _log_file
    RUNS.mkdir(exist_ok=True)
    path = RUNS / f"{datetime.datetime.now():%Y-%m-%d-%H%M%S}-{kind}.log"
    _log_file = open(path, "w")
    latest = RUNS / "latest.log"
    latest.unlink(missing_ok=True)
    latest.symlink_to(path.name)
    return path


def log(msg):
    line = f"{datetime.datetime.now():%H:%M:%S} {msg}"
    print(line, flush=True)
    if _log_file:
        _log_file.write(line + "\n")
        _log_file.flush()


# ---- processes

def processes():
    out = subprocess.run(["ps", "-axo", "pid=,ppid=,rss=,command="],
                         capture_output=True, text=True).stdout
    procs = []
    for line in out.splitlines():
        parts = line.split(None, 3)
        if len(parts) == 4 and parts[0].isdigit():
            procs.append((int(parts[0]), int(parts[1]), int(parts[2]), parts[3]))
    return procs


def rss_mb(root_pid, match=None):
    """Resident memory of `root_pid` and its descendants, plus every process
    whose command line holds `match` (a daemon started apart), in MB."""
    procs = processes()
    children = {}
    for pid, ppid, _, _ in procs:
        children.setdefault(ppid, []).append(pid)
    tree, stack = set(), [root_pid]
    while stack:
        pid = stack.pop()
        if pid not in tree:
            tree.add(pid)
            stack += children.get(pid, [])
    return sum(rss for pid, _, rss, cmd in procs
               if pid in tree or (match and match in cmd and pid != os.getpid())) / 1024


# ---- one server process

def same_uri(a, b):
    return urllib.parse.unquote(a).lower() == urllib.parse.unquote(b).lower()


class Session:
    """A server started in a workspace of its own, initialized.  A server
    still running `limit` seconds after its start is killed (`killed`)."""

    def __init__(self, spec, limit):
        self.spec = spec
        self.killed = False
        self.ws = os.path.realpath(tempfile.mkdtemp(prefix="lspbench-"))
        for name, content in spec.get("workspace_files", {}).items():
            pathlib.Path(self.ws, name).write_text(content)
        self.uri = pathlib.Path(self.ws, "bench.sql").as_uri()
        self.version = 0
        self.lines = None
        t0 = time.perf_counter()
        self.client = Client(cmd=spec["cmd"](), cwd=self.ws, stderr=subprocess.DEVNULL)
        self.watchdog = threading.Timer(limit, self.kill)
        self.watchdog.daemon = True
        self.watchdog.start()
        self.client.request_handler = self.answer
        result = self.client.request("initialize", {
            "processId": os.getpid(),
            "rootUri": pathlib.Path(self.ws).as_uri(),
            "rootPath": self.ws,
            "workspaceFolders": [{"uri": pathlib.Path(self.ws).as_uri(), "name": "bench"}],
            "capabilities": {
                "textDocument": {
                    "synchronization": {"didSave": True},
                    "publishDiagnostics": {"versionSupport": True},
                    "completion": {"completionItem": {"snippetSupport": False}},
                },
                "workspace": {"configuration": True, "workspaceFolders": True},
            },
            "initializationOptions": spec.get("init_options"),
        }, timeout=120)
        self.startup = time.perf_counter() - t0
        self.capabilities = result.get("capabilities", {})
        self.client.notify("initialized", {})
        if "settings" in spec:
            self.client.notify("workspace/didChangeConfiguration", {"settings": spec["settings"]})

    def answer(self, msg):
        if msg["method"] == "workspace/configuration":
            settings = self.spec.get("settings", {})
            out = []
            for item in msg["params"].get("items", []):
                value = settings
                for key in filter(None, (item.get("section") or "").split(".")):
                    value = value.get(key) if isinstance(value, dict) else None
                out.append(value)
            return out
        return None

    def rss(self):
        return rss_mb(self.client.proc.pid, self.spec.get("process_match"))

    def full_sync(self):
        sync = self.capabilities.get("textDocumentSync")
        kind = sync.get("change") if isinstance(sync, dict) else sync
        return kind == 1

    def diagnostics_at(self, line, after, timeout, present=True):
        """Waits for diagnostics of the document that hold (or, with
        present=False, no longer hold) an error on `line`; a linter's
        warnings and hints there do not count."""
        def error_on_line(diagnostics):
            return any(d["range"]["start"]["line"] == line and d.get("severity", 1) == 1
                       for d in diagnostics)
        if self.spec["diagnostics"] == "pull":
            # Asked for after each change, as an editor does; asked again
            # until they show the change.
            deadline = time.monotonic() + timeout
            while True:
                result = self.client.request("textDocument/diagnostic",
                                             {"textDocument": {"uri": self.uri}},
                                             max(1, deadline - time.monotonic()))
                if error_on_line(result.get("items", [])) == present:
                    return
                if time.monotonic() > deadline:
                    raise TimeoutError("no diagnostics showing the change")
                time.sleep(0.005)

        def holds(p):
            return same_uri(p["uri"], self.uri) and error_on_line(p["diagnostics"]) == present
        self.client.wait_notification("textDocument/publishDiagnostics", holds, timeout, after)

    def first_diagnostics(self, after, timeout):
        """Waits for the document's first diagnostics of any kind (a linter
        that reports no syntax errors)."""
        self.client.wait_notification("textDocument/publishDiagnostics",
                                      lambda p: same_uri(p["uri"], self.uri), timeout, after)

    def open(self, w, timeout):
        """Seconds to the diagnostics of the last line's error (for a
        "lint" server, to its first diagnostics), None past `timeout` or for
        a server that reports none."""
        self.lines = w.text.split("\n")
        mark = self.client.mark()
        t0 = time.perf_counter()
        self.client.notify("textDocument/didOpen", {"textDocument": {
            "uri": self.uri, "languageId": "sql", "version": 0, "text": w.text}})
        for method, params in self.spec.get("after_open", lambda uri: [])(self.uri):
            self.client.notify(method, params)
        if self.spec["diagnostics"] is None:
            time.sleep(1)
            return None
        try:
            if self.spec["diagnostics"] == "lint":
                self.first_diagnostics(mark, timeout)
            else:
                self.diagnostics_at(w.error_line, mark, timeout)
        except TimeoutError:
            return None
        return time.perf_counter() - t0

    def change(self, line, character, removed, inserted):
        self.version += 1
        text = self.lines[line]
        self.lines[line] = text[:character] + inserted + text[character + removed:]
        if self.full_sync():
            change = {"text": "\n".join(self.lines)}
        else:
            change = {"range": {"start": {"line": line, "character": character},
                                "end": {"line": line, "character": character + removed}},
                      "text": inserted}
        self.client.notify("textDocument/didChange", {
            "textDocument": {"uri": self.uri, "version": self.version}, "contentChanges": [change]})

    def edits(self, w, rounds, timeout):
        """(appear times, clear times) of SELECT -> SELEC -> SELECT."""
        appear, clear = [], []
        for _ in range(rounds):
            mark = self.client.mark()
            t0 = time.perf_counter()
            self.change(w.edit_line, 5, 1, "")
            self.diagnostics_at(w.edit_line, mark, timeout)
            appear.append(time.perf_counter() - t0)
            mark = self.client.mark()
            t0 = time.perf_counter()
            self.change(w.edit_line, 5, 0, "T")
            self.diagnostics_at(w.edit_line, mark, timeout, present=False)
            clear.append(time.perf_counter() - t0)
        return appear, clear

    def completion(self, w, timeout):
        """(seconds, item count) per completion point."""
        out = []
        for line, character in w.completion_points:
            t0 = time.perf_counter()
            result = self.client.request("textDocument/completion", {
                "textDocument": {"uri": self.uri},
                "position": {"line": line, "character": character},
                "context": {"triggerKind": 1}}, timeout)
            seconds = time.perf_counter() - t0
            items = result if isinstance(result, list) else (result or {}).get("items", [])
            out.append((seconds, len(items)))
        return out

    def formatting(self, timeout):
        """(seconds, edit count)."""
        t0 = time.perf_counter()
        result = self.client.request("textDocument/formatting", {
            "textDocument": {"uri": self.uri},
            "options": {"tabSize": 4, "insertSpaces": True}}, timeout)
        return time.perf_counter() - t0, len(result or [])

    def kill(self):
        # Its output ends, which fails whatever waits for it.
        self.killed = True
        self.client.proc.kill()

    def close(self):
        self.watchdog.cancel()
        if self.killed:
            self.client.proc.wait()
        try:
            self.client.close()
        except Exception:  # noqa: BLE001 -- a server that hangs on shutdown is killed
            pass
        if self.client.proc.poll() is None:
            self.client.proc.kill()
            self.client.proc.wait()
        if "stop" in self.spec:
            subprocess.run(self.spec["stop"](), cwd=self.ws, capture_output=True, timeout=30)
        shutil.rmtree(self.ws, ignore_errors=True)


# ---- the run

def server_version(name, spec):
    if spec["version"]:
        return spec["version"]
    out = subprocess.run(spec["cmd"]()[:1] + ["--version"], capture_output=True, text=True).stdout
    return out.strip().split()[-1] if out.strip() else "?"


def machine():
    if platform.system() == "Darwin":
        def sysctl(key):
            return subprocess.run(["sysctl", "-n", key], capture_output=True, text=True).stdout.strip()
        return {"cpu": sysctl("machdep.cpu.brand_string"), "cores": int(sysctl("hw.ncpu")),
                "memory_gb": round(int(sysctl("hw.memsize")) / 2**30), "os": f"macOS {platform.mac_ver()[0]}"}
    cpu = next((line.split(":", 1)[1].strip() for line in open("/proc/cpuinfo")
                if line.startswith("model name")), platform.machine())
    mem = next(int(line.split()[1]) for line in open("/proc/meminfo") if line.startswith("MemTotal"))
    return {"cpu": cpu, "cores": os.cpu_count(), "memory_gb": round(mem / 2**20),
            "os": platform.platform()}


def median_ms(values):
    values = [v for v in values if v is not None]
    return round(statistics.median(values) * 1000, 2) if values else None


def measure(spec, size_name, w, args):
    """The measures of one server at one size."""
    out = {"bytes": len(w.text.encode()), "open_ms_runs": [], "startup_ms_runs": []}
    for run in range(args.repeat):
        try:
            s = Session(spec, args.session_limit)
        except (TimeoutError, Lsp_error, OSError) as e:
            out["error"] = f"start: {e}"
            break
        try:
            out["startup_ms_runs"].append(round(s.startup * 1000, 2))
            log(f"  run {run + 1}/{args.repeat}: started in {s.startup * 1000:.0f} ms, opening")
            opened = s.open(w, args.open_timeout)
            log("  opened: " + ("no diagnostics" if opened is None else f"error after {opened * 1000:.1f} ms"))
            out["open_ms_runs"].append(None if opened is None else round(opened * 1000, 2))
            if run > 0:
                continue
            out["memory_after_open_mb"] = round(s.rss(), 1)
            reports = spec["diagnostics"] is not None
            if reports and opened is None:
                out["open_timed_out_s"] = args.open_timeout
                break
            # A linter that reports no syntax errors never shows the broken SELECT.
            if reports and spec["diagnostics"] != "lint":
                rounds = args.edit_rounds if len(w.text) < 5_000_000 else max(1, args.edit_rounds // 2)
                log(f"  edits: {rounds} rounds")
                try:
                    appear, clear = s.edits(w, rounds, args.open_timeout)
                    out["edit_appear_ms"] = median_ms(appear)
                    out["edit_clear_ms"] = median_ms(clear)
                    out["edit_rounds"] = rounds
                except TimeoutError:
                    out["edit_timed_out_s"] = args.open_timeout
            if s.capabilities.get("completionProvider"):
                try:
                    # As someone typing sees it: the file has been open a
                    # moment.  (An editor asks for document symbols on open;
                    # here one completion and a pause stand in for that.)
                    log(f"  completion: {len(w.completion_points)} points")
                    s.completion(workload.Workload(w.text, w.error_line, w.edit_line,
                                                   w.completion_points[:1]), args.open_timeout)
                    time.sleep(2)
                    points = s.completion(w, args.open_timeout)
                    out["completion_ms"] = median_ms([p[0] for p in points])
                    out["completion_items"] = [min(p[1] for p in points), max(p[1] for p in points)]
                except (TimeoutError, Lsp_error) as e:
                    out["completion_error"] = str(e)
            else:
                out["completion"] = "not offered"
            if size_name in args.format_sizes:
                if s.capabilities.get("documentFormattingProvider"):
                    log("  formatting")
                    try:
                        seconds, edits = s.formatting(args.open_timeout)
                        out["formatting_ms"], out["formatting_edits"] = round(seconds * 1000, 1), edits
                    except (TimeoutError, Lsp_error) as e:
                        out["formatting_error"] = str(e)
                else:
                    out["formatting"] = "not offered"
            if not s.killed:
                out["memory_end_mb"] = round(s.rss(), 1)
        except (TimeoutError, Lsp_error, OSError) as e:
            out["error"] = str(e)
        finally:
            s.close()
            if s.killed:
                out["killed_after_s"] = args.session_limit
        if s.killed:
            break
    out["startup_ms"] = median_ms([v / 1000 for v in out["startup_ms_runs"]])
    out["open_ms"] = median_ms([v / 1000 for v in out["open_ms_runs"] if v is not None]) \
        if all(v is not None for v in out["open_ms_runs"]) else None
    return out


def fmt_ms(v):
    if v is None:
        return "—"
    return f"{v:,.1f} ms" if v < 10 else f"{v:,.0f} ms"


def markdown(results):
    names = list(results["servers"])
    label = {n: results["servers"][n]["label"] for n in names}
    lines = [f"# Language server benchmark, {results['date']}", "",
             f"{results['machine']['cpu']}, {results['machine']['cores']} cores, "
             f"{results['machine']['memory_gb']} GB, {results['machine']['os']}. No database connection.", "",
             "| Server | Version |", "| --- | --- |"]
    lines += [f"| {label[n]} | {results['servers'][n]['version']} |" for n in names]

    def table(title, cell):
        rows = [f"## {title}", "", "| Size | " + " | ".join(label[n] for n in names) + " |",
                "| --- |" + " ---: |" * len(names)]
        def any_cell(m, srv):
            return "not run" if m and "not_run" in m else cell(m, srv)
        for size in results["sizes"]:
            rows.append(f"| {size} | " + " | ".join(
                any_cell(results["servers"][n]["sizes"].get(size), results["servers"][n]) for n in names) + " |")
        return rows + [""]

    def open_cell(m, srv):
        if m is None:
            return "skipped"
        if srv["diagnostics"] is None:
            return "no diagnostics"
        if "open_timed_out_s" in m:
            return f"none in {m['open_timed_out_s']} s"
        return fmt_ms(m["open_ms"]) + (" (lints only)" if srv["diagnostics"] == "lint" else "")

    def edit_cell(m, srv):
        if m is None or srv["diagnostics"] in (None, "lint") or "open_timed_out_s" in m:
            return "—"
        if "edit_timed_out_s" in m:
            return f"none in {m['edit_timed_out_s']} s"
        return f"{fmt_ms(m['edit_appear_ms'])} / {fmt_ms(m['edit_clear_ms'])}"

    def completion_cell(m, _):
        if m is None:
            return "—"
        if "completion_ms" in m:
            lo, hi = m["completion_items"]
            return f"{fmt_ms(m['completion_ms'])} ({lo}–{hi} items)"
        return m.get("completion", m.get("completion_error", "—"))

    def formatting_cell(m, _):
        if m is None or ("formatting_ms" not in m and "formatting" not in m and "formatting_error" not in m):
            return "—"
        if "formatting_ms" in m:
            return f"{fmt_ms(m['formatting_ms'])}, {m['formatting_edits']:,} edits"
        return m.get("formatting", m.get("formatting_error"))

    def memory_cell(m, _):
        if m is None or "memory_after_open_mb" not in m:
            return "—"
        end = m.get("memory_end_mb")
        return f"{m['memory_after_open_mb']:,.0f} / {end:,.0f} MB" if end else f"{m['memory_after_open_mb']:,.0f} MB"

    lines += table("Startup (spawn to initialize reply, median)",
                   lambda m, _: fmt_ms(m["startup_ms"]) if m else "skipped")
    lines += table("Open → diagnostics with the last line's error (median)", open_cell)
    lines += table("Edit → diagnostics, error appears / clears (median)", edit_cell)
    lines += table("Completion after FROM and WHERE (median)", completion_cell)
    lines += table("Formatting the whole document", formatting_cell)
    lines += table("Memory, after open / at the end (RSS)", memory_cell)
    notes = []
    for n in names:
        for size, m in results["servers"][n]["sizes"].items():
            if "not_run" in m:
                notes.append(f"- {label[n]}, {size}: not run, {m['not_run']}")
            elif "killed_after_s" in m:
                notes.append(f"- {label[n]}, {size}: still busy after {m['killed_after_s']} s, killed"
                             + (f" ({m['error']})" if "error" in m else ""))
            elif "error" in m:
                notes.append(f"- {label[n]}, {size}: {m['error']}")
    if notes:
        lines += ["## Notes", ""] + notes + [""]
    return "\n".join(lines)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--servers", default=",".join(SERVERS))
    p.add_argument("--sizes", default=",".join(workload.SIZES))
    p.add_argument("--repeat", type=int, default=3, help="runs per size for startup and open")
    p.add_argument("--edit-rounds", type=int, default=10, help="(half of it at 10 MB)")
    p.add_argument("--format-sizes", default="1M")
    p.add_argument("--open-timeout", type=int, default=30,
                   help="seconds to wait for diagnostics, formatting and completion")
    p.add_argument("--session-limit", type=int, default=60,
                   help="seconds a server process may run before it is killed")
    p.add_argument("--out")
    p.add_argument("--append", action="store_true",
                   help="keep the other servers already in --out, replace these")
    args = p.parse_args()
    args.format_sizes = args.format_sizes.split(",")
    names = args.servers.split(",")
    for n in names:
        if n not in SERVERS:
            sys.exit(f"unknown server {n}; known: {', '.join(SERVERS)}")
    sizes = args.sizes.split(",")
    date = datetime.date.today().isoformat()
    out = pathlib.Path(args.out) if args.out else \
        HERE / "results" / f"{date}-{socket.gethostname().split('.')[0].lower()}.json"
    log(f"log: {open_log('stdio')}")
    results = {"date": date, "machine": machine(), "sizes": sizes, "servers": {}}
    if args.append and out.exists():
        kept = json.loads(out.read_text())
        results["servers"] = {n: v for n, v in kept["servers"].items() if n not in names}
        results["sizes"] = list(dict.fromkeys(kept["sizes"] + sizes))
    for name in names:
        spec = SERVERS[name]
        srv = results["servers"][name] = {"label": spec["label"], "version": server_version(name, spec),
                                          "diagnostics": spec["diagnostics"], "sizes": {}}
        timed_out = False
        for size in sizes:
            if timed_out:
                log(f"{name} {size}: skipped (the open timed out at a smaller size)")
                continue
            if "max_size" in spec and workload.SIZES[size] > workload.SIZES[spec["max_size"]]:
                srv["sizes"][size] = {"not_run": f"larger than {spec['max_size']}: "
                                                  + spec.get("max_size_reason", "servers.py max_size")}
                log(f"{name} {size}: not run (its max_size is {spec['max_size']})")
                continue
            log(f"{name} {size} ...")
            m = measure(spec, size, workload.generate(workload.SIZES[size]), args)
            srv["sizes"][size] = m
            log(f"{name} {size} done: {json.dumps({k: v for k, v in m.items() if not k.endswith('_runs')})}")
            timed_out = "open_timed_out_s" in m or "killed_after_s" in m
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(results, indent=2) + "\n")
    out.with_suffix(".md").write_text(markdown(results) + "\n")
    log(f"wrote {out} and {out.with_suffix('.md')}")


if __name__ == "__main__":
    main()
