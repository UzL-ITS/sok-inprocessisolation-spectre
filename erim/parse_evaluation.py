#!/usr/bin/env python3
"""Collect the ERIM leakage and performance results into two CSV files.

* leakage CSV     -- one row per variant/configuration from leaks_<variant>.txt.
                     score = runs with leakage, total = runs, leakage = 1 if any run leaked.
* performance CSV -- one row per switch type and size from context_switch_meas_<SIZE>.txt,
                     summarising the raw "[<category>]: <cycles>" samples.
"""

import argparse
import csv
import re
from pathlib import Path

SCENARIOS = {"SCCA", "TCA", "STB"}
MEASURES = {"exec prev": "Execution prevention"}

# "STB + exec prev: Should not leak"
HEADER_RE = re.compile(r"^(.+?):\s*Should")
# "3, 50000, 18 <--"
RESULT_RE = re.compile(r"^\s*(\d+),\s*(\d+),\s*(\d+)")
# "[ERIM+exec:U->T (64)]: 170"
SAMPLE_RE = re.compile(r"^\[(.+)\]:\s*(\d+)$")


def parse_leakage(path: Path):
    """Return {(scenario, measure): [leaked symbols per run]} for one leaks file."""
    configs = {}
    current = None
    for line in path.read_text().splitlines():
        header = HEADER_RE.match(line)
        if header:
            scenario, _, measure = (p.strip() for p in header.group(1).partition("+"))
            if scenario not in SCENARIOS:
                current = None
                continue
            current = configs.setdefault((scenario, MEASURES.get(measure, measure)), [])
            continue
        result = RESULT_RE.match(line)
        if result and current is not None:
            current.append(int(result.group(3)))
    return configs


def write_leakage_csv(files, path: Path):
    with path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["sandbox", "variant", "scenario", "security-measure",
                         "score", "total", "leakage"])
        for f in files:
            variant = f.stem.split("_", 1)[1].upper()
            for (scenario, measure), runs in parse_leakage(f).items():
                leaky = sum(1 for r in runs if r > 0)
                writer.writerow(["erim", variant, scenario, measure,
                                 leaky, len(runs), 1 if leaky else 0])


def quantile(values, q):
    """Linear-interpolated quantile of a sorted list."""
    pos = (len(values) - 1) * q
    lo = int(pos)
    hi = min(lo + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (pos - lo)


def write_performance_csv(files, path: Path):
    samples = {}
    for f in files:
        with f.open() as fh:
            for line in fh:
                match = SAMPLE_RE.match(line.strip())
                if match:
                    samples.setdefault(match.group(1), []).append(int(match.group(2)))

    with path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["category", "iterations", "mean", "stddev",
                         "min", "Q1", "median", "Q3", "max"])
        for category, values in samples.items():
            values.sort()
            n = len(values)
            mean = sum(values) / n
            stddev = (sum((v - mean) ** 2 for v in values) / n) ** 0.5
            writer.writerow([category, n, f"{mean:.2f}", f"{stddev:.2f}", values[0],
                             f"{quantile(values, 0.25):.2f}", f"{quantile(values, 0.5):.2f}",
                             f"{quantile(values, 0.75):.2f}", values[-1]])
    return len(samples)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dir", default=".",
                        help="directory containing the result files (default: %(default)s)")
    parser.add_argument("--leakage-csv", default="leakage.csv",
                        help="output path for the leakage CSV (default: %(default)s)")
    parser.add_argument("--performance-csv", default="performance.csv",
                        help="output path for the performance CSV (default: %(default)s)")
    args = parser.parse_args(argv)

    result_dir = Path(args.dir)
    leak_files = sorted(result_dir.glob("leaks_*.txt"))
    perf_files = sorted(result_dir.glob("context_switch_meas_*.txt"),
                        key=lambda f: int(f.stem.rsplit("_", 1)[1]))

    write_leakage_csv(leak_files, Path(args.leakage_csv))
    n_perf = write_performance_csv(perf_files, Path(args.performance_csv))

    print(f"leakage rows from {len(leak_files)} files -> {args.leakage_csv}")
    print(f"performance rows ({n_perf}) from {len(perf_files)} files -> {args.performance_csv}")


if __name__ == "__main__":
    main()
