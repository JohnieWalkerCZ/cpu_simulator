#pragma once
#include "ui_common.hpp"
#include <iostream>
#include <string>
#include <vector>

inline void UI_RegisterFile(CPU &cpu, GUIState &gui) {
    ImGui::Begin("Register File");
    auto &regs = cpu.get_registers();
    const auto &reg_defs = regs.get_defs();

    ImGui::Checkbox("Show Register Bits (Interactive)",
                    &gui.show_register_bits);
    ImGui::SameLine();
    ImGui::Checkbox("Show Nesting Hierarchy", &gui.show_subreg_hierarchy);
    ImGui::Separator();

    std::vector<int> reg_depths(reg_defs.size(), 0);
    for (size_t i = 0; i < reg_defs.size(); ++i) {
        int parent = reg_defs[i].parent_index;
        int depth = 0;
        while (parent != -1) {
            depth++;
            parent = reg_defs[parent].parent_index;
        }
        reg_depths[i] = depth;
    }

    // Collapse padding completely when displaying bits for seamless vertical
    // stacking
    // if (gui.show_register_bits) {
    //     ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
    // }

    int num_cols = gui.show_register_bits ? 5 : 4;
    if (ImGui::BeginTable("RegTable", num_cols,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Register", ImGuiTableColumnFlags_WidthFixed,
                                150.0f);
        ImGui::TableSetupColumn("Hex", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        if (gui.show_register_bits) {
            ImGui::TableSetupColumn("Bits (MSB -> LSB)",
                                    ImGuiTableFlags_SizingStretchProp);
        }
        ImGui::TableSetupColumn("Decimal", ImGuiTableColumnFlags_WidthFixed,
                                100.0f);
        ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed,
                                120.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < reg_defs.size(); ++i) {
            const auto &def = reg_defs[i];
            int depth = gui.show_subreg_hierarchy ? reg_depths[i] : 0;
            uint64_t val = regs.read(def.name);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            std::string padded_name = "";
            for (int d = 1; d < depth; ++d)
                padded_name += "   ";
            if (depth > 0)
                padded_name += "|- ";
            padded_name += def.name;

            ImGui::Text("%s", padded_name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s",
                               FormatHexValue(val, def.width).c_str());

            if (gui.show_register_bits) {
                ImGui::TableSetColumnIndex(2);
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                                    ImVec2(0.0f, 0.0f)); // Zero horizontal gap

                // Align based on highest physical register ancestor size
                int grid_width = GetRegisterGridWidth(def, reg_defs);

                for (int p = grid_width - 1; p >= 0; --p) {
                    int bit_idx = -1;
                    for (int b = 0; b < def.width; ++b) {
                        if (def.bit_mapping[b] == p) {
                            bit_idx = b;
                            break;
                        }
                    }

                    char btn_id[32];
                    snprintf(btn_id, sizeof(btn_id), "%s_b%d", def.name.c_str(),
                             p);
                    ImGui::PushID(btn_id);

                    if (bit_idx != -1) {
                        bool is_set = (val >> bit_idx) & 1;
                        ImVec4 bit_color = is_set
                                               ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
                                               : ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
                        ImGui::PushStyleColor(ImGuiCol_Button, bit_color);
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, bit_color);
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                              bit_color);

                        if (ImGui::Button(is_set ? "1" : "0", ImVec2(24, 24))) {
                            uint64_t new_val = val ^ (1ULL << bit_idx);
                            regs.write(def.name, new_val);
                        }

                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("Register: %s", def.name.c_str());
                            ImGui::Text("Register Bit: %d", bit_idx);
                            ImGui::Text("Physical Bit Index: %d",
                                        def.bit_mapping[bit_idx]);
                            ImGui::Text("Physical Reg Index: %d",
                                        def.physical_index);
                            ImGui::EndTooltip();
                        }

                        ImGui::PopStyleColor(3);
                    } else {
                        // Empty spacer block for unmapped portions of
                        // sub-registers
                        ImGui::PushStyleColor(
                            ImGuiCol_Button, ImVec4(0.08f, 0.08f, 0.08f, 0.6f));
                        ImGui::PushStyleColor(
                            ImGuiCol_ButtonActive,
                            ImVec4(0.08f, 0.08f, 0.08f, 0.6f));
                        ImGui::PushStyleColor(
                            ImGuiCol_ButtonHovered,
                            ImVec4(0.08f, 0.08f, 0.08f, 0.6f));

                        ImGui::Button("-", ImVec2(24, 24));

                        if (ImGui::IsItemHovered()) {
                            ImGui::BeginTooltip();
                            ImGui::Text("Unmapped slot for physical bit %d", p);
                            ImGui::EndTooltip();
                        }

                        ImGui::PopStyleColor(3);
                    }
                    ImGui::PopID();
                    if (p > 0)
                        ImGui::SameLine();
                }
                ImGui::PopStyleVar();
            }

            ImGui::TableSetColumnIndex(gui.show_register_bits ? 3 : 2);
            ImGui::Text("%llu", val);

            ImGui::TableSetColumnIndex(gui.show_register_bits ? 4 : 3);
            if (!def.role.empty()) {
                ImGui::TextDisabled("%s", def.role.c_str());
            } else if (def.is_alias && def.parent_index != -1) {
                ImGui::TextDisabled("alias of %s",
                                    reg_defs[def.parent_index].name.c_str());
            } else {
                ImGui::TextDisabled("-");
            }
        }
        ImGui::EndTable();
    }
/*
    if (gui.show_register_bits) {
        ImGui::PopStyleVar();
    }
*/
    ImGui::End();
}
