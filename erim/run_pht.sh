#!/bin/bash

CPU=${1:-1}

ITER=10

make clean && make SCENARIO=1
echo "" > leaks_pht.txt
echo "SCCA: Should not leak" >> leaks_pht.txt
for ((i = 1; i <= ITER; i++))
do
    sudo taskset -c "$CPU" ./test_application_pht >> leaks_pht.txt
done


echo "" >> leaks_pht.txt
echo "TCA: Should not leak" >> leaks_pht.txt
make clean && make SCENARIO=2
for ((i = 1; i <= ITER; i++))
do
    sudo taskset -c "$CPU" ./test_application_pht >> leaks_pht.txt
done


echo "" >> leaks_pht.txt
echo "STB: Should leak!" >> leaks_pht.txt
make clean && make SCENARIO=3
for ((i = 1; i <= ITER; i++))
do
    sudo taskset -c "$CPU" ./test_application_pht >> leaks_pht.txt
done

echo "" >> leaks_pht.txt
echo "STB + exec prev: Should not leak" >> leaks_pht.txt
make clean && make SCENARIO=3 EXEC_PREV=1
for ((i = 1; i <= ITER; i++))
do
    sudo taskset -c "$CPU" ./test_application_pht >> leaks_pht.txt
done

cat leaks_pht.txt