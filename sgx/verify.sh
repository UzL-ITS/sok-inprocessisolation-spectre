#!/bin/bash

# Builds and runs the TESTING variant of every experiment (attack within a single
# domain, 3 runs each) and checks that it leaks. An experiment passes if at least one run leaked.

cd "$(dirname "$0")" || exit 1

# Usage: ./verify.sh [CPU]  (CPU core to pin the experiments to, default: 1)
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

# run.sh uses sudo; ask for the password once up front
sudo -v || exit 1

summary=()
failed=0
for experiment in "${experiments[@]}"; do
    echo "Verifying: $experiment"
    out="$experiment/out_testing.txt"
    rm -f "$out"

    (cd "$experiment" && ./run.sh --testing "$CPU") > "$experiment/verify.log" 2>&1

    runs=$(grep -cE '^[0-9]+, [0-9]+' "$out" 2>/dev/null)
    leaky=$(grep -c '<--' "$out" 2>/dev/null)
    runs=${runs:-0}
    leaky=${leaky:-0}

    if [ "$runs" -eq 0 ]; then
        summary+=("FAIL  $experiment  (no results, see $experiment/verify.log)")
        failed=$((failed + 1))
    elif [ "$leaky" -eq 0 ]; then
        summary+=("FAIL  $experiment  (0/$runs runs leaked)")
        failed=$((failed + 1))
    else
        summary+=("PASS  $experiment  ($leaky/$runs runs leaked)")
    fi
done

echo -e "\n--- Verification summary ---"
printf '%s\n' "${summary[@]}"
echo "----------------------------"

if [ "$failed" -eq 0 ]; then
    echo "All ${#experiments[@]} experiments leaked in TESTING mode. The setup works."
else
    echo "$failed of ${#experiments[@]} experiments failed."
    exit 1
fi
