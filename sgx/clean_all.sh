#!/bin/bash

cd "$(dirname "$0")" || exit 1

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
    "MeasureBench"
)

# Run make clean in each folder and remove the key copied by setup.sh
# (make clean already removes the copy in Enclave/)
for experiment in "${experiments[@]}"; do
    echo "Cleaning: $experiment"

    if [ -d "$experiment" ]; then
        (cd "$experiment" && make clean)
        rm -f "$experiment/Enclave_private_test.pem"
    else
        echo "$experiment is not a directory"
    fi
done
