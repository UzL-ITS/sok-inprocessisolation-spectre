#!/bin/bash

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

cd "$(dirname "$0")" || exit 1

KEY=Enclave_private_test.pem

# Create one enclave signing key (3072-bit RSA, exponent 3 as required by sgx_sign).
if [ ! -f "$KEY" ]; then
    echo "Generating $KEY"
    openssl genrsa -out "$KEY" -3 3072 || exit 1
fi

# Copy pem key to all enclaves.
for experiment in "${experiments[@]}"; do
    echo "Copying $KEY to: $experiment"

    if [ -d "$experiment" ]; then
        cp "$KEY" "$experiment/$KEY"
    else
        echo "$experiment is not a directory"
    fi
done
