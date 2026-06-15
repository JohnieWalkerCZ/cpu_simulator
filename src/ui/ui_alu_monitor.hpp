#pragma once
#include "ui_common.hpp"

inline void UI_ALUMonitor(CPU &cpu) {
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
