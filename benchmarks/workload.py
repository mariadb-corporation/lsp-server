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

"""The benchmark's SQL script: plain SQL that MariaDB, MySQL, SQL Server
(T-SQL) and PostgreSQL all accept, so that every server parses the same
text.

Blocks of CREATE TABLE, single-row INSERTs, a multi-line SELECT with JOIN
and GROUP BY, an UPDATE and a DELETE repeat until the size is reached; the
last line holds one syntax error (`SELEC`), so a server reports it only
after parsing the whole file.  No backticks, double-quoted identifiers,
AUTO_INCREMENT, IDENTITY, LIMIT or TOP; single-row INSERTs, since
sql-language-server's parser takes no multi-row VALUES.  Deterministic."""

from dataclasses import dataclass, field

ROWS = 8  # INSERTs per table and block

SIZES = {"10K": 10_000, "100K": 100_000, "1M": 1_000_000, "10M": 10_000_000}


@dataclass
class Workload:
    text: str
    error_line: int                 # the last line's syntax error
    edit_line: int                  # a SELECT in the middle, broken and fixed by the edits
    # (line, character) right after `FROM ` and `WHERE ` of SELECTs spread
    # over the file, for completion
    completion_points: list = field(default_factory=list)


def block(n):
    lines = [
        f"CREATE TABLE customer_{n} (",
        "  id INT NOT NULL PRIMARY KEY,",
        "  name VARCHAR(100) NOT NULL,",
        "  city VARCHAR(50),",
        "  created DATE",
        ");",
        f"CREATE TABLE orders_{n} (",
        "  id INT NOT NULL PRIMARY KEY,",
        "  customer_id INT NOT NULL,",
        "  amount DECIMAL(10,2),",
        "  status VARCHAR(20)",
        ");",
    ]
    for r in range(1, ROWS + 1):
        lines.append(f"INSERT INTO customer_{n} (id, name, city, created) "
                     f"VALUES ({r}, 'Customer {n}-{r}', 'City {r % 5}', '2026-0{1 + r % 9}-1{r % 10}');")
    for r in range(1, ROWS + 1):
        lines.append(f"INSERT INTO orders_{n} (id, customer_id, amount, status) "
                     f"VALUES ({r}, {1 + r % ROWS}, {r * 37 % 500}.{r % 100:02d}, "
                     f"'{('open', 'paid', 'cancelled')[r % 3]}');")
    select_at = len(lines)
    lines += [
        "SELECT c.name, COUNT(o.id) AS orders, SUM(o.amount) AS total",
        f"FROM customer_{n} c",
        f"JOIN orders_{n} o ON o.customer_id = c.id",
        "WHERE o.status = 'open'",
        "GROUP BY c.name",
        "ORDER BY total DESC;",
        f"UPDATE orders_{n} SET status = 'paid' WHERE amount > 100;",
        f"DELETE FROM orders_{n} WHERE status = 'cancelled';",
        "",
    ]
    return lines, select_at


def generate(size):
    """The workload of about `size` bytes."""
    lines, selects, total, n = [], [], 0, 0
    while total < size:
        b, select_at = block(n)
        selects.append(len(lines) + select_at)
        lines += b
        total += sum(len(x) + 1 for x in b)
        n += 1
    error_line = len(lines)
    lines.append("SELEC name FROM customer_0;")
    edit_line = selects[len(selects) // 2]
    # Up to 10 SELECTs evenly spread; the cursor after `FROM ` and after `WHERE `.
    step = max(1, len(selects) // 10)
    points = []
    for s in selects[::step][:10]:
        points.append((s + 1, len("FROM ")))
        points.append((s + 3, len("WHERE ")))
    return Workload("\n".join(lines) + "\n", error_line, edit_line, points)


if __name__ == "__main__":
    # workload.py SIZE: the script; workload.py SIZE --json: its positions
    import json
    import sys
    w = generate(SIZES.get(sys.argv[1], 10_000) if len(sys.argv) > 1 else 10_000)
    if "--json" in sys.argv:
        print(json.dumps({"error_line": w.error_line, "edit_line": w.edit_line,
                          "completion_points": w.completion_points}))
    else:
        sys.stdout.write(w.text)
