# Language server benchmark, 2026-10-25

Apple M5 Pro, 18 cores, 64 GB, macOS 27.0.1. No database connection.

| Server | Version |
| --- | --- |
| MariaDB LSP | 26.10.1 |
| SQL Tools Service (mssql) | 6.0.20261023.1 |
| sql-language-server | 1.7.1 |
| sqls | 0.2.48 |
| postgres-language-server | 0.28.2 |
| sqruff | 0.43.0 |
| squawk | 2.70.0 |
## Startup (spawn to initialize reply, median)

| Size | MariaDB LSP | SQL Tools Service (mssql) | sql-language-server | sqls | postgres-language-server | sqruff | squawk |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10K | 4.0 ms | 295 ms | 207 ms | 36 ms | 62 ms | 5.3 ms | 6.0 ms |
| 100K | 3.9 ms | 293 ms | 201 ms | 36 ms | 60 ms | 5.2 ms | 5.2 ms |
| 1M | 4.0 ms | 279 ms | 201 ms | skipped | 60 ms | not run | 5.2 ms |
| 10M | 4.1 ms | 277 ms | skipped | skipped | 60 ms | not run | 5.3 ms |

## Open → diagnostics with the last line's error (median)

| Size | MariaDB LSP | SQL Tools Service (mssql) | sql-language-server | sqls | postgres-language-server | sqruff | squawk |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10K | 0.8 ms | 850 ms | 6.9 ms | no diagnostics | 3.3 ms | 29 ms (lints only) | 2.9 ms |
| 100K | 6.1 ms | 896 ms | 19 ms | no diagnostics | 22 ms | 273 ms (lints only) | 26 ms |
| 1M | 24 ms | 1,215 ms | none in 30 s | skipped | 172 ms | not run | 1,182 ms |
| 10M | 85 ms | 3,170 ms | skipped | skipped | 1,684 ms | not run | none in 30 s |

## Edit → diagnostics, error appears / clears (median)

| Size | MariaDB LSP | SQL Tools Service (mssql) | sql-language-server | sqls | postgres-language-server | sqruff | squawk |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10K | 0.1 ms / 0.1 ms | 752 ms / 752 ms | 1.1 ms / 1.7 ms | — | 305 ms / 305 ms | — | 1.8 ms / 1.8 ms |
| 100K | 0.1 ms / 0.1 ms | 759 ms / 759 ms | 6.2 ms / 11 ms | — | 335 ms / 338 ms | — | 25 ms / 25 ms |
| 1M | 0.1 ms / 0.1 ms | 874 ms / 873 ms | — | — | 490 ms / 489 ms | not run | 1,250 ms / 1,234 ms |
| 10M | 0.7 ms / 0.7 ms | 1,788 ms / 1,664 ms | — | — | 1,970 ms / 1,967 ms | not run | — |

## Completion after FROM and WHERE (median)

| Size | MariaDB LSP | SQL Tools Service (mssql) | sql-language-server | sqls | postgres-language-server | sqruff | squawk |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10K | 0.2 ms (10–36 items) | 3.6 ms (535–535 items) | 2.5 ms (5–11 items) | 93 ms (0–0 items) | 0.2 ms (0–0 items) | not offered | 0.7 ms (4–10 items) |
| 100K | 0.3 ms (36–98 items) | 4.0 ms (535–535 items) | 6.5 ms (5–11 items) | no response to request 9 | 0.3 ms (0–0 items) | not offered | 3.0 ms (4–10 items) |
| 1M | 1.0 ms (36–500 items) | 19 ms (535–535 items) | — | — | 0.2 ms (0–0 items) | not run | 22 ms (4–10 items) |
| 10M | 7.0 ms (36–500 items) | 151 ms (535–535 items) | — | — | 0.2 ms (0–0 items) | not run | — |

## Formatting the whole document

| Size | MariaDB LSP | SQL Tools Service (mssql) | sql-language-server | sqls | postgres-language-server | sqruff | squawk |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10K | — | — | — | — | — | — | — |
| 100K | — | — | — | — | — | — | — |
| 1M | 217 ms, 9,044 edits | 413 ms, 0 edits | — | — | 13,654 ms, 46,115 edits | not run | 22 ms, 0 edits |
| 10M | — | — | — | — | — | not run | — |

## Memory, after open / at the end (RSS)

| Size | MariaDB LSP | SQL Tools Service (mssql) | sql-language-server | sqls | postgres-language-server | sqruff | squawk |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 10K | 4 / 6 MB | 150 / 166 MB | 86 / 120 MB | 19 / 28 MB | 38 / 39 MB | 33 / 33 MB | 15 / 18 MB |
| 100K | 5 / 7 MB | 159 / 177 MB | 108 / 219 MB | 20 MB | 59 / 78 MB | 194 / 194 MB | 33 / 61 MB |
| 1M | 8 / 29 MB | 217 / 298 MB | 156 MB | — | 166 / 411 MB | not run | 180 / 981 MB |
| 10M | 40 / 125 MB | 804 / 983 MB | — | — | 381 / 1,111 MB | not run | 478 MB |

## Notes

- sqls, 100K: still busy after 60 s, killed
- sqruff, 1M: not run, larger than 100K: from 1 MB on it stops with one warning, "Maximum parse node count exceeded (limit 100000)"
- sqruff, 10M: not run, larger than 100K: from 1 MB on it stops with one warning, "Maximum parse node count exceeded (limit 100000)"

