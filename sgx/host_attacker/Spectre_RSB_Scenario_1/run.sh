#!/bin/bash

# Usage: ./run.sh [--testing] [CPU]
# --testing builds the TESTING variant (attack within a single domain, should leak)
TESTING=0
OUT=out.txt
N=20  # Set the number of executions
if [ "$1" = "--testing" ]; then
    TESTING=1
    OUT=out_testing.txt
    N=3
    shift
fi
CPU=${1:-1}

# rebuild
make clean && make SGX_DEBUG=0 TESTING=$TESTING

# sign enclave
cp Enclave_private_test.pem Enclave/Enclave_private_test.pem
$SGX_SIGN sign -key Enclave/Enclave_private_test.pem -enclave enclave.so -out enclave.signed.so -config Enclave/Enclave.config.xml


# clear output file
echo "" > $OUT

dots=""
# run multiple times
for i in $(seq 1 $N); do
    sudo taskset -c "$CPU" ./app >> $OUT
    dots+="."
    echo -ne "\rProgress$dots"
done
