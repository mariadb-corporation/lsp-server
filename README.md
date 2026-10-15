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

## Layout

| Path | What it is |
| --- | --- |
| `libs/mariadbparser/` | the MariaDB server grammar and lexer as a standalone C library ([README](libs/mariadbparser/README.md)) |
| `unittest/` | GoogleTest suites, built into one `run_unit_tests` binary |
| `cmake/` | build modules, most copied from MariaDB Shell |
| `docs/plans/` | the implementation plan |

Contributing information can be found in the [CONTRIBUTING.md](CONTRIBUTING.md) file.

## License

This project is part of the MariaDB Shell project and is therefore licensed under
**GPL-2.0** — see [LICENSE](LICENSE)
