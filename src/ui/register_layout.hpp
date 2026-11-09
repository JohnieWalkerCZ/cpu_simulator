#pragma once
// Pure (no ImGui) bit-layout math for a register and its sub-registers,
// used by the configurator's sub-register bit bar. A sub-register slices
// its parent either by offset (bits [offset, offset + width)) or by mask
// (the set bits of the mask pattern). The span computation mirrors the
// loader (src/core/config.cpp parse_mask_pattern +
// parse_register_recursive) so the bar shows what the CPU will actually
// do:
//
//   - Offset slice: parent bits offset..offset+width-1; bits past the
//     parent width map to nothing (out_of_range).
//   - Mask slice: the pattern is applied cyclically over the parent
//     width (bit i of the parent tests pattern bit i % pattern_len),
//     and the sub-register maps its width onto the first `width`
//     active parent positions, in increasing order. A mask with fewer
//     active positions than the sub-register width leaves the rest
//     unmapped (out_of_range). Pattern length follows the loader:
//     JSON number -> the sub-register width; "0b..." -> number of
//     binary digits; "0x..." -> 4 * number of hex digits; anything
//     else -> 8.
//
// Bit 0 is the least significant bit; the bar draws it on the right.

#include "core/wide_int.hpp"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace RegisterLayout {

struct SubSpan {
    std::string name;
    // Parent bit positions actually used, ascending. Empty when the
    // slice covers no in-range parent bit at all.
    std::vector<int> bits;
    int first_bit = -1; // lowest parent bit used, -1 when bits is empty
    int last_bit = -1;  // highest parent bit used, -1 when bits is empty
    bool is_mask = false;     // sliced by mask instead of offset
    bool overlaps = false;    // shares a parent bit with an earlier sibling
    bool out_of_range = false; // part of the slice falls outside the parent
};

// Parse a mask value the way the loader does. Returns false when the
// mask cannot be parsed at all.
inline bool ParseMask(const nlohmann::json &val, word_t &pattern,
                      int &pattern_len, int sub_width) {
    if (val.is_number_unsigned()) {
        pattern = static_cast<word_t>(val.get<unsigned long long>());
        pattern_len = sub_width;
        return true;
    }
    if (val.is_number_integer() && val.get<long long>() >= 0) {
        pattern = static_cast<word_t>(val.get<unsigned long long>());
        pattern_len = sub_width;
        return true;
    }
    if (!val.is_string())
        return false;
    const std::string str = val.get<std::string>();
    try {
        if (str.compare(0, 2, "0b") == 0 || str.compare(0, 2, "0B") == 0) {
            pattern_len = static_cast<int>(str.size()) - 2;
            pattern = parse_word("0b" + str.substr(2));
        } else if (str.compare(0, 2, "0x") == 0 ||
                   str.compare(0, 2, "0X") == 0) {
            pattern_len = static_cast<int>(str.size() - 2) * 4;
            pattern = parse_word(str);
        } else {
            pattern_len = 8;
            pattern = parse_word(str);
        }
    } catch (...) {
        return false;
    }
    if (pattern_len > sub_width) {
        pattern_len = sub_width;
        pattern &= mask_for_width(pattern_len);
    }
    return pattern_len > 0;
}

// Parent bit positions one sub-register actually uses.
inline std::vector<int> UsedBits(const nlohmann::json &sub,
                                 int parent_width) {
    const int w = sub.value("width", 0);
    std::vector<int> bits;
    if (parent_width <= 0 || w <= 0)
        return bits;
    if (sub.contains("mask")) {
        word_t pattern = 0;
        int pattern_len = 0;
        if (!ParseMask(sub["mask"], pattern, pattern_len, w))
            return bits;
        std::vector<int> active;
        for (int i = 0; i < parent_width; ++i) {
            const int bit_idx = i % pattern_len;
            if (((pattern >> bit_idx) & 1) != 0)
                active.push_back(i);
        }
        for (int j = 0; j < w && j < static_cast<int>(active.size()); ++j)
            bits.push_back(active[j]);
    } else {
        const int off = sub.value("offset", 0);
        for (int i = 0; i < w; ++i) {
            const int b = off + i;
            if (b >= 0 && b < parent_width)
                bits.push_back(b);
        }
    }
    return bits;
}

// One span per sub-register, in document order.
inline std::vector<SubSpan> SubRegisterSpans(const nlohmann::json &reg) {
    const int pw = reg.value("width", 0);
    std::vector<SubSpan> spans;
    if (!reg.contains("sub_registers") ||
        !reg["sub_registers"].is_array())
        return spans;
    std::vector<bool> used(pw > 0 ? static_cast<size_t>(pw) : 0, false);
    for (const auto &sub : reg["sub_registers"]) {
        SubSpan s;
        s.name = sub.value("name", "?");
        s.is_mask = sub.contains("mask");
        s.bits = UsedBits(sub, pw);
        s.first_bit = s.bits.empty() ? -1 : s.bits.front();
        s.last_bit = s.bits.empty() ? -1 : s.bits.back();
        const int w = sub.value("width", 0);
        if (s.is_mask)
            s.out_of_range = static_cast<int>(s.bits.size()) < w;
        else
            s.out_of_range = sub.value("offset", 0) < 0 ||
                             sub.value("offset", 0) + w > pw;
        for (int b : s.bits) {
            if (b >= 0 && b < static_cast<int>(used.size()) && used[b])
                s.overlaps = true;
        }
        for (int b : s.bits)
            if (b >= 0 && b < static_cast<int>(used.size()))
                used[b] = true;
        spans.push_back(std::move(s));
    }
    return spans;
}

// Smallest offset at which a slice of `width` bits fits without
// overlapping any existing sub-register, or -1 when there is no room.
inline int FirstFreeOffset(const nlohmann::json &reg, int width) {
    const int pw = reg.value("width", 0);
    if (pw <= 0 || width <= 0 || width > pw)
        return -1;
    std::vector<bool> used(static_cast<size_t>(pw), false);
    for (const auto &s : SubRegisterSpans(reg))
        for (int b : s.bits)
            if (b >= 0 && b < pw)
                used[static_cast<size_t>(b)] = true;
    for (int off = 0; off + width <= pw; ++off) {
        bool free = true;
        for (int i = 0; i < width; ++i)
            if (used[static_cast<size_t>(off + i)]) {
                free = false;
                break;
            }
        if (free)
            return off;
    }
    return -1;
}

} // namespace RegisterLayout
