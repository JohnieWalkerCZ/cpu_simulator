# Customizable Microcoded CPU Simulator & Sandbox

A data-driven C++ simulation environment and GUI for custom processor architectures. This system allows you to define bus widths, register layouts (with hierarchical bit-slicing and masking), memory segment protections, ALU expression math, microcode execution flows, and active memory-mapped I/O peripherals using JSON configuration files.

The simulator includes a dynamic multi-pass assembler, a microcoded instruction execution engine, and an interactive ImGui-based visual dashboard.

---

## Key Features

### 1. Parametric Architecture Widths
* Configure native data and address buses to any power-of-two width between 4-bit and 64-bit.
* Evaluates instruction fetches, register boundaries, sign-extension, and wrap-around arithmetic dynamically according to the configured system parameters.
* Scaled byte-level memory addressing ensures multi-byte system instruction widths and PC increments align accurately without memory gaps or bit truncation.

### 2. Hierarchical Register Aliasing & Slicing
* Define physical registers and slice them recursively using bitwise offsets or binary masks.
* Virtual sub-registers automatically synchronize writes to and reads from their physical parent registers.
* Native lookup support for special registers like Program Counter (`PC`), Stack Pointer (`SP`), Status Flags (`FLAGS`), or custom Coprocessor registers.

### 3. Flexible Memory & Protection Subsystem
* Toggleable memory architecture topologies supporting either **Von Neumann** (unified) or **Harvard** (isolated code and data spaces).
* Address ranges and memory segments support a full **64-bit absolute address space** ($2^{64}-1$), avoiding 32-bit truncation limitations on high-width address buses.
* Byte ordering configurable as Big-Endian or Little-Endian.
* Strict segment-level memory access checks for Read (`R`), Write (`W`), and Execute (`X`) privileges, throwing diagnostic hardware faults on illegal memory access.

### 4. Expression-Based ALU Engine
* Custom ALU operations are written directly as standard math expressions in JSON.
* A hand-rolled tokenizer and Shunting-Yard compiler parses infix mathematical expressions into Reverse Polish Notation (RPN) for high-performance evaluation.
* Custom flag-evaluation rules update the `$FLAGS` register based on default rules (such as zero, negative, carry addition/subtraction, and signed overflow logic) or custom logical expressions.

### 5. Microcode Control Unit
* CPU execution runs through discrete states: `FETCH`, `DECODE`, `EXECUTE_UOPS`, and `DONE`.
* Instructs the execution engine via a micro-operation list including `copy`, `alu`, `mem_read`, `mem_write`, `port_read`, `port_write`, `coproc_read`, `coproc_write`, `branch`, and `halt`.
* Supports both **dynamic execution** (one micro-op per clock cycle, stalling for multi-cycle ALU operations) and **strict instruction-level latency overrides**.
* Latency overrides mathematically distribute micro-operation execution: compressing multiple side effects into fewer clock cycles (down to a single-cycle execution phase) or padding execution with pipeline stalls to match exact hardware timing specifications.
* Implements vectored hardware interrupts with automatic hardware stack saving and restoring.

### 6. Declarative MMIO Peripherals
* Define functional hardware devices directly in the JSON file.
* Includes state variables, internal registers, and Abstract Syntax Tree (AST) triggers evaluated during system clock ticks or memory read/write events.
* Integrated system hooks for virtual UART terminals, input streams, timer ticks, and co-processor devices.

---

## Project Structure

```text
.
├── CMakeLists.txt                  # Build configuration for CMake
├── configs                         # JSON CPU configuration files
│   ├── 8bit.json
│   ├── test.json
│   └── tiny4.json
├── CONFIGURATION.md                # Configuration reference manual
├── diagrams                        # Architecture and execution diagrams
│   ├── classes.mmd                 # Class diagram
│   ├── complete.mmd                # System overview flowchart
│   ├── executor.mmd                # Executor state machine
│   ├── generate_images.sh          # Script to convert .mmd to .png
│   ├── microop.mmd                 # Micro-operation flowchart
├── README.md                       # Main manual and build guide
├── src                             # Source code
│   ├── core                        # Core simulator engine
│   │   ├── alu                     # ALU (expression tokenizer & parser)
│   │   │   ├── alu.cpp
│   │   │   └── alu.hpp
│   │   ├── assembly                # Assembler (binary translation & label resolver)
│   │   │   ├── assembler.cpp
│   │   │   └── assembler.hpp
│   │   ├── config.cpp              # JSON configuration loader & validator
│   │   ├── config.hpp
│   │   ├── cpu.cpp                 # System loop coordinator
│   │   ├── cpu.hpp
│   │   ├── execution               # Decoder & Executor pipeline
│   │   │   ├── decoder.cpp
│   │   │   ├── decoder.hpp
│   │   │   ├── executor.cpp
│   │   │   └── executor.hpp
│   │   ├── memory                  # Memory (permissions, endianness, MMIO)
│   │   │   ├── memory.cpp
│   │   │   └── memory.hpp
│   │   ├── peripherals             # Declarative MMIO peripherals
│   │   │   ├── declarative_peripheral.cpp
│   │   │   └── declarative_peripheral.hpp
│   │   └── registers               # Register file (aliasing, masks, offsets)
│   │       ├── register_file.cpp
│   │       └── register_file.hpp
│   └── main.cpp                    # ImGui-based desktop visual dashboard
└── tests                           # Unit and integration test suites
    ├── test_alu_operations.cpp
    ├── test_assembler.cpp
    ├── test_config_parser.cpp
    ├── test_cpu_integration.cpp
    ├── test_decoder.cpp
    ├── test_memory.cpp
    └── test_register_file.cpp
```
---

## System Diagrams & Visualization

The architecture is documented visually using [Mermaid](https://mermaid.js.org/) (`.mmd`) specifications located inside the `diagrams/` directory. These source diagrams describe:
* **`complete.mmd`**: The entire hardware loop, connecting the memory spaces, register routing, execution pipelines, and declarative peripherals.
* **`classes.mmd`**: Object relationships and class structure interfaces.
* **`executor.mmd`**: The internal control unit state machine transitions.
* **`microop.mmd`**: Micro-operation flow, cycle latency stalling, and operand resolution pathways.

### Generating PNG Diagrams
High-resolution `.png` images are compiled from the `.mmd` source files using the `mermaid-cli` compiler utility (`mmdc`).

#### 1. Install the Mermaid CLI
If you do not have it installed globally on your machine, you can install it via Node.js:
```bash
npm install -g @mermaid-js/mermaid-cli
```

#### 2. Execute the Generator Script
An automation script is provided in the `diagrams/` folder. It reads each diagram file and builds high-width outputs configured to match their logical complexity:
```bash
cd diagrams
chmod +x generate_images.sh
./generate_images.sh
```

#### Under the Hood: `generate_images.sh`
The generation shell script works as follows:
```bash
#!/bin/bash
diagrams=("complete 10000" "classes 3000" "executor 4000" "microop 4000")

for i in "${diagrams[@]}"; 
do
    set -- $i 
    echo "GENERATING: $1.mmd"
    mmdc -i $1.mmd -o $1.png -w $2
    echo "" 
done
```

---

## Setup & Prerequisites

### Requirements
To compile the project, ensure your development environment has:
* A compiler supporting C++17 or higher (or C++26 as configured).
* CMake (3.20+).
* SDL2 development libraries.
* OpenGL drivers.
* `nlohmann/json` library (integrated or fetched via dependency downloaders).

### Compilation Steps
1. Clone the repository and navigate to its root directory.
2. Initialize and compile the project using CMake:
   ```bash
   mkdir build
   cd build
   cmake -DCMAKE_BUILD_TYPE=Release ..
   make
   ```
3. This generates the primary binaries inside the build output:
   * `cpu_sim`: The graphic visual sandbox environment.
   * `test_*`: Verification binaries mapping to each core engine component.

---

## Operating the Simulator

### Loading an Architecture Configuration
Start the GUI executable by passing the relative path to an architecture configuration file:
```bash
./cpu_sim ../configs/8bit.json
```

### Visual Interface Guide
Once loaded, the graphical workspace organizes execution monitoring into specialized windows:

* **Control Tower:** Provides physical clock speed control, execution modes (run by whole instructions or single microcode clock cycles), and state triggers.
* **Micro-op Pipeline:** Highlights the current microcode step in the control unit pipeline. Visualizes active register arguments, instruction bit layouts, the active execution latency mode (Strict, Bottleneck, Additive, or Dynamic), and provides a visual progress bar indicating elapsed execution clock cycles against structural targets.
* **Live Assembler:** Allows you to write and modify custom assembly programs on the fly. The built-in reference table displays mnemonic formats and expected parameter types derived from the loaded schema.
* **Register File:** Monitors runtime values in hexadecimal, decimal, and bitwise forms across all physical registers and virtual sub-registers.
* **Memory & Flash ROM Explorers:** Visualizes active code memory and data memory addresses. Addresses pointing to the current Program Counter (PC) are highlighted automatically.
* **I/O Peripherals:** Features interactive hardware displays, terminal output panels for mapped console UARTs, and live register state tables for declaratively modeled hardware coprocessors.

---

## Verification & Testing

Each primary module has a dedicated verification suite under the testing pipeline. You can run individual tests to observe isolation logs and verify behaviors:

* **Config Parser Suite (`test_config_parser`):** Ensures correct recursive decoding of custom JSON configuration schemas.
* **Register Alias Suite (`test_register_file`):** Validates bitwise read/write integrity across complex masked sub-register slices.
* **Segment Isolation Suite (`test_memory`):** Confirms that segment violation faults are triggered when unauthorized reads, writes, or execution sweeps are performed.
* **Assembler Validation (`test_assembler`):** Tests multi-pass compilation, variable instruction layouts, and symbolic label resolution.
* **Decoder Pipeline Suite (`test_decoder`):** Verifies binary parsing, bitfield extraction, and safety handling of invalid register indices.
* **Integrated CPU Suite (`test_cpu_integration`):** Simulates complex multi-cycle program execution, declarative peripheral handshakes, and hardware stack-saving ISR cycles.

To execute the verification suite, run the following command within your build directory:
```bash
ctest --output-on-failure
```
