// Copyright (c) 2026, MariaDB plc.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License, version 2.0,
// as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,  but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
// the GNU General Public License, version 2.0, for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

// Runs inside the VS Code that run.js starts, as its test runner: opens the
// workload files and times, through the VS Code API, what the installed SQL
// extension does with them.  The extension itself does nothing.

const vscode = require("vscode");
const fs = require("fs");
const path = require("path");
const { execFileSync } = require("child_process");

const now = () => Number(process.hrtime.bigint()) / 1e6; // ms
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// A line in run.js's step log (benchmarks/.runs/latest.log).
let logPath = null;
function log(msg) {
  const line = `${new Date().toTimeString().slice(0, 8)}   [vscode] ${msg}`;
  console.log(line);
  if (logPath) fs.appendFileSync(logPath, line + "\n");
}

function median(values) {
  const v = values.filter((x) => x !== null).sort((a, b) => a - b);
  if (!v.length) return null;
  const m = Math.floor(v.length / 2);
  return Math.round((v.length % 2 ? v[m] : (v[m - 1] + v[m]) / 2) * 100) / 100;
}

// Resident memory, in MB, of the extension host's descendants: the
// language servers and whatever they start.
function serverMemoryMb() {
  const out = execFileSync("ps", ["-axo", "pid=,ppid=,rss="], { encoding: "utf8" });
  const children = new Map();
  const rss = new Map();
  for (const line of out.trim().split("\n")) {
    const [pid, ppid, kb] = line.trim().split(/\s+/).map(Number);
    rss.set(pid, kb);
    if (!children.has(ppid)) children.set(ppid, []);
    children.get(ppid).push(pid);
  }
  let total = 0;
  const stack = [...(children.get(process.pid) || [])];
  while (stack.length) {
    const pid = stack.pop();
    total += rss.get(pid) || 0;
    stack.push(...(children.get(pid) || []));
  }
  return Math.round(total / 102.4) / 10;
}

// Resolves once the diagnostics of `uri` satisfy `holds`, or with null past
// `timeoutMs`.
function waitDiagnostics(uri, holds, timeoutMs) {
  return new Promise((resolve) => {
    const check = () => holds(vscode.languages.getDiagnostics(uri));
    if (check()) return resolve(now());
    const timer = setTimeout(() => { sub.dispose(); resolve(null); }, timeoutMs);
    const sub = vscode.languages.onDidChangeDiagnostics((e) => {
      if (e.uris.some((u) => u.toString() === uri.toString()) && check()) {
        clearTimeout(timer);
        sub.dispose();
        resolve(now());
      }
    });
  });
}

// Errors are looked for near a statement, not on one line: an extension may
// put a syntax error on a neighbouring line (Oracle's at the end of the
// statement before), and may report only the first error of a file, so a
// second one moves it instead of adding one.  `near(line)`: an error from two
// lines before `line` to five after it (a workload SELECT is six lines).
// Warnings and hints do not count.
const near = (line) => (d) => d.severity === vscode.DiagnosticSeverity.Error &&
  d.range.start.line >= line - 2 && d.range.start.line <= line + 5;
const errorNear = (line) => (diags) => diags.some(near(line));
const noErrorNear = (line) => (diags) => !diags.some(near(line));

// Before each file: other tabs (an extension's welcome page) and
// notifications (its prompts, left unanswered) out of the way.
async function clearWorkbench() {
  await vscode.commands.executeCommand("workbench.action.closeAllEditors");
  await vscode.commands.executeCommand("notifications.clearAll");
}

// Edits the document itself, whether or not a tab shows it.
async function replace(uri, range, text) {
  const edit = new vscode.WorkspaceEdit();
  edit.replace(uri, range, text);
  await vscode.workspace.applyEdit(edit);
}

// `record(out)` saves what is measured so far: a run cut off keeps it.
async function measureSize(dir, size, cfg, record) {
  const uri = vscode.Uri.file(path.join(dir, `bench-${size}.sql`));
  const meta = JSON.parse(fs.readFileSync(path.join(dir, `bench-${size}.json`), "utf8"));
  const out = {};
  await clearWorkbench();
  const t0 = now();
  const doc = await vscode.workspace.openTextDocument(uri);
  await vscode.window.showTextDocument(doc, { preview: false });
  out.language = doc.languageId;
  if (cfg.diagnostics === false) {
    // No syntax errors offline: completion and formatting only, once the
    // extension had a moment with the file.
    log(`${size}: opened as ${doc.languageId} (no diagnostics offline), a 2 s pause`);
    out.no_diagnostics = true;
    await sleep(2000);
    out.memory_after_open_mb = serverMemoryMb();
    record(out);
  }
  const opened = cfg.diagnostics === false ? null : await (async () => {
    log(`${size}: opened as ${doc.languageId}, waiting for the last line's error`);
    return waitDiagnostics(uri, errorNear(meta.error_line), cfg.timeoutMs);
  })();
  if (cfg.diagnostics !== false) {
    out.open_ms = opened === null ? null : Math.round((opened - t0) * 100) / 100;
    out.memory_after_open_mb = serverMemoryMb();
    record(out);
  }
  if (cfg.diagnostics === false) {
    // nothing to wait for
  } else if (opened === null) {
    out.open_timed_out_s = cfg.timeoutMs / 1000;
  } else {
    log(`${size}: error after ${out.open_ms} ms; edits`);
    const rounds = size === "10M" ? Math.max(1, Math.floor(cfg.editRounds / 2)) : cfg.editRounds;
    const appear = [];
    const clear = [];
    const at = new vscode.Position(meta.edit_line, 5);
    for (let r = 0; r < rounds; r++) {
      let t = now();
      await replace(uri, new vscode.Range(at, at.translate(0, 1)), "");
      let done = await waitDiagnostics(uri, errorNear(meta.edit_line), cfg.timeoutMs);
      appear.push(done === null ? null : done - t);
      t = now();
      await replace(uri, new vscode.Range(at, at), "T");
      done = await waitDiagnostics(uri, noErrorNear(meta.edit_line), cfg.timeoutMs);
      clear.push(done === null ? null : done - t);
    }
    out.edit_appear_ms = median(appear);
    out.edit_clear_ms = median(clear);
    out.edit_rounds = rounds;
    record(out);
  }
  log(`${size}: completion`);
  // As in run.py: one completion and a 2 s pause first (the file open a moment).
  const [line0, character0] = meta.completion_points[0];
  await vscode.commands.executeCommand(
    "vscode.executeCompletionItemProvider", uri, new vscode.Position(line0, character0));
  await sleep(2000);
  const times = [];
  const counts = [];
  for (const [line, character] of meta.completion_points) {
    const t = now();
    const list = await vscode.commands.executeCommand(
      "vscode.executeCompletionItemProvider", uri, new vscode.Position(line, character));
    times.push(now() - t);
    counts.push(list ? list.items.length : 0);
  }
  out.completion_ms = median(times);
  out.completion_items = [Math.min(...counts), Math.max(...counts)];
  if (cfg.formatSizes.includes(size)) {
    log(`${size}: formatting`);
    const t = now();
    const edits = await vscode.commands.executeCommand(
      "vscode.executeFormatDocumentProvider", uri, { tabSize: 4, insertSpaces: true });
    out.formatting_ms = Math.round((now() - t) * 10) / 10;
    out.formatting_edits = edits ? edits.length : 0;
  }
  out.memory_end_mb = serverMemoryMb();
  // The edits leave the text as it was, but the document is dirty: saved, so
  // that closing it asks nothing.
  await doc.save();
  return out;
}

async function run() {
  const cfg = JSON.parse(process.env.BENCH_CONFIG);
  logPath = cfg.log;
  const results = { extension: cfg.extension, sizes: {} };
  const save = () => fs.writeFileSync(cfg.out, JSON.stringify(results, null, 2) + "\n");
  try {
    const dir = vscode.workspace.workspaceFolders[0].uri.fsPath;
    // The first file also starts the language server: its time is the cold start.
    log("cold start: opening the warm-up file");
    const warm = vscode.Uri.file(path.join(dir, "bench-warmup.sql"));
    const meta = JSON.parse(fs.readFileSync(path.join(dir, "bench-warmup.json"), "utf8"));
    await clearWorkbench();
    const t0 = now();
    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(warm), { preview: false });
    if (cfg.diagnostics === false) {
      // No error to time the start by: a pause for the extension to start.
      await sleep(3000);
      results.cold_start_ms = null;
      results.memory_idle_mb = serverMemoryMb();
      log("cold start: not measured (no diagnostics offline)");
    }
    const ready = cfg.diagnostics === false ? 0
      : await waitDiagnostics(warm, errorNear(meta.error_line), cfg.timeoutMs);
    if (cfg.diagnostics !== false) {
      results.cold_start_ms = ready === null ? null : Math.round(ready - t0);
      results.memory_idle_mb = serverMemoryMb();
      log(`cold start: ${results.cold_start_ms === null ? "no error reported" : results.cold_start_ms + " ms"}`);
    }
    if (ready === null) {
      // What there is to go on: the file's language, the SQL extension, the diagnostics.
      const doc = vscode.workspace.textDocuments.find((d) => d.uri.toString() === warm.toString());
      const sqlExtensions = vscode.extensions.all
        .filter((e) => !e.id.startsWith("vscode.") && e.id !== "mariadb.lsp-benchmark")
        .map((e) => `${e.id}${e.isActive ? " (active)" : " (inactive)"}`);
      const diags = vscode.languages.getDiagnostics(warm)
        .map((d) => `${d.range.start.line}:${d.severity}:${d.source || ""}:${d.message.slice(0, 60)}`);
      log(`  language ${doc && doc.languageId}; extensions ${sqlExtensions.join(", ")}; ` +
          `${diags.length} diagnostics ${JSON.stringify(diags.slice(0, 5))}`);
      results.error = "the extension reported no error on the warm-up file";
      save();
      return;
    }
    await vscode.commands.executeCommand("workbench.action.closeAllEditors");
    save();
    for (const size of cfg.sizes) {
      log(`${size} ...`);
      results.sizes[size] = await measureSize(dir, size, cfg, (o) => { results.sizes[size] = o; save(); });
      log(`${size} done: ${JSON.stringify(results.sizes[size])}`);
      save();
      if (results.sizes[size].open_timed_out_s) break;
    }
  } catch (e) {
    results.error = String(e && e.stack || e);
    log(`error: ${results.error}`);
    save();
  }
}

module.exports = { run, activate() {}, deactivate() {} };
