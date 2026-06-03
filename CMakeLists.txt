cmake_minimum_required(VERSION 3.20)
project(CPUSandbox)

set(CMAKE_CXX_STANDARD 26)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json_single
    URL "https://raw.githubusercontent.com/nlohmann/json/develop/include/nlohmann/json.hpp"
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(nlohmann_json_single)

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
target_include_directories(cpu_core PUBLIC src)

target_link_libraries(cpu_core PUBLIC nlohmann_json)

add_executable(cpu_sim
    src/main.cpp
)
target_link_libraries(cpu_sim cpu_core imgui SDL2::SDL2 ${OPENGL_LIBRARIES} dl)

add_executable(test_config_parser
    tests/test_config_parser.cpp
)
target_link_libraries(test_config_parser PRIVATE cpu_core)

add_executable(test_alu_operations
    tests/test_alu_operations.cpp
)
target_link_libraries(test_alu_operations PRIVATE cpu_core)

add_executable(test_register_file
    tests/test_register_file.cpp
)
target_link_libraries(test_register_file PRIVATE cpu_core)

add_executable(test_memory
    tests/test_memory.cpp
)
target_link_libraries(test_memory PRIVATE cpu_core)

add_executable(test_decoder
    tests/test_decoder.cpp
)
target_link_libraries(test_decoder PRIVATE cpu_core)

add_executable(test_assembler
    tests/test_assembler.cpp
)
target_link_libraries(test_assembler PRIVATE cpu_core)

add_executable(test_cpu_integration
    tests/test_cpu_integration.cpp
)
target_link_libraries(test_cpu_integration PRIVATE cpu_core)
