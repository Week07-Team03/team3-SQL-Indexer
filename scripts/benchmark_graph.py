#!/usr/bin/env python3

import csv
import math
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "build"
OUTPUT_DIR = BUILD_DIR / "benchmark"
BENCHMARK_BINARY = BUILD_DIR / "mini_sql"
CSV_PATH = OUTPUT_DIR / "benchmark_results.csv"
SVG_PATH = OUTPUT_DIR / "benchmark_results.svg"
REPORT_PATH = OUTPUT_DIR / "benchmark_report.md"
LOOKUP_COUNT = 200
RECORD_COUNTS = [1000, 10000, 100000, 500000, 1000000]


def run_benchmark(record_count: int) -> dict[str, float]:
    completed = subprocess.run(
        [str(BENCHMARK_BINARY), "--benchmark", str(record_count), str(LOOKUP_COUNT)],
        cwd=ROOT,
        check=True,
        capture_output=True,
        text=True,
    )
    output = completed.stdout

    index_match = re.search(
        r"select by id \(B\+ tree\): ([0-9.]+) sec total, ([0-9.]+) usec/query", output
    )
    linear_match = re.search(
        r"select by name \(linear scan\): ([0-9.]+) sec total, ([0-9.]+) usec/query", output
    )
    speedup_match = re.search(r"speedup: ([0-9.]+)x", output)

    if index_match is None or linear_match is None or speedup_match is None:
        raise RuntimeError(f"Unexpected benchmark output:\n{output}")

    return {
        "record_count": record_count,
        "lookup_count": LOOKUP_COUNT,
        "bptree_total_sec": float(index_match.group(1)),
        "bptree_usec_per_query": float(index_match.group(2)),
        "linear_total_sec": float(linear_match.group(1)),
        "linear_usec_per_query": float(linear_match.group(2)),
        "speedup": float(speedup_match.group(1)),
    }


def write_csv(rows: list[dict[str, float]]) -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    with CSV_PATH.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(
            file,
            fieldnames=[
                "record_count",
                "lookup_count",
                "bptree_total_sec",
                "bptree_usec_per_query",
                "linear_total_sec",
                "linear_usec_per_query",
                "speedup",
            ],
        )
        writer.writeheader()
        writer.writerows(rows)


def map_x(value: float, min_value: float, max_value: float, left: float, width: float) -> float:
    ratio = (math.log10(value) - math.log10(min_value)) / (math.log10(max_value) - math.log10(min_value))
    return left + ratio * width


def map_y(value: float, min_value: float, max_value: float, top: float, height: float) -> float:
    if max_value == min_value:
        return top + height / 2.0
    ratio = (math.log10(value) - math.log10(min_value)) / (math.log10(max_value) - math.log10(min_value))
    return top + height - ratio * height


def build_path(rows: list[dict[str, float]], key: str, min_x: float, max_x: float, min_y: float, max_y: float) -> str:
    left = 90.0
    top = 70.0
    width = 820.0
    height = 420.0
    commands: list[str] = []

    for row in rows:
        x = map_x(float(row["record_count"]), min_x, max_x, left, width)
        y = map_y(float(row[key]), min_y, max_y, top, height)
        commands.append(f"{'M' if not commands else 'L'} {x:.2f} {y:.2f}")

    return " ".join(commands)


def write_svg(rows: list[dict[str, float]]) -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    width = 1000
    height = 620
    left = 90.0
    top = 70.0
    plot_width = 820.0
    plot_height = 420.0
    min_x = float(min(row["record_count"] for row in rows))
    max_x = float(max(row["record_count"] for row in rows))
    min_y = min(min(row["bptree_usec_per_query"], row["linear_usec_per_query"]) for row in rows)
    max_y = max(max(row["bptree_usec_per_query"], row["linear_usec_per_query"]) for row in rows)
    x_ticks = RECORD_COUNTS
    y_ticks = [0.01, 0.1, 1, 10, 100, 1000, 10000]
    y_ticks = [tick for tick in y_ticks if min_y <= tick <= max_y]
    if min_y < 0.01:
        y_ticks.insert(0, min_y)
    if max_y not in y_ticks:
        y_ticks.append(max_y)

    bptree_path = build_path(rows, "bptree_usec_per_query", min_x, max_x, min_y, max_y)
    linear_path = build_path(rows, "linear_usec_per_query", min_x, max_x, min_y, max_y)

    point_elements: list[str] = []
    table_rows: list[str] = []
    for row in rows:
        x = map_x(float(row["record_count"]), min_x, max_x, left, plot_width)
        bptree_y = map_y(float(row["bptree_usec_per_query"]), min_y, max_y, top, plot_height)
        linear_y = map_y(float(row["linear_usec_per_query"]), min_y, max_y, top, plot_height)
        point_elements.append(
            f'<circle cx="{x:.2f}" cy="{bptree_y:.2f}" r="4" fill="#0f766e" />'
        )
        point_elements.append(
            f'<circle cx="{x:.2f}" cy="{linear_y:.2f}" r="4" fill="#dc2626" />'
        )
        table_rows.append(
            f"<tr><td>{int(row['record_count']):,}</td><td>{row['bptree_usec_per_query']:.3f}</td>"
            f"<td>{row['linear_usec_per_query']:.3f}</td><td>{row['speedup']:.2f}x</td></tr>"
        )

    x_grid = []
    for tick in x_ticks:
        x = map_x(float(tick), min_x, max_x, left, plot_width)
        x_grid.append(
            f'<line x1="{x:.2f}" y1="{top:.2f}" x2="{x:.2f}" y2="{top + plot_height:.2f}" stroke="#d4d4d8" stroke-dasharray="4 4" />'
        )
        x_grid.append(
            f'<text x="{x:.2f}" y="{top + plot_height + 24:.2f}" text-anchor="middle" font-size="13" fill="#3f3f46">{tick:,}</text>'
        )

    y_grid = []
    for tick in y_ticks:
        y = map_y(float(tick), min_y, max_y, top, plot_height)
        label = f"{tick:.2f}" if tick < 1 else f"{tick:,.0f}"
        y_grid.append(
            f'<line x1="{left:.2f}" y1="{y:.2f}" x2="{left + plot_width:.2f}" y2="{y:.2f}" stroke="#d4d4d8" stroke-dasharray="4 4" />'
        )
        y_grid.append(
            f'<text x="{left - 12:.2f}" y="{y + 5:.2f}" text-anchor="end" font-size="13" fill="#3f3f46">{label}</text>'
        )

    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">
  <rect width="100%" height="100%" fill="#fffdf8" />
  <text x="60" y="42" font-size="26" font-weight="700" fill="#18181b">B+ Tree vs Linear Scan Benchmark</text>
  <text x="60" y="565" font-size="14" fill="#52525b">X-axis: record count (log scale) / Y-axis: average lookup latency in usec per query (log scale)</text>
  <rect x="{left}" y="{top}" width="{plot_width}" height="{plot_height}" fill="#ffffff" stroke="#a1a1aa" />
  {''.join(x_grid)}
  {''.join(y_grid)}
  <path d="{bptree_path}" fill="none" stroke="#0f766e" stroke-width="3" />
  <path d="{linear_path}" fill="none" stroke="#dc2626" stroke-width="3" />
  {''.join(point_elements)}
  <rect x="690" y="92" width="210" height="56" rx="10" fill="#fafaf9" stroke="#d6d3d1" />
  <line x1="710" y1="112" x2="745" y2="112" stroke="#0f766e" stroke-width="3" />
  <text x="755" y="117" font-size="14" fill="#18181b">B+ tree lookup</text>
  <line x1="710" y1="132" x2="745" y2="132" stroke="#dc2626" stroke-width="3" />
  <text x="755" y="137" font-size="14" fill="#18181b">Linear scan lookup</text>
  <text x="{left + plot_width / 2:.2f}" y="{top + plot_height + 52:.2f}" text-anchor="middle" font-size="15" fill="#18181b">Record count</text>
  <text x="24" y="{top + plot_height / 2:.2f}" transform="rotate(-90 24 {top + plot_height / 2:.2f})" text-anchor="middle" font-size="15" fill="#18181b">Latency (usec/query)</text>
  <foreignObject x="60" y="580" width="880" height="34">
    <table xmlns="http://www.w3.org/1999/xhtml" style="border-collapse:collapse;font:12px sans-serif;color:#18181b;">
      <tr>
        <th style="padding-right:18px;text-align:left;">records</th>
        <th style="padding-right:18px;text-align:left;">B+ tree</th>
        <th style="padding-right:18px;text-align:left;">linear</th>
        <th style="padding-right:18px;text-align:left;">speedup</th>
      </tr>
      {''.join(table_rows)}
    </table>
  </foreignObject>
</svg>
"""
    SVG_PATH.write_text(svg, encoding="utf-8")


def write_report(rows: list[dict[str, float]]) -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    fastest = rows[-1]
    slowest = rows[0]
    lines = [
        "# Benchmark Report",
        "",
        f"- Lookup repetitions per measurement: `{LOOKUP_COUNT}`",
        f"- Smallest dataset: `{int(slowest['record_count']):,}` rows",
        f"- Largest dataset: `{int(fastest['record_count']):,}` rows",
        f"- Largest dataset speedup: `{fastest['speedup']:.2f}x`",
        "",
        "## Files",
        "",
        f"- CSV: `{CSV_PATH.name}`",
        f"- SVG Graph: `{SVG_PATH.name}`",
    ]
    REPORT_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    subprocess.run(["make", str(BENCHMARK_BINARY)], cwd=ROOT, check=True)
    rows = [run_benchmark(record_count) for record_count in RECORD_COUNTS]
    write_csv(rows)
    write_svg(rows)
    write_report(rows)
    print(f"Wrote {CSV_PATH.name}, {SVG_PATH.name}, and {REPORT_PATH.name}")


if __name__ == "__main__":
    main()
