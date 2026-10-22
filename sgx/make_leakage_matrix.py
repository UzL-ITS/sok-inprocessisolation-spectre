#!/usr/bin/env python3
"""Pivot leakage.csv into one matrix per flavor (enclave attacker, host attacker),
with one row per configuration and one column per variant.

Cells hold the leakage flag (0 = no leakage, 1 = leakage), or "-" if the
configuration was not measured for SGX (e.g. BTB STB, or the security measures).
"""

import argparse
import csv
from pathlib import Path

VARIANTS = ["PHT", "BTB", "RSB", "STL"]

# sandbox in leakage.csv -> suffix of the output file
FLAVORS = {"sgx-enclave-attacker": "enclave_attacker",
           "sgx-host-attacker": "host_attacker"}

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
    parser.add_argument("-o", "--out-prefix", default="leakage_matrix",
                        help="output prefix, writes <prefix>_<flavor>.csv (default: %(default)s)")
    args = parser.parse_args(argv)

    with open(args.leakage_csv, newline="") as fh:
        table = {(r["sandbox"], r["variant"], r["scenario"], r["security-measure"].lower()): r["leakage"]
                 for r in csv.DictReader(fh)}

    for sandbox, suffix in FLAVORS.items():
        rows = [[label] + [table.get((sandbox, v, scenario, measure.lower()), "-") for v in VARIANTS]
                for label, scenario, measure in ROWS]

        out = Path(f"{args.out_prefix}_{suffix}.csv")
        with out.open("w", newline="") as fh:
            fh.write("Scenario," + ",".join(VARIANTS) + "\n")
            for label, *cells in rows:
                if " " in label:
                    label = "{" + label + "}"
                fh.write(",".join([label, *cells]) + "\n")

        print(f"wrote {out}")
        print("Scenario".ljust(28) + "".join(v.rjust(5) for v in VARIANTS))
        for label, *cells in rows:
            print(label.ljust(28) + "".join(c.rjust(5) for c in cells))
        print()


if __name__ == "__main__":
    main()
