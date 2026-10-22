#!/usr/bin/env python3
"""Extract leakage and performance results from a tme-box evaluation log.

The log is a docker-compose run capture where each evaluated primitive is
introduced by a banner:

    ################################################################
    ###   <VARIANT> <SCENARIO> [+ <MEASURE> ...]   ################
    ...
    SCORE: <leaked>/<total>     >>> PROTECTED|LEAKY <<<
    [optional performance measurement block]

This script splits every banner title into:
    * variant  -- Spectre variant (PHT, BTB, RSB, STL, ...)
    * scenario -- "Plain" or "Scenario N"
    * measure  -- additional security measure(s) after '+', or "" if none

and writes two CSV files:
    * leakage CSV     -- one row per primitive, leakage = 1 if any bytes
                         leaked (SCORE numerator > 0) else 0
    * performance CSV -- one row per benchmarked primitive, parsed from the
                         "CSV:" rows emitted by benchmarking.c:

      CSV:Category,Iterations,Mean,StdDev,Min,Q1,Median,Q3,Max
      CSV:TME-Box,100000,151.11,116.11,126,134.00,140.00,166.00,28908

      These rows are not necessarily contiguous in the captured log (the
      benchmark's stdout interleaves with its stderr progress messages), so
      they are collected in a dedicated pass over the whole log.
"""

import argparse
import csv
import re
import sys
from pathlib import Path

# Matches every ANSI SGR colour escape, e.g. \x1b[33m or \x1b[0m
ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
# Strips the docker-compose service prefix "builder-1  | "
PREFIX_RE = re.compile(r"^\s*[\w.-]+\s*\|\s?")
# Banner title between the '###' markers, e.g. "###   PHT Scenario 2   ####"
HEADER_RE = re.compile(r"^#+\s+(.+?)\s+#+\s*$")
# "SCORE: 27/100000"
SCORE_RE = re.compile(r"SCORE:\s*(\d+)\s*/\s*(\d+)")
# A performance row, e.g. "CSV:TME-Box,100000,151.11,..." (after the prefix
# and ANSI codes are stripped). The header row starts with "CSV:Category,".
PERF_CSV_RE = re.compile(r"^CSV:(.+)$")
# Column layout emitted by benchmarking.c; used as a fallback when the header
# row is absent from the capture.
PERF_COLUMNS = ["Category", "Iterations", "Mean", "StdDev",
                "Min", "Q1", "Median", "Q3", "Max"]

# Known Spectre variants (first token of the banner title).
VARIANTS = {"PHT", "BTB", "RSB", "STL"}


def clean(line: str) -> str:
    """Remove ANSI colour codes and the docker service prefix from a line."""
    return PREFIX_RE.sub("", ANSI_RE.sub("", line)).rstrip("\n")


def parse_title(title: str):
    """Split a banner title into (variant, scenario, measure).

    Examples:
        "PHT Plain"                              -> ("PHT", "Plain", "")
        "RSB Scenario1"                          -> ("RSB", "Scenario 1", "")
        "PHT Scenario 2 + Serialized Domain Switch"
                          -> ("PHT", "Scenario 2", "Serialized Domain Switch")
        "RSB Scenario 2 + Return + Serialized Domain Switch"
              -> ("RSB", "Scenario 2", "Return + Serialized Domain Switch")
    """
    scenario_name = {
        "Scenario 1": "SCCA",
        "Scenario 2": "TCA",
        "Scenario 3": "STB",
    }
    measure_name = {
        "Serialized Domain Switch": "Serialization",
        "Execution Prevention": "Execution prevention",
    }
    first, _, rest = title.partition(" ")
    variant = first
    parts = [p.strip() for p in rest.split("+")]

    # First part is the scenario; normalise "Scenario1" -> "Scenario 1".
    scenario = parts[0].strip()
    for k,v in scenario_name.items():
        scenario = scenario.replace(k, v)

    # Anything after the first '+' is an additional security measure.
    measure = " + ".join(p for p in parts[1:] if p)
    measure = measure_name.get(measure, measure)
    return variant, scenario, measure


def parse_log(text: str):
    """Return a list of section dicts in the order they appear in the log."""
    sections = []
    current = None

    for raw in text.splitlines():
        line = clean(raw)

        header = HEADER_RE.match(line)
        if header:
            title = header.group(1).strip()
            # Ignore pure '###...' separator lines (no real title content).
            if not title or set(title) <= {"#"}:
                continue
            variant, scenario, measure = parse_title(title)
            # Only keep banners whose first token is a known variant.
            if variant not in VARIANTS:
                continue
            current = {
                "sandbox": "tme-box",
                "variant": variant,
                "scenario": scenario,
                "measure": measure,
                "title": title,
                "leaked": None,
                "total": None,
            }
            sections.append(current)
            continue

        if current is None:
            continue

        score = SCORE_RE.search(line)
        if score and current["leaked"] is None:
            current["leaked"] = int(score.group(1))
            current["total"] = int(score.group(2))
            continue

    return sections


def parse_performance(text: str):
    """Collect benchmark rows from the "CSV:" lines scattered through the log.

    Returns a list of dicts keyed by the (lower-cased) header column names,
    e.g. {"category": "TME-Box", "iterations": "100000", "mean": "151.11", ...}.
    The header row may appear anywhere relative to the data rows, so the whole
    log is scanned and the rows are matched up afterwards.
    """
    columns = None
    data_rows = []

    for raw in text.splitlines():
        line = clean(raw)
        match = PERF_CSV_RE.match(line)
        if not match:
            continue
        body = match.group(1).strip()
        if body.startswith(PERF_COLUMNS[0] + ","):  # the "Category,..." header
            columns = [c.strip() for c in body.split(",")]
        else:
            data_rows.append(body)

    if columns is None:
        columns = PERF_COLUMNS

    records = []
    # rsplit on the metric columns so a category containing commas
    # still keeps its numeric tail aligned to the header.
    n_metrics = len(columns) - 1
    for body in data_rows:
        fields = body.rsplit(",", n_metrics)
        if len(fields) != len(columns):
            continue  # malformed / unexpected row -> skip defensively
        records.append({col.lower(): val.strip()
                        for col, val in zip(columns, fields)})
    return records


def write_leakage_csv(sections, path: Path):
    with path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(
            ["sandbox", "variant", "scenario", "security-measure",
             "score", "total", "leakage"]
        )
        for s in sections:
            if s["leaked"] is None:
                continue
            leakage = 1 if s["leaked"] > 0 else 0
            writer.writerow(
                [s["sandbox"], s["variant"], s["scenario"], s["measure"],
                 s["leaked"], s["total"], leakage]
            )


def write_performance_csv(records, path: Path):
    # Output the five-number summary in box-plot order (min, Q1, median, Q3,
    # max) so the file feeds a box plot directly. Keys are lower-case to match
    # parse_performance(); the header is written with conventional casing.
    header = ["category", "iterations",
              "mean", "stddev", "min", "Q1", "median", "Q3", "max"]
    keys = ["category", "iterations",
            "mean", "stddev", "min", "q1", "median", "q3", "max"]
    with path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(header)
        for rec in records:
            writer.writerow([rec.get(k, "") for k in keys])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", nargs="?", default="evaluation.log",
                        help="path to the evaluation log (default: %(default)s)")
    parser.add_argument("--leakage-csv", default="leakage.csv",
                        help="output path for the leakage CSV")
    parser.add_argument("--performance-csv", default="performance.csv",
                        help="output path for the performance CSV")
    args = parser.parse_args(argv)

    log_path = Path(args.log)
    if not log_path.is_file():
        parser.error(f"log file not found: {log_path}")

    text = log_path.read_text(errors="replace")
    sections = parse_log(text)
    perf_records = parse_performance(text)

    leak_path = Path(args.leakage_csv)
    perf_path = Path(args.performance_csv)
    write_leakage_csv(sections, leak_path)
    write_performance_csv(perf_records, perf_path)

    n_leak = sum(1 for s in sections if s["leaked"] is not None)
    print(f"Parsed {len(sections)} primitives, {len(perf_records)} benchmarks.")
    print(f"  leakage rows     -> {leak_path} ({n_leak})")
    print(f"  performance rows -> {perf_path} ({len(perf_records)})")

    if n_leak == 0:
        print(f"WARNING: no leakage scores found in {log_path}; "
              "is the run complete?", file=sys.stderr)
    if not perf_records:
        print(f"WARNING: no 'CSV:' benchmark rows found in {log_path}; "
              "is the run complete?", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
