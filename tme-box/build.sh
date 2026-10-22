MAIN_REPO="TME-MK-Fine-Grained-Encryption-Integrity"

# Pin the CPU clock for all evaluations: no turbo, performance governor on the
# pinned core and its hyperthread siblings.
# The previous settings are restored when this script exits.
CPU_SYSFS=/sys/devices/system/cpu
PINNED_CORE=${PINNED_CORE:-23}

sysfs_write() {  # <file> <value>
    echo "pass" | sudo -S -p "" sh -c "echo $2 > $1"
}

expand_cpus() {  # "7,23" or "22-23" -> one CPU per line
    local part
    for part in ${1//,/ }; do
        seq "${part%-*}" "${part#*-}"
    done
}

print_clock() {
    local cpu
    for cpu in ${CLOCK_CPUS}; do
        echo "[clock] $1 cpu${cpu}: boost=$(cat ${CPU_SYSFS}/cpufreq/boost)" \
             "governor=$(cat ${CPU_SYSFS}/cpu${cpu}/cpufreq/scaling_governor)"
    done
}

CLOCK_CPUS=$(expand_cpus "$(cat ${CPU_SYSFS}/cpu${PINNED_CORE}/topology/thread_siblings_list)")
OLD_BOOST=$(cat ${CPU_SYSFS}/cpufreq/boost)
declare -A OLD_GOVERNOR
for cpu in ${CLOCK_CPUS}; do
    OLD_GOVERNOR[$cpu]=$(cat ${CPU_SYSFS}/cpu${cpu}/cpufreq/scaling_governor)
done

restore_clock() {
    for cpu in ${CLOCK_CPUS}; do
        sysfs_write ${CPU_SYSFS}/cpu${cpu}/cpufreq/scaling_governor ${OLD_GOVERNOR[$cpu]}
    done
    sysfs_write ${CPU_SYSFS}/cpufreq/boost ${OLD_BOOST}
    print_clock restored
}
trap restore_clock EXIT
trap "exit 143" TERM INT  # so docker stop / Ctrl-C also restore the clock

sysfs_write ${CPU_SYSFS}/cpufreq/boost 0
for cpu in ${CLOCK_CPUS}; do
    sysfs_write ${CPU_SYSFS}/cpu${cpu}/cpufreq/scaling_governor performance
done
print_clock pinned
if [ "$(cat ${CPU_SYSFS}/cpufreq/boost)" != 0 ] ||
   grep -qv performance $(printf "${CPU_SYSFS}/cpu%s/cpufreq/scaling_governor " ${CLOCK_CPUS}); then
    echo "[clock] WARNING: could not pin the CPU clock, leakage and benchmark results will be noisy"
fi

pushd ${MAIN_REPO}

timeout 60 make package_dependencies
timeout 60 make glibc
timeout 60 make partition
timeout 60 make run_test
if [ $? -ne 0 ]; then
    echo "[OK] as expected, the program hung up and was killed by timeout"
fi

timeout 60 make -s run_pht_plain
timeout 60 make -s run_pht_protected_scenario1
timeout 60 make -s run_pht_protected_scenario2
timeout 60 make -s run_pht_protected_scenario2_serial
timeout 60 make -s run_pht_protected_scenario3
timeout 60 make -s run_pht_protected_scenario3_exec

timeout 60 make -s run_btb_plain
timeout 60 make -s run_btb_protected_scenario1
timeout 60 make -s run_btb_protected_scenario2
timeout 60 make -s run_btb_protected_scenario2_serial
timeout 60 make -s run_btb_protected_scenario3
timeout 60 make -s run_btb_protected_scenario3_exec

timeout 60 make -s run_rsb_plain
timeout 60 make -s run_rsb_protected_scenario1
timeout 60 make -s run_rsb_protected_scenario2
timeout 60 make -s run_rsb_protected_scenario2_serial
timeout 60 make -s run_rsb_protected_scenario3
timeout 60 make -s run_rsb_protected_scenario3_exec
timeout 60 make -s run_rsb_protected_scenario1_ret
timeout 60 make -s run_rsb_protected_scenario2_ret
timeout 60 make -s run_rsb_protected_scenario2_ret_serial
timeout 60 make -s run_rsb_protected_scenario3_ret
timeout 60 make -s run_rsb_protected_scenario3_ret_exec

timeout 60 make -s run_stl_plain
timeout 60 make -s run_stl_protected_scenario1
timeout 60 make -s run_stl_protected_scenario2
timeout 60 make -s run_stl_protected_scenario2_serial
timeout 60 make -s run_stl_protected_scenario3
timeout 60 make -s run_stl_protected_scenario3_exec

timeout 60 make -s run_benchmarks

popd
