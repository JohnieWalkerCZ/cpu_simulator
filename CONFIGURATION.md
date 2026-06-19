# Configuration Options for the CPU Sandbox Simulator

This document details the structure and meaning of the JSON configuration files used to define custom CPU architectures for the Modular CPU Sandbox simulator. By modifying these JSON files, you can define everything from the fundamental bus widths and register sets to complex instruction formats and even the behavior of memory-mapped I/O peripherals.

---

## 1. Top-Level Structure

The root of each configuration file is a JSON object containing the primary architectural parameters.

```json
{
  "name": "ExampleISA",
  "data_bus": { ... },
  "address_bus": { ... },
  "memory": { ... },
  "registers": { ... },
  "alu": { ... },
  "instruction_set": { ... },
  "peripherals": [ ... ]
}
```

### 1.1. `name`
*   **Type:** `string`
*   **Description:** A human-readable identifier for the simulated CPU architecture. This name is primarily for informational purposes.

### 1.2. `data_bus`
*   **Type:** `object`
*   **Description:** Defines the primary data bus width for the architecture.
    *   **`width`** (`integer`): The native data width in bits. Must be one of `4`, `8`, `16`, `32`, or `64`.
        *   *Impact:* This is a fundamental setting that influences many other parts of the CPU:
            *   The default width for ALU operations.
            *   The size of individual memory words fetched by default.
            *   The width of immediate operands that are not explicitly sized (e.g., `imm8`, `imm16`).
            *   The default width for general register reads/writes if not otherwise specified by the register definition.

### 1.3. `address_bus`
*   **Type:** `object`
*   **Description:** Defines the width of the CPU's address bus.
    *   **`width`** (`integer`): The address bus width in bits. Must be between `4` and `64`.
        *   *Impact:* This determines the maximum amount of physical memory the CPU can address. For example, a 16-bit address bus allows for `2^16 = 65,536` unique addresses. It also dictates the size of fields specified as `"address"` in instruction encodings.

---

## 2. Memory Architecture & Segmenting

The `memory` object defines the memory map, size, and access policies.

### 2.1. `memory`
*   **Type:** `object`
*   **Description:** Defines the memory subsystem parameters.
    *   **`size`** (`integer`): The total size of the primary data memory in bytes. This defines the upper bound of the addressable memory space (e.g., `2^address_bus_width`).
    *   **`architecture`** (`string: `"von_neumann"` | `"harvard"`):
        *   `"von_neumann"` (Default): A single memory space is used for both instructions and data.
        *   `"harvard"`: A separate instruction memory space (ROM) is maintained alongside the data memory. Writes to instruction memory are disallowed in Harvard mode, and reads from instruction memory use a dedicated path if available.
    *   **`endianness`** (`string: `"little"` | `"big"`):
        *   `"little"` (Default): The least significant byte is stored at the lowest memory address.
        *   `"big"`: The most significant byte is stored at the lowest memory address.
        *   *Impact:* Affects how multi-byte values (e.g., 16-bit registers or immediate values) are laid out in memory.
    *   **`segments`** (`array` of `MemorySegmentDef` objects):
        *   *Description:* Defines specific regions within the total memory space with distinct access permissions. If this array is empty, a single default segment spanning the entire memory size is created with read, write, and execute permissions.
        *   *See `MemorySegmentDef` below.*

### 2.2. `MemorySegmentDef`
*   **Type:** `object`
*   **Description:** Defines a contiguous block of memory with specific access rules.
    *   **`name`** (`string`): A human-readable name for the segment (e.g., `"ROM"`, `"RAM"`, `"STACK"`).
    *   **`start`** (`string` or `integer`): The starting memory address of the segment. Can be specified in decimal or hexadecimal (e.g., `"0x0000"`). Supports full **64-bit address limits**.
    *   **`end`** (`string` or `integer`): The ending memory address of the segment. Can be specified in decimal or hexadecimal. Supports full **64-bit address limits**.
    *   **`R`** (`boolean`): If `true`, read access is permitted within this segment.
    *   **`W`** (`boolean`): If `true`, write access is permitted within this segment.
    *   **`X`** (`boolean`): If `true`, execute access is permitted within this segment.
        *   *Impact:* The simulator checks these permissions on every memory read, write, or instruction fetch. Violations will halt the CPU and generate a specific error message.

---

## 3. Registers

The `registers` object defines all the registers accessible to the CPU, categorized into general-purpose and special-purpose registers.

### 3.1. `registers`
*   **Type:** `object`
*   **Description:** Container for all register definitions.
    *   **`general_purpose`** (`array` of `RegisterDef` objects): Registers typically used for data manipulation.
    *   **`special`** (`array` of `RegisterDef` objects): Registers with specific roles tied to CPU control or functionality (e.g., PC, SP, Flags).

### 3.2. `RegisterDef`
*   **Type:** `object`
*   **Description:** Defines a single register.
    *   **`name`** (`string`): The unique identifier for this register. This is used in assembly code and internally by the simulator.
    *   **`width`** (`integer`): The bit-width of this register.
    *   **`initial`** (`integer`): The value the register should be initialized to upon CPU reset. Supports full 64-bit initialization.
    *   **`role`** (`string`, optional): Assigns a special role to the register. Supported roles include:
        *   `"program_counter"` or `"pc"`: The instruction pointer.
        *   `"stack_pointer"` or `"sp"`: Points to the current top of the stack.
        *   `"status_flags"` or `"flags"`: Holds condition flags (Zero, Carry, Overflow, etc.).
        *   *Impact:* Registers with specific roles are used by the Executor for automatic stack operations, PC incrementing, and flag checking in conditional branches.
    *   **`is_coproc`** (`boolean`, optional, default: `false`): Marks this register as belonging to a coprocessor.
    *   **`coproc_id`** (`integer`, optional): Identifies the coprocessor unit if `is_coproc` is true.
    *   **`coproc_reg_id`** (`integer`, optional): Identifies the specific register within the coprocessor unit.
    *   **`sub_registers`** (`array` of `RegisterDef` objects, optional): Allows defining aliases or slices of the parent register. These are hierarchical. See below for detailed sub-register configuration.

### 3.3. `RegisterDef` (for `sub_registers`)
When defining sub-registers, the following properties are particularly important:
*   **`name`** (`string`): The name of the alias/slice.
*   **`width`** (`integer`): The bit-width of this alias.
*   **`offset`** (`integer`, optional): If specified, this sub-register represents a contiguous slice starting at the given bit offset from the parent's beginning. The `width` determines how many bits are taken from this offset.
*   **`mask`** (`string` or `integer`, optional): If specified, this sub-register represents a set of disjoint bits within the parent register. The `mask` defines which physical bits are included in the alias. The `width` of the sub-register must be equal to the number of set bits in the `mask`. When writing to a masked sub-register, only the bits specified by the mask in the parent register are affected.

---

## 4. ALU & Status Flags

The `alu` object configures the Arithmetic Logic Unit's operations and how status flags are managed.

### 4.1. `alu`
*   **Type:** `object`
*   **Description:** Contains definitions for ALU operations and status flags.
    *   **`flags`** (`array` of `FlagDef` objects): Defines the bits within the `$FLAGS` register.
    *   **`operations`** (`array` of `ALUOp` objects): Defines the available ALU operations.

### 4.2. `FlagDef`
*   **Type:** `object`
*   **Description:** Defines a single status flag.
    *   **`name`** (`string`): The short name for the flag (e.g., `"Z"`, `"C"`, `"N"`). This is used in assembly mnemonics like `JZ` (Jump if Zero).
    *   **`bit`** (`integer`): The bit position (0-indexed) within the `$FLAGS` register where this flag resides.
    *   **`type`** (`string`, optional): A semantic type for the flag, used for common flag evaluations (e.g., `"zero"`, `"carry"`, `"overflow"`, `"negative"`).
    *   **`expression`** (`string`, optional): A custom boolean expression that determines the flag's value based on ALU results. This expression is evaluated using the same parser as ALU operations. If `type` and `expression` are both absent, the flag is not automatically managed by ALU operations.

### 4.3. `ALUOp`
*   **Type:** `object`
*   **Description:** Defines a single ALU operation.
    *   **`name`** (`string`): The identifier for this ALU operation (e.g., `"ADD"`, `"SUB"`, `"AND"`). This name is used in microcode `"alu"` actions.
    *   **`code`** (`integer`, optional): A numeric code. Currently unused by the simulator but could be used for future instruction encoding schemes.
    *   **`expression`** (`string`): The core arithmetic or logical calculation for this operation. It takes operands `a`, `b`, and `c` (where `c` is typically unused) and produces a result. Examples: `"a + b"`, `"a - b"`, `"a & b"`, `"~a"`.
    *   **`flag_rules`** (`object`, optional): Maps flag names to their evaluation logic for this specific operation.
        *   Keys are flag names (must match names defined in `alu.flags`).
        *   Values can be:
            *   A semantic type string corresponding to built-in evaluation logic:
                *   `"result_zero"`: Evaluates to `true` if the final operation output is equal to 0.
                *   `"result_negative"`: Evaluates to `true` if the operation output's sign bit is active.
                *   `"carry_add"`: Sets if an unsigned carry-out occurs during addition.
                *   `"carry_sub"`: Sets if an unsigned borrow occurs during subtraction.
                *   `"overflow_add"`: Sets if a signed overflow occurs during addition.
                *   `"overflow_sub"`: Sets if a signed overflow occurs during subtraction.
            *   An empty string (`""`) or absent value: Indicates the flag is not affected by this operation.
            *   A custom expression string (e.g., `"!(res & 1)"`): A boolean expression evaluated to determine the flag's state. The result of the ALU operation is available as `res`.
    *   **`latency`** (`integer`, optional, default: `1`): The number of clock cycles this operation takes to complete. Used to introduce stalls in the microcode pipeline.

---

## 5. Instruction Set Architecture & Microcode

The `instruction_set` object defines the machine code and execution flow for the CPU.

### 5.1. `instruction_set`
*   **Type:** `object`
*   **Description:** Container for all instruction definitions.
    *   **`instructions`** (`array` of `Instruction` objects): Defines each individual instruction.

### 5.2. `Instruction`
*   **Type:** `object`
*   **Description:** Defines a single machine instruction.
    *   **`name`** (`string`): The mnemonic used for this instruction in assembly code (e.g., `"MOV"`, `"ADD"`, `"HLT"`).
    *   **`opcode`** (`integer`): The primary numeric opcode for this instruction.
    *   **`latency`** (`integer`, optional, default: `-1`): Sets the baseline clock cycle budget for the `EXECUTE_UOPS` phase. Used in tandem with `latency_mode`.
    *   **`latency_mode`** (`string`, optional, default: `"dynamic"`): Governs how instruction-level latency interacts with internal micro-operations and multi-cycle ALU functional units. Supported behaviors:
        *   `"dynamic"`: Standard sequential mode. The instruction execution phase takes exactly as many cycles as it has micro-operations, ignoring any instruction-level `latency` property.
        *   `"strict"` (or `"fixed"`): Enforces a rigid execution window of exactly `latency` cycles. If there are too many micro-ops, multiple operations are compressed and executed simultaneously in single clock cycles to fit the budget. If there are too few, execution stretches and padding cycles are automatically inserted.
        *   `"bottleneck"` (or `"max"`): Emulates a realistic variable-latency pipeline. The execution phase takes the maximum of either the instruction `latency` threshold or the cumulative cycle durations of the underlying micro-operations and stalled ALU units.
        *   `"additive"`: Appends the instruction-level `latency` value as static structural or write-back overhead cycles that run directly after the dynamic micro-operation sequence has fully completed.
    *   **`encoding`** (`array`): Defines how the instruction is laid out in binary. It is a sequence of tokens representing different parts of the instruction's bitstream.
        *   `integer` (>= 0): Represents a literal field. The value is directly encoded. The first literal token typically represents the instruction's opcode. Subsequent literals are often fixed bit fields.
        *   `string` representing a register token:
            *   `"dest"`: A destination register operand.
            *   `"src"`: A source register operand.
            *   `"addr_reg"`: A register used as a memory address.
        *   `string` representing an immediate token:
            *   `"imm8"`: An 8-bit immediate value.
            *   `"imm16"`: A 16-bit immediate value.
            *   `"address"`: An immediate value whose width is determined by the `address_bus.width`.
            *   `"offset"`: Typically an 8-bit signed offset.
            *   `"imm"`: A generic immediate value, defaulting to 8 bits.
        *   *Impact:* The sequence and type of tokens determine the instruction's binary format and how operands are extracted by the decoder. The assembler uses this to match assembly mnemonics and operands to the correct binary encoding.
    *   **`microcode`** (`array` of `MicroOp` objects): A sequence of low-level operations that constitute the execution of this instruction. The executor processes these operations one by one.
        *   *See `MicroOp` below.*

### 5.3. `MicroOp`
*   **Type:** `object`
*   **Description:** Defines a single microcode operation.
    *   **`action`** (`string`): The type of action to perform. Supported actions:
        *   `"copy"`: Copy data from a source to a destination.
        *   `"alu"`: Perform an ALU operation.
        *   `"mem_read"`: Read data from memory.
        *   `"mem_write"`: Write data to memory.
        *   `"port_read"`: Read from an I/O port.
        *   `"port_write"`: Write to an I/O port.
        *   `"coproc_read"`: Read from a coprocessor register.
        *   `"coproc_write"`: Write to a coprocessor register.
        *   `"branch"`: Perform a conditional or unconditional jump.
        *   `"halt"`: Stop CPU execution.
    *   **`args`** (`object`): A map of key-value pairs providing arguments specific to the `action`. Argument names and meanings depend on the `action` type:
        *   **General arguments:**
            *   `"source"` / `"dest"` / `"out"`: The operand to read from/write to. Can be register names (e.g., `"$R0"`, `"$PC"`, `"$SP"`, `"$FLAGS"`), memory access via register (`"@dest"` where `dest` is a register token from the instruction encoding), or literal values (`"#42"`).
            *   `"a"`, `"b"`, `"c"`: Operands for ALU operations.
            *   `"addr"`: Memory address for memory access operations.
            *   `"data"`: Data value for memory/port/coproc writes.
            *   `"op"`: Name of the ALU operation to execute (must match an `ALUOp.name`).
            *   `"update_flags"` (`"true"`/`"false"`): If `"true"`, the ALU operation's flag results will be written to the `$FLAGS` register.
            *   `"condition"`: For `"branch"` actions, specifies the flag condition (e.g., `"Z"`, `"!Z"`, `"C"`, `"O"`).
            *   `"target"`: The address for branch operations.
            *   `"relative"` (`"true"`/`"false"`): If `"true"` for branches, the target is relative to the current PC.
            *   `"cp"`: Coprocessor ID for coproc operations.
            *   `"reg"`: Coprocessor register ID for coproc operations.
            *   `"port"`: I/O port number.
        *   **Operand Resolution:** Values in `args` can refer to:
            *   Literal constants: Prefixed with `#` (e.g., `"#42"`, `"#1"`). Special literals like `"#WORD_SIZE"` and `"#ADDR_SIZE"` are supported.
            *   Special registers: Prefixed with `$` (e.g., `"$PC"`, `"$SP"`, `"$FLAGS"`, `"$NEXT_PC"`).
            *   Instruction operands: Prefixed with `@` (e.g., `"@dest"`, `"@imm8"`). These refer to operands decoded from the instruction bitstream based on the `encoding` field.
        *   *Impact:* The microcode defines the step-by-step behavior of each instruction, enabling complex instruction logic and efficient pipeline utilization.

---

## 6. Declarative MMIO Peripherals

The `peripherals` array allows defining custom hardware components that interact with the CPU via memory-mapped I/O (MMIO).

### 6.1. `PeripheralDef`
*   **Type:** `object`
*   **Description:** Defines a single peripheral device.
    *   **`name`** (`string`): A unique name for this peripheral.
    *   **`type`** (`string`): The type of peripheral. Supported types include:
        *   `"text_display"`: Simulates a simple serial console output.
        *   `"grid_display"`: Renders an LED matrix / grid display widget in the UI.
        *   `"input"`: Renders a host key-press input widget in the UI.
        *   `"declarative"`: A highly flexible peripheral whose behavior is defined by AST logic.
        *   **Note:** Only `"text_display"` and `"declarative"` are currently wired into the memory-mapped I/O bus (via `Memory::map_io_region`), so a running program can actually read/write them at `address_start`-`address_end`. `"grid_display"` and `"input"` currently exist only as UI widgets in the I/O Peripherals panel (backed by local UI state) and are not yet connected to CPU memory reads/writes.
    *   **`address_start`** (`string` or `integer`): The starting MMIO address for this peripheral. Supports full **64-bit address limits**.
    *   **`address_end`** (`string` or `integer`): The ending MMIO address for this peripheral. Supports full **64-bit address limits**.
    *   **`parameters`** (`object`, optional): A key-value map of configuration parameters for certain peripheral types (e.g., `"width"` for a `"grid_display"`).
    *   **`registers`** (`array` of `PeripheralRegisterDef` objects, optional): Defines memory-mapped registers within the peripheral.
    *   **`internal_state`** (`object` of key-value pairs, optional): Defines internal hardware state variables for `"declarative"` peripherals.
    *   **`tick_behavior`** (`json` array, optional): AST logic executed on each CPU clock cycle (for `"declarative"` peripherals).

### 6.2. `PeripheralRegisterDef`
*   **Type:** `object`
*   **Description:** Defines a single MMIO register within a peripheral.
    *   **`name`** (`string`): The name of the register (used in AST logic).
    *   **`offset`** (`integer`): The offset from the peripheral's `address_start` to this register. Supports full 64-bit address offsets.
    *   **`size_bytes`** (`integer`, optional, default: `1`): The size of the register in bytes.
    *   **`access`** (`string`, optional, default: `"rw"`): Defines read/write permissions (e.g., `"r"`, `"w"`, `"rw"`).
    *   **`initial`** (`integer`): The reset value of the register.
    *   **`on_read`** (`json` array, optional): AST logic executed when this register is read. The current register value is available as `value` in the AST context.
    *   **`on_write`** (`json` array, optional): AST logic executed when this register is written to. The written value is available as `value` in the AST context.

### 6.3. AST Logic Structure (for `on_read`, `on_write`, `tick_behavior`)
The AST (Abstract Syntax Tree) logic is represented as a JSON array of nodes. Each node is an object with a `"type"` field. Supported node types:

*   **`"if"`**: Conditional execution.
    *   `"condition"` (`string`): An expression to evaluate.
    *   `"then"` (`array`): AST nodes to execute if the condition is true.
    *   `"else"` (`array`, optional): AST nodes to execute if the condition is false.
*   **`"assign"`**: Assigns a value to a variable.
    *   `"target"` (`string`): The name of the variable (register, internal state, or `value` from context) to assign to.
    *   `"expr"` (`string`): The expression to evaluate for the new value.
*   **`"call"`**: Calls a built-in function or method.
    *   `"func"` (`string`): The name of the function to call (e.g., `"trigger_interrupt"`, `"sys_write"`, `"host_print"`).
    *   `"args"` (`array` of `string`, optional): Arguments for the function, each being an expression.

#### Expression Syntax
The expressions used in `"condition"`, `"expr"`, and function arguments follow a C-like syntax:
*   **Operators:** `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `<<`, `>>`, `==`, `!=`, `>=`, `<=`, `>`, `<`, `&&`, `||`, `!`, `~` (unary).
*   **Primary Expressions:**
    *   Literals (decimal, hex `0x...`, binary `0b...`)
    *   Function calls usable anywhere inside an expression, returning a value: `sys_read(addr)` (reads a byte from CPU memory) and `host_pop_char()` (pops one buffered host input character via the peripheral's `host_pop_` hook). These are distinct from the `"call"` AST node type above, which invokes side-effecting functions (`trigger_interrupt`, `sys_write`, `host_print`) and discards any return value.
    *   Identifiers (register names, internal state names, `value` context)
    *   Parenthesized expressions `(...)`
