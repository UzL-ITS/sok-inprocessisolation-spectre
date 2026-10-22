#!/usr/bin/env python3
"""Pivot leakage.csv into a matrix with one row per configuration and one column per variant.

Cells hold the leakage flag (0 = no leakage, 1 = leakage), or "-" if the
configuration was not measured for ERIM (e.g. TCA + Serialization).
"""

import argparse
import csv
from pathlib import Path

VARIANTS = ["PHT", "BTB", "RSB", "STL"]

# (display label, scenario, security-measure)
ROWS = [
    ("SCCA",                       "SCCA", ""),
    ("TCA",                        "TCA",  ""),
    ("TCA + Serialization",        "TCA",  "Serialization"),
    ("STB",                        "STB",  ""),
    ("STB + Execution prevention", "STB",  "Execution prevention"),
]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("leakage_csv", nargs="?", default="leakage.csv",
                        help="input CSV (default: %(default)s)")
    parser.add_argument("-o", "--out", default="leakage_matrix.csv",
                        help="output matrix CSV (default: %(default)s)")
    args = parser.parse_args(argv)

    with open(args.leakage_csv, newline="") as fh:
        table = {(r["variant"], r["scenario"], r["security-measure"].lower()): r["leakage"]
                 for r in csv.DictReader(fh)}

    rows = [[label] + [table.get((v, scenario, measure.lower()), "-") for v in VARIANTS]
            for label, scenario, measure in ROWS]

    with Path(args.out).open("w", newline="") as fh:
        fh.write("Scenario," + ",".join(VARIANTS) + "\n")
        for label, *cells in rows:
            if " " in label:
                label = "{" + label + "}"
            fh.write(",".join([label, *cells]) + "\n")

    print(f"wrote {args.out}")
    print("Scenario".ljust(28) + "".join(v.rjust(5) for v in VARIANTS))
    for label, *cells in rows:
        print(label.ljust(28) + "".join(c.rjust(5) for c in cells))


if __name__ == "__main__":
    main()
