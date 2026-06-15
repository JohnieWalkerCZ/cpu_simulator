#pragma once
#include "ui_common.hpp"

inline void UI_Peripherals(CPU &cpu, const Config &config,
                           PeripheralsState &p_state) {
    if (config.peripherals.empty())
        return;

    ImGui::Begin("I/O Peripherals");

    for (const auto &def : config.peripherals) {
        if (ImGui::CollapsingHeader(def.name.c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {

            if (def.type == "text_display") {
                ImGui::TextDisabled("Mapped to 0x%04llX", def.address_start);
                ImGui::BeginChild((def.name + "_scroll").c_str(),
                                  ImVec2(0, 100), true);
                ImGui::TextUnformatted(
                    p_state.console_buffers[def.name].c_str());
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                    ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
            }

            else if (def.type == "grid_display") {
                ImGui::TextDisabled("Mapped to 0x%04llX - 0x%04llX",
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
                ImGui::TextDisabled("Mapped to 0x%04llX", def.address_start);
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
                ImGui::TextDisabled("Mapped to 0x%04llX - 0x%04llX",
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

inline void InitializePeripherals(CPU &cpu, PeripheralsState &p_state) {
    cpu.get_memory().reset_io_hooks();

    for (const auto &def : cpu.get_config().peripherals) {
        if (def.type == "text_display") {
            p_state.console_buffers[def.name] = "";
            cpu.get_memory().map_io_region(
                def.address_start, def.address_end, nullptr,
                [&p_state, name = def.name](uint64_t address, uint64_t val) {
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
                    [&dp, start = def.address_start](uint64_t addr) {
                        return dp.read(addr - start);
                    },
                    [&dp, start = def.address_start](uint64_t addr,
                                                     uint64_t val) {
                        dp.write(addr - start, val);
                    });
            }
        }
    }
}

inline void ResetPeripheralsState(const Config &config,
                                  PeripheralsState &p_state) {
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
