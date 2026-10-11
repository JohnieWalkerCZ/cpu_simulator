#include "core/assembly/assembler.hpp"
#include "core/cpu.hpp"
#include "core/wide_int.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string read_text_file(const std::string &path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Cannot open " + path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string state_name(ExecutionState state) {
    switch (state) {
    case ExecutionState::FETCH:
        return "fetch";
    case ExecutionState::DECODE:
        return "decode";
    case ExecutionState::EXECUTE_UOPS:
        return "execute_uops";
    case ExecutionState::DONE:
        return "done";
    }
    return "unknown";
}

nlohmann::json word_json(word_t value, int width) {
    return {{"hex", "0x" + word_to_hex_string(value, width)},
            {"decimal", word_to_dec_string(value)}};
}

nlohmann::json raw_word_json(word_t value) {
    const word_t original = value;
    std::string hex;
    do {
        hex.insert(hex.begin(), "0123456789ABCDEF"[static_cast<int>(value & 0xF)]);
        value >>= 4;
    } while (value != 0);
    return {{"hex", "0x" + hex}, {"decimal", word_to_dec_string(original)}};
}

nlohmann::json registers_json(const CPU &cpu) {
    nlohmann::json registers = nlohmann::json::object();
    const auto &defs = cpu.get_registers().get_defs();
    for (const auto &def : defs)
        registers[def.name] = word_json(cpu.get_registers().read(def.name), def.width);
    return registers;
}

struct Options {
    std::string config_path;
    std::string program_path;
    std::string result_path;
    std::string trace_path;
    int max_uops = 10000;
    int repetitions = 1;
    std::vector<std::string> expectations;
};

Options parse_options(int argc, char **argv) {
    if (argc < 3)
        throw std::runtime_error(
            "Usage: cpu_validate <config.json> <program.asm> "
            "[--max-uops N] [--repetitions N] [--expect REGISTER=VALUE] "
            "[--result file] [--trace file]");

    Options options{argv[1], argv[2]};
    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--max-uops" || arg == "--repetitions" || arg == "--expect" ||
             arg == "--result" || arg == "--trace") &&
            ++i < argc) {
            if (arg == "--max-uops")
                options.max_uops = std::stoi(argv[i]);
            else if (arg == "--repetitions")
                options.repetitions = std::stoi(argv[i]);
            else if (arg == "--expect")
                options.expectations.emplace_back(argv[i]);
            else if (arg == "--result")
                options.result_path = argv[i];
            else
                options.trace_path = argv[i];
        } else {
            throw std::runtime_error("Unknown or incomplete option: " + arg);
        }
    }
    if (options.max_uops < 1 || options.repetitions < 1)
        throw std::runtime_error("--max-uops and --repetitions must be positive");
    if (!options.trace_path.empty() && options.repetitions != 1)
        throw std::runtime_error("--trace requires --repetitions 1");
    return options;
}

void verify_expectations(const CPU &cpu, const std::vector<std::string> &expectations) {
    for (const auto &expectation : expectations) {
        const size_t equal = expectation.find('=');
        if (equal == std::string::npos || equal == 0 || equal == expectation.size() - 1)
            throw std::runtime_error("Invalid expectation: " + expectation);

        const std::string name = expectation.substr(0, equal);
        const word_t expected = parse_word(expectation.substr(equal + 1));
        const word_t actual = cpu.get_registers().read(name);
        if (actual != expected) {
            const auto &defs = cpu.get_registers().get_defs();
            int width = 128;
            for (const auto &def : defs) {
                if (def.name == name) {
                    width = def.width;
                    break;
                }
            }
            throw std::runtime_error("Expectation failed for " + name + ": expected 0x" +
                                     word_to_hex_string(expected, width) + ", got 0x" +
                                     word_to_hex_string(actual, width));
        }
    }
}

void write_json(const std::string &path, const nlohmann::json &value) {
    std::ofstream file(path);
    if (!file)
        throw std::runtime_error("Cannot write " + path);
    file << value.dump(2) << '\n';
}

} // namespace

int main(int argc, char **argv) {
    try {
        const Options options = parse_options(argc, argv);
        Config config = Config::from_file(options.config_path);
        if (!config.validate())
            throw std::runtime_error("Invalid configuration: " + config.get_error());

        Assembler assembler(config);
        const auto program = assembler.assemble(read_text_file(options.program_path));

        nlohmann::json trace = nlohmann::json::array();
        CPU cpu(config);
        int uops = 0;
        int total_uops = 0;
        const auto started = std::chrono::steady_clock::now();
        for (int repetition = 0; repetition < options.repetitions; ++repetition) {
            cpu.load_program(program);
            cpu.reset();
            uops = 0;
            while (!cpu.is_halted() && uops < options.max_uops) {
                cpu.step_uop();
                ++uops;

                if (!options.trace_path.empty()) {
                    const auto &executor = cpu.get_executor();
                    const auto &instruction = executor.get_current_inst();
                    trace.push_back({
                        {"step", uops},
                        {"cycle", executor.get_total_cycles()},
                        {"state", state_name(executor.get_state())},
                        {"pc", word_json(cpu.get_registers().get_pc(), config.addr_width)},
                        {"instruction", instruction.is_valid ? instruction.name : ""},
                        {"uop_index", executor.get_current_uop_index()},
                        {"address", word_json(executor.get_last_addr_val(), config.addr_width)},
                        {"data", raw_word_json(executor.get_last_data_val())},
                    });
                }
            }
            total_uops += uops;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();

        if (!cpu.is_halted())
            throw std::runtime_error("Execution limit reached after " + std::to_string(uops) +
                                     " simulation steps");
        verify_expectations(cpu, options.expectations);

        nlohmann::json result = {
            {"schema_version", 1},
            {"config", {{"name", config.name},
                        {"data_width", config.data_width},
                        {"address_width", config.addr_width},
                        {"memory_architecture", config.memory_architecture},
                        {"endianness", config.endianness}}},
            {"program", options.program_path},
            {"halted", cpu.is_halted()},
            {"simulation_steps", uops},
            {"cycles", cpu.get_executor().get_total_cycles()},
            {"repetitions", options.repetitions},
            {"mean_wall_microseconds", static_cast<double>(elapsed) / options.repetitions},
            {"simulation_steps_per_second",
             elapsed == 0 ? 0.0 : (1000000.0 * total_uops) / elapsed},
            {"registers", registers_json(cpu)},
        };
        if (!options.trace_path.empty()) {
            write_json(options.trace_path, trace);
            result["trace"] = options.trace_path;
        }

        if (options.result_path.empty())
            std::cout << result.dump(2) << '\n';
        else
            write_json(options.result_path, result);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Experiment failed: " << error.what() << '\n';
        return 1;
    }
}
