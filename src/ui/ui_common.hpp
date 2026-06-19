#pragma once
#include "core/cpu.hpp"
#include "imgui.h"
#include <SDL.h>
#include <cmath>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Forward Declarations of Peripherals management functions (defined in
// ui_peripherals.hpp)
struct Config;
struct PeripheralsState;
class CPU;
void ResetPeripheralsState(const Config &config, PeripheralsState &p_state);
void InitializePeripherals(CPU &cpu, PeripheralsState &p_state);

enum class UITheme { MODERN_DARK, RETRO_AMBER, GREEN_PHOSPHOR, FLAT_CLASSIC };

enum class DataBusRoute {
    MEM_TO_REG,
    REG_TO_MEM,
    DECODER_TO_REG,
    REG_TO_REG,
    INACTIVE
};

struct BusHighlighter {
    float data_bus = 0.0f;
    float address_bus = 0.0f;
    float alu_path = 0.0f;
    float reg_file_path = 0.0f;
    float mmio_path = 0.0f;
    float ir_bus = 0.0f;
    float ctrl_alu_bus = 0.0f;
    float ctrl_reg_bus = 0.0f;
    float ctrl_mem_bus = 0.0f;
    float ctrl_periph_bus = 0.0f;

    std::string data_val = "";
    std::string address_val = "";
    std::string alu_a_val = "";
    std::string alu_b_val = "";
    std::string alu_out_val = "";

    bool alu_a_is_reg = true;
    bool alu_b_is_reg = true;
    int alu_a_reg_row = 2;
    int alu_b_reg_row = 3;
    DataBusRoute data_bus_route = DataBusRoute::INACTIVE;

    void decay(float delta_time, float decay_rate = 3.0f) {
        auto step = [&](float &val) {
            val -= delta_time * decay_rate;
            if (val < 0.0f)
                val = 0.0f;
        };
        step(data_bus);
        step(address_bus);
        step(alu_path);
        step(reg_file_path);
        step(mmio_path);
        step(ir_bus);
        step(ctrl_alu_bus);
        step(ctrl_reg_bus);
        step(ctrl_mem_bus);
        step(ctrl_periph_bus);
    }
};

struct CPUSnapshot {
    std::vector<uint64_t> physical_registers;
    std::vector<uint8_t> memory;
    std::vector<uint8_t> instruction_memory;
    Executor::ExecutorSnapshot executor_state;
};

struct GUIState {
    bool is_running = false;
    float clock_speed = 2.0f;
    double last_step_time = 0;
    bool show_full_memory = false;
    bool run_by_uop = false;
    bool show_register_bits = false;
    bool show_subreg_hierarchy = true;

    float zoom = 1.0f;
    ImVec2 pan = ImVec2(0.0f, 0.0f);

    std::deque<CPUSnapshot> history;
    std::unordered_set<uint64_t> breakpoints;
    uint64_t last_breakpoint_hit = (uint64_t)-1;

    char asm_source[8 * 1000] = "HLT";
    std::string asm_status = "";
    bool asm_error = false;

    std::string cpu_error_message = "";

    UITheme active_theme = UITheme::MODERN_DARK;
    BusHighlighter highlighter;
};

struct PeripheralsState {
    std::unordered_map<std::string, std::string> console_buffers;
    std::unordered_map<std::string, std::vector<bool>> led_matrices;
    std::unordered_map<std::string, uint64_t> key_states;
};

inline std::string FormatHexValue(uint64_t val, int width) {
    char buf[32];
    if (width <= 8)
        snprintf(buf, sizeof(buf), "0x%02X", (unsigned int)val);
    else if (width <= 16)
        snprintf(buf, sizeof(buf), "0x%04X", (unsigned int)val);
    else if (width <= 24)
        snprintf(buf, sizeof(buf), "0x%06X",
                 (unsigned int)val); // 24-bit instruction support
    else if (width <= 32)
        snprintf(buf, sizeof(buf), "0x%08X", (unsigned int)val);
    else if (width <= 48)
        snprintf(buf, sizeof(buf), "0x%012llX",
                 (unsigned long long)val); // 48-bit instruction support
    else
        snprintf(buf, sizeof(buf), "0x%016llX", (unsigned long long)val);
    return std::string(buf);
}

inline int MapRegToRow(int reg_idx, const RegisterFile &regs) {
    if (reg_idx < 0 || reg_idx >= static_cast<int>(regs.get_defs().size()))
        return 0;
    return reg_idx;
}

inline void UpdateHighlights(CPU &cpu, BusHighlighter &h, float delta_time) {
    h.decay(delta_time);

    auto &exec = cpu.get_executor();
    auto &config = cpu.get_config();
    auto &regs = cpu.get_registers();
    auto &mem = cpu.get_memory();

    if (exec.get_state() == ExecutionState::EXECUTE_UOPS ||
        exec.get_state() == ExecutionState::FETCH) {
        int data_display_width = config.data_width;

        if (exec.get_state() == ExecutionState::FETCH ||
            exec.get_state() == ExecutionState::DECODE) {
            // Retrieve current units fetched or execute lookahead peek-fetch to
            // show upcoming instruction word
            uint64_t current_pc = regs.get_pc();
            int unit_bits = config.data_width;
            int unit_bytes = (config.data_width + 7) / 8;

            if (current_pc < mem.size()) {
                uint64_t peek_first_unit = mem.read(current_pc, true);
                peek_first_unit &= ((1ULL << unit_bits) - 1);

                Decoder peek_decoder(config);
                uint8_t peek_opcode = peek_decoder.peek_opcode(peek_first_unit);
                int peek_total_bits = peek_decoder.get_total_bits(peek_opcode);
                int peek_units = (peek_total_bits + unit_bits - 1) / unit_bits;

                uint64_t peek_raw = peek_first_unit;
                for (int i = 1; i < peek_units &&
                                (current_pc + i * unit_bytes) < mem.size();
                     ++i) {
                    uint64_t next_u =
                        mem.read(current_pc + i * unit_bytes, true);
                    next_u &= ((1ULL << unit_bits) - 1);
                    peek_raw = (peek_raw << unit_bits) | next_u;
                }

                data_display_width = peek_units * config.data_width;
                h.data_val = FormatHexValue(peek_raw, data_display_width);
            } else {
                h.data_val =
                    FormatHexValue(exec.get_last_data_val(), config.data_width);
            }
        } else {
            h.data_val =
                FormatHexValue(exec.get_last_data_val(), config.data_width);
        }

        h.address_val =
            FormatHexValue(exec.get_last_addr_val(), config.addr_width);
        h.alu_a_val = FormatHexValue(exec.get_last_alu_a(), config.data_width);
        h.alu_b_val = FormatHexValue(exec.get_last_alu_b(), config.data_width);
        h.alu_out_val =
            FormatHexValue(exec.get_last_alu_out(), config.data_width);

        if (exec.get_state() == ExecutionState::FETCH) {
            h.address_bus = 1.0f;
            h.ir_bus = 1.0f;
            h.reg_file_path = 1.0f;
            h.data_bus_route = DataBusRoute::INACTIVE;

            // Trigger Fetch phase control signals
            h.ctrl_reg_bus = 1.0f; // Accessing Program Counter register
            h.ctrl_mem_bus = 1.0f; // Executing rom instruction fetch
        } else {
            const MicroOp *uop = nullptr;
            const auto &current_inst = exec.get_current_inst();
            for (const auto &inst : config.instructions) {
                if (inst.opcode == current_inst.opcode) {
                    size_t idx = exec.get_current_uop_index();
                    if (idx < inst.microcode.size()) {
                        uop = &inst.microcode[idx];
                    }
                    break;
                }
            }

            if (uop) {
                h.alu_a_is_reg = true;
                h.alu_b_is_reg = true;
                h.alu_a_reg_row = 2;
                h.alu_b_reg_row = 3;

                // Inspect active operand types (Registers vs Immediates) for
                // line routing
                if (uop->action == "copy" || uop->action == "alu") {
                    h.reg_file_path = 1.0f;
                    h.ctrl_reg_bus =
                        1.0f; // Highlight Register Select/Write path

                    if (uop->action == "copy") {
                        h.data_bus = 1.0f;
                        h.alu_path = 0.0f;
                        bool src_is_reg = true;
                        std::string src_arg = uop->args.at("source");
                        if (src_arg[0] == '@') {
                            std::string token = src_arg.substr(1);
                            if (current_inst.imms.count(token))
                                src_is_reg = false;
                        } else if (src_arg[0] == '#') {
                            src_is_reg = false;
                        }
                        h.data_bus_route = src_is_reg
                                               ? DataBusRoute::REG_TO_REG
                                               : DataBusRoute::DECODER_TO_REG;
                    } else if (uop->action == "alu") {
                        h.alu_path = 1.0f;
                        h.ctrl_alu_bus = 1.0f; // Command the ALU operation
                        h.data_bus =
                            0.0f; // Keep data bus inactive during ALU steps
                        h.data_bus_route = DataBusRoute::INACTIVE;
                        if (uop->args.count("a")) {
                            std::string arg = uop->args.at("a");
                            if (arg[0] == '@') {
                                std::string token = arg.substr(1);
                                if (current_inst.imms.count(token))
                                    h.alu_a_is_reg = false;
                                else if (current_inst.regs.count(token)) {
                                    h.alu_a_is_reg = true;
                                    h.alu_a_reg_row = MapRegToRow(
                                        current_inst.regs.at(token), regs);
                                }
                            } else if (arg[0] == '#') {
                                h.alu_a_is_reg = false;
                            } else if (arg[0] == '$') {
                                h.alu_a_is_reg = true;
                                std::string role_name = arg.substr(1);
                                int found_row = -1;
                                if (role_name == "SP") {
                                    found_row =
                                        regs.find_by_role("stack_pointer");
                                    if (found_row == -1)
                                        found_row = regs.find_by_role("sp");
                                } else if (role_name == "PC") {
                                    found_row =
                                        regs.find_by_role("program_counter");
                                    if (found_row == -1)
                                        found_row = regs.find_by_role("pc");
                                } else {
                                    found_row = regs.find_by_role(role_name);
                                }
                                h.alu_a_reg_row = MapRegToRow(found_row, regs);
                            }
                        }
                        if (uop->args.count("b")) {
                            std::string arg = uop->args.at("b");
                            if (arg[0] == '@') {
                                std::string token = arg.substr(1);
                                if (current_inst.imms.count(token))
                                    h.alu_b_is_reg = false;
                                else if (current_inst.regs.count(token)) {
                                    h.alu_b_is_reg = true;
                                    h.alu_b_reg_row = MapRegToRow(
                                        current_inst.regs.at(token), regs);
                                }
                            } else if (arg[0] == '#') {
                                h.alu_b_is_reg = false;
                            } else if (arg[0] == '$') {
                                h.alu_b_is_reg = true;
                                std::string role_name = arg.substr(1);
                                int found_row = -1;
                                if (role_name == "SP") {
                                    found_row =
                                        regs.find_by_role("stack_pointer");
                                    if (found_row == -1)
                                        found_row = regs.find_by_role("sp");
                                } else if (role_name == "PC") {
                                    found_row =
                                        regs.find_by_role("program_counter");
                                    if (found_row == -1)
                                        found_row = regs.find_by_role("pc");
                                } else {
                                    found_row = regs.find_by_role(role_name);
                                }
                                h.alu_b_reg_row = MapRegToRow(found_row, regs);
                            }
                        }
                    }
                } else if (uop->action == "mem_read" ||
                           uop->action == "mem_write") {
                    h.address_bus = 1.0f;
                    h.data_bus = 1.0f;
                    h.reg_file_path = 1.0f;
                    h.ctrl_reg_bus =
                        1.0f; // Accessing address pointer/destination registers

                    h.data_bus_route = (uop->action == "mem_read")
                                           ? DataBusRoute::MEM_TO_REG
                                           : DataBusRoute::REG_TO_MEM;
                    uint64_t addr = exec.get_last_addr_val();
                    bool is_peripheral = false;
                    for (const auto &p : config.peripherals) {
                        if (addr >= p.address_start && addr <= p.address_end) {
                            h.mmio_path = 1.0f;
                            is_peripheral = true;
                            break;
                        }
                    }

                    if (is_peripheral) {
                        h.ctrl_periph_bus =
                            1.0f; // Target Peripheral Control Select
                    } else {
                        h.ctrl_mem_bus = 1.0f; // Target Memory Control Select
                    }
                } else if (uop->action == "branch") {
                    h.address_bus = 1.0f;
                    h.data_bus_route = DataBusRoute::INACTIVE;
                    h.ctrl_reg_bus = 1.0f; // Directly modifying PC Register
                }
            }
        }
    }
}

inline void ApplyTheme(UITheme theme) {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    switch (theme) {
    case UITheme::MODERN_DARK: {
        ImGui::StyleColorsDark();
        style.Colors[ImGuiCol_Text] = ImVec4(0.85f, 0.87f, 0.91f, 1.00f);
        style.Colors[ImGuiCol_TextDisabled] =
            ImVec4(0.40f, 0.44f, 0.52f, 1.00f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.10f, 0.14f, 1.00f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.17f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_BorderShadow] =
            ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.15f, 0.17f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered] =
            ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
        style.Colors[ImGuiCol_FrameBgActive] =
            ImVec4(0.25f, 0.28f, 0.36f, 1.00f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.17f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] =
            ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_TitleBgCollapsed] =
            ImVec4(0.12f, 0.14f, 0.18f, 0.80f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.15f, 0.17f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrab] =
            ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] =
            ImVec4(0.25f, 0.28f, 0.36f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] =
            ImVec4(0.30f, 0.34f, 0.44f, 1.00f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.27f, 0.73f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.27f, 0.73f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_SliderGrabActive] =
            ImVec4(0.40f, 0.80f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
        style.Colors[ImGuiCol_ButtonHovered] =
            ImVec4(0.27f, 0.73f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_ButtonActive] =
            ImVec4(0.20f, 0.60f, 0.90f, 1.00f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.23f, 0.29f, 1.00f);
        style.Colors[ImGuiCol_HeaderHovered] =
            ImVec4(0.25f, 0.28f, 0.36f, 1.00f);
        style.Colors[ImGuiCol_HeaderActive] =
            ImVec4(0.27f, 0.73f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.17f, 0.22f, 1.00f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.28f, 0.36f, 1.00f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.18f, 0.20f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_DockingPreview] =
            ImVec4(0.27f, 0.73f, 1.00f, 0.40f);
        style.Colors[ImGuiCol_DockingEmptyBg] =
            ImVec4(0.12f, 0.14f, 0.18f, 1.00f);
        break;
    }
    case UITheme::RETRO_AMBER: {
        ImGui::StyleColorsDark();
        style.WindowRounding = 0.0f;
        style.FrameRounding = 0.0f;
        style.ScrollbarRounding = 0.0f;
        style.GrabRounding = 0.0f;
        style.TabRounding = 0.0f;
        style.FrameBorderSize = 1.0f;

        style.Colors[ImGuiCol_Text] = ImVec4(1.00f, 0.65f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_TextDisabled] =
            ImVec4(0.50f, 0.30f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.03f, 0.015f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.015f, 0.008f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.04f, 0.02f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.80f, 0.45f, 0.00f, 0.50f);
        style.Colors[ImGuiCol_BorderShadow] =
            ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.025f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered] =
            ImVec4(0.10f, 0.05f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_FrameBgActive] =
            ImVec4(0.15f, 0.08f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.04f, 0.02f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] =
            ImVec4(0.20f, 0.10f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_TitleBgCollapsed] =
            ImVec4(0.03f, 0.015f, 0.00f, 0.80f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.04f, 0.02f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarBg] =
            ImVec4(0.03f, 0.015f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrab] =
            ImVec4(0.15f, 0.08f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] =
            ImVec4(0.25f, 0.12f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] =
            ImVec4(0.40f, 0.20f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(1.00f, 0.65f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.80f, 0.45f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_SliderGrabActive] =
            ImVec4(1.00f, 0.65f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.05f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_ButtonHovered] =
            ImVec4(0.25f, 0.12f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_ButtonActive] =
            ImVec4(0.45f, 0.22f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.05f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_HeaderHovered] =
            ImVec4(0.20f, 0.10f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_HeaderActive] =
            ImVec4(0.35f, 0.18f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_Tab] = ImVec4(0.04f, 0.02f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.15f, 0.08f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.10f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_DockingPreview] =
            ImVec4(1.00f, 0.65f, 0.00f, 0.30f);
        style.Colors[ImGuiCol_DockingEmptyBg] =
            ImVec4(0.03f, 0.015f, 0.00f, 1.00f);
        break;
    }
    case UITheme::GREEN_PHOSPHOR: {
        ImGui::StyleColorsDark();
        style.WindowRounding = 0.0f;
        style.FrameRounding = 0.0f;
        style.ScrollbarRounding = 0.0f;
        style.GrabRounding = 0.0f;
        style.TabRounding = 0.0f;
        style.FrameBorderSize = 1.0f;

        style.Colors[ImGuiCol_Text] = ImVec4(0.00f, 1.00f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_TextDisabled] =
            ImVec4(0.00f, 0.45f, 0.10f, 1.00f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.03f, 0.005f, 1.00f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.015f, 0.002f, 1.00f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.00f, 0.04f, 0.005f, 1.00f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.00f, 0.80f, 0.20f, 0.50f);
        style.Colors[ImGuiCol_BorderShadow] =
            ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.00f, 0.05f, 0.01f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered] =
            ImVec4(0.00f, 0.10f, 0.02f, 1.00f);
        style.Colors[ImGuiCol_FrameBgActive] =
            ImVec4(0.00f, 0.15f, 0.03f, 1.00f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.00f, 0.04f, 0.005f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] =
            ImVec4(0.00f, 0.20f, 0.05f, 1.00f);
        style.Colors[ImGuiCol_TitleBgCollapsed] =
            ImVec4(0.00f, 0.03f, 0.005f, 0.80f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.04f, 0.005f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarBg] =
            ImVec4(0.00f, 0.03f, 0.005f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrab] =
            ImVec4(0.00f, 0.15f, 0.03f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] =
            ImVec4(0.00f, 0.25f, 0.05f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] =
            ImVec4(0.00f, 0.40f, 0.08f, 1.00f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 1.00f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 0.80f, 0.20f, 1.00f);
        style.Colors[ImGuiCol_SliderGrabActive] =
            ImVec4(0.00f, 1.00f, 0.25f, 1.00f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.00f, 0.10f, 0.02f, 1.00f);
        style.Colors[ImGuiCol_ButtonHovered] =
            ImVec4(0.00f, 0.25f, 0.05f, 1.00f);
        style.Colors[ImGuiCol_ButtonActive] =
            ImVec4(0.00f, 0.45f, 0.09f, 1.00f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.00f, 0.10f, 0.02f, 1.00f);
        style.Colors[ImGuiCol_HeaderHovered] =
            ImVec4(0.00f, 0.20f, 0.04f, 1.00f);
        style.Colors[ImGuiCol_HeaderActive] =
            ImVec4(0.00f, 0.35f, 0.07f, 1.00f);
        style.Colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.04f, 0.005f, 1.00f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.00f, 0.15f, 0.03f, 1.00f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.00f, 0.20f, 0.05f, 1.00f);
        style.Colors[ImGuiCol_DockingPreview] =
            ImVec4(0.00f, 1.00f, 0.25f, 0.30f);
        style.Colors[ImGuiCol_DockingEmptyBg] =
            ImVec4(0.00f, 0.03f, 0.005f, 1.00f);
        break;
    }
    case UITheme::FLAT_CLASSIC: {
        ImGui::StyleColorsLight();
        style.WindowRounding = 0.0f;
        style.FrameRounding = 0.0f;
        style.ScrollbarRounding = 0.0f;
        style.GrabRounding = 0.0f;
        style.TabRounding = 0.0f;
        style.FrameBorderSize = 1.0f;

        style.Colors[ImGuiCol_Text] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_TextDisabled] =
            ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
        style.Colors[ImGuiCol_ChildBg] = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
        style.Colors[ImGuiCol_PopupBg] = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        style.Colors[ImGuiCol_BorderShadow] =
            ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_FrameBgHovered] =
            ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
        style.Colors[ImGuiCol_FrameBgActive] =
            ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
        style.Colors[ImGuiCol_TitleBgActive] =
            ImVec4(0.00f, 0.00f, 0.50f, 1.00f);
        style.Colors[ImGuiCol_TitleBgCollapsed] =
            ImVec4(0.50f, 0.50f, 0.50f, 0.80f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.88f, 0.88f, 0.85f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrab] =
            ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] =
            ImVec4(0.80f, 0.80f, 0.80f, 1.00f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] =
            ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
        style.Colors[ImGuiCol_SliderGrabActive] =
            ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
        style.Colors[ImGuiCol_ButtonHovered] =
            ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
        style.Colors[ImGuiCol_ButtonActive] =
            ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.50f, 1.00f);
        style.Colors[ImGuiCol_HeaderHovered] =
            ImVec4(0.10f, 0.10f, 0.60f, 1.00f);
        style.Colors[ImGuiCol_HeaderActive] =
            ImVec4(0.20f, 0.20f, 0.70f, 1.00f);
        style.Colors[ImGuiCol_Tab] = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
        style.Colors[ImGuiCol_TabHovered] = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
        style.Colors[ImGuiCol_TabActive] = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
        style.Colors[ImGuiCol_DockingPreview] =
            ImVec4(0.00f, 0.00f, 0.50f, 0.30f);
        style.Colors[ImGuiCol_DockingEmptyBg] =
            ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
        break;
    }
    }
}

inline void DrawFlagLED(const char *label, bool active) {
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

inline void DrawDataCapsule(ImDrawList *draw_list, ImVec2 start, ImVec2 end,
                            const std::string &text, ImU32 text_color,
                            float intensity, float zoom = 1.0f) {
    if (text.empty() || intensity <= 0.1f)
        return;

    ImVec2 midpoint =
        ImVec2((start.x + end.x) / 2.0f, (start.y + end.y) / 2.0f);

    // Scale text bounds
    ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
    text_size.x *= zoom;
    text_size.y *= zoom;

    float padding_x = 8.0f * zoom;
    float padding_y = 4.0f * zoom;
    ImVec2 rect_min = ImVec2(midpoint.x - text_size.x / 2.0f - padding_x,
                             midpoint.y - text_size.y / 2.0f - padding_y);
    ImVec2 rect_max = ImVec2(midpoint.x + text_size.x / 2.0f + padding_x,
                             midpoint.y + text_size.y / 2.0f + padding_y);

    int alpha = static_cast<int>(240 * intensity);

    ImVec2 shadow_offset(2.0f * zoom, 2.0f * zoom);
    draw_list->AddRectFilled(
        ImVec2(rect_min.x + shadow_offset.x, rect_min.y + shadow_offset.y),
        ImVec2(rect_max.x + shadow_offset.x, rect_max.y + shadow_offset.y),
        IM_COL32(0, 0, 0, static_cast<int>(120 * intensity)), 4.0f * zoom);

    ImU32 bg_col = IM_COL32(10, 12, 16, alpha);

    ImU32 border_col = IM_COL32(
        static_cast<int>(((text_color >> 0) & 0xFF) * intensity),
        static_cast<int>(((text_color >> 8) & 0xFF) * intensity),
        static_cast<int>(((text_color >> 16) & 0xFF) * intensity), alpha);

    draw_list->AddRectFilled(rect_min, rect_max, bg_col, 4.0f * zoom);
    draw_list->AddRect(rect_min, rect_max, border_col, 4.0f * zoom, 0,
                       1.5f * zoom);

    ImU32 drawn_text_col =
        IM_COL32((text_color >> 0) & 0xFF, (text_color >> 8) & 0xFF,
                 (text_color >> 16) & 0xFF, static_cast<int>(255 * intensity));

    // Explicitly pass scaled font properties
    draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * zoom,
                       ImVec2(midpoint.x - text_size.x / 2.0f,
                              midpoint.y - text_size.y / 2.0f),
                       drawn_text_col, text.c_str());
}

inline void DrawFlowArrow(ImDrawList *draw_list, ImVec2 start, ImVec2 end,
                          float intensity) {
    if (intensity <= 0.1f)
        return;

    float t = static_cast<float>(SDL_GetTicks() % 1000) / 1000.0f;

    ImVec2 dir = ImVec2(end.x - start.x, end.y - start.y);
    float len = sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len == 0)
        return;
    dir.x /= len;
    dir.y /= len;

    for (int i = 0; i < 3; ++i) {
        float offset = (t + static_cast<float>(i) / 3.0f);
        if (offset > 1.0f)
            offset -= 1.0f;

        ImVec2 p = ImVec2(start.x + dir.x * len * offset,
                          start.y + dir.y * len * offset);
        draw_list->AddCircleFilled(
            p, 4.0f,
            IM_COL32(255, 255, 255, static_cast<int>(255 * intensity)));
    }
}

inline int GetRegisterGridWidth(const RegisterDef &def,
                                const std::vector<RegisterDef> &defs) {
    int curr_idx = def.parent_index;
    int ancestor_idx = -1;
    while (curr_idx != -1) {
        ancestor_idx = curr_idx;
        curr_idx = defs[curr_idx].parent_index;
    }
    if (ancestor_idx != -1) {
        return defs[ancestor_idx].width;
    }
    return def.width;
}

inline void CaptureSnapshot(CPU &cpu, GUIState &gui) {
    CPUSnapshot snap;
    snap.physical_registers = cpu.get_registers().get_physical_registers();
    snap.memory = cpu.get_memory().raw();
    snap.instruction_memory = cpu.get_memory().raw_instruction();
    snap.executor_state = cpu.get_executor().take_snapshot();

    gui.history.push_back(snap);
    if (gui.history.size() > 1000) {
        gui.history.pop_front();
    }
}
