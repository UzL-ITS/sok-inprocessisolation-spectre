# SGX Artifact
In the following we describe how the SGX Artifacts are structured and how the experiments can be run.

## Structure
Each experiment is located in its own directory, named `Spectre_<Variant>_Scenario_<N>`. The Spectre PoCs are grouped by which side is the attacker (see [Experiment flavors](#experiment-flavors)):
```
enclave_attacker/   Spectre PoCs where the enclave attacks the host app
host_attacker/      Spectre PoCs where the host app attacks the enclave (results from the paper)
MeasureBench/       measures ocall and ecall execution cycles
```
Using `run_all.sh` all experiments can be executed, and `verify.sh` checks that the setup works.

## Setup
Requirements on the host:
* SGX enabled in the BIOS/UEFI and an FLC-capable CPU.
* Linux kernel 5.11 or newer, so that the in-kernel SGX driver provides `/dev/sgx_enclave` and `/dev/sgx_provision`.
* The SGX runtime (PSW), at least `libsgx-urts` and `sgx-aesm-service`, e.g. from Intel's SGX apt repository.

### 1. Get an SGX SDK
Any of the following options works. We used SDK version 2.30.101.1.

**Option A: Use an existing SDK.** If an SGX SDK is already installed on your system (e.g. in `/opt/intel/sgxsdk`), continue with step 2 and use its path.

**Option B: Install a prebuilt SDK.** Download the SDK installer `sgx_linux_x64_sdk_<version>.bin` for your distribution from Intel ([https://download.01.org/intel-sgx/latest/linux-latest/distro/](https://download.01.org/intel-sgx/latest/linux-latest/distro/)) and install it into this folder (creates `./sgxsdk`):
```bash
chmod +x sgx_linux_x64_sdk_<version>.bin
./sgx_linux_x64_sdk_<version>.bin --prefix=$PWD
```

**Option C: Build the SDK from source** (requires Docker with Compose):
1. Clone the [Intel SGX repository](https://github.com/intel/confidential-computing.sgx) into this folder and check out the commit we used:
    ```bash
    git clone https://github.com/intel/confidential-computing.sgx.git
    cd confidential-computing.sgx
    git checkout 3ab6a6bdb97adc76f690c0083c618bb98992fde7
    make preparation    # fetches the submodules and prebuilt binaries
    ```
2. Build the SDK in Docker. This builds the images `sgx_aesm` and `sgx_sample` and then starts an SGX sample; once the sample is running, the build is complete and you can stop it with `Ctrl+C`:
    ```bash
    cd docker/build
    ./build_compose_run.sh
    cd ../../..
    ```
3. Copy the SDK installer out of the image and install the SDK into this folder (creates `./sgxsdk`):
    ```bash
    docker create --name sgx_sdk_tmp sgx_sample
    docker cp sgx_sdk_tmp:/opt/intel/sgx_linux_x64_sdk_2.30.101.1.bin .
    docker rm sgx_sdk_tmp
    chmod +x sgx_linux_x64_sdk_2.30.101.1.bin
    ./sgx_linux_x64_sdk_2.30.101.1.bin --prefix=$PWD
    ```

### 2. Export the SDK paths
Source the SDK environment and export the paths (in every new shell). Replace `$PWD/sgxsdk` with the path of your SDK if you use an existing one:
```bash
source $PWD/sgxsdk/environment
export SGX_SDK=$PWD/sgxsdk
export SGX_SIGN=$SGX_SDK/bin/x64/sgx_sign
```

### 3. Create the enclave signing key
Run `./setup.sh` (requires `openssl`). It generates one test key `Enclave_private_test.pem` (3072-bit RSA) and copies it to every experiment folder, from where `run.sh` uses it to sign the enclave. If `Enclave_private_test.pem` already exists in this folder, that key is distributed instead.

### Experiment flavors
All Spectre experiments come in two flavors, in which the roles of host app and enclave are swapped. This is the same for every Spectre variant and scenario:

| Flavor (directory)  | Attacker | Victim (holds the secret) |
|---------------------|----------|---------------------------|
| `enclave_attacker/` | Enclave  | Host app                  |
| `host_attacker/`    | Host app | Enclave                   |

How the attack is split between the two domains depends on the scenario:
* **Scenario 1 (SCCA):** the whole attack runs in the attacker domain and targets the victim's secret.
* **Scenario 2 (TCA):** the disclosure gadget runs in the victim domain, transmission and recovery via the cache run in the attacker domain.
* **Scenario 3 (STB):** the victim domain transiently executes transmission code located in the attacker domain.

This allows verifying, e.g., that an enclave can access host memory speculatively, while the host app cannot access the enclave's memory speculatively. Spectre-BTB has no Scenario 3.

> **Note:** The results reported in the paper are those of the `host_attacker/` experiments. The `enclave_attacker/` experiments are additional, showing the opposite direction.

## Verifying the Setup
To verify that the PoCs work on your machine, run:
```bash
./verify.sh      # runs on core 1
./verify.sh 3    # runs on core 3
```
It builds every Spectre experiment in its TESTING variant, in which the whole attack runs within a single domain, so it should always leak. Each experiment is run 3 times. At the end it prints a summary with `PASS` or `FAIL` per experiment. An experiment passes if at least one of its runs leaked. The build and run output of each experiment is kept in its `verify.log`.

A single experiment can be checked with `./run.sh --testing [CPU]` inside its folder. The output is written to `out_testing.txt` instead of `out.txt`, so the results of the regular runs are kept. The TESTING variant can also be built directly with `make SGX_DEBUG=0 TESTING=1`.

## Running the Experiments
If correctly setup you can run `./run_all.sh`, which executes every experiment sequentially (20 runs per Spectre experiment, 100 for MeasureBench). Each experiment logs its output in an `out.txt` file.

> **Note:** Since only the `host_attacker/` results are relevant for the paper, the `enclave_attacker/` experiments can be excluded to decrease the running time: comment out their entries in the `experiments` list at the top of `run_all.sh`. Either way, the measurements may take 1 to 2 hours.

All experiments are pinned to a CPU core with `taskset`. The core can be passed as the first argument and defaults to core 1:
```bash
./run_all.sh      # runs on core 1
./run_all.sh 3    # runs on core 3
```

A single experiment can be run with `./run.sh [CPU]` inside its folder, e.g.:
```bash
cd host_attacker/Spectre_PHT_Scenario_1 && ./run.sh 3
```

### Results
At the end, `run_all.sh` collects the results into CSV files, in the same format as for ERIM and TME-Box:
* `leakage.csv`: one row per flavor, variant and scenario. `sandbox` is `sgx-enclave-attacker` or `sgx-host-attacker`, `score` is the number of runs with leakage, `total` the number of runs, and `leakage` is 1 if any run leaked.
* `leakage_matrix_host_attacker.csv` and `leakage_matrix_enclave_attacker.csv`: the leakage flags as a matrix (configurations × variants) per flavor. **`leakage_matrix_host_attacker.csv` corresponds to the results in the paper.** Configurations not measured for SGX (Spectre-BTB Scenario 3, Spectre-STL, and the additional security measures) are marked with `-`.
* `performance.csv`: mean, standard deviation and five-number summary (in cycles) of the ecall and ocall transitions measured by MeasureBench (`SGX:ecall:u->t`, `SGX:ecall:t->u`, `SGX:ocall:t->u`, `SGX:ocall:u->t`).

The CSV files can also be regenerated from existing results with `python3 parse_evaluation.py` followed by `python3 make_leakage_matrix.py`.
