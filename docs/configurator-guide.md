# Graphical configurator: user guide

The configurator edits a processor definition in JSON. See the [configuration reference](../CONFIGURATION.md) for field definitions and the [workflow](configurator-workflow.md) for a guided example. [Česky](configurator-guide.cs.md)

## Start

From the project root, build and start the editor:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target cpu_configurator
./build/cpu_configurator
./build/cpu_configurator configs/8bit.json
```

Without an argument, the editor opens a new template. Pass a path to open an existing configuration. Build requirements are in the [README](../README.md).

## Files and toolbar

- **New** creates a template; preset buttons load the supplied configurations.
- **Open** opens a file and lists recent files below it.
- **Save** saves the document, **Save As** uses another path, and **Export JSON** exports a valid configuration.
- **Launch Simulator** saves the configuration and starts the simulator. Build `cpu_sim` alongside the configurator first. The simulator's **Edit in Configurator** button opens the configuration again.
- **Undo** and **Redo** reverse or repeat up to 50 edits. Deletion and leaving unsaved changes require confirmation.

| Shortcut | Action |
| --- | --- |
| Ctrl+S | Save |
| Ctrl+Z | Undo |
| Ctrl+Shift+Z or Ctrl+Y | Redo |
| Ctrl+D | Duplicate the selected item in the active tab |

Shortcuts do not fire while editing a text field. The toolbar controls the UI scale; settings and recent files are stored in `~/.config/cpu_configurator/settings.txt`.

## Tabs

- **System**: name and bus widths.
- **Registers**: physical registers and sub-registers. A colored bit display shows usage and overlaps. The list supports filtering and duplication.
- **Memory**: memory architecture, byte order and segments with permissions.
- **ALU**: flags and operations defined by expressions or programs; operations have their own latency.
- **Instructions**: instructions, encoding and micro-operations. The preview shows the assembly form, operands and effective latency.
- **Problems**: diagnostics for the whole configuration. Click a problem to navigate to its item.
- **Peripherals**: peripherals, registers, state and read, write and tick behavior.
- **JSON**: edit the document directly. **Apply JSON** applies the text; **Reload from document** restores the text from the current document.

Hover over `(?)` for help with a field. The status line summarizes validation. Export and simulator launch are blocked while the configuration is invalid.

Saving serializes JSON with two-space indentation; key order and line breaks can change.
