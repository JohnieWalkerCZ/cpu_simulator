#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "ui/ui_assembler.hpp"
#include "ui/ui_common.hpp"
#include "ui/ui_cpu.hpp"
#include "ui/ui_memory.hpp"
#include "ui/ui_peripherals.hpp"
#include "ui/ui_stack_frame_explorer.hpp"
#include <SDL.h>
#include <SDL_opengl.h>
#include <iostream>
#include <mutex>
#include <thread>

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout << "Usage: cpu_sim <config.json>\n";
        return 1;
    }
    Config cfg = Config::from_file(argv[1]);
    if (!cfg.validate()) {
        std::cerr << "Hardware Configuration Error: The specified architecture "
                     "layout is invalid.\n";
        return 1;
    }
    CPU cpu(cfg);
    PeripheralsState p_state;
    InitializePeripherals(cpu, p_state);

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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 130");

    GUIState gui;
    ApplyTheme(gui.active_theme);
    bool done = false;

    // CPU execution runs on a background thread (RunCPUWorkerLoop) so that
    // high clock_speed settings can step as fast as requested without
    // blocking SDL/ImGui rendering. gui.cpu_mutex serializes access between
    // that thread and the UI-thread block below that reads/mutates the CPU.
    std::thread cpu_worker(RunCPUWorkerLoop, std::ref(cpu), std::ref(gui));

    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
        }

        static Uint64 last_ticks = SDL_GetPerformanceCounter();
        Uint64 current_ticks = SDL_GetPerformanceCounter();
        float delta_time = static_cast<float>(current_ticks - last_ticks) /
                           SDL_GetPerformanceFrequency();
        last_ticks = current_ticks;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        {
            std::lock_guard<std::mutex> lock(gui.cpu_mutex);

            UpdateHighlights(cpu, gui.highlighter, delta_time);
            UpdateStackFrameTracking(cpu, gui);

            UI_ControlTower(cpu, gui, p_state);
            UI_RegisterFile(cpu, gui); // Pass GUIState
            UI_ALUMonitor(cpu);
            UI_MemoryView(cpu, gui);
            UI_StackFrameExplorer(cpu, gui);
            UI_FlashROMView(cpu, gui);
            UI_MicrocodePipeline(cpu);
            UI_Assembler(cpu, gui, p_state);
            UI_ProgramView(cpu, gui);
            UI_Peripherals(cpu, cfg, p_state);
            UI_SystemSchematic(cpu, gui); // Pass GUIState
        }

        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    gui.stop_worker = true;
    cpu_worker.join();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
