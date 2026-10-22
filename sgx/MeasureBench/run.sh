#!/bin/bash

# Usage: ./run.sh [CPU]
CPU=${1:-1}

# rebuild
make clean && make SGX_DEBUG=0

cp Enclave_private_test.pem Enclave/Enclave_private_test.pem
$SGX_SIGN sign -key Enclave/Enclave_private_test.pem -enclave enclave.so -out enclave.signed.so -config Enclave/Enclave.config.xml

# clear output file
echo "" > out.txt

# run multiple times
N=100  # Set the number of executions
for i in $(seq 1 $N); do
    sudo taskset -c "$CPU" ./app >> out.txt
done