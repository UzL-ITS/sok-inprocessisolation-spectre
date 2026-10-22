#!/usr/bin/env python3
"""Collect the SGX leakage and performance results into two CSV files.

* leakage CSV     -- one row per flavor/variant/scenario from
                     <flavor>/Spectre_<VARIANT>_Scenario_<N>/out.txt.
                     sandbox = sgx-enclave-attacker or sgx-host-attacker,
                     score = runs with leakage, total = runs, leakage = 1 if any run leaked.
* performance CSV -- one row per transition from MeasureBench/out.txt,
                     summarising the raw "[ecall|ocall][<direction>]: <cycles>" samples.
"""

import argparse
import csv
import re
from pathlib import Path

FLAVORS = {"enclave_attacker": "sgx-enclave-attacker",
           "host_attacker": "sgx-host-attacker"}
SCENARIOS = {"1": "SCCA", "2": "TCA", "3": "STB"}

# "Spectre_PHT_Scenario_1"
EXPERIMENT_RE = re.compile(r"^Spectre_([A-Z]+)_Scenario_(\d+)$")
# "10000, 7 <--"
RESULT_RE = re.compile(r"^\s*(\d+),\s*(\d+)")
# "[ecall][u->t]: 1234"
SAMPLE_RE = re.compile(r"^\[(\w+)\]\[(.+?)\]:\s*(\d+)$")


def write_leakage_csv(root: Path, path: Path):
    rows = 0
    with path.open("w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(["sandbox", "variant", "scenario", "security-measure",
                         "score", "total", "leakage"])
        for flavor, sandbox in FLAVORS.items():
            for exp in sorted((root / flavor).glob("Spectre_*")):
                match = EXPERIMENT_RE.match(exp.name)
                out = exp / "out.txt"
                if not match or not out.is_file():
                    continue
                variant, scenario = match.group(1), SCENARIOS.get(match.group(2), match.group(2))
                runs = [int(m.group(2)) for line in out.read_text().splitlines()
                        if (m := RESULT_RE.match(line))]
                if not runs:
                    continue
                leaky = sum(1 for r in runs if r > 0)
                writer.writerow([sandbox, variant, scenario, "",
                                 leaky, len(runs), 1 if leaky else 0])
                rows += 1
    return rows


def quantile(values, q):
    """Linear-interpolated quantile of a sorted list."""
    pos = (len(values) - 1) * q
    lo = int(pos)
    hi = min(lo + 1, len(values) - 1)
    return values[lo] + (values[hi] - values[lo]) * (pos - lo)


def write_performance_csv(bench_out: Path, path: Path):
    samples = {}
    if bench_out.is_file():
        for line in bench_out.read_text().splitlines():
            match = SAMPLE_RE.match(line.strip())
            if match:
                category = f"SGX:{match.group(1)}:{match.group(2)}"
                samples.setdefault(category, []).append(int(match.group(3)))

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
                        help="SGX artifact directory (default: %(default)s)")
    parser.add_argument("--leakage-csv", default="leakage.csv",
                        help="output path for the leakage CSV (default: %(default)s)")
    parser.add_argument("--performance-csv", default="performance.csv",
                        help="output path for the performance CSV (default: %(default)s)")
    args = parser.parse_args(argv)

    root = Path(args.dir)
    n_leak = write_leakage_csv(root, Path(args.leakage_csv))
    n_perf = write_performance_csv(root / "MeasureBench" / "out.txt", Path(args.performance_csv))

    print(f"leakage rows ({n_leak}) -> {args.leakage_csv}")
    print(f"performance rows ({n_perf}) -> {args.performance_csv}")


if __name__ == "__main__":
    main()
