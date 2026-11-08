cmake_minimum_required(VERSION 3.20)
project(CPUSandbox)

set(CMAKE_CXX_STANDARD 26)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json_single
    # Pinned release asset: the single-header amalgamation. The develop
    # branch URL used to point at the same file, but upstream split
    # json.hpp into many headers, so it no longer compiles standalone.
    URL "https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp"
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(nlohmann_json_single)

# The single-header download lands as <src>/json.hpp, but the code includes
# <nlohmann/json.hpp>: mirror it into a nlohmann/ subdirectory so the
# include resolves without a system-installed nlohmann/json package.
file(COPY ${nlohmann_json_single_SOURCE_DIR}/json.hpp
     DESTINATION ${nlohmann_json_single_SOURCE_DIR}/nlohmann)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${nlohmann_json_single_SOURCE_DIR})

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

if(NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    if(SDL2_LIBRARIES)
        set_target_properties(SDL2::SDL2 PROPERTIES
            INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
        )
    else()
        set_target_properties(SDL2::SDL2 PROPERTIES
            INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIR}"
        )
    endif()
endif()

FetchContent_Declare(
    imgui_src
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.8-docking 
)
FetchContent_MakeAvailable(imgui_src)

set(IMGUI_DIR ${imgui_src_SOURCE_DIR})

file(GLOB IMGUI_SOURCES 
    ${IMGUI_DIR}/*.cpp 
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp 
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2 ${OPENGL_LIBRARIES})

add_library(cpu_core STATIC
    src/core/config.cpp
    src/core/registers/register_file.cpp
    src/core/alu/alu.cpp
    src/core/memory/memory.cpp
    src/core/cpu.cpp
    src/core/execution/decoder.cpp
    src/core/execution/executor.cpp
    src/core/assembly/assembler.cpp
    src/core/peripherals/declarative_peripheral.cpp
)
target_include_directories(cpu_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(cpu_core PUBLIC nlohmann_json)

add_executable(cpu_sim
    src/main.cpp
)
target_link_libraries(cpu_sim cpu_core imgui SDL2::SDL2 ${OPENGL_LIBRARIES} ${CMAKE_DL_LIBS})

add_executable(cpu_configurator
    src/configurator_main.cpp
)
target_link_libraries(cpu_configurator cpu_core imgui SDL2::SDL2 ${OPENGL_LIBRARIES} ${CMAKE_DL_LIBS})

enable_testing()

set(TEST_WORKING_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests")

# 1. Config Parser Test
add_executable(test_config_parser
    tests/test_config_parser.cpp
)
target_link_libraries(test_config_parser PRIVATE cpu_core)
add_test(NAME test_config_parser COMMAND test_config_parser)

# 2. ALU Operations Test
add_executable(test_alu_operations
    tests/test_alu_operations.cpp
)
target_link_libraries(test_alu_operations PRIVATE cpu_core)
add_test(NAME test_alu_operations COMMAND test_alu_operations)
set_tests_properties(test_alu_operations PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# 3. Register File Test
add_executable(test_register_file
    tests/test_register_file.cpp
)
target_link_libraries(test_register_file PRIVATE cpu_core)
add_test(NAME test_register_file COMMAND test_register_file)

# 4. Memory Protection Test
add_executable(test_memory
    tests/test_memory.cpp
)
target_link_libraries(test_memory PRIVATE cpu_core)
add_test(NAME test_memory COMMAND test_memory)

# 5. Instruction Decoder Test
add_executable(test_decoder
    tests/test_decoder.cpp
)
target_link_libraries(test_decoder PRIVATE cpu_core)
add_test(NAME test_decoder COMMAND test_decoder)
set_tests_properties(test_decoder PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# 6. Assembler Test
add_executable(test_assembler
    tests/test_assembler.cpp
)
target_link_libraries(test_assembler PRIVATE cpu_core)
add_test(NAME test_assembler COMMAND test_assembler)
set_tests_properties(test_assembler PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# 7. CPU Integration Test
add_executable(test_cpu_integration
    tests/test_cpu_integration.cpp
)
target_link_libraries(test_cpu_integration PRIVATE cpu_core)
add_test(NAME test_cpu_integration COMMAND test_cpu_integration)
set_tests_properties(test_cpu_integration PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# 8. Instruction Preview Test (configurator, no GUI needed)
add_executable(test_instruction_preview
    tests/test_instruction_preview.cpp
)
target_link_libraries(test_instruction_preview PRIVATE cpu_core)
add_test(NAME test_instruction_preview COMMAND test_instruction_preview)
set_tests_properties(test_instruction_preview PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# 9. Config Diagnostics Test
add_executable(test_config_diagnostics
    tests/test_config_diagnostics.cpp
)
target_link_libraries(test_config_diagnostics PRIVATE cpu_core)
add_test(NAME test_config_diagnostics COMMAND test_config_diagnostics)
set_tests_properties(test_config_diagnostics PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")

# 10. Micro-op Editor Test (configurator, no GUI needed)
add_executable(test_microop_editor
    tests/test_microop_editor.cpp
)
target_link_libraries(test_microop_editor PRIVATE cpu_core)
add_test(NAME test_microop_editor COMMAND test_microop_editor)
set_tests_properties(test_microop_editor PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_executable(test_register_layout
    tests/test_register_layout.cpp
)
target_link_libraries(test_register_layout PRIVATE cpu_core)
add_test(NAME test_register_layout COMMAND test_register_layout)
set_tests_properties(test_register_layout PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_executable(test_hook_editor
    tests/test_hook_editor.cpp
)
target_link_libraries(test_hook_editor PRIVATE cpu_core)
add_test(NAME test_hook_editor COMMAND test_hook_editor)
set_tests_properties(test_hook_editor PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_executable(test_undo_history
    tests/test_undo_history.cpp
)
target_link_libraries(test_undo_history PRIVATE cpu_core)
add_test(NAME test_undo_history COMMAND test_undo_history)
set_tests_properties(test_undo_history PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_executable(test_app_settings
    tests/test_app_settings.cpp
)
target_link_libraries(test_app_settings PRIVATE cpu_core)
add_test(NAME test_app_settings COMMAND test_app_settings)
set_tests_properties(test_app_settings PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_executable(test_duplicate
    tests/test_duplicate.cpp
)
target_link_libraries(test_duplicate PRIVATE cpu_core)
add_test(NAME test_duplicate COMMAND test_duplicate)
set_tests_properties(test_duplicate PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")

add_executable(test_example_items
    tests/test_example_items.cpp
)
target_link_libraries(test_example_items PRIVATE cpu_core)
add_test(NAME test_example_items COMMAND test_example_items)
set_tests_properties(test_example_items PROPERTIES WORKING_DIRECTORY "${TEST_WORKING_DIR}")
