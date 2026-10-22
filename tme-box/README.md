# TME-Box Artifact
In this folder you will find the Spectre PoCs we used together with TME-Box, as well as the harness that runs all of them and renders the leakage table.

## Structure
* `TME-MK-Fine-Grained-Encryption-Integrity/` is the upstream IntegriTag repository ("Memory Tagging using Cryptographic Integrity on Commodity x86 CPUs") with our additions. It contains the kernel/glibc/PartitionAlloc patches, our PoCs (`spectre_pht.c`, `spectre_btb.c`, `spectre_rsb.c` + `spectre_rsb.asm`, `spectre_stl.c`), the switch benchmark (`benchmarking.c`) and the `Makefile` that builds and runs every configuration.
* `Dockerfile`, `docker-compose.yml`, `.env` and `build.sh` wrap the whole evaluation in a container. `build.sh` is the container entrypoint and executes every PoC configuration sequentially.
* The top-level `Makefile` drives evaluation, parsing, and table generation. `parse_evaluation.py` and `make_leakage_matrix.py` post-process the log, `leakage_table.tex` typesets the resulting matrix.

## Setup
The code has been tested on a Ubuntu 24.04 host with a Ubuntu 20.04 Docker container using a Emerald Rapids CPU (Xeon Silver 4514Y). TME-MK **integrity** keys have to be enabled in the BIOS/UEFI.

1. Adapt `.env`: `UID`/`GID` have to match your host user (`id -u`, `id -g`) so that the build artifacts in the bind mount stay writable. `PINNED_CORE` is the CPU core every PoC and benchmark is pinned to (default: 23, see [Running the Experiments](#running-the-experiments)). `docker-compose.yml` bind-mounts this folder into the container, so it can be located anywhere on the host.
2. Build and install the patched kernel. The kernel is built inside the container, which bind-mounts the base kernel config from `kernel-configs/` to `/kernel-configs/`:
    ```bash
    docker compose build
    docker compose run --rm --entrypoint bash evaluator   # opens a shell in the container
    cd TME-MK-Fine-Grained-Encryption-Integrity && make build_linux_initial
    ```
    `make build_linux_initial` downloads Linux v5.15, applies `patches/linux_v5-15_integrity.patch`, and builds Debian packages, which end up in this folder. Install them on the host (`sudo dpkg -i linux-image-*tmebox*.deb linux-headers-*tmebox*.deb`) and boot the `5.15.0-tmebox` kernel.
3. Check that BIOS and kernel set up the MSRs correctly: `sudo ./msr.sh` in `TME-MK-Fine-Grained-Encryption-Integrity/`. We expect `AES-XTS 128+I : 1`, `TME_ACTIVATE_ENABLED : 2`, `MK_TME_KEYID_BITS : 6` (or higher) and `MK_TME_CRYPTO_ALGS : 3`. See the README of the upstream repository for details.

The container runs `privileged` because the PoCs need MSR access and MKTME keys from the patched kernel, and it needs network access on the first run, since glibc, depot_tools, LLVM and PartitionAlloc are downloaded and built from scratch.

## Running the Experiments
Running `make` executes all steps in order:

```bash
make evaluate    # docker compose up --build, captured in evaluation.log
make parse       # evaluation.log -> leakage.csv + performance.csv
make plot_table  # leakage.csv -> leakage_matrix.csv -> leakage_table.pdf (default target)
```

The PDF is typeset in the `texlive/texlive` container, so no local TeX installation is required. `make clean` removes the containers and the TeX by-products, `make clean_all` additionally deletes the log, the CSVs, and the PDF.

`make evaluate` refuses to run unless `uname -r` starts with `5.15.0-tmebox`, because on any other kernel the protected scenarios cannot allocate MKTME keys and the log contains no results. If your TME-Box kernel has a different name, pass its prefix with `make EVAL_KERNEL=<prefix>`; `FORCE_EVAL=1` skips the check entirely. The output is captured in `evaluation.log.tmp` first and only replaces `evaluation.log` if it contains both leakage scores and benchmark rows, so a failed run never overwrites a previous result.

All PoCs and the benchmark are pinned to the core `PINNED_CORE` from `.env` (default: 23, so adapt it on CPUs with fewer than 24 logical cores). Editing `.env` triggers a new evaluation on the next `make`. The core can also be overridden from the shell, e.g. `PINNED_CORE=3 make -B evaluate` (`-B` is needed because Make does not notice a changed environment variable).

`build.sh` also fixes the core clock for the duration of the evaluation: it disables Turbo Boost and sets the `performance` governor on `PINNED_CORE` and its hyperthread siblings, and restores the previous settings when it exits (also on `docker stop` or Ctrl-C). The log starts with `[clock] pinned` lines showing the applied settings. Disabling Turbo Boost uses `/sys/devices/system/cpu/cpufreq/boost`, which is provided by the `acpi-cpufreq` driver. With `intel_pstate`, this file does not exist, so the script prints a `[clock] WARNING` and the evaluation runs without a fixed clock.

Individual configurations can also be run inside the container (`docker compose run --rm --entrypoint bash evaluator`), e.g. `cd TME-MK-Fine-Grained-Encryption-Integrity && make run_pht_protected_scenario1`. They have to run in the container because the protected binaries are linked against the custom glibc built there. `build.sh` lists all available targets.

## PoCs
Every Spectre variant (PHT, BTB, RSB, STL) is executed unprotected (`run_*_plain`) and under TME-Box in the three scenarios of the paper, which are selected at compile time via `-D SCENARIO1/2/3`:

* **Scenario 1** (SCCA in the output of the parser): the sandbox masks the key-ID bits of the pointer into its own domain before dereferencing it (`ASSERT_DOMAIN`).
* **Scenario 2** (TCA): the sandbox accesses out of its bounds into a foreign domain, so the data is decrypted with the wrong key. `run_*_scenario2_serial` adds the serializing instruction after the domain switch (`-D SERIALIZE_SW`): `mfence` for PHT, BTB, and STL, and `cpuid` for RSB.
* **Scenario 3** (STB): the transient path executes code that resides in another domain. `run_*_scenario3_exec` additionally removes the execute permission from that code page (`-D EXEC_PREV`).

For Spectre-RSB there are additional `_ret` variants, where the transient path is entered via `ret` instead of an indirect jump. These measurements are deliberately not part of the leakage table (see `make_leakage_matrix.py`); the table shows the non-Return scenarios only because TME-Box natively replaces all return instructions by indirect jumps.

Each configuration performs 100000 measurements and finally prints the recovered string together with

```
SCORE: <leaked>/100000	>>> PROTECTED <<<
```

There is one exception: Spectre-BTB in the STB scenario performs 10000 measurements due to its runtime. Spectre-RSB cycles through the 19 characters of the secret and does not score the space, so its `SCORE` is the number of correctly leaked characters out of the 94737 measurements on non-space characters.

Each PoC calibrates the Flush+Reload threshold at startup and prints it (`Flush+Reload Threshold: ...`) before the measurements.

`PROTECTED` means that no byte of the secret was recovered; any leakage is reported as `LEAKY` and results in a leakage flag of 1 in `leakage.csv` and a ghost icon in `leakage_table.pdf`.

## Benchmark
`make run_benchmarks` (the last step of `build.sh`) measures the cost of the TME-Box switches with `benchmarking.c`: `TME-Box` (pointer masking), `TME-Box-Serial` (switch serialized with `mfence`), and `TME-Box-ExecPrev (64)`/`(4096)` (execution prevention via `mprotect` over 64 and 4096 pages). The results are printed as `CSV:` rows containing iterations, mean, standard deviation and the five-number summary, which `parse_evaluation.py` collects into `performance.csv`. The [top-level README](../README.md#transition-overhead) explains how these numbers relate to the overhead figure of the paper.

### Special cases
* All steps in `build.sh` are wrapped in `timeout 60`. This is sufficient for the PoC runs, but not for the initial builds of glibc and PartitionAlloc. Build these once beforehand in a container shell (`make glibc`, `make partition` in `TME-MK-Fine-Grained-Encryption-Integrity/`) so that the artifacts already exist in the bind mount when the evaluation starts.
* `make run_test` runs the allocator self-test, which deliberately performs an out-of-bounds access on tagged memory. The expected outcome is a crash (or the timeout kill), reported as `[OK] as expected, ...`.
* `gclient sync` does not work inside the container (see the note in the upstream `Makefile`). If the PartitionAlloc checkout is incomplete, run `gclient sync` and `make partition_alloc_builder_patch` on the host; the bind-mounted directory is shared with the container.
* If key allocation fails with `add_key FAILED!`, check `dmesg` for `MKTME: no valid processor proximity domain`. The kernel patch requires the ACPI HMAT to mark a valid processor proximity domain; some BIOS versions do not set that flag.
