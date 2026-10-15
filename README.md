# MariaDB LSP Server

The MariaDB LSP Server provides code editors and agentic coding tools with high-performance SQL language services. It is part of MariaDB Shell and the MariaDB AI Plugins project.

The implementation plan is in [docs/plans/core-implementation.md](docs/plans/core-implementation.md).

## Building

The build follows MariaDB Shell's. It needs CMake 3.22 or newer, Ninja, bison 3.8 or newer (Homebrew bison on macOS), git, and a C++23 compiler.

```bash
cmake -S . -B bld -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_VCPKG_TRIPLET=<triplet>
cmake --build bld
ctest --test-dir bld
```

- `-DWITH_VCPKG_TRIPLET=<triplet>` (for example `arm64-osx`, `x64-linux-dynamic`) fetches vcpkg into `../vcpkg`, or uses `VCPKG_ROOT`, and installs the dependencies in `vcpkg.json`. `-DWITH_TESTS=0` leaves GoogleTest out.
- The MariaDB client library is built from a MariaDB server tree, as in MariaDB Shell. Without options the configure step fetches the server at the commit `libs/mariadbparser` is pinned to into `../mariadb-server-lsp` and builds only the client targets there. To use an existing server build instead, pass `-DMARIADB_SOURCE_DIR=<src> -DMARIADB_BUILD_DIR=<bld>`, with `mariadbclient mysys mysys_ssl caching_sha2_password GenError` built in it. `-DWITH_MARIADB_CLIENT=OFF` builds without the client library.

## Command line

```bash
mariadb-lsp check FILE...         # syntax errors as FILE:LINE:COL, exit 1 if any
mariadb-lsp check --format json FILE...
mariadb-lsp outline FILE          # the schema objects the file defines, as JSON
mariadb-lsp split FILE            # the statements: position, kind, delimiter, sql_mode
mariadb-lsp split --format json --text FILE
mariadb-lsp tokens FILE           # the tokens of every statement
mariadb-lsp tree FILE             # the parse tree of every statement
```

`--sql-mode`, `--delimiter` and `--version-id` set the state at the start of the file; `SET sql_mode` and `DELIMITER` in the file are followed from there. `mariadb-lsp --help` lists all options.

Files are read as UTF-8. A Latin-1 file is converted to UTF-8 first when `--charset latin1` is given, or when it declares `SET NAMES latin1` (also in a versioned comment, as mysqldump writes it) before its first byte that is not UTF-8. Line and column numbers stay those of the original file. Any other file that is not valid UTF-8 is an error naming the first such byte, with exit status 2.

## Layout

| Path | What it is |
| --- | --- |
| `libs/mariadbparser/` | the MariaDB server grammar and lexer as a standalone C library ([README](libs/mariadbparser/README.md)) |
| `libs/sqlsplitter/` | MariaDB Shell's SQL statement splitter, maintained by hand ([UPSTREAM.md](libs/sqlsplitter/UPSTREAM.md)) |
| `libs/mariadblanguageserver/` | the language server engine: statement index, line map, sql_mode tracking, parser pool, syntax check, parse trees, schema catalog, outline |
| `src/mariadb-lsp/` | the `mariadb-lsp` command: `check`, `outline`, `split`, `tokens`, `tree` |
| `tests/fixtures/sql/` | SQL files used by tests (sakila schema, world, a mysqldump file) |
| `scripts/gen_fixture_dump.py` | writes a mysqldump-style file of a given size for benchmarks |
| `unittest/` | GoogleTest suites, built into one `run_unit_tests` binary |
| `cmake/` | build modules, most copied from MariaDB Shell |
| `docs/plans/` | the implementation plan |
| `docs/outline-format.md` | the outline JSON format |

Contributing information can be found in the [CONTRIBUTING.md](CONTRIBUTING.md) file.

## License

This project is part of the MariaDB Shell project and is therefore licensed under
**GPL-2.0** — see [LICENSE](LICENSE)
