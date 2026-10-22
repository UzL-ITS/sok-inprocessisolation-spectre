#!/bin/bash

SIZE=${1:-4096}
CPU=${2:-1}
OUT=context_switch_meas_${SIZE}.txt

# rebuild
make clean && make EXEC_PREV=1 SCENARIO=3 BENCHMARK_SIZE="$SIZE"

# clear output file
echo "" > "$OUT"

# run multiple times
N=100  # Set the number of executions
echo "Running..."
for i in $(seq 1 $N); do
    sudo taskset -c "$CPU" ./micro_bench >> "$OUT" 2>&1
done
