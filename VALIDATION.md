# Reproducible simulator validation

## Research question and claims

**Research question:** Can one simulation engine execute several processor
configurations described as data, while preserving their expected architectural
results and exposing the executor's internal state?

The validation checks three bounded claims:

1. The same `cpu_validate` binary executes the included 4-, 8-, and 16-bit
   configurations and produces independently calculated final register values.
2. The same 8-bit program produces identical architectural results on the
   included Von Neumann and Harvard configurations.
3. The simulator's ALU, memory, register aliases, control flow, interrupts,
   and MMIO behave as specified in selected boundary and integration cases.

These claims concern the configurations and cases listed below. They do not
establish correctness for every possible JSON-defined architecture. The
simulator is a functional model, not a physical timing or gate-level model.

## Run

```bash
./run_validation.sh
```

The command builds the project, runs all CTest checks, executes the validation
matrix, and writes JSON reports to `build/validation-results/`. `run_demo.sh`
writes an equivalent trace for the 16-bit multiplication demonstration.

`cpu_validate` accepts a configuration and assembly source, executes one
simulation step at a time, and exits non-zero unless every `--expect
REGISTER=VALUE` assertion holds. `--trace FILE` records deterministic state
after each step. `--repetitions N` provides a host-dependent throughput sample;
use it only to compare runs on the same machine and build.

## Validation matrix

| Case | Configuration | Program property | Expected result |
| --- | --- | --- | --- |
| Tiny4 | 4-bit Von Neumann | register addition | `A = 4` |
| 8-bit VN | 8-bit Von Neumann | 8-bit wraparound and carry | `R0 = 4`, `FLAGS = 2` |
| 8-bit Harvard | 8-bit Harvard | same program as 8-bit VN | `R0 = 4`, `FLAGS = 2` |
| 16-bit RISC | 16-bit Von Neumann | multi-cycle multiplication | `R0 = 42` |
| CALL/branch/stack | 8-bit Von Neumann | subroutine, return, conditional branch | `R0 = 0`, `R2 = 42`, `SP = 65535` |
| Declarative MMIO | 8-bit Von Neumann | multiply through memory-mapped device | `R4 = 65` |

The assembly-case expectations are calculated from the programs' operations,
not read back from the simulator. They are passed to `cpu_validate`; a mismatch
makes that validation case fail. The 8-bit VN and Harvard cases deliberately
use the same assembled source to isolate the memory-topology setting.

Additional automated checks provide independent oracles for specific
mechanisms:

| Mechanism | Check | Independent expected result |
| --- | --- | --- |
| 8-bit ALU | all 65,536 operand pairs for both ADD and SUB | native integer result plus separately derived carry, overflow, zero, and negative flags |
| Endianness | write `0x1234` as a 16-bit value, inspect each byte | little endian: `34 12`; big endian: `12 34` |
| Register aliases | write parent and masked/sliced aliases | existing register tests check parent/alias bit mappings |
| Memory protection | write to ROM and fetch from non-executable RAM | both accesses must raise a fault |
| Interrupts | execute ISR and return through the saved PC | ISR side effect occurs and SP is restored |
| CALL/RET | validation program calls a subroutine and checks SP | return target is reached and stack pointer returns to its initial value |
| MMIO | assembly writes operands/command and reads device result | `13 × 5 = 65` |

The existing suites also cover JSON validation, assembler labels, decoding,
Harvard instruction/data isolation, and registers wider than 64 bits. The
machine-readable summaries and trace are generated under
`build/validation-results/` by `run_validation.sh`.

## Interpretation and limits

`cycles` and `simulation_steps` describe this model's executor, including
fetch, decode, execute, and completion states. They are not nanoseconds and
must not be compared to a physical processor clock. The JSON trace is evidence
for a particular configuration/program pair; it is not a proof of correctness
for every possible architecture configuration.

The comparison distinguishes the modelling level and configuration mechanism.
Ripes supports RISC-V assembly, processor microarchitecture models, memory-
mapped I/O, and command-line telemetry. Logisim-evolution simulates circuits
assembled from components, offers system-on-chip components, and supports
automated circuit test vectors. This project specifies instruction fields,
microcode, memory topology, and declarative peripherals in JSON, then runs
those definitions through one C++ engine. This is a feature-scope comparison;
it does not claim that either existing tool lacks comparable individual
features or that one tool is universally superior.

| Dimension | This project | Ripes | Logisim-evolution |
| --- | --- | --- | --- |
| Main model | JSON-defined ISA and microcode | RISC-V ISA with selectable processor models | User-assembled digital circuit; includes SoC components |
| Configuration target | instruction format, registers, memory, microcode, MMIO | supported RISC-V extensions and processor model | circuit structure and component parameters |
| Automated evidence | expected register values and per-step JSON trace | command-line runs and processor telemetry | command-line verification and test vectors |

Sources: [Ripes introduction and CLI release notes](https://github.com/mortbopet/Ripes/blob/master/docs/introduction.md),
[Ripes releases](https://github.com/mortbopet/Ripes/releases),
[Logisim-evolution guide](https://mbaillif.github.io/Logisim-evolution-documentation/en/html/guide/index.html),
[Logisim-evolution test vectors](https://mbaillif.github.io/Logisim-evolution-documentation/en/html/guide/verify/test.html),
[Logisim-evolution SoC library](https://mbaillif.github.io/Logisim-evolution-documentation/en/html/libs/soc/index.html).
