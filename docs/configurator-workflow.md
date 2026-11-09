# Graphical configurator: processor workflow

This workflow builds on the [configurator guide](configurator-guide.md). To get started, open `configs/8bit.json` and save a copy with **Save As**. [Česky](configurator-workflow.cs.md)

## 1. System, registers and memory

On **System**, set the name and data and address bus widths. On **Registers**, add registers and assign the appropriate program counter, stack pointer and flags roles. Sub-registers can select a contiguous range with an offset or individual bits with a mask. Check overlaps in the bit display.

On **Memory**, choose unified or separate instruction and data memory, byte order and segments. Set the range and R, W and X permissions for each segment. See the [configuration reference](../CONFIGURATION.md) for field details and limits.

## 2. ALU and instructions

On **ALU**, define flags and operations. An operation can use an expression or a program; its latency sets its cycle count. Inputs `a`, `b` and `c` are supplied by the instruction's ALU micro-operation arguments.

On **Instructions**, add an instruction, set its opcode and encoding, and add micro-operations. The editor synchronizes the opcode with the first encoding element. After selecting an action, choose its arguments, registers and operands; literal and custom values have their own options. Changing an action removes arguments that do not belong to the new action.

The instruction preview shows operands, micro-operations and timing. Latency modes are `dynamic`, `strict`, `bottleneck` and `additive`. Without an instruction latency, `dynamic` is used. See the [micro-operation reference](../CONFIGURATION.md#53-microop) for exact fields and arguments.

## 3. Peripherals

On **Peripherals**, add a device and its registers. Parameters and internal state are edited as `key = value` lines. Edit `on_read`, `on_write` and `tick_behavior` in **Blocks** or **Text** mode.

In the block editor, insert `if`, `assign` or `call` from the palette; drag blocks or move them with buttons. A condition can contain then and else branches. Unknown node types are preserved and displayed in grey; edit them in text mode. Example buttons insert a sample peripheral or hook.

## 4. Validate and run

Open **Problems**, review errors and click each one to navigate to its field. After fixing errors, save the file and choose **Launch Simulator**. In the simulator, try the configuration with an assembly program and inspect registers, memory, micro-operations and peripherals.
