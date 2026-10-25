#!/usr/bin/env node
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

// Measures SQL extensions inside VS Code, for servers that only run inside
// their extension (Oracle SQL Developer).  Per extension: a pinned VS Code
// (downloaded to benchmarks/.vscode-test, not in git) with only that
// extension installed, in a profile of its own; bench-extension/ times what
// it does with the workload files through the VS Code API.
//
// usage: node run.js [--extensions mariadb,oracle] [--sizes 10K,100K,1M,10M] [--out FILE.json] [--append]
//                    [--limit SECONDS per VS Code run, 60]

const fs = require("fs");
const os = require("os");
const path = require("path");
const { execFileSync } = require("child_process");
const { downloadAndUnzipVSCode, resolveCliArgsFromVSCodeExecutablePath, runTests } =
  require("@vscode/test-electron");

const VSCODE_VERSION = "1.143.1";
const HERE = __dirname;
const BENCH = path.dirname(HERE);
const CACHE = path.join(BENCH, ".vscode-test");

// Each extension: the Marketplace id and pinned version, the language its
// files get, and its settings.
const EXTENSIONS = {
  mariadb: {
    label: "MariaDB Shell for VS Code (MariaDB LSP)",
    id: "mariadb.mariadb-shell",
    version: "26.10.1",
    // Not on the Marketplace: its .vsix, from a vscode-extension checkout.
    vsix: () => process.env.MARIADB_VSIX ||
      path.join(BENCH, "..", "..", "vscode-extension", "mariadb-shell-26.10.1.vsix"),
    language: "mariadb-sql",
    settings: () => ({
      // This checkout's Release build, with the check alone (no typing pause).
      "mariadb.lsp.path": process.env.MARIADB_LSP || path.join(BENCH, "..", "build-release", "bin", "mariadb-lsp"),
      "mariadb.lsp.debounceMs": 0,
    }),
  },
  oracle: {
    label: "Oracle SQL Developer for VS Code",
    id: "oracle.sql-developer",
    version: "26.3.0",
    language: "oracle-sql",
    settings: () => ({ "sqldeveloper.telemetry.enabled": false }),
  },
  mssql: {
    label: "SQL Server (mssql)",
    id: "ms-mssql.mssql",
    version: "1.47.0",
    language: "sql",
    settings: () => ({ "mssql.intelliSense.enableErrorChecking": true, "mssql.intelliSense.enableIntelliSense": true }),
  },
  sqltools: {
    diagnostics: false, // no syntax errors without a database: completion and formatting only
    label: "SQLTools",
    id: "mtxr.sqltools",
    version: "0.28.6",
    language: "sql",
    settings: () => ({}),
  },
  pgsql: {
    diagnostics: false, // no syntax errors without a database: completion and formatting only
    label: "PostgreSQL (Microsoft)",
    id: "ms-ossdata.vscode-pgsql",
    version: "1.33.0",
    language: "sql",
    settings: () => ({ "pgsql.intelliSense.enableIntelliSense": true }),
  },
  snowflake: {
    diagnostics: false, // no syntax errors without a database: completion and formatting only
    label: "Snowflake",
    id: "snowflake.snowflake-vsc",
    version: "1.42.3",
    language: "snowflake-sql",
    settings: () => ({}),
  },
  sqlfluff: {
    label: "sqlfluff",
    id: "sqlfluff.vscode-sqlfluff",
    version: "4.0.2",
    language: "sql",
    // The sqlfluff command from install.py.  Its lints are warnings and its
    // parse errors (PRS) errors, so that an edit's syntax error is told apart.
    settings: () => ({
      "sqlfluff.executablePath": path.join(BENCH, ".servers", "sqlfluff", "bin", "sqlfluff"),
      "sqlfluff.dialect": "ansi",
      "sqlfluff.linter.run": "onType",
      "sqlfluff.linter.diagnosticSeverity": "warning",
      "sqlfluff.linter.diagnosticSeverityByRule": [{ rule: "PRS", severity: "error" }],
    }),
  },
};

// The step log: benchmarks/.runs/<time>-vscode.log, latest.log points at it.
const RUNS = path.join(BENCH, ".runs");
let logPath = null;
function openLog() {
  fs.mkdirSync(RUNS, { recursive: true });
  const stamp = new Date().toISOString().slice(0, 19).replace("T", "-").replace(/:/g, "");
  logPath = path.join(RUNS, `${stamp}-vscode.log`);
  fs.writeFileSync(logPath, "");
  const latest = path.join(RUNS, "latest.log");
  fs.rmSync(latest, { force: true });
  fs.symlinkSync(path.basename(logPath), latest);
}
function log(msg) {
  const line = `${new Date().toTimeString().slice(0, 8)} ${msg}`;
  console.log(line);
  if (logPath) fs.appendFileSync(logPath, line + "\n");
}

// Ends every process of the benchmark's VS Code, all under .vscode-test
// (never the user's own).
function killVsCode() {
  try { execFileSync("pkill", ["-f", CACHE]); } catch { /* none left */ }
}

function arg(name, fallback) {
  const i = process.argv.indexOf(name);
  return i > 0 ? process.argv[i + 1] : fallback;
}

async function main() {
  // Started from a terminal inside VS Code, the environment would make the
  // benchmark's VS Code run as plain Node (ELECTRON_RUN_AS_NODE) or talk to
  // the running one (VSCODE_IPC_HOOK_CLI and the rest).
  for (const key of Object.keys(process.env)) {
    if (key === "ELECTRON_RUN_AS_NODE" || key.startsWith("VSCODE_")) delete process.env[key];
  }
  const names = arg("--extensions", Object.keys(EXTENSIONS).join(",")).split(",");
  const sizes = arg("--sizes", "10K,100K,1M,10M").split(",");
  const date = new Date().toISOString().slice(0, 10);
  const out = arg("--out", path.join(BENCH, "results", `${date}-${os.hostname().split(".")[0].toLowerCase()}-vscode.json`));
  openLog();
  log(`log: ${logPath}`);
  const limitMs = Number(arg("--limit", "60")) * 1000;
  log(`VS Code ${VSCODE_VERSION}: downloading if needed`);
  const vscodePath = await downloadAndUnzipVSCode({ version: VSCODE_VERSION, cachePath: CACHE });
  // Only the CLI's path: the arguments that come with it name default
  // folders in the working directory.
  const [cli] = resolveCliArgsFromVSCodeExecutablePath(vscodePath);

  // The workload files, the same as run.py's, plus a small one that starts the server.
  const workspace = fs.mkdtempSync(path.join(os.tmpdir(), "lspbench-vscode-"));
  const write = (name, size) => {
    const py = (...a) => execFileSync("python3", [path.join(BENCH, "workload.py"), size, ...a],
                                      { maxBuffer: 64 << 20 });
    fs.writeFileSync(path.join(workspace, `${name}.sql`), py());
    fs.writeFileSync(path.join(workspace, `${name}.json`), py("--json"));
  };
  write("bench-warmup", "10K");
  for (const size of sizes) write(`bench-${size}`, size);

  const results = { date, vscode: VSCODE_VERSION, machine: `${os.cpus()[0].model}, ${os.cpus().length} cores`,
                    extensions: {} };
  // --append: keep the other extensions already in --out, replace these.
  if (process.argv.includes("--append") && fs.existsSync(out)) {
    const kept = JSON.parse(fs.readFileSync(out, "utf8")).extensions || {};
    for (const [n, e] of Object.entries(kept)) if (!names.includes(n)) results.extensions[n] = e;
  }
  for (const name of names) {
    const ext = EXTENSIONS[name];
    if (!ext) throw new Error(`unknown extension ${name}; known: ${Object.keys(EXTENSIONS).join(", ")}`);
    const extDir = path.join(CACHE, `extensions-${name}`);
    // Installed: listed in the folder's extensions.json (a failed install
    // leaves an empty list).
    const registry = path.join(extDir, "extensions.json");
    const installed = fs.existsSync(registry) && JSON.parse(fs.readFileSync(registry, "utf8"))
      .some((e) => e.identifier.id.toLowerCase() === ext.id && e.version === ext.version);
    // The profile stays (benchmarks/.vscode-test/profile-<name>): an
    // extension's first-start pages (welcome, telemetry question) and their
    // "done" marks are kept in it, so they show only on the start that
    // follows its install.  The benchmark's settings are written every run.
    const profile = path.join(CACHE, `profile-${name}`);
    fs.mkdirSync(path.join(profile, "User"), { recursive: true });
    fs.writeFileSync(path.join(profile, "User", "settings.json"), JSON.stringify({
      "files.associations": { "*.sql": ext.language },
      "workbench.startupEditor": "none",
      "workbench.welcomePage.walkthroughs.openOnInstall": false,
      "telemetry.telemetryLevel": "off",
      "extensions.autoUpdate": false,
      "security.workspace.trust.enabled": false,
      ...ext.settings(),
    }, null, 2));

    // One VS Code with the extension, at most --limit seconds: `sizes` measured
    // into `partial` (none: the first start after an install, measuring nothing).
    const startVsCode = async (sizesToRun, partial) => {
      let timer;
      let cutOff = false;
      try {
        const limit = new Promise((_, reject) => {
          timer = setTimeout(() => {
            cutOff = true;
            killVsCode();
            reject(new Error(`still running after ${limitMs / 1000} s: VS Code closed`));
          }, limitMs);
        });
        await Promise.race([limit, runTests({
          vscodeExecutablePath: vscodePath,
          extensionDevelopmentPath: path.join(HERE, "bench-extension"),
          extensionTestsPath: path.join(HERE, "bench-extension", "extension.js"),
          launchArgs: [workspace, "--extensions-dir", extDir, "--user-data-dir", profile, "--disable-gpu"],
          extensionTestsEnv: { BENCH_CONFIG: JSON.stringify({
            extension: name, sizes: sizesToRun, out: partial, log: logPath, timeoutMs: 30_000, editRounds: 10,
            diagnostics: ext.diagnostics !== false,
            formatSizes: ["1M"] }) },
        })]);
      } catch (e) {
        log(`${name}: ${e.message || e}`);
      } finally {
        clearTimeout(timer);
        killVsCode();
      }
      return cutOff;
    };

    if (!installed) {
      const source = ext.vsix ? ext.vsix() : `${ext.id}@${ext.version}`;
      log(`${name}: installing ${source}`);
      execFileSync(cli, ["--extensions-dir", extDir, "--user-data-dir", path.join(CACHE, "cli-user-data"),
                         "--install-extension", source], { stdio: "inherit" });
    }
    // The first start in this profile (after the install, or with a new
    // profile), not measured: the extension's welcome pages show here, once.
    const firstStart = path.join(profile, "first-start.json");
    if (!fs.existsSync(firstStart)) {
      log(`${name}: first start in its profile, not measured (welcome pages show here, once)`);
      await startVsCode([], firstStart);
    }

    // One VS Code run per size, each at most --limit seconds.
    const entry = results.extensions[name] = { label: ext.label, version: ext.version, sizes: {} };
    for (const size of sizes) {
      const partial = path.join(profile, `result-${size}.json`);
      fs.rmSync(partial, { force: true });
      log(`${name} ${size}: starting VS Code with ${ext.label} ${ext.version} (limit ${limitMs / 1000} s)`);
      const cutOff = await startVsCode([size], partial);
      const measured = fs.existsSync(partial) ? JSON.parse(fs.readFileSync(partial, "utf8")) : {};
      if (entry.cold_start_ms === undefined) entry.cold_start_ms = measured.cold_start_ms ?? null;
      const m = (measured.sizes && measured.sizes[size]) || {};
      if (cutOff) m.cut_off_after_s = limitMs / 1000;
      if (measured.error) m.error = measured.error;
      entry.sizes[size] = m;
      fs.mkdirSync(path.dirname(out), { recursive: true });
      fs.writeFileSync(out, JSON.stringify(results, null, 2) + "\n");
      if (ext.diagnostics !== false && (m.open_timed_out_s || m.open_ms === undefined)) {
        log(`${name}: no open result at ${size}, larger sizes skipped`);
        break;
      }
    }
  }
  fs.rmSync(workspace, { recursive: true, force: true });
  log(`wrote ${out}`);
}

main().catch((e) => { console.error(e); process.exit(1); });
