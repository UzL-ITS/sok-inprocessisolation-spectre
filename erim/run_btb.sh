#!/bin/bash

CPU=${1:-1}

ITER=10

make clean && make SCENARIO=1
echo "" > leaks_btb.txt
echo "SCCA: Should not leak" >> leaks_btb.txt
for ((i = 1; i <= ITER; i++))
do
    sudo taskset -c "$CPU" ./test_application_btb  >> leaks_btb.txt
done


echo "" >> leaks_btb.txt
echo "TCA: Should not leak" >> leaks_btb.txt
make clean && make SCENARIO=2
for ((i = 1; i <= ITER; i++))
do
    sudo taskset -c "$CPU" ./test_application_btb  >> leaks_btb.txt
done


echo "" >> leaks_btb.txt
echo "STB: Should leak!" >> leaks_btb.txt
make clean && make SCENARIO=3
for ((i = 1; i <= ITER; i++))
do
    sudo taskset -c "$CPU" ./test_application_btb  >> leaks_btb.txt
done


echo "" >> leaks_btb.txt
echo "STB + exec prev: Should not leak" >> leaks_btb.txt
make clean && make SCENARIO=3 EXEC_PREV=1
for ((i = 1; i <= ITER; i++))
do
    sudo taskset -c "$CPU" ./test_application_btb  >> leaks_btb.txt
done

cat leaks_btb.txt