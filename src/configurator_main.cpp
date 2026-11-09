#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "ui/ui_common.hpp"
#include "ui/ui_configurator.hpp"
#include <SDL.h>
#include <SDL_opengl.h>
#include <cstdlib>
#include <iostream>

// Graphical CPU configurator: edit a CPU architecture in forms and export
// it as JSON, or launch the simulator with it directly.
//
// Usage: cpu_configurator [config.json]
// With no argument (or an unreadable file) it starts with a blank,
// valid template configuration.
int main(int argc, char **argv) {
    ConfiguratorState state;
    state.argv0 = argc > 0 ? argv[0] : "";
    // Persistent settings (UI scale + recent files): one file under
    // the user's home directory, never in the repo. Loaded before
    // the document so LoadFromFile can record the opened file.
    {
        const char *home = std::getenv("HOME");
        const std::string base = home ? home : ".";
        state.settings_path =
            base + "/.config/cpu_configurator/settings.txt";
        state.settings = LoadSettings(state.settings_path);
        if (state.settings.ui_scale < 0.5)
            state.settings.ui_scale = 0.5;
        if (state.settings.ui_scale > 2.0)
            state.settings.ui_scale = 2.0;
    }
    if (argc >= 2) {
        if (!state.LoadFromFile(argv[1])) {
            const std::string message = state.action_msg;
            state.LoadTemplate();
            state.action_msg = message +
                               " Started with a blank template instead.";
        }
    } else {
        state.LoadTemplate();
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window *window = SDL_CreateWindow(
        "CPU Configurator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1600, 1000, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.FontGlobalScale = static_cast<float>(state.settings.ui_scale);

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 130");

    ApplyTheme(state.theme);
    bool done = false;

    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) {
                // With unsaved changes, ask first: the quit modal
                // runs in the UI; only quit once it is confirmed.
                // A second close while the modal is open is ignored.
                if (state.dirty && !state.quit_confirmed)
                    state.quit_requested = true;
                else
                    done = true;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        UI_Configurator(state);
        if (state.quit_confirmed)
            done = true;

        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Persist settings on a clean shutdown (scale + recent files;
    // loads and saves already persist them on the way).
    if (!state.settings_path.empty())
        SaveSettings(state.settings_path, state.settings);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
