#!/bin/bash

cd "$(dirname "$0")" || exit 1

# Usage: ./run_all.sh [CPU]  (CPU core to pin the experiments to, default: 1)
CPU=${1:-1}

# List of predefined folders
experiments=(
    "enclave_attacker/Spectre_PHT_Scenario_1"
    "enclave_attacker/Spectre_PHT_Scenario_2"
    "enclave_attacker/Spectre_PHT_Scenario_3"
    "enclave_attacker/Spectre_BTB_Scenario_1"
    "enclave_attacker/Spectre_BTB_Scenario_2"
    "enclave_attacker/Spectre_RSB_Scenario_1"
    "enclave_attacker/Spectre_RSB_Scenario_2"
    "enclave_attacker/Spectre_RSB_Scenario_3"
    "host_attacker/Spectre_PHT_Scenario_1"
    "host_attacker/Spectre_PHT_Scenario_2"
    "host_attacker/Spectre_PHT_Scenario_3"
    "host_attacker/Spectre_BTB_Scenario_1"
    "host_attacker/Spectre_BTB_Scenario_2"
    "host_attacker/Spectre_RSB_Scenario_1"
    "host_attacker/Spectre_RSB_Scenario_2"
    "host_attacker/Spectre_RSB_Scenario_3"
)

# First loop: execute run.sh in each folder
for experiment in "${experiments[@]}"; do
    echo -e "\nRunning script in: $experiment"

    if [ -x "$experiment/run.sh" ]; then
        (cd "$experiment" && ./run.sh "$CPU")
    else
        echo "run.sh not found or not executable in $experiment"
    fi
done

# Second loop: print folder name and contents of out.txt
for experiment in "${experiments[@]}"; do
    echo -e "\n--- Output from $experiment ---"

    if [ -f "$experiment/out.txt" ]; then
        cat "$experiment/out.txt"
    else
        echo "out.txt not found in $experiment"
    fi

    echo "--------------------------"
done

# Execute MeasureBench
if [ -x "MeasureBench/run.sh" ]; then
    (cd MeasureBench && ./run.sh "$CPU")
    echo "Results for MeasureBench are located at MeasureBench/out.txt"
else
    echo "run.sh not found or not executable in MeasureBench"
fi

# Collect the results into CSV files
python3 parse_evaluation.py
python3 make_leakage_matrix.py
