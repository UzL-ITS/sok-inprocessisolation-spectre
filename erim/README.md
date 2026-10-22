# ERIM Artifact
This directory contains the artifact for all experiments conducted for ERIM.

## Setup
Run `make` in this folder:
```bash
make
```
If [ERIM](https://github.com/vahldiek/erim) is not present in `./erim`, the Makefile clones it automatically. It then builds the required ERIM libraries and compiles our programs.

If you already have ERIM cloned somewhere else, point the Makefile to it:
```bash
make ERIM_DIR=/path/to/erim
```
You can test if ERIM works on your machine by running `make` in `erim/src/erim/testinlined`.

Use `make clean` to remove our binaries and `make distclean` to additionally clean the ERIM build output.

## Running everything
`run_all.sh` runs all PoCs and the benchmark (64 and 4096 pages) and then collects the results into CSV files. Like the other scripts, it takes the CPU core as the first argument (default: 1):
```bash
./run_all.sh      # runs on core 1
./run_all.sh 3    # runs on core 3
```
This produces:
* `leakage.csv`: one row per variant and configuration. `score` is the number of runs with leakage, `total` the number of runs, and `leakage` is 1 if any run leaked.
* `leakage_matrix.csv`: the leakage flags as a matrix (configurations × variants), in the same format as for TME-Box. Configurations not measured for ERIM (TCA + Serialization) are marked with `-`.
* `performance.csv`: mean, standard deviation and five-number summary (in cycles) per switch type and page count.

The CSV files can also be regenerated from existing result files with `python3 parse_evaluation.py` followed by `python3 make_leakage_matrix.py`.

## PoCs
Each Spectre variant has its own run script:

| Script       | Attack           | Output file      |
|--------------|------------------|------------------|
| `run_pht.sh` | Spectre-PHT      | `leaks_pht.txt`  |
| `run_btb.sh` | Spectre-BTB      | `leaks_btb.txt`  |
| `run_rsb.sh` | Spectre-RSB      | `leaks_rsb.txt`  |
| `run_stl.sh` | Spectre-STL      | `leaks_stl.txt`  |

The scripts pin the PoC to a CPU core with `taskset`. The core can be passed as the first argument and defaults to core 1:
```bash
./run_pht.sh      # runs on core 1
./run_pht.sh 3    # runs on core 3
```

Each script rebuilds and runs the PoC in four configurations, which are separated by an empty line in the output file. Each line contains three numbers per program execution:
1. Scenario
2. Number of iterations the Spectre primitive was used
3. Number of leaked symbols

Lines with leakage are marked with `<--`. A configuration without leakage should show 0 leaked symbols over all runs.

### IBPB for Spectre-BTB
Testing IBPB for Spectre-BTB requires a change in our ERIM wrapper header `spex.h`: in `switchU` and `switchT`, comment in the `write_msr` call and comment out the `mprotect` call. Then Scenario 3 should also show no leakage.

## Benchmark
Run `micro_bench.sh` to measure the ERIM switches, both for plain ERIM and for ERIM extended with execution prevention. The first argument is the number of pages protected by execution prevention (default: 4096), the second the CPU core (default: 1). The results are written to `context_switch_meas_<SIZE>.txt`:
```bash
./micro_bench.sh          # 4096 pages on core 1 -> context_switch_meas_4096.txt
./micro_bench.sh 64 3     # 64 pages on core 3   -> context_switch_meas_64.txt
```
The size can also be set directly when building: `make BENCHMARK_SIZE=64`.
