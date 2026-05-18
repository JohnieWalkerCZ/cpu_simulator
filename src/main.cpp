#include "core/cpu.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <string>
#include <vector>

struct GUIState {
    bool is_running = false;
    float clock_speed = 2.0f; // Hz
    double last_step_time = 0;
    bool show_full_memory = false;
};

// Helper to draw a circular "LED" for flags
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
            uint8_t val = (uint8_t)mem.read(addr);

            if (addr == pc)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
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

void UI_ControlTower(CPU &cpu, GUIState &state) {
    ImGui::Begin("Control Tower");

    if (state.is_running) {
        if (ImGui::Button("PAUSE", ImVec2(80, 0)))
            state.is_running = false;
    } else {
        if (ImGui::Button("RUN", ImVec2(80, 0)))
            state.is_running = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("STEP INST"))
        cpu.step();

    ImGui::SameLine();
    if (ImGui::Button("STEP UOP"))
        cpu.step_uop();

    ImGui::SameLine();
    if (ImGui::Button("RESET"))
        cpu.reset();

    ImGui::Separator();
    ImGui::SliderFloat("Speed", &state.clock_speed, 0.5f, 100.0f, "%.1f Hz");

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

int main(int argc, char **argv) {
    // 1. Initialize CPU
    Config cfg = Config::from_file("../configs/8bit.json");
    CPU cpu(cfg);

    cpu.load_program({
    0x5, 0b000'010'00,
    0xF}, 0);

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

        // Automatic Clock Timing
        if (gui.is_running && !cpu.is_halted()) {
            double current_time = SDL_GetTicks() / 1000.0;
            if (current_time - gui.last_step_time >= (1.0 / gui.clock_speed)) {
                cpu.step(); // Steps a full instruction
                gui.last_step_time = current_time;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Background Dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // Draw the 5 design-spec windows
        UI_ControlTower(cpu, gui);
        UI_RegisterFile(cpu);
        UI_ALUMonitor(cpu);
        UI_MemoryView(cpu, gui);
        UI_MicrocodePipeline(cpu);

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
