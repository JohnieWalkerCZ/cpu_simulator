cmake_minimum_required(VERSION 3.20)
project(CPUSandbox)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

add_library(cpu_core STATIC
    src/core/config.cpp
    src/core/registers/register_file.cpp
    src/core/alu/alu.cpp
    src/core/memory/memory.cpp
)

target_include_directories(cpu_core PRIVATE src)
target_link_libraries(cpu_core PRIVATE nlohmann_json)

add_executable(cpu_sim
    src/main.cpp
    src/core/cpu.cpp
    src/core/execution/executor.cpp
)

target_include_directories(cpu_sim PRIVATE src)
target_link_libraries(cpu_sim PRIVATE cpu_core nlohmann_json)

enable_testing()
add_executable(config_test 
    tests/test_config_parser.cpp
)

target_include_directories(config_test PRIVATE src)
target_link_libraries(config_test PRIVATE cpu_core nlohmann_json)
add_test(NAME ConfigParser COMMAND config_test)































