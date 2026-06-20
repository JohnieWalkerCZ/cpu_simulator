#pragma once
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

#if !defined(__SIZEOF_INT128__)
#error "This simulator requires a compiler with 128-bit integer support (GCC/Clang __int128) to model widths above 64 bits."
#endif

// The universal value/address type for the simulation core (register
// contents, ALU operands, memory addresses and data, decoded instruction
// fields). Standard library facilities (std::to_string, std::stoull,
// printf-family %llu/%llx, nlohmann::json's get<T>()) have no native
// support for __int128, so the helpers below stand in for them wherever
// a word_t needs to become text or be parsed from it.
using word_t = unsigned __int128;

// std::unordered_map has no built-in hash for __int128; this combines the
// high/low 64-bit halves for use as a key (e.g. page indices in Memory).
struct WordHash {
    size_t operator()(word_t v) const noexcept {
        uint64_t hi = static_cast<uint64_t>(v >> 64);
        uint64_t lo = static_cast<uint64_t>(v);
        return std::hash<uint64_t>{}(hi) ^
              (std::hash<uint64_t>{}(lo) * 0x9e3779b97f4a7c15ULL);
    }
};

inline word_t mask_for_width(int width_bits) {
    if (width_bits <= 0)
        return 0;
    if (width_bits >= 128)
        return ~static_cast<word_t>(0);
    return (static_cast<word_t>(1) << width_bits) - 1;
}

inline std::string word_to_dec_string(word_t v) {
    if (v == 0)
        return "0";
    std::string s;
    while (v > 0) {
        s.insert(s.begin(),
                 static_cast<char>('0' + static_cast<int>(v % 10)));
        v /= 10;
    }
    return s;
}

// Renders the low `width_bits` of v as uppercase hex, zero-padded to the
// exact digit count that width implies (no "0x" prefix).
inline std::string word_to_hex_string(word_t v, int width_bits) {
    int digits = (width_bits + 3) / 4;
    if (digits <= 0)
        digits = 1;
    std::string s(digits, '0');
    for (int i = digits - 1; i >= 0 && v != 0; --i) {
        s[i] = "0123456789ABCDEF"[static_cast<int>(v & 0xF)];
        v >>= 4;
    }
    return s;
}

// Replacement for std::stoull that doesn't lose precision above 64 bits.
// Accepts optional leading sign, then a decimal, "0x.../0X..." hex, or
// "0b.../0B..." binary literal -- the same conventions std::stoull(..., 0)
// supported, just widened.
inline word_t parse_word(const std::string &str) {
    size_t pos = 0;
    bool neg = false;
    if (pos < str.size() && (str[pos] == '+' || str[pos] == '-')) {
        neg = (str[pos] == '-');
        ++pos;
    }

    int base = 10;
    if (str.compare(pos, 2, "0x") == 0 || str.compare(pos, 2, "0X") == 0) {
        base = 16;
        pos += 2;
    } else if (str.compare(pos, 2, "0b") == 0 ||
              str.compare(pos, 2, "0B") == 0) {
        base = 2;
        pos += 2;
    }

    word_t result = 0;
    bool any_digit = false;
    for (; pos < str.size(); ++pos) {
        char c = str[pos];
        int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F')
            digit = 10 + (c - 'A');
        else
            throw std::runtime_error("Invalid numeric literal: " + str);

        if (digit >= base)
            throw std::runtime_error("Invalid numeric literal: " + str);

        result = result * static_cast<word_t>(base) + static_cast<word_t>(digit);
        any_digit = true;
    }

    if (!any_digit)
        throw std::runtime_error("Invalid numeric literal: " + str);

    return neg ? static_cast<word_t>(0) - result : result;
}
