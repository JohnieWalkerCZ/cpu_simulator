#pragma once
#include "ui_common.hpp"

inline void UI_MemoryView(CPU &cpu, GUIState &gui) {
    ImGui::Begin("Memory Explorer");
    ImGui::Checkbox("Show Full Address Space", &gui.show_full_memory);
    ImGui::Separator();

    auto &mem = cpu.get_memory();
    uint32_t pc = (uint32_t)cpu.get_registers().get_pc();

    ImGui::BeginChild("MemScroll");

    uint32_t display_limit = static_cast<uint32_t>(mem.size());
    if (!gui.show_full_memory && display_limit > 256) {
        display_limit = 256;
    }

    for (uint32_t i = 0; i < display_limit; i += 8) {
        ImGui::TextDisabled("%04X: ", i);
        ImGui::SameLine();

        for (uint32_t j = 0; j < 8 && (i + j) < display_limit; j++) {
            uint32_t addr = i + j;
            uint8_t val = mem.raw()[addr];

            if (addr == pc && !mem.is_harvard())
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 1, 0, 1));
            ImGui::Text("%02X", val);
            if (addr == pc && !mem.is_harvard())
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

inline void UI_ProgramView(CPU &cpu, GUIState &gui) {
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

    uint32_t pc = static_cast<uint32_t>(regs.get_pc());
    uint32_t curr_addr = cpu.get_load_address();
    uint32_t end_addr =
        curr_addr + static_cast<uint32_t>(cpu.get_code().size());
    int unit_bits = config.data_width;
    int unit_bytes = (config.data_width + 7) / 8;

    auto get_reg_name = [&](int idx) -> std::string {
        if (idx >= 0 && idx < config.registers.size())
            return config.registers[idx].name;
        return "?";
    };

    while (curr_addr < end_addr) {
        uint64_t first_unit = 0;
        try {
            first_unit = mem.read(curr_addr, true) & ((1ULL << unit_bits) - 1);
        } catch (const std::exception &) {
            try {
                first_unit =
                    mem.read(curr_addr, false) & ((1ULL << unit_bits) - 1);
            } catch (const std::exception &) {
                curr_addr += unit_bytes;
                continue;
            }
        }

        uint8_t opcode = decoder.peek_opcode(first_unit);
        int total_bits = decoder.get_total_bits(opcode);
        int units = (total_bits + unit_bits - 1) / unit_bits;

        if (units <= 0)
            units = 1;

        uint64_t raw = first_unit;
        for (int i = 1; i < units && (curr_addr + i * unit_bytes) < mem.size();
             ++i) {
            uint64_t next_unit = 0;
            try {
                next_unit = mem.read(curr_addr + i * unit_bytes, true) &
                            ((1ULL << unit_bits) - 1);
            } catch (const std::exception &) {
                try {
                    next_unit = mem.read(curr_addr + i * unit_bytes, false) &
                                ((1ULL << unit_bits) - 1);
                } catch (const std::exception &) {
                    next_unit = 0;
                }
            }
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

        ImGui::PushID(curr_addr);
        bool is_bp = gui.breakpoints.count(curr_addr);
        if (is_bp)
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), " (O) ");
        else
            ImGui::TextDisabled("  -  ");

        if (ImGui::IsItemClicked()) {
            if (is_bp)
                gui.breakpoints.erase(curr_addr);
            else
                gui.breakpoints.insert(curr_addr);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Toggle Breakpoint");
        ImGui::PopID();
        ImGui::SameLine();

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

        curr_addr += units * unit_bytes;
    }

    ImGui::End();
}

inline void UI_FlashROMView(CPU &cpu, GUIState &gui) {
    ImGui::Begin("Instruction ROM (Flash)");

    auto &mem = cpu.get_memory();
    if (!mem.is_harvard()) {
        ImGui::TextWrapped("Von Neumann Architecture is Active.");
        ImGui::TextDisabled("Instruction Memory is unified with Data RAM. "
                            "Check the 'Memory Explorer' window.");
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Show Full Address Space##flash", &gui.show_full_memory);
    ImGui::Separator();

    uint32_t pc = (uint32_t)cpu.get_registers().get_pc();
    const auto &raw_rom = mem.raw_instruction();

    ImGui::BeginChild("FlashScroll");

    uint32_t display_limit = static_cast<uint32_t>(raw_rom.size());
    if (!gui.show_full_memory && display_limit > 256) {
        display_limit = 256;
    }

    for (uint32_t i = 0; i < display_limit; i += 8) {
        ImGui::TextDisabled("%04X: ", i);
        ImGui::SameLine();

        for (uint32_t j = 0; j < 8 && (i + j) < display_limit; j++) {
            uint32_t addr = i + j;
            uint8_t val = raw_rom[addr];

            if (addr == pc)
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(0.2f, 1.0f, 0.2f, 1.0f));
            ImGui::Text("%02X", val);
            if (addr == pc)
                ImGui::PopStyleColor();

            ImGui::SameLine();
        }
        ImGui::NewLine();
    }

    if (!gui.show_full_memory && raw_rom.size() > 256) {
        ImGui::TextDisabled("... (Use checkbox to see all %zu bytes) ...",
                            raw_rom.size());
    }

    ImGui::EndChild();
    ImGui::End();
}
