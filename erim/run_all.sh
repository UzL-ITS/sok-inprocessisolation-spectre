#!/bin/bash

CPU=${1:-1}

cd "$(dirname "$0")" || exit 1

./run_pht.sh "$CPU"
./run_btb.sh "$CPU"
./run_rsb.sh "$CPU"
./run_stl.sh "$CPU"

./micro_bench.sh 64 "$CPU"
./micro_bench.sh 4096 "$CPU"

python3 parse_evaluation.py
python3 make_leakage_matrix.py
