---
description: Re-run the language server benchmarks on this machine, commit the results through a PR and update the slide deck. Usage: /run-benchmarks [notes, e.g. the machine or a .vsix path]
---

# Run the benchmarks

Re-run the language server benchmarks in this repository (mariadb-corporation/lsp-server, on main) on this machine, and update the results and the slide deck with its numbers. Earlier results are the newest files in benchmarks/results/ (the README's "## Results" section names them and their machine); keep those files and add new ones.

$ARGUMENTS

Start by reading benchmarks/README.md: it explains both harnesses, what they measure, the pinned versions and the commands. Use the benchmarks exactly as committed: same workload, servers, versions, sizes and limits. Change nothing in how they measure.

Find the machine with `sysctl -n machdep.cpu.brand_string`, `sysctl -n hw.ncpu hw.memsize` and `sw_vers`. The results files are named `<date>-<host>`, from the host name.

## 1. Prepare

- Build MariaDB LSP in Release mode into build-release/:
  cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DWITH_TESTS=0
  cmake --build build-release --target mariadb-lsp
  Add the usual toolchain flags for this machine; take them from the CMakeCache.txt of an existing build/ if there is one. On macOS that is at least `-DWITH_VCPKG_TRIPLET=arm64-osx` (without it configure fails to find RapidJSON) and `-DBISON_EXECUTABLE=$(brew --prefix bison)/bin/bison`. If the configure step fetches the MariaDB server tree, let it; check that the server tree it links (../mariadb-server-lsp/bld) is a Release build.
  Then export MARIADB_LSP=$PWD/build-release/bin/mariadb-lsp and check `$MARIADB_LSP --version`.
- Node.js and npm are needed (sql-language-server, the VS Code harness). If they are missing, ask me to install them (`brew install node`).
- Install the competing servers: python3 benchmarks/install.py. Install the VS Code harness: cd benchmarks/vscode && npm install.
  If install.py stops part way, re-run it for every server not yet in benchmarks/.servers/, sqlfluff included (it comes last, and the sqlfluff VS Code extension needs it). Compare `ls benchmarks/.servers` with the list install.py knows.
  Claude Code's auto mode blocks downloading and running third-party binaries. If that happens, don't try to work around it: give me the exact command and I will run it.
- The VS Code run installs the MariaDB extension from its .vsix: $MARIADB_VSIX, else ../vscode-extension/mariadb-shell-<version>.vsix next to this checkout (benchmarks/vscode/run.js names the version). If neither exists, ask me where it is.
- Check what each binary is built for with `file`. The sqls release is x86_64 only and runs under Rosetta; the results note that.

## 2. Run, so that I can follow it at any time

Start every run as a tracked background task. Tell me to follow it with: tail -f benchmarks/.runs/latest.log
Both harnesses log every step there. Run them one after the other, never at the same time: they share the CPU.

  python3 benchmarks/run.py
  node benchmarks/vscode/run.js
  python3 benchmarks/charts.py benchmarks/results/<date>-<host>

The limits are deliberate; don't raise them:
- every wait gives up after 30 s;
- every server process, and every VS Code run, is ended after 60 s;
- sqls and sqruff run only up to 100 KB (max_size in servers.py, with the reasons).

The VS Code run opens real VS Code windows. Each extension first gets one unmeasured start in its own profile, which is where its welcome pages appear (Oracle's welcome tab, a telemetry question); that is expected.

If one server or extension fails in a way that looks like a one-off, re-run just that one with --append. Earlier, mssql once got no error on its warm-up file, Oracle once got cut off at 1 MB, and sqlfluff failed because it was not installed; all worked on a re-run.

## 3. Check the results before using them

Read results/<date>-<host>.md and the -vscode.json.
- Compare them with the earlier results. Each figure should be similar or better; any big jump in either direction needs a look before you report it. To check whether a jump repeats without touching the results, re-run those servers with `--out` into a scratch file.
- Look at the six SVG charts by rendering them: qlmanage -t -s 1520 -o <dir> <file.svg>, then read the PNG.

## 4. Commit through a PR

Create a branch from main, add the new results/ files (the json, md and svg files), commit, push, and open a PR.
- Commit messages get no Co-Authored-By line.
- If the PR touches only results files, put [bypass-ci] in its title.
- Update the "## Results" section of benchmarks/README.md to the new files and machine. That still touches docs only.
- Ask me before pushing.

## 5. Update the slides

The deck is https://claude.ai/artifact/GC2rd5ZKJjN6sG5agTQSiZ
Read it with the Artifact tool's read action first, all of it: project/deck.json and every project/slides/*.html.
It has five pages, in this order: summary, results, charts-lsp, vscode, charts-vscode.

Keep the structure, wording and layout; change only numbers, the machine and versions. Replace the earlier machine's name with this one's wherever it appears (setup lines, footers, speaker notes in <aside>).
- summary: the three headline figures and the competitor figures under them, and the speaker notes.
- results: the whole table, and the figures in the text and in the notes.
- vscode: the whole table, and the figures in the cards and notes.
- charts-lsp and charts-vscode: the images, the alt texts and the sentence under the VS Code charts (its "N times MariaDB's memory" needs recomputing).
Take every number from the new results files. Memory figures are MiB labelled "MB"; GB means MB / 1024.

Charts on slides: make light-only copies with
  python3 benchmarks/charts.py benchmarks/results/<date>-<host> --light <scratch dir>
These copies have their colours written as attributes on each element, with no <style>. That matters: the deck appears to strip the <style> from SVG images, and the normal charts then showed as black boxes.
1. Upload the six copies as assets to the deck (publish with asset: true and file_paths).
2. Point the six <img src="/_blob/…"> in charts-lsp.html and charts-vscode.html at the new URLs.
3. Publish the changed slides.
4. Delete the six old assets the slides used before.
Then ask me to look at pages 3 and 5.

## 6. Clean up

When I confirm the deck and the PR is merged, delete everything that was downloaded or generated, and nothing else:
- benchmarks/.servers, benchmarks/.vscode-test, benchmarks/.runs, benchmarks/vscode/node_modules, benchmarks/__pycache__, build-release;
- ~/Library/Caches/dev.supabase-community.pgls (postgres-language-server's logs);
- any lspbench-* folders left in the system temp folder.
Use literal paths, never a shell variable in rm.
