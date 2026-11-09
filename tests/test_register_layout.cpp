// Tests for the sub-register bit-layout math
// (src/ui/register_layout.hpp): offset and mask spans, overlap
// detection, out-of-range slices, and the first free offset used when
// a new sub-register is added.
#include "ui/register_layout.hpp"
#include <cassert>
#include <iostream>

using nlohmann::json;
using namespace RegisterLayout;

static bool HasBit(const SubSpan &s, int bit) {
    for (int b : s.bits)
        if (b == bit)
            return true;
    return false;
}

int main() {
    // Offset spans: two 4-bit halves of an 8-bit register.
    {
        json reg = json::parse(
            R"({"name":"R","width":8,"sub_registers":[{"name":"LO","width":4,"offset":0},{"name":"HI","width":4,"offset":4}]})");
        std::vector<SubSpan> spans = SubRegisterSpans(reg);
        assert(spans.size() == 2);
        assert(spans[0].name == "LO" && spans[0].first_bit == 0 &&
               spans[0].last_bit == 3 && !spans[0].is_mask &&
               !spans[0].overlaps && !spans[0].out_of_range);
        assert(spans[1].name == "HI" && spans[1].first_bit == 4 &&
               spans[1].last_bit == 7 && !spans[1].overlaps &&
               !spans[1].out_of_range);
        assert(FirstFreeOffset(reg, 1) == -1); // fully covered
    }

    // A gap stays free: first free 1-bit offset is the gap, a 2-bit
    // slice does not fit in it.
    {
        json reg = json::parse(
            R"({"name":"R","width":8,"sub_registers":[{"name":"A","width":2,"offset":0},{"name":"B","width":2,"offset":4}]})");
        assert(FirstFreeOffset(reg, 1) == 2);
        assert(FirstFreeOffset(reg, 2) == 2);
        assert(FirstFreeOffset(reg, 3) == 6);
        assert(FirstFreeOffset(reg, 8) == -1);
        assert(FirstFreeOffset(reg, 0) == -1);
        std::vector<SubSpan> spans = SubRegisterSpans(reg);
        assert(!spans[0].overlaps && !spans[1].overlaps);
        assert(!HasBit(spans[0], 2) && !HasBit(spans[1], 2));
    }

    // Empty sub-register list: offset 0 is free.
    {
        json reg = json::parse(R"({"name":"R","width":8})");
        assert(SubRegisterSpans(reg).empty());
        assert(FirstFreeOffset(reg, 1) == 0);
        assert(FirstFreeOffset(reg, 8) == 0);
        assert(FirstFreeOffset(reg, 9) == -1);
    }

    // Mask spans: "0b0101" over an 8-bit parent selects bits 0, 2, 4, 6
    // (the pattern repeats cyclically).
    {
        json reg = json::parse(
            R"({"name":"R","width":8,"sub_registers":[{"name":"EVEN","width":4,"mask":"0b0101"}]})");
        std::vector<SubSpan> spans = SubRegisterSpans(reg);
        assert(spans.size() == 1);
        assert(spans[0].is_mask && spans[0].first_bit == 0 &&
               spans[0].last_bit == 6 && !spans[0].overlaps &&
               !spans[0].out_of_range);
        assert(spans[0].bits == (std::vector<int>{0, 2, 4, 6}));
        // The mask occupies 0, 2, 4, 6, so the first free bit is 1.
        assert(FirstFreeOffset(reg, 1) == 1);
        assert(FirstFreeOffset(reg, 2) == -1); // no two adjacent free bits
    }

    // Hex mask: "0x5" is the same 0b0101 pattern.
    {
        json reg = json::parse(
            R"({"name":"R","width":8,"sub_registers":[{"name":"EVEN","width":4,"mask":"0x5"}]})");
        std::vector<SubSpan> spans = SubRegisterSpans(reg);
        assert(spans[0].bits == (std::vector<int>{0, 2, 4, 6}));
    }

    // A mask with fewer active bits than the sub-register width leaves
    // the rest unmapped: "0b0001" repeats over the parent, so only
    // bits 0 and 4 are active, and a 4-bit sub-register gets just those.
    {
        json reg = json::parse(
            R"({"name":"R","width":8,"sub_registers":[{"name":"ONE","width":4,"mask":"0b0001"}]})");
        std::vector<SubSpan> spans = SubRegisterSpans(reg);
        assert(spans[0].bits == (std::vector<int>{0, 4}));
        assert(spans[0].out_of_range);
    }

    // Numeric mask: the JSON number 3 is 0b11 over the sub width.
    {
        json reg = json::parse(
            R"({"name":"R","width":8,"sub_registers":[{"name":"LO2","width":2,"mask":3}]})");
        std::vector<SubSpan> spans = SubRegisterSpans(reg);
        assert(spans[0].bits == (std::vector<int>{0, 1}));
    }

    // Unparseable mask: no bits, out of range.
    {
        json reg = json::parse(
            R"({"name":"R","width":8,"sub_registers":[{"name":"BAD","width":2,"mask":"nonsense"}]})");
        std::vector<SubSpan> spans = SubRegisterSpans(reg);
        assert(spans[0].bits.empty() && spans[0].first_bit == -1 &&
               spans[0].out_of_range);
    }

    // Overlap: the second sub-register shares bits 2 and 3 with the
    // first; only the later one is flagged.
    {
        json reg = json::parse(
            R"({"name":"R","width":8,"sub_registers":[{"name":"A","width":4,"offset":0},{"name":"B","width":4,"offset":2}]})");
        std::vector<SubSpan> spans = SubRegisterSpans(reg);
        assert(!spans[0].overlaps && spans[1].overlaps);
        assert(spans[1].first_bit == 2 && spans[1].last_bit == 5);
        assert(FirstFreeOffset(reg, 1) == 6);
    }

    // Offset slice past the parent width: only the in-range bits are
    // used, and the slice is flagged.
    {
        json reg = json::parse(
            R"({"name":"R","width":8,"sub_registers":[{"name":"TAIL","width":4,"offset":6}]})");
        std::vector<SubSpan> spans = SubRegisterSpans(reg);
        assert(spans[0].bits == (std::vector<int>{6, 7}));
        assert(spans[0].out_of_range);
        assert(FirstFreeOffset(reg, 1) == 0);
    }

    // Mask and offset slices can overlap too.
    {
        json reg = json::parse(
            R"({"name":"R","width":8,"sub_registers":[{"name":"EVEN","width":4,"mask":"0b0101"},{"name":"LO","width":2,"offset":0}]})");
        std::vector<SubSpan> spans = SubRegisterSpans(reg);
        assert(!spans[0].overlaps && spans[1].overlaps); // LO shares bit 0
    }

    std::cout << "All register layout tests passed!" << std::endl;
    return 0;
}
