# SoK: In-Process Isolation in the Era of Spectre Attacks — Artifacts

This repository contains the artifacts of the paper *SoK: In-Process Isolation in the Era of Spectre Attacks* by Jonah Heller, Anja Rabich, Thore Tiemann, Wolfgang Kunz, and Thomas Eisenbarth (IACR TCHES 2027, issue 1).

It holds all sources of the three case studies in the evaluation (ERIM, Intel SGX, and TME-Box):

* the proof-of-concept (PoC) code for every Spectre variant and scenario class,
* the scripts that run all PoCs and collect their results into the leakage matrix of the paper,
* the compartment-transition benchmarks behind the overhead figure of the paper, and
* the descriptions, scripts, and configuration files required to reproduce the evaluation setups.

## Repository Structure

| Folder | Content |
| --- | --- |
| [`erim/`](erim/) | Spectre-PHT, -BTB, -RSB (ret2spec), and -STL PoCs on top of [ERIM](https://github.com/vahldiek/erim), the wrapper `spex.h` that adds execution prevention via `mprotect` to ERIM's transitions, and the transition micro-benchmark. The makefile clones and builds ERIM, and `run_all.sh` runs all experiments and collects the results. |
| [`sgx/`](sgx/) | One SGX SDK project per experiment in two flavors: `host_attacker/`, where the host app attacks the enclave (the results in the paper), and `enclave_attacker/` with swapped roles. `MeasureBench/` times ecalls and ocalls. `setup.sh` creates the enclave signing key, `verify.sh` checks that the PoCs leak without isolation, and `run_all.sh` runs all experiments and collects the results. |
| [`tme-box/`](tme-box/) | The TME-Box artifact ([IntegriTag](https://github.com/davidschrammel/TME-MK-Fine-Grained-Encryption-Integrity/tree/502f9c1f9bdd73f9f7731633916e680fe9835608)) with our PoCs and the switch benchmark, the base config for the patched Linux 5.15 kernel, a Docker setup that runs every configuration, and the scripts that parse the log and typeset the leakage table. |

Each folder has its own README with detailed build and run instructions.

## Attack Scenarios

The PoCs cover the three scenario classes of the paper. In each class, the attacker compartment tries to leak a secret of the victim compartment:

| Class | Name | Attack | Mitigation under test |
| --- | --- | --- | --- |
| 1 | Speculative Cross Compartment Access (SCCA) | The attacker speculatively accesses the victim's data. | Secure access |
| 2 | Transition Constraint Attack (TCA) | A compartment switch occurs between the attack primitives: Predict or Transmit is in the attacker compartment, Access is in the victim compartment. | Serialization on compartment transitions |
| 3 | Speculative Transition Bypass (STB) | Architectural control flow stays in the victim, but a prediction unit diverts it so that Access or Transmit code of the attacker compartment runs speculatively. | Execution prevention of inactive compartments |

All PoCs transmit the secret through the cache and extract it with Flush+Reload, using a hit threshold that each PoC calibrates at startup.
Each leak attempt targets a single secret byte, and a configuration counts as leaky as soon as one attempt recovers a correct byte.
Every PoC is first run without any isolation mechanism on the target machine; only PoCs that leak in this unprotected configuration enter the evaluation.

## System Setup

### ERIM and SGX

The ERIM and SGX case studies were run on the same machine.

| Component | Configuration |
| --- | --- |
| CPU | Intel Xeon Silver 4309Y (Ice Lake) |
| Microcode | `0xd0003e7` |
| OS | Ubuntu 24.04 |
| Kernel | 6.8.0-45-generic |
| SGX SDK | 2.30.101.1 (built from [intel/confidential-computing.sgx](https://github.com/intel/confidential-computing.sgx) at commit `3ab6a6b`) |
| Spectre v1 mitigations | barriers, pointer sanitization |
| Spectre v2/v5 mitigations | eIBRS, IBPB conditional, RSB filling, PBRSB-eIBRS SW sequence, BHI SW loop, KVM SW loop |
| Spectre v4 mitigations | SSBD via prctl and seccomp |

### TME-Box

| Component | Configuration |
| --- | --- |
| CPU | Intel Xeon Silver 4514Y (Emerald Rapids) |
| Microcode | `0x21000230` |
| Motherboard | Supermicro X13SEI-TF |
| BIOS | American Megatrends Inc., version 2.1 |
| BIOS settings | Limit CPU PA to 46 Bits: **off**<br>Memory Encryption (TME): **on**<br>TME Bypass: **off**<br>TME Multi-Tenant (TME-MT): **on**<br>Memory Integrity: **on** |
| OS | Ubuntu 20.04 in Docker on an Ubuntu 24.04 host |
| Kernel | Linux 5.15 with TME-Box patches, built from [`tme-box/kernel-configs/config-5.15.0-112-generic`](tme-box/kernel-configs/config-5.15.0-112-generic) |
| Spectre v1 mitigations | barriers, pointer sanitization |
| Spectre v2/v5 mitigations | eIBRS, IBPB conditional, RSB filling |
| Spectre v4 mitigations | SSBD via prctl and seccomp |

Linux's default Spectre countermeasures stay enabled throughout. The upstream TME-Box artifact does not include the secure compiler pass, so the PoCs mimic it by hand: they mask the addresses of data accesses and indirect jump targets with the domain of the active compartment and replace returns with indirect jumps.

## Reproducing the Results

Every case study pins its experiments to one CPU core: ERIM and SGX take the core as an argument (default: 1), TME-Box reads it from `PINNED_CORE` in [`tme-box/.env`](tme-box/.env) (default: 23).

**ERIM** ([details](erim/README.md))
```bash
cd erim
make             # clones and builds ERIM, then builds the PoCs
./run_all.sh 1   # runs all PoCs and benchmarks on core 1
```
The results are written to `leakage_matrix.csv` and `performance.csv`. Without execution prevention, the STB configuration leaks for every variant, which confirms that the PoCs work on the machine.

**SGX** ([details](sgx/README.md)), after installing the SGX SDK and exporting its paths:
```bash
cd sgx
./setup.sh       # creates and distributes the enclave signing key
./verify.sh 1    # runs every PoC within a single domain, where it has to leak
./run_all.sh 1   # runs all experiments (1 to 2 hours)
```
The results of the paper are written to `leakage_matrix_host_attacker.csv`, the transition timings to `performance.csv`.

**TME-Box** ([details](tme-box/README.md)), after booting the patched kernel:
```bash
cd tme-box
make             # runs the evaluation in Docker, parses the log, and typesets the table
```
The results are written to `leakage_matrix.csv`, `leakage_table.pdf`, and `performance.csv`. The `run_*_plain` configurations run each PoC without TME-Box and have to report `LEAKY`; they appear as `Plain` rows in `leakage.csv`. During the evaluation, `build.sh` disables Turbo Boost and sets the `performance` governor on the pinned core, and restores the previous settings afterwards (see the [TME-Box README](tme-box/README.md#running-the-experiments)).

## Expected Results

### Leakage

The leakage matrices use `1` for observed leakage, `0` for no observed leakage, and `-` for configurations that were not measured. Each cell below lists the flags for PHT, BTB, RSB, and STL, in this order, and corresponds to the leakage table of the paper:

| Configuration | ERIM | TME-Box | SGX (`host_attacker`) |
| --- | :---: | :---: | :---: |
| SCCA | `0 0 0 0` | `0 0 0 0` | `0 0 0 -` |
| TCA | `0 0 0 0` | `1 1 0 1` | `0 0 0 -` |
| TCA + Serialization | `- - - -` | `0 0 0 0` | `- - - -` |
| STB | `1 1 1 1` | `0 1 0 0` | `0 - 0 -` |
| STB + Execution prevention | `0 0 0 0` | `0 0 0 0` | `- - - -` |

ERIM is not measured with additional serialization, because its transitions already execute `WRPKRU`. SGX needs neither additional mitigation: enclave entries are serializing, and none of the measured STB attacks leaks. For SGX, we did not run Spectre-STL, because SGX implicitly enables SSBD in enclaves, nor Spectre-BTB in the STB scenario, because enclave entries trigger IBRS. The paper infers protection for these cells from the documented countermeasures.

### Transition Overhead

The benchmarks report transition times in cycles of the time-stamp counter (`rdtsc`). For TME-Box, the current harness gives different numbers than the overhead figure of the paper (medians in cycles):

| Switch | Paper | Artifact |
| --- | ---: | ---: |
| `TME-Box` (pointer masking) | 144 | 134 |
| `TME-Box-Serial` | 178 | 178 |
| `TME-Box-ExecPrev (64)` | 738 | 812 |
| `TME-Box-ExecPrev (4096)` | 850 | 1,276 |

The benchmark code is unchanged, but the numbers in the paper were measured with an earlier version of the harness, which neither pinned the benchmark to a core nor fixed the core clock. The current harness runs the benchmark on `PINNED_CORE` with Turbo Boost disabled and the `performance` governor (see the [TME-Box README](tme-box/README.md#running-the-experiments)). Because the time-stamp counter ticks at a constant rate regardless of the core clock, the measured cycle counts depend on the frequency the core runs at.

The relations the paper draws on hold for both setups: serialization adds a few tens of cycles to a TME-Box switch, and execution prevention via `mprotect` costs a multiple of a plain switch.

## Experiment Repetitions

Each configuration runs its PoC or benchmark several times, and each execution performs a fixed number of attempts (for the benchmarks: timed transitions per switch type):

| Mechanism | Experiment | Attempts per execution | Executions | Total |
| --- | --- | ---: | ---: | ---: |
| ERIM | Spectre-PHT, -BTB, -STL | 50,000 | 10 | 500,000 |
| | Spectre-RSB | 5,000 | 10 | 50,000 |
| | Transitions (per page count) | 10,000 | 100 | 1,000,000 |
| SGX | Spectre-PHT, -RSB (SCCA) | 100,000 | 20 | 2,000,000 |
| | Spectre-BTB (SCCA), Spectre-PHT (TCA, STB) | 10,000 | 20 | 200,000 |
| | Spectre-BTB, -RSB (TCA) | 1,000 | 20 | 20,000 |
| | Spectre-RSB (STB) | 5,000 | 20 | 100,000 |
| | Transitions | 10,000 | 100 | 1,000,000 |
| TME-Box | Spectre-PHT, -BTB, -RSB, -STL | 100,000 | 1 | 100,000 |
| | Spectre-BTB (STB) | 10,000 | 1 | 10,000 |
| | Transitions | 100,000 | 1 | 100,000 |

The SGX counts are those of `host_attacker/`. The `enclave_attacker/` experiments use the same counts, except for Spectre-PHT in SCCA, which performs 10,000 attempts per execution.
Spectre-BTB in the STB scenario under TME-Box uses fewer attempts on purpose, because of its runtime. TME-Box Spectre-RSB scores 94,737 of its 100,000 attempts, because it skips the space in the secret, so `leakage.csv` lists 94,737 as its total.

## License

See [LICENSE](LICENSE). The upstream TME-Box code in [`tme-box/TME-MK-Fine-Grained-Encryption-Integrity/`](tme-box/TME-MK-Fine-Grained-Encryption-Integrity/) keeps its own [license](tme-box/TME-MK-Fine-Grained-Encryption-Integrity/LICENSE).
