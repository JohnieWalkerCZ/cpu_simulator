#pragma once
#include "ui_common.hpp"
#include <string>

inline void UI_MicrocodePipeline(CPU &cpu) {
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
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "Raw Bits: 0x%s",
                           word_to_hex_string(current_inst.raw_bits, 128)
                               .c_str());
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
            ImGui::TextDisabled(
                "Raw: 0x%s",
                word_to_hex_string(current_inst.raw_bits,
                                   current_inst.length_bytes * 8)
                    .c_str());
            ImGui::Separator();

            std::string mode_str = "DYNAMIC (MICRO-OP)";
            ImVec4 mode_color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);

            if (inst_def->latency_mode == LatencyMode::STRICT) {
                mode_str = "STRICT (FIXED)";
                mode_color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            } else if (inst_def->latency_mode == LatencyMode::BOTTLENECK) {
                mode_str = "BOTTLENECK (MAX)";
                mode_color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
            } else if (inst_def->latency_mode == LatencyMode::ADDITIVE) {
                mode_str = "ADDITIVE (OVERHEAD)";
                mode_color = ImVec4(0.4f, 0.7f, 1.0f, 1.0f);
            }

            ImGui::Text("Latency Mode: ");
            ImGui::SameLine();
            ImGui::TextColored(mode_color, "%s", mode_str.c_str());

            if (inst_def->execution_latency > 0) {
                int curr_cyc = executor.get_current_execution_cycles();
                int tgt_cyc = inst_def->execution_latency;

                if (inst_def->latency_mode == LatencyMode::ADDITIVE) {
                    ImGui::Text("Execution Stall Progress: ");
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f),
                                       "%d Added Cycle(s) Pending", tgt_cyc);
                } else {
                    float progress = (tgt_cyc > 0)
                                         ? (static_cast<float>(curr_cyc) /
                                            static_cast<float>(tgt_cyc))
                                         : 1.0f;
                    if (progress > 1.0f)
                        progress = 1.0f;

                    char progress_label[32];
                    snprintf(progress_label, sizeof(progress_label),
                             "%d / %d Cycles", curr_cyc, tgt_cyc);

                    ImGui::Text("Execution Phase: ");
                    ImGui::SameLine();
                    ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f),
                                       progress_label);
                }
            } else {
                ImGui::TextDisabled("Execution Target: Dynamic duration");
            }
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
                                word_to_dec_string(current_inst.imms.at(token));
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
