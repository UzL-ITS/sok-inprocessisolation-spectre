#!/bin/bash

CPU=${1:-1}

ITER=10

echo "" > leaks_stl.txt
echo "SCCA: Should not leak" >> leaks_stl.txt
make clean && make SCENARIO=1
for ((i = 1; i <= ITER; i++))
do
    sudo taskset -c "$CPU" ./test_application_stl >> leaks_stl.txt
done

echo "" >> leaks_stl.txt
echo "TCA: Should not leak" >> leaks_stl.txt
make clean && make SCENARIO=2
for ((i = 1; i <= ITER; i++))
do
    sudo taskset -c "$CPU" ./test_application_stl >> leaks_stl.txt
done


echo "" >> leaks_stl.txt
echo "STB: Should leak!" >> leaks_stl.txt
make clean && make SCENARIO=3
for ((i = 1; i <= ITER; i++))
do
    sudo taskset -c "$CPU" ./test_application_stl >> leaks_stl.txt
done

echo "" >> leaks_stl.txt
echo "STB + exec prev: Should not leak" >> leaks_stl.txt
make clean && make SCENARIO=3 EXEC_PREV=1
for ((i = 1; i <= ITER; i++))
do
    sudo taskset -c "$CPU" ./test_application_stl >> leaks_stl.txt
done

cat leaks_stl.txt