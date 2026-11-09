# Customizable Microcoded CPU Simulator & Sandbox

A data-driven C++ simulation environment and GUI for custom processor architectures. This system allows you to define bus widths, register layouts (with hierarchical bit-slicing and masking), memory segment protections, ALU expression math, microcode execution flows, and active memory-mapped I/O peripherals using JSON configuration files.

The simulator includes a dynamic multi-pass assembler, a microcoded instruction execution engine, and an interactive ImGui-based visual dashboard.

---

## Key Features

### 1. Parametric Architecture Widths
* Configure native data and address buses to any multiple of 4 bits between 4-bit and 128-bit.
* Evaluates instruction fetches, register boundaries, sign-extension, and wrap-around arithmetic dynamically according to the configured system parameters.
* Scaled byte-level memory addressing ensures multi-byte system instruction widths and PC increments align accurately without memory gaps or bit truncation.

### 2. Hierarchical Register Aliasing & Slicing
* Define physical registers and slice them recursively using bitwise offsets or binary masks.
* Virtual sub-registers automatically synchronize writes to and reads from their physical parent registers.
* Native lookup support for special registers like Program Counter (`PC`), Stack Pointer (`SP`), Status Flags (`FLAGS`), or custom Coprocessor registers.

### 3. Flexible Memory & Protection Subsystem
* Toggleable memory architecture topologies supporting either **Von Neumann** (unified) or **Harvard** (isolated code and data spaces).
* Addresses and segment boundaries use the configured address width, up to 128 bits; memory pages are allocated lazily.
* Byte ordering configurable as Big-Endian or Little-Endian.
* Strict segment-level memory access checks for Read (`R`), Write (`W`), and Execute (`X`) privileges, throwing diagnostic hardware faults on illegal memory access.

### 4. Expression-Based ALU Engine
* Custom ALU operations can use expressions or small programs with variables, branches and loops in JSON.
* A hand-rolled tokenizer and Shunting-Yard compiler parses infix mathematical expressions into Reverse Polish Notation (RPN) for high-performance evaluation.
* Custom flag-evaluation rules update the `$FLAGS` register based on default rules (such as zero, negative, carry addition/subtraction, and signed overflow logic) or custom logical expressions.

### 5. Microcode Control Unit
* CPU execution runs through discrete states: `FETCH`, `DECODE`, `EXECUTE_UOPS`, and `DONE`.
* Instructs the execution engine via a micro-operation list including `copy`, `alu`, `mem_read`, `mem_write`, `port_read`, `port_write`, `coproc_read`, `coproc_write`, `branch`, and `halt`.
* Supports dynamic, strict, bottleneck and additive execution latency modes, including multi-cycle ALU operations.
* Latency overrides mathematically distribute micro-operation execution: compressing multiple side effects into fewer clock cycles (down to a single-cycle execution phase) or padding execution with pipeline stalls to match exact hardware timing specifications.
* Implements vectored hardware interrupts: on trigger, the executor pushes the return PC when an SP register exists and sets PC to the interrupt ID; the handler returns via a normal `RET` instruction, the same as any subroutine call.

### 6. Declarative MMIO Peripherals
* Define functional hardware devices directly in the JSON file.
* Includes state variables, internal registers, and Abstract Syntax Tree (AST) triggers evaluated during system clock ticks or memory read/write events.
* Integrated system hooks for virtual UART terminals, input streams, timer ticks, and co-processor devices.

---

## Project Structure

```text
configs/               Example JSON architectures
diagrams/              Mermaid source diagrams and PNG generator
src/core/              CPU, configuration, assembler, memory and execution engine
src/ui/                Simulator panels and graphical configurator
src/main.cpp           Simulator entry point
src/configurator_main.cpp  Configurator entry point
tests/                 Core and configurator tests
README.cs.md           Czech overview
CONFIGURATION.md       Configuration reference
CONFIGURATION.cs.md    Czech configuration overview
```
---

## System Diagrams & Visualization

The architecture is documented visually using [Mermaid](https://mermaid.js.org/) (`.mmd`) specifications located inside the `diagrams/` directory. These source diagrams describe:
* **`complete.mmd`**: The entire hardware loop, connecting the memory spaces, register routing, execution pipelines, and declarative peripherals.
* **`classes.mmd`**: Object relationships and class structure interfaces.
* **`executor.mmd`**: The internal control unit state machine transitions.
* **`microop.mmd`**: Micro-operation actions and their effects.

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
* A compiler and CMake toolchain supporting the configured C++26 standard.
* CMake (3.20+).
* SDL2 development libraries.
* OpenGL drivers.
* Network access for CMake FetchContent to obtain nlohmann/json and Dear ImGui on the first build.

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
   * `cpu_configurator`: The graphical CPU configurator (see below).
   * `test_*`: Verification binaries mapping to each core engine component.

---

## Graphical Configurator

`cpu_configurator` is a graphical editor for CPU configurations. Instead of
hand-editing JSON, you fill in forms and it writes the configuration file
for you:

```bash
./cpu_configurator                  # start with a blank valid template
./cpu_configurator ../configs/8bit.json   # open an existing configuration
```

* **Top bar:** file path with Open / Save / Save As, preset buttons for the
  shipped configurations (`tiny4`, `8bit`, `8bit_harvard`, `16bit`,
  `16bit_lab`), Export JSON, and **Launch Simulator**.
* **Tabs:** System, Registers (including recursive sub-registers with
  offset or mask slicing), Memory, ALU (flags and operations, including the
  `program` mini-language), Instructions (encoding and microcode),
  Problems, Peripherals (including graphical AST hook blocks), and a raw JSON
  view/edit tab.
* **Validation:** the edited document is validated live with the same
  `Config::from_json()` + `validate()` rules the simulator uses. The
  status line shows whether the configuration is valid; Export and Launch
  are blocked while it is invalid.
* **ALU inputs are automatic:** an ALU operation's `a`, `b`, `c` are
  bound at run time by the microcode `alu` action of the instruction
  that uses the operation (its `a`/`b`/`c` arguments; the result width
  comes from its `out`). They are not declared in the ALU tab.
* **Latency mode lives on the instruction, not on the ALU operation:**
  an ALU operation only has a `latency` in cycles. Each instruction has
  a `latency` and a `latency_mode` (`dynamic` / `strict` / `bottleneck` /
  `additive`); with no latency set the mode is ignored and the loader
  forces dynamic. The Instructions tab explains the selected mode in
  plain words and warns when it is ignored.
* **Instruction preview:** the Instructions tab shows the selected
  instruction as assembly (`ADD dest, src`), its encoding, a table of
  its micro-ops with inputs, outputs and cycle counts, the effective
  latency under the selected mode, and only the two latency warnings.
  Anything else wrong with the instruction is reported exactly once,
  with a fix, in the Problems list below the preview.
* **Guided micro-op editor:** pick a micro-op's Action and the editor
  shows exactly the arguments that action needs, each as its own
  field. The ALU `op` is a combo of the defined ALU operations and
  operand values are picked from a select of the valid references for
  that slot (encoding operands, registers, literals via a
  `# (literal...)` entry, `Custom...` for free text). Writable slots
  offer only writable targets. Changing the Action drops arguments
  that do not belong to the new action and prefills missing required
  ones with their first valid option. Field labels sit above their
  combos; an unset optional argument shows `(not set)` with no empty
  box underneath. The microcode panes size to their content, so the
  outer instruction editor scrolls instead of the inner boxes.
* **Sub-register bit bar:** a register with sub-registers shows where
  they sit inside it (bit 0 on the right, grey where nothing is
  covered, red where two sub-registers overlap), and a new
  sub-register starts at the first free bit.
* **Peripheral hook blocks:** each `on_read`, `on_write` and
  `tick_behavior` hook has a Blocks/Text toggle (default Blocks).
  Blocks view is a Scratch-style editor: coloured, rounded blocks
  (orange `if`, blue `assign`, green `call`) with their fields
  inline; `if` blocks are C-shaped, with their then/else lists
  nested inside. Drag a block by its header to move it between
  lists (a yellow insertion line shows the drop position), or drag
  a type in from the palette at the top of the hook editor; click a
  palette button to append a node. Move, remove and "+ Add node"
  controls stay as fallbacks. Text view is the pretty-printed JSON
  box; both edit the same JSON. Parameters, internal state and
  registers stay as text forms. Multiline text boxes are taller
  throughout.
* **Problems:** under the selected instruction's preview, a Problems
  list shows everything wrong with that instruction, each as a problem
  line followed by a concrete fix naming the field and value to change
  (e.g. `encoding[0] is 6, but the opcode is 5` -- `Set "encoding":
  [5, ...] so the first element equals "opcode"`). A Problems tab
  lists all problems in the document grouped by area (System /
  Registers / ALU / Instructions / Memory / Peripherals), and the top
  status line summarizes the count and the first problem. The checks
  mirror the loader's validation rules (`src/ui/config_diagnostics.hpp`).
* **Undo / redo and confirmations:** every edit is undoable (up to 50
  steps): Ctrl+Z to undo, Ctrl+Shift+Z / Ctrl+Y to redo, with Undo /
  Redo buttons in the top bar. Every Remove button asks first, and
  opening a file or closing the window with unsaved changes asks first
  too.
* **Clickable Problems and filters:** each problem row jumps to the tab
  and item it belongs to. The instruction, register, ALU-operation and
  peripheral lists have a name filter with an "N of M" count, and
  instruction rows show "NAME (opcode N, M uops)".
* **Duplicate, shortcuts, recent files:** Duplicate next to Remove
  copies the selected item (fresh opcode/code for instructions and ALU
  operations). Ctrl+S saves, Ctrl+D duplicates the selected item in the
  active tab. The last 8 files are clickable buttons under Open.
* **Tooltips and examples:** a grey "(?)" after the trickiest fields
  explains it in one sentence. "Insert example peripheral" adds a small
  declarative timer; "Insert example hook" adds the MathCoProcessor-style
  sample to a hook.
* **Window and readability:** default window 1600x1000, with a UI scale
  combo (0.9x / 1.0x / 1.25x / 1.5x) in the top bar, persisted with the
  recent files in `~/.config/cpu_configurator/settings.txt`.
* **Round-trip:** the configurator edits the JSON document itself, so a
  loaded file saved untouched keeps its content (values, hex strings such
  as `"0x2000"`, nested sub-registers and raw peripheral JSON). Saving
  re-serializes with 2-space indentation, which normalizes key order and
  line breaks.

Two-way launch: **Launch Simulator** in the configurator saves the current
configuration and starts `cpu_sim` with it; **Edit in Configurator** in the
simulator's Control Tower starts `cpu_configurator` with the configuration
the simulator loaded. Both buttons look for the sibling binary next to the
running executable and report a clear message when it has not been built
yet.

---

## Operating the Simulator

### Loading an Architecture Configuration
Start the GUI executable by passing the relative path to an architecture configuration file:
```bash
./cpu_sim ../configs/8bit.json
```

### Visual Interface Guide
Once loaded, the graphical workspace organizes execution monitoring into specialized windows:

* **Control Tower:** Provides physical clock speed control, execution modes (run by whole instructions or single microcode clock cycles), and state triggers. It also shows the loaded configuration file and an **Edit in Configurator** button, which opens the configuration in `cpu_configurator`.
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

The suite also covers configurator previews, diagnostics, editors, undo history and settings. To execute all registered tests, run the following command within your build directory:
```bash
ctest --output-on-failure
```
