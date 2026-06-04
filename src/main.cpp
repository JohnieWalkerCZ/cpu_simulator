#include "core/assembly/assembler.hpp"
#include "core/cpu.hpp"
#include "core/execution/decoder.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

struct GUIState {
    bool is_running = false;
    float clock_speed = 2.0f; // Hz
    double last_step_time = 0;
    bool show_full_memory = false;
    bool run_by_uop = false;

    char asm_source[8 * 1000] = "HLT";
    std::string asm_status = "";
    bool asm_error = false;

    std::string cpu_error_message = "";
};

struct PeripheralsState {
    std::unordered_map<std::string, std::string> console_buffers;
    std::unordered_map<std::string, std::vector<bool>> led_matrices;
    std::unordered_map<std::string, uint64_t> key_states;
};

void InitializePeripherals(CPU &cpu, PeripheralsState &p_state) {
    cpu.get_memory().reset_io_hooks();

    for (const auto &def : cpu.get_config().peripherals) {
        if (def.type == "text_display") {
            p_state.console_buffers[def.name] = "";
            cpu.get_memory().map_io_region(
                def.address_start, def.address_end, nullptr,
                [&p_state, name = def.name](uint32_t address, uint64_t val) {
                    if (val == '\n' || val == '\r')
                        p_state.console_buffers[name] += '\n';
                    else if (val >= 32 && val <= 126)
                        p_state.console_buffers[name] += (char)val;
                });
        }
    }

    auto &dec_peripherals = cpu.get_peripherals();
    for (auto &dp : dec_peripherals) {
        for (const auto &def : cpu.get_config().peripherals) {
            if (def.type == "declarative" && def.name == dp.get_name()) {

                dp.set_host_print_hook([&p_state, name = def.name](char c) {
                    p_state.console_buffers[name] += c;
                });
                dp.set_host_pop_hook([&p_state, name = def.name]() -> char {
                    char c = p_state.key_states[name];
                    p_state.key_states[name] = 0;
                    return c;
                });

                cpu.get_memory().map_io_region(
                    def.address_start, def.address_end,
                    [&dp, start = def.address_start](uint32_t addr) {
                        return dp.read(addr - start);
                    },
                    [&dp, start = def.address_start](uint32_t addr,
                                                     uint64_t val) {
                        dp.write(addr - start, val);
                    });
            }
        }
    }
}
void ResetPeripheralsState(const Config &config, PeripheralsState &p_state) {
    for (const auto &def : config.peripherals) {
        if (def.type == "text_display") {
            p_state.console_buffers[def.name].clear();
        } else if (def.type == "grid_display") {
            std::fill(p_state.led_matrices[def.name].begin(),
                      p_state.led_matrices[def.name].end(), false);
        } else if (def.type == "input") {
            p_state.key_states[def.name] = 0;
        }
    }
}

void DrawFlagLED(const char *label, bool active) {
    ImGui::BeginGroup();
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float size = 10.0f;
    float radius = size * 0.5f;
    ImVec2 center = ImVec2(pos.x + radius, pos.y + radius + 5);

    ImU32 color = active ? IM_COL32(0, 255, 0, 255) : IM_COL32(50, 50, 50, 255);
    ImU32 outline =
        active ? IM_COL32(100, 255, 100, 255) : IM_COL32(30, 30, 30, 255);

    draw_list->AddCircleFilled(center, radius, color);
    draw_list->AddCircle(center, radius, outline, 12, 2.0f);

    ImGui::Dummy(ImVec2(size + 5, size + 10));
    ImGui::SameLine();
    ImGui::Text("%s", label);
    ImGui::EndGroup();
}

void UI_RegisterFile(CPU &cpu) {
    ImGui::Begin("Register File");
    auto &regs = cpu.get_registers();

    if (ImGui::BeginTable("RegTable", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Register");
        ImGui::TableSetupColumn("Hex");
        ImGui::TableSetupColumn("Decimal");
        ImGui::TableSetupColumn("Role");
        ImGui::TableHeadersRow();

        for (const auto &def : regs.get_defs()) {
            uint64_t val = regs.read(def.name);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", def.name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "0x%0X",
                               (unsigned int)val);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%llu", val);

            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("%s",
                                def.role.empty() ? "-" : def.role.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void UI_MicrocodePipeline(CPU &cpu) {
    ImGui::Begin("Micro-op Pipeline");
    auto &executor = cpu.get_executor();
    const auto &current_inst = executor.get_current_inst();
    auto state = executor.get_state();
    auto &reg_file = cpu.get_registers();
    auto &reg_defs = reg_file.get_defs();

    if (state == ExecutionState::FETCH) {
        ImGui::TextDisabled("State: FETCH NEXT...");

    } else if (state == ExecutionState::DECODE) {
        ImGui::Text("Status: Decoding Instruction...");
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Raw Bits: 0x%llX",
                           current_inst.raw_bits);
        ImGui::Separator();
        ImGui::TextDisabled("Identifying opcode and operands...");
    } else {
        const Instruction *inst_def = nullptr;
        for (const auto &ins : cpu.get_config().instructions) {
            if (ins.opcode == current_inst.opcode) {
                inst_def = &ins;
                break;
            }
        }

        if (inst_def) {
            ImGui::Text("Instruction: %s (0x%02X)", inst_def->name.c_str(),
                        current_inst.opcode);
            ImGui::TextDisabled("Raw: 0x%llX", current_inst.raw_bits);
            ImGui::Separator();

            for (size_t i = 0; i < inst_def->microcode.size(); ++i) {
                bool is_current = (i == executor.get_current_uop_index() &&
                                   (state == ExecutionState::EXECUTE_UOPS ||
                                    state == ExecutionState::DONE));

                if (is_current)
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImVec4(1.0f, 1.0f, 0.0f, 1.0f));

                ImGui::BulletText("[%s]",
                                  inst_def->microcode[i].action.c_str());

                for (auto const &[key, val] : inst_def->microcode[i].args) {
                    ImGui::SameLine();

                    std::string resolved_val = val;

                    if (val[0] == '@') {
                        std::string token = val.substr(1);
                        if (current_inst.regs.count(token)) {
                            int reg_idx = current_inst.regs.at(token);
                            resolved_val = (reg_idx < reg_defs.size())
                                               ? reg_defs[reg_idx].name
                                               : "ERR";
                        } else if (current_inst.imms.count(token)) {
                            resolved_val =
                                std::to_string(current_inst.imms.at(token));
                        }
                    } else if (val[0] == '$') {
                        resolved_val = val.substr(1);
                    } else if (val[0] == '#') {
                        resolved_val = val.substr(1);
                    }

                    ImGui::TextDisabled("%s:", key.c_str());
                    ImGui::SameLine(0, 2);
                    ImGui::Text("%s", resolved_val.c_str());
                }

                if (is_current) {
                    ImGui::SameLine();

                    int latency = executor.get_current_uop_latency();
                    if (latency > 1) {
                        ImGui::TextColored(
                            ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "(Cycle %d/%d)",
                            executor.get_current_uop_cycles() + 1, latency);
                        ImGui::SameLine();
                    }

                    ImGui::Text(" <--");
                    ImGui::PopStyleColor();
                }
            }
        } else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Unknown Opcode: 0x%02X",
                               current_inst.opcode);
        }
    }
    ImGui::End();
}

void UI_ALUMonitor(CPU &cpu) {
    ImGui::Begin("ALU Monitor");
    auto &cfg = cpu.get_config();
    int flags_idx = cpu.get_registers().find_by_role("status_flags");
    uint64_t current_flags =
        (flags_idx != -1) ? cpu.get_registers().read(flags_idx) : 0;

    ImGui::Text("Status Flags (from $FLAGS)");
    ImGui::Separator();
    if (flags_idx == -1) {
        ImGui::TextDisabled("No FLAGS register defined.");
    } else {
        for (const auto &f : cfg.alu_flags) {
            bool active = (current_flags >> f.bit) & 1;
            DrawFlagLED(f.name.c_str(), active);
            ImGui::SameLine(0, 20);
        }
    }
    ImGui::NewLine();
    ImGui::Separator();
    ImGui::Text("ALU Operations Configured: %zu", cfg.alu_ops.size());
    ImGui::End();
}

void UI_MemoryView(CPU &cpu, GUIState &gui) {
    ImGui::Begin("Memory Explorer");
    ImGui::Checkbox("Show Full Address Space", &gui.show_full_memory);
    ImGui::Separator();

    auto &mem = cpu.get_memory();
    uint32_t pc = (uint32_t)cpu.get_registers().get_pc();

    ImGui::BeginChild("MemScroll");

    uint32_t display_limit = mem.size();
    if (!gui.show_full_memory && display_limit > 256) {
        display_limit = 256;
    }

    for (uint32_t i = 0; i < display_limit; i += 8) {
        ImGui::TextDisabled("%04X: ", i);
        ImGui::SameLine();

        for (uint32_t j = 0; j < 8 && (i + j) < display_limit; j++) {
            uint32_t addr = i + j;
            uint8_t val = mem.raw()[addr];

            if (addr == pc)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 1, 0, 1));
            ImGui::Text("%02X", val);
            if (addr == pc)
                ImGui::PopStyleColor();

            ImGui::SameLine();
        }
        ImGui::NewLine();
    }

    if (!gui.show_full_memory && mem.size() > 256) {
        ImGui::TextDisabled("... (Use checkbox to see all %zu bytes) ...",
                            mem.size());
    }

    ImGui::EndChild();
    ImGui::End();
}

void UI_ControlTower(CPU &cpu, GUIState &state, PeripheralsState &p_state) {
    ImGui::Begin("Control Tower");

    if (state.is_running) {
        if (ImGui::Button("PAUSE", ImVec2(80, 0)))
            state.is_running = false;
    } else {
        if (ImGui::Button("RUN", ImVec2(80, 0)))
            state.is_running = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("STEP INST") && state.cpu_error_message == "")
        try {
            cpu.step();
        } catch (const std::exception &e) {
            state.is_running = false;
            state.cpu_error_message = e.what();
        }

    ImGui::SameLine();
    if (ImGui::Button("STEP UOP") && state.cpu_error_message == "")
        try {
            cpu.step_uop();
        } catch (const std::exception &e) {
            state.is_running = false;
            state.cpu_error_message = e.what();
        }

    ImGui::SameLine();
    if (ImGui::Button("RESET")) {
        cpu.reset();
        ResetPeripheralsState(cpu.get_config(), p_state);
        state.cpu_error_message = "";
    }

    if (!state.cpu_error_message.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "HARDWARE FAULT:");
        ImGui::TextWrapped("%s", state.cpu_error_message.c_str());
    }

    ImGui::Separator();
    ImGui::Text("Execution mode:");
    if (ImGui::RadioButton("Instruction-by-Instruction", !state.run_by_uop)) {
        state.run_by_uop = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Micro-op (Cycle-by-Cycle)", state.run_by_uop)) {
        state.run_by_uop = true;
    }

    ImGui::Separator();
    const char *speed_unit = state.run_by_uop ? "%.1f Hz (Clock Cycles)"
                                              : "%.1f IPS (Instructions/s)";
    ImGui::SliderFloat("Speed", &state.clock_speed, 0.5f, 100.0f, speed_unit);

    ImGui::Separator();
    ImGui::Text("Total Cycles: %d", cpu.get_executor().get_total_cycles());

    auto exec_state = cpu.get_executor().get_state();
    const char *state_str = "UNKNOWN";
    if (exec_state == ExecutionState::FETCH)
        state_str = "FETCH";
    else if (exec_state == ExecutionState::DECODE)
        state_str = "DECODE";
    else if (exec_state == ExecutionState::EXECUTE_UOPS)
        state_str = "EXECUTE_UOPS";
    else if (exec_state == ExecutionState::DONE)
        state_str = "DONE";

    ImGui::Text("Pipeline State: ");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s", state_str);

    if (cpu.is_halted()) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "CPU STATUS: HALTED");
    }

    ImGui::End();
}

void UI_Assembler(CPU &cpu, GUIState &gui, PeripheralsState &p_state) {
    ImGui::Begin("Live Assembler");

    if (ImGui::BeginTable("AssemblerLayout", 2,
                          ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_Resizable)) {

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);

        ImGui::TextDisabled("Write custom ISA assembly here:");
        ImGui::InputTextMultiline(
            "##source", gui.asm_source, sizeof(gui.asm_source),
            ImVec2(-1.0f, ImGui::GetTextLineHeight() * 40),
            ImGuiInputTextFlags_AllowTabInput);

        if (ImGui::Button("Assemble & Load Program", ImVec2(-1.0f, 30))) {
            try {
                Assembler assembler(cpu.get_config());
                auto machine_code =
                    assembler.assemble(std::string(gui.asm_source), 0);
                cpu.load_program(machine_code, 0);
                cpu.reset();
                ResetPeripheralsState(cpu.get_config(), p_state);

                gui.asm_status = "Success! Compiled " +
                                 std::to_string(machine_code.size()) +
                                 " memory units.";
                gui.asm_error = false;
            } catch (const std::exception &e) {
                gui.asm_status = std::string("Error: ") + e.what();
                gui.asm_error = true;
            }
        }

        if (!gui.asm_status.empty()) {
            ImGui::Separator();
            ImVec4 color = gui.asm_error ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                         : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
            ImGui::TextColored(color, "%s", gui.asm_status.c_str());
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::Text("Architecture Reference Guide");
        ImGui::Separator();

        ImGui::BeginChild("InstGuideScroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);

        if (ImGui::BeginTable("InstTable", 2,
                              ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Mnemonic",
                                    ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Operands");
            ImGui::TableHeadersRow();

            for (const auto &inst : cpu.get_config().instructions) {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s",
                                   inst.name.c_str());

                ImGui::TableSetColumnIndex(1);

                std::string operands = "";
                bool first_op = true;

                for (int enc : inst.encoding) {
                    if (enc >= 0)
                        continue;

                    if (!first_op)
                        operands += ", ";

                    if (enc == -1)
                        operands += "dest";
                    else if (enc == -2)
                        operands += "src";
                    else if (enc == -3)
                        operands += "addr_reg";
                    else if (enc == -4)
                        operands += "offset";
                    else if (enc == -5)
                        operands += "imm8";
                    else if (enc == -6)
                        operands += "imm16";
                    else if (enc == -7)
                        operands += "address";
                    else
                        operands += "?";

                    first_op = false;
                }

                if (operands.empty()) {
                    ImGui::TextDisabled("-");
                } else {
                    ImGui::Text("%s", operands.c_str());
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::EndTable();
    }
    ImGui::End();
}

void UI_ProgramView(CPU &cpu) {
    ImGui::Begin("Program Execution");

    if (cpu.get_code().empty()) {
        ImGui::TextDisabled("No program currently loaded.");
        ImGui::End();
        return;
    }

    auto &config = cpu.get_config();
    auto &mem = cpu.get_memory();
    auto &regs = cpu.get_registers();
    Decoder decoder(config);

    uint32_t pc = regs.get_pc();
    uint32_t curr_addr = cpu.get_load_address();
    uint32_t end_addr = curr_addr + cpu.get_code().size();
    int unit_bits = config.data_width;

    auto get_reg_name = [&](int idx) -> std::string {
        if (idx >= 0 && idx < config.registers.size())
            return config.registers[idx].name;
        return "?";
    };

    while (curr_addr < end_addr) {
        uint64_t first_unit = mem.read(curr_addr) & ((1ULL << unit_bits) - 1);
        uint8_t opcode = decoder.peek_opcode(first_unit);
        int total_bits = decoder.get_total_bits(opcode);
        int units = (total_bits + unit_bits - 1) / unit_bits;

        if (units <= 0)
            units = 1;

        uint64_t raw = first_unit;
        for (int i = 1; i < units && (curr_addr + i) < mem.size(); ++i) {
            uint64_t next_unit =
                mem.read(curr_addr + i) & ((1ULL << unit_bits) - 1);
            raw = (raw << unit_bits) | next_unit;
        }

        DecodedInstruction dec = decoder.decode(raw, units * unit_bits);
        std::string asm_line;

        if (!dec.is_valid) {
            asm_line = "??? (Data)";
            units = 1;
        } else {
            asm_line = dec.name;

            const Instruction *inst_def = nullptr;
            for (const auto &ins : config.instructions) {
                if (ins.opcode == dec.opcode) {
                    inst_def = &ins;
                    break;
                }
            }

            if (inst_def) {
                bool first_op = true;
                for (int enc : inst_def->encoding) {
                    if (enc >= 0)
                        continue;

                    if (first_op) {
                        asm_line += " ";
                        first_op = false;
                    } else {
                        asm_line += ", ";
                    }

                    char buf[32];
                    if (enc == -1 && dec.regs.count("dest"))
                        asm_line += get_reg_name(dec.regs.at("dest"));
                    else if (enc == -2 && dec.regs.count("src"))
                        asm_line += get_reg_name(dec.regs.at("src"));
                    else if (enc == -3 && dec.regs.count("addr_reg"))
                        asm_line += get_reg_name(dec.regs.at("addr_reg"));
                    else if (enc == -4 && dec.imms.count("offset"))
                        asm_line +=
                            std::to_string((int8_t)dec.imms.at("offset"));
                    else if (enc == -5 && dec.imms.count("imm8"))
                        asm_line += std::to_string(dec.imms.at("imm8"));
                    else if (enc == -6 && dec.imms.count("imm16"))
                        asm_line += std::to_string(dec.imms.at("imm16"));
                    else if (enc == -7 && dec.imms.count("address")) {
                        snprintf(buf, sizeof(buf), "0x%X",
                                 (uint32_t)dec.imms.at("address"));
                        asm_line += buf;
                    }
                }
            }
        }

        bool is_current_pc = (curr_addr == pc);

        if (is_current_pc) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
            ImGui::Text("-> %04X: %s", curr_addr, asm_line.c_str());
            ImGui::PopStyleColor();

            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(0.5f);
        } else {
            ImGui::Text("   %04X: %s", curr_addr, asm_line.c_str());
        }

        curr_addr += units;
    }

    ImGui::End();
}

void UI_Peripherals(CPU &cpu, const Config &config, PeripheralsState &p_state) {
    if (config.peripherals.empty())
        return;

    ImGui::Begin("I/O Peripherals");

    for (const auto &def : config.peripherals) {
        if (ImGui::CollapsingHeader(def.name.c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {

            if (def.type == "text_display") {
                ImGui::TextDisabled("Mapped to 0x%04X", def.address_start);
                ImGui::BeginChild((def.name + "_scroll").c_str(),
                                  ImVec2(0, 100), true);
                ImGui::TextUnformatted(
                    p_state.console_buffers[def.name].c_str());
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
            }

            else if (def.type == "grid_display") {
                ImGui::TextDisabled("Mapped to 0x%04X - 0x%04X",
                                    def.address_start, def.address_end);
                int width = def.parameters.count("width")
                                ? std::stoi(def.parameters.at("width"))
                                : 8;

                ImDrawList *draw_list = ImGui::GetWindowDrawList();
                ImVec2 cursor = ImGui::GetCursorScreenPos();
                float cell_size = 20.0f;

                for (size_t i = 0; i < p_state.led_matrices[def.name].size();
                     ++i) {
                    float x = cursor.x + (i % width) * cell_size;
                    float y = cursor.y + (i / width) * cell_size;

                    bool on = p_state.led_matrices[def.name][i];
                    ImU32 color = on ? IM_COL32(50, 255, 50, 255)
                                     : IM_COL32(30, 30, 30, 255);

                    draw_list->AddRectFilled(
                        ImVec2(x, y),
                        ImVec2(x + cell_size - 2, y + cell_size - 2), color);
                }
                ImGui::Dummy(
                    ImVec2(width * cell_size,
                           (p_state.led_matrices[def.name].size() / width) *
                               cell_size));
            }

            else if (def.type == "input") {
                ImGui::TextDisabled("Mapped to 0x%04X", def.address_start);
                ImGui::Text("Press a key to send to CPU:");
                if (ImGui::Button(" A "))
                    p_state.key_states[def.name] = 'A';
                ImGui::SameLine();
                if (ImGui::Button(" B "))
                    p_state.key_states[def.name] = 'B';
                ImGui::SameLine();
                if (ImGui::Button(" ENT "))
                    p_state.key_states[def.name] = '\n';
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Buffer: %c",
                                   (char)p_state.key_states[def.name]);
            }

            else if (def.type == "declarative") {
                ImGui::TextDisabled("Mapped to 0x%04X - 0x%04X",
                                    def.address_start, def.address_end);

                DeclarativePeripheral *active_dp = nullptr;
                for (auto &dp : cpu.get_peripherals()) {
                    if (dp.get_name() == def.name) {
                        active_dp = &dp;
                        break;
                    }
                }

                if (active_dp) {
                    if (!def.registers.empty()) {
                        ImGui::Text("Memory-Mapped Registers:");
                        if (ImGui::BeginTable((def.name + "_regs").c_str(), 2,
                                              ImGuiTableFlags_Borders |
                                                  ImGuiTableFlags_RowBg)) {
                            ImGui::TableSetupColumn("Name (Offset)");
                            ImGui::TableSetupColumn("Value");
                            ImGui::TableHeadersRow();

                            const auto &live_regs = active_dp->get_registers();
                            for (const auto &rdef : def.registers) {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::Text("%s (+%d)", rdef.name.c_str(),
                                            rdef.offset);

                                ImGui::TableSetColumnIndex(1);
                                uint64_t val = live_regs.count(rdef.name)
                                                   ? live_regs.at(rdef.name)
                                                   : 0;
                                ImGui::TextColored(
                                    ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "0x%02X",
                                    (unsigned int)val);
                                ImGui::SameLine();
                                ImGui::TextDisabled("(%llu)", val);
                            }
                            ImGui::EndTable();
                        }
                    }

                    const auto &ivars = active_dp->get_internal_vars();
                    if (!ivars.empty()) {
                        ImGui::Spacing();
                        ImGui::Text("Internal Hardware State (Hidden):");
                        if (ImGui::BeginTable((def.name + "_ivars").c_str(), 2,
                                              ImGuiTableFlags_Borders |
                                                  ImGuiTableFlags_RowBg)) {
                            ImGui::TableSetupColumn("Variable");
                            ImGui::TableSetupColumn("Value");
                            ImGui::TableHeadersRow();

                            for (const auto &[var_name, val] : ivars) {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextDisabled("%s", var_name.c_str());

                                ImGui::TableSetColumnIndex(1);
                                ImGui::Text("%llu", val);
                            }
                            ImGui::EndTable();
                        }
                    }
                }
            }
            ImGui::Separator();
        }
    }
    ImGui::End();
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: cpu_sim <config.json>\n";
        return 1;
    }
    // 1. Initialize CPU
    Config cfg = Config::from_file(argv[1]);
    CPU cpu(cfg);
    PeripheralsState p_state;
    InitializePeripherals(cpu, p_state);

    // 2. Setup Graphics Boilerplate
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window *window = SDL_CreateWindow(
        "Modular CPU Sandbox", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Window Docking

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 130");

    GUIState gui;
    bool done = false;

    // 3. Main GUI Loop
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
        }

        if (gui.is_running && !cpu.is_halted() &&
            gui.cpu_error_message.empty()) {
            double current_time = SDL_GetTicks() / 1000.0;
            double interval = 1.0 / gui.clock_speed;

            if (current_time - gui.last_step_time >= interval) {
                try {
                    if (gui.run_by_uop) {
                        cpu.step_uop();
                    } else {
                        cpu.step();
                    }
                } catch (const std::exception &e) {
                    gui.is_running = false;
                    gui.cpu_error_message = e.what();
                }
                gui.last_step_time = current_time;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Background Dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // Draw the 5 design-spec windows
        UI_ControlTower(cpu, gui, p_state);
        UI_RegisterFile(cpu);
        UI_ALUMonitor(cpu);
        UI_MemoryView(cpu, gui);
        UI_MicrocodePipeline(cpu);
        UI_Assembler(cpu, gui, p_state);
        UI_ProgramView(cpu);
        UI_Peripherals(cpu, cfg, p_state);

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
