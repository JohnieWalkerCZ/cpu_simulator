#pragma once
#include "ui_common.hpp"
#include <exception>

inline void UI_ControlTower(CPU &cpu, GUIState &state,
                            PeripheralsState &p_state) {
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

    ImGui::Separator();
    ImGui::Text("Register Visualization Options:");
    ImGui::Checkbox("Show Register Bits (Interactive)",
                    &state.show_register_bits);
    ImGui::Checkbox("Show Nesting Hierarchy", &state.show_subreg_hierarchy);

    ImGui::Separator();
    ImGui::Text("Appearance Setup:");
    const char *themes[] = {"Modern Dark", "Retro Amber", "Green Phosphor",
                            "Flat Classic Windows"};
    int current_theme_idx = static_cast<int>(state.active_theme);
    if (ImGui::Combo("GUI Theme", &current_theme_idx, themes,
                     IM_ARRAYSIZE(themes))) {
        state.active_theme = static_cast<UITheme>(current_theme_idx);
        ApplyTheme(state.active_theme);
    }

    ImGui::End();
}
