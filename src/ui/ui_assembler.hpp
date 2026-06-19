#pragma once
#include "core/assembly/assembler.hpp"
#include "ui_common.hpp"

inline void UI_Assembler(CPU &cpu, GUIState &gui, PeripheralsState &p_state) {
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
                gui.stack_frames = StackFrameState();
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

        ImGui::TextDisabled("Assembler Directives:");
        ImGui::BulletText(".equ NAME, VALUE   - define a named constant");
        ImGui::BulletText(".db v1, v2, ...    - emit raw bytes");
        ImGui::BulletText(".dw v1, v2, ...    - emit 16-bit words");
        ImGui::BulletText(".ascii \"text\"      - emit ASCII bytes (no "
                          "terminator)");
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
