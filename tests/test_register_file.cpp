#include "core/registers/register_file.hpp"
#include "core/config.hpp"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>

int main() {
    try {
        nlohmann::json j = {
            {"name", "TestISA"},
            {"data_bus", {{"width", 8}}},
            {"address_bus", {{"width", 16}}},
            {"memory", {{"size", 1024}}},
            {"registers", {
                {"general_purpose", {
                    {
                        {"name", "R0"}, {"width", 8}, {"initial", 0},
                        {"sub_registers", {
                            {
                                {"name", "R0H"}, {"width", 4}, {"offset", 4},
                                {"sub_registers", {
                                    {{"name", "R0H_even"}, {"width", 2}, {"mask", "0b01"}},
                                    {{"name", "R0H_odd"}, {"width", 2}, {"mask", "0b10"}}
                                }}
                            },
                            {{"name", "R0_even"}, {"width", 4}, {"mask", "0b0101"}},
                            {{"name", "R0_odd"}, {"width", 4}, {"mask", "0b1010"}},
                            {{"name", "R0_clip"}, {"width", 2}, {"mask", "0b0101"}}
                        }}
                    }
                }},
                {"special", {
                    {{"name", "PC"}, {"width", 16}, {"initial", 0}, {"role", "program_counter"}}
                }}
            }}
        };
        Config cfg = Config::from_json(j);
        RegisterFile regs(cfg);

        // Test 1: Writing to root and reading aliases
        regs.write("R0", 0xF0); // 1111 0000
        assert(regs.read("R0") == 0xF0);
        assert(regs.read("R0H") == 0x0F); // High nibble = 1111 = 15
        
        // R0H_even maps to physical bits 4 and 6 of R0. 
        // In 0xF0 (1111 0000), bits 4 and 6 are both 1.
        // Packed reading should return 0x03 (binary 11) because both virtual bits are active.
        assert(regs.read("R0H_even") == 0x03); 

        // Test 2: Writing to an alias and reading parent
        regs.reset();
        regs.write("R0H", 0x0A); // Write 1010 to high nibble. R0 should become 1010 0000 = 160
        assert(regs.read("R0") == 160);

        // Test 3: Overlapping masked aliases (Odd/Even interleaving)
        regs.reset();
        regs.write("R0_even", 0x0F); // Set all 4 even bits (0, 2, 4, 6) -> R0 becomes 0x55 (85)
        assert(regs.read("R0") == 85);
        
        regs.write("R0_odd", 0x0F); // Set all 4 odd bits (1, 3, 5, 7) -> R0 becomes 0x55 | 0xAA = 0xFF (255)
        assert(regs.read("R0") == 255);

        // Test 4: Mask Clipping
        regs.reset();
        regs.write("R0_clip", 0x03); // Write binary 11 to R0_clip -> sets physical bits 0 and 2.
        assert(regs.read("R0") == 0x05); // R0 becomes 0000 0101 (5)

        // Test 5: A register wider than 64 bits round-trips a >64-bit value
        // correctly, exercising both the contiguous fast path (W0, W0H) and
        // the bit-by-bit fallback (W0_even, a masked alias).
        {
            nlohmann::json wide_j = {
                {"name", "WideISA"},
                {"data_bus", {{"width", 8}}},
                {"address_bus", {{"width", 16}}},
                {"memory", {{"size", 1024}}},
                {"registers", {
                    {"general_purpose", {
                        {
                            {"name", "W0"}, {"width", 100}, {"initial", 0},
                            {"sub_registers", {
                                {{"name", "W0H"}, {"width", 36}, {"offset", 64}},
                                {{"name", "W0_even"}, {"width", 4}, {"mask", "0b0101"}}
                            }}
                        }
                    }},
                    {"special", {
                        {{"name", "PC"}, {"width", 16}, {"initial", 0}, {"role", "program_counter"}}
                    }}
                }}
            };
            Config wide_cfg = Config::from_json(wide_j);
            RegisterFile wide_regs(wide_cfg);

            // is_contiguous is computed by RegisterFile's constructor into
            // its own copy of the defs, not onto the Config object itself.
            assert(wide_regs.get_defs()[0].is_contiguous);
            assert(wide_regs.get_defs()[1].is_contiguous); // W0H: offset-based

            word_t big_val = parse_word("0xABCDEF0123456789ABCDEF012");
            wide_regs.write("W0", big_val);
            assert(wide_regs.read("W0") == big_val);

            // W0H is bits [64, 100) of W0 -- verify it matches a manual shift.
            word_t expected_high = big_val >> 64;
            assert(wide_regs.read("W0H") == expected_high);

            // W0_even (masked, non-contiguous: bits 0,2,4,6) still works via
            // the fallback path on a register backed by a value above 64
            // bits. Writing 1111 sets all four mapped bits -> 0x55.
            wide_regs.reset();
            wide_regs.write("W0_even", 0x0F);
            assert(wide_regs.read("W0") == 0x55);
        }

        std::cout << "RegisterFile unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "Test failed: " << e.what() << "\n";
        return 1;
    }
}
