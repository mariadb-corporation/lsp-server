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

"""SVG bar charts of a benchmark run, for the README: one per measure,
MariaDB LSP in blue, the others in gray, each bar labelled with its value;
times on a log scale.  Light and dark follow prefers-color-scheme.

usage: charts.py results/<date>-<host> [--light DIR]
       (reads .json and -vscode.json, writes <date>-<host>-<chart>.svg beside
       them; --light: light-only charts, for slides, into DIR)"""

import json
import math
import re
import pathlib
import sys
from xml.sax.saxutils import escape

# Validated with the dataviz skill's palette checker: blue (categorical slot
# 1) against a neutral gray, >= 3:1 on both surfaces.
STYLE = """
.surface{fill:#fcfcfb}.t1{fill:#0b0b0b}.t2{fill:#52514e}.t3{fill:#6f6e69}
.grid{stroke:#e6e5e1}.axis{stroke:#c9c8c2}.hl{fill:#2a78d6}.ctx{fill:#84837a}
.halo{stroke:#fcfcfb;stroke-width:5px;paint-order:stroke;stroke-linejoin:round}
@media (prefers-color-scheme: dark){
.surface{fill:#1a1a19}.t1{fill:#f0efec}.t2{fill:#c3c2b7}.t3{fill:#9d9c95}
.grid{stroke:#2e2e2c}.axis{stroke:#4a4a46}.hl{fill:#3987e5}.ctx{fill:#76756d}
.halo{stroke:#1a1a19}}
text{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Helvetica,Arial,sans-serif}
"""
# For slides, which are light whatever the viewer's system: no dark block.
LIGHT_STYLE = re.sub(r"@media \(prefers-color-scheme: dark\)\{.*?\}\}", "", STYLE, flags=re.S)

WIDTH, LABEL_W, VALUE_W = 760, 250, 110
PLOT_X = LABEL_W
PLOT_W = WIDTH - LABEL_W - VALUE_W
ROW, BAR, TOP = 34, 16, 70


def fmt_ms(v):
    if v >= 1000:
        return f"{v / 1000:.1f} s"
    if v >= 10:
        return f"{v:,.0f} ms"
    return f"{v:.1f} ms"


SIZE_LABEL = {"10K": "10 KB", "100K": "100 KB", "1M": "1 MB", "10M": "10 MB"}


def fmt_mb(v):
    if v >= 1024:
        gb = v / 1024
        return f"{gb:.0f} GB" if abs(gb - round(gb)) < 0.05 and v % 1024 == 0 else f"{gb:.1f} GB"
    return f"{v:,.0f} MB"


def bar_path(x, y, w, h, r=4):
    """A bar from the baseline at x, its far end rounded."""
    r = min(r, w, h / 2)
    return (f"M{x:.1f},{y:.1f}h{w - r:.1f}a{r},{r} 0 0 1 {r},{r}v{h - 2 * r:.1f}"
            f"a{r},{r} 0 0 1 -{r},{r}h-{w - r:.1f}z")


def chart(title, subtitle, rows, log, fmt, unit, light=False):
    """rows: (label, value or None, note, highlighted); returns the SVG text."""
    values = [v for _, v, _, _ in rows if v]
    if log:
        lo = 10 ** math.floor(math.log10(min(values)))
        hi = 10 ** math.ceil(math.log10(max(values)))
        ticks, t = [], lo
        while t <= hi * 1.0001:
            ticks.append(t)
            t *= 10
        def scale(v):
            return PLOT_W * (math.log10(v) - math.log10(lo)) / (math.log10(hi) - math.log10(lo))
    else:
        top = max(values)
        # Round steps in the unit the labels show (GB from 1024 MB on).
        base = 1024 if top >= 1024 else 1
        step = base * 10 ** math.floor(math.log10(top / base))
        step = next(s for s in (step, 2 * step, 5 * step, 10 * step) if top / s <= 6)
        hi = step * math.ceil(top / step)
        ticks = [i * step for i in range(int(hi / step) + 1)]
        def scale(v):
            return PLOT_W * v / hi
    height = TOP + ROW * len(rows) + 46
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{height}" '
           f'viewBox="0 0 {WIDTH} {height}" role="img" aria-labelledby="t d">',
           f"<title id=\"t\">{escape(title)}</title>",
           "<desc id=\"d\">" + escape("; ".join(
               f"{label}: {fmt(v) if v else note}" for label, v, note, _ in rows)) + "</desc>",
           f"<style>{LIGHT_STYLE if light else STYLE}</style>",
           f'<rect class="surface" width="{WIDTH}" height="{height}" rx="8"/>',
           f'<text class="t1" x="20" y="30" font-size="16" font-weight="600">{escape(title)}</text>',
           f'<text class="t2" x="20" y="50" font-size="12">{escape(subtitle)}</text>']
    plot_bottom = TOP + ROW * len(rows)
    for t in ticks:
        x = PLOT_X + scale(t)
        out.append(f'<line class="grid" x1="{x:.1f}" y1="{TOP - 6}" x2="{x:.1f}" y2="{plot_bottom}" stroke-width="1"/>')
        label = fmt(t).replace(".0 ", " ") if unit == "ms" else fmt(t)
        out.append(f'<text class="t3" x="{x:.1f}" y="{plot_bottom + 18}" font-size="11" text-anchor="middle">{escape(label)}</text>')
    out.append(f'<line class="axis" x1="{PLOT_X}" y1="{TOP - 6}" x2="{PLOT_X}" y2="{plot_bottom}" stroke-width="1"/>')
    for i, (label, v, note, hl) in enumerate(rows):
        y = TOP + i * ROW + (ROW - BAR) / 2
        mid = y + BAR / 2 + 4
        weight = ' font-weight="600"' if hl else ""
        out.append(f'<text class="t1" x="{PLOT_X - 12}" y="{mid:.1f}" font-size="13" text-anchor="end"{weight}>{escape(label)}</text>')
        if v:
            w = max(scale(v), 2)
            out.append(f'<path class="{"hl" if hl else "ctx"}" d="{bar_path(PLOT_X, y, w, BAR)}"/>')
            out.append(f'<text class="t2 halo" x="{PLOT_X + w + 8:.1f}" y="{mid:.1f}" font-size="12"{weight}>{escape(fmt(v))}</text>')
        else:
            out.append(f'<text class="t3 halo" x="{PLOT_X + 8}" y="{mid:.1f}" font-size="12" font-style="italic">{escape(note)}</text>')
    out.append("</svg>")
    return "\n".join(out) + "\n"


def ordered(rows):
    """MariaDB first, then by value, then those without one."""
    first = [r for r in rows if r[3]]
    with_value = sorted((r for r in rows if not r[3] and r[1]), key=lambda r: r[1])
    return first + with_value + [r for r in rows if not r[3] and not r[1]]


def stdio_rows(results, size, measure):
    sizes = results["sizes"]
    rows = []
    for name, srv in results["servers"].items():
        m = srv["sizes"].get(size)
        label = srv["label"] + (" (lints only)" if srv["diagnostics"] == "lint" else "")
        hl = name == "mariadb-lsp"
        value = m.get(measure) if m else None
        if srv["diagnostics"] is None and measure != "memory_after_open_mb":
            note = "no errors without a database"
        elif m is None:
            # Skipped: why, from the size before.
            before = [s for s in sizes[:sizes.index(size)] if s in srv["sizes"]]
            last = srv["sizes"][before[-1]] if before else {}
            if "killed_after_s" in last:
                note = f"killed at {SIZE_LABEL[before[-1]]} (busy after {last['killed_after_s']} s)"
            elif "open_timed_out_s" in last:
                note = f"no errors within {last['open_timed_out_s']} s at {SIZE_LABEL[before[-1]]}"
            else:
                note = "not run"
        elif "not_run" in m:
            note = "not run: " + m["not_run"].split(": ", 1)[-1].split(",")[0]
        elif "killed_after_s" in m:
            note = f"killed, busy after {m['killed_after_s']} s"
        elif "open_timed_out_s" in m and measure != "memory_after_open_mb":
            note = f"no errors within {m['open_timed_out_s']} s"
        elif srv["diagnostics"] == "lint" and measure.startswith("edit"):
            note = "reports no syntax errors"
        else:
            note = "—"
        if "open_timed_out_s" in (m or {}) and measure == "memory_after_open_mb":
            value = None
            note = f"no errors within {m['open_timed_out_s']} s"
        rows.append((label, value, note, hl))
    return ordered(rows)


# Short names for the VS Code charts.
VSCODE_LABEL = {"mariadb": "MariaDB (MariaDB LSP)", "oracle": "Oracle SQL Developer",
                "mssql": "SQL Server (mssql)", "pgsql": "PostgreSQL (Microsoft)"}


def vscode_rows(results, size, measure, sizes=("10K", "100K", "1M", "10M")):
    rows = []
    for name, ext in results["extensions"].items():
        m = ext["sizes"].get(size, {})
        hl = name == "mariadb"
        offline_silent = any(s.get("no_diagnostics") for s in ext["sizes"].values())
        before = [s for s in sizes[:sizes.index(size)] if s in ext["sizes"]]
        if offline_silent and measure == "open_ms":
            note = "no errors without a database"
        elif not m and before and ext["sizes"][before[-1]].get("open_timed_out_s"):
            note = f"no errors within 30 s at {SIZE_LABEL[before[-1]]}"
        elif m.get("open_timed_out_s"):
            note = "no errors within 30 s"
        elif m.get("cut_off_after_s") and measure not in m:
            note = f"run cut off at {m['cut_off_after_s']} s"
        else:
            note = "—"
        rows.append((VSCODE_LABEL.get(name, ext["label"]), m.get(measure), note, hl))
    return ordered(rows)


# The light colours as presentation attributes: some viewers (a slide deck's
# image sanitizer) drop an SVG's <style>, which leaves every mark black.
LIGHT_ATTRS = {
    "surface": 'fill="#fcfcfb"', "t1": 'fill="#0b0b0b"', "t2": 'fill="#52514e"',
    "t3": 'fill="#6f6e69"', "grid": 'stroke="#e6e5e1"', "axis": 'stroke="#c9c8c2"',
    "hl": 'fill="#2a78d6"', "ctx": 'fill="#84837a"',
    "halo": 'stroke="#fcfcfb" stroke-width="5" paint-order="stroke" stroke-linejoin="round"',
}
FONT = 'font-family="-apple-system, BlinkMacSystemFont, Segoe UI, Helvetica, Arial, sans-serif"'


def inline_light(svg):
    """`svg` with its classes turned into attributes and no <style>."""
    svg = re.sub(r"<style>.*?</style>", "", svg, flags=re.S)

    def attrs(m):
        return " ".join(LIGHT_ATTRS[c] for c in m.group(1).split())
    svg = re.sub(r'class="([^"]+)"', attrs, svg)
    return svg.replace("<text ", f"<text {FONT} ")


def main():
    args = sys.argv[1:]
    light_dir = None
    if len(args) == 3 and args[1] == "--light":
        light_dir = pathlib.Path(args[2])
        args = args[:1]
    if len(args) != 1:
        sys.exit(__doc__)
    base = pathlib.Path(args[0])
    light = light_dir is not None
    stdio = json.loads(base.with_suffix(".json").read_text())
    vscode_file = base.parent / (base.name + "-vscode.json")
    machine = stdio["machine"]
    where = f"{machine['cpu']}, {stdio['date']} · lower is better"
    charts = {
        "open-1mb": chart("Open a 1 MB file → its syntax errors, over LSP",
                          f"ms, log scale · {where}",
                          stdio_rows(stdio, "1M", "open_ms"), True, fmt_ms, "ms", light),
        "open-10mb": chart("Open a 10 MB file → its syntax errors, over LSP",
                           f"ms, log scale · {where}",
                           stdio_rows(stdio, "10M", "open_ms"), True, fmt_ms, "ms", light),
        "edit-10mb": chart("Edit in a 10 MB file → the error appears, over LSP",
                           f"ms, log scale (MariaDB LSP without its 150 ms typing pause) · {where}",
                           stdio_rows(stdio, "10M", "edit_appear_ms"), True, fmt_ms, "ms", light),
        "memory-10mb": chart("Memory with a 10 MB file open, over LSP",
                             f"MB resident, all the server's processes · {where}",
                             stdio_rows(stdio, "10M", "memory_after_open_mb"), False, fmt_mb, "MB", light),
    }
    if vscode_file.exists():
        vs = json.loads(vscode_file.read_text())
        vwhere = f"VS Code {vs['vscode']} · {stdio['date']} · lower is better"
        charts["vscode-open-10mb"] = chart("Open a 10 MB file → its syntax errors, inside VS Code",
                                           f"ms, log scale · {vwhere}",
                                           vscode_rows(vs, "10M", "open_ms"), True, fmt_ms, "ms", light)
        charts["vscode-memory-10mb"] = chart("Memory with a 10 MB file open, inside VS Code",
                                             f"MB resident, the processes the extension started · {vwhere}",
                                             vscode_rows(vs, "10M", "memory_after_open_mb"), False, fmt_mb, "MB", light)
    for name, svg in charts.items():
        if light:
            svg = inline_light(svg)
        path = (light_dir or base.parent) / f"{base.name}-{name}.svg"
        path.write_text(svg)
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
