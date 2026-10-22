#!/usr/bin/env python3
"""Pivot the long-format leakage CSV into the wide matrix used by the LaTeX table.

`leakage.csv` has one row per evaluated primitive:

    sandbox,variant,scenario,security-measure,score,total,leakage

`leakage_table.tex` wants a 5x5 matrix instead: one row per scenario/measure
combination, one column per Spectre variant, each cell holding the leakage flag
(0 = no leakage, 1 = leakage). This script performs that reshape.

The RSB "Return" measurements are intentionally NOT used: the table reflects the
plain (non-Return) scenarios only.
"""

import argparse
import csv
from pathlib import Path

# Column order of the output matrix (the Spectre variants).
VARIANTS = ["PHT", "BTB", "RSB", "STL"]

# Output rows: (display label, scenario, security-measure to match).
ROWS = [
    ("SCCA",                         "SCCA", ""),
    ("TCA",                          "TCA",  ""),
    ("TCA + Serialization",          "TCA",  "Serialization"),
    ("STB",                          "STB",  ""),
    ("STB + Execution prevention",   "STB",  "Execution Prevention"),
]


def load_leakage(path: Path):
    """Return {(variant, scenario, measure_lower): leakage} from the long CSV."""
    table = {}
    with path.open(newline="") as fh:
        for r in csv.DictReader(fh):
            key = (r["variant"],
                   r["scenario"].strip(),
                   r["security-measure"].strip().lower())
            table[key] = r["leakage"].strip()
    return table


def build_matrix(table):
    """Yield matrix rows: [label, PHT, BTB, RSB, STL]."""
    for label, scenario, measure in ROWS:
        cells = []
        for variant in VARIANTS:
            key = (variant, scenario, measure.lower())
            if key not in table:
                raise KeyError(f"no leakage entry for {key}")
            cells.append(table[key])
        yield [label, *cells]


def write_matrix(rows, path: Path):
    """Write the wide CSV. Multi-word scenario labels are wrapped in braces so
    pgfplotstable treats them as a single cell."""
    with path.open("w", newline="") as fh:
        fh.write("Scenario," + ",".join(VARIANTS) + "\n")
        for label, *cells in rows:
            if " " in label:
                label = "{" + label + "}"
            fh.write(",".join([label, *cells]) + "\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("leakage_csv", nargs="?", default="leakage.csv",
                        help="input long-format CSV (default: %(default)s)")
    parser.add_argument("-o", "--out", default="leakage_matrix.csv",
                        help="output wide-matrix CSV (default: %(default)s)")
    args = parser.parse_args(argv)

    table = load_leakage(Path(args.leakage_csv))
    rows = list(build_matrix(table))
    write_matrix(rows, Path(args.out))

    print(f"wrote {args.out}")
    print("Scenario".ljust(28) + "".join(v.rjust(5) for v in VARIANTS))
    for label, *cells in rows:
        print(label.ljust(28) + "".join(c.rjust(5) for c in cells))


if __name__ == "__main__":
    main()
