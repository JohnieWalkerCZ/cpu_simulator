#include "memory.hpp"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

Memory::Memory(const Config &config)
    : memory_size_(config.memory_size),
      word_size_bytes_((config.data_width + 7) / 8),
      segments_(config.memory_segments),
      endianness_(config.endianness == "big" ? Endianness::Big
                                             : Endianness::Little),
      arch_(config.memory_architecture == "harvard" ? MemoryArch::Harvard
                                                    : MemoryArch::VonNeumann) {

    if (config.data_width < 4 || config.data_width > 128 ||
        (config.data_width % 4) != 0) {
        throw std::runtime_error("Unsupported word size.");
    }

    if (word_size_bytes_ == 0)
        word_size_bytes_ = 1;
    mask_ = mask_for_width(config.data_width);
}

uint8_t Memory::read_page_byte(const PageTable &pages, word_t address) {
    auto it = pages.find(address / PAGE_SIZE);
    if (it == pages.end())
        return 0;
    return it->second[static_cast<size_t>(address % PAGE_SIZE)];
}

void Memory::write_page_byte(PageTable &pages, word_t address,
                             uint8_t value) {
    auto &page = pages[address / PAGE_SIZE];
    if (page.empty())
        page.assign(PAGE_SIZE, 0);
    page[static_cast<size_t>(address % PAGE_SIZE)] = value;
}

void Memory::check_access(word_t address, bool req_r, bool req_w,
                          bool req_x) const {

    if (!is_valid_address(address)) {
        throw std::runtime_error("Memory Access Violation: Address 0x" +
                                 word_to_dec_string(address) +
                                 " is out of physical bounds.");
    }

    for (const auto &seg : segments_) {
        if (address >= seg.start && address <= seg.end) {
            if (req_r && !seg.r)
                throw std::runtime_error(
                    "Memory Protection Fault: Read violation at 0x" +
                    word_to_dec_string(address));
            if (req_w && !seg.w)
                throw std::runtime_error(
                    "Memory Protection Fault: Write violation at 0x" +
                    word_to_dec_string(address));
            if (req_x && !seg.x)
                throw std::runtime_error(
                    "Memory Protection Fault: Execution violation at 0x" +
                    word_to_dec_string(address));
            return;
        }
    }
    throw std::runtime_error("Memory Protection Fault: Address 0x" +
                             word_to_dec_string(address) + " is unmapped.");
}

word_t Memory::read(word_t address, bool is_execute, int width_bits) const {
    if (auto reg = find_io_region(address)) {
        return reg->read_cb ? reg->read_cb(address) : 0;
    }

    int target_width = (width_bits > 0) ? width_bits : (word_size_bytes_ * 8);
    int target_bytes = (target_width + 7) / 8;

    for (int i = 0; i < target_bytes; ++i) {
        check_access(address + i, !is_execute, false, is_execute);
    }

    const PageTable &target_pages =
        (arch_ == MemoryArch::Harvard && is_execute) ? instruction_pages_
                                                     : pages_;

    word_t result = 0;
    for (int i = 0; i < target_bytes; ++i) {
        int shift = (endianness_ == Endianness::Little)
                        ? (i * 8)
                        : ((target_bytes - 1 - i) * 8);
        result |= (static_cast<word_t>(
                      read_page_byte(target_pages, address + i)))
                  << shift;
    }
    return result;
}

void Memory::write(word_t address, word_t value, int width_bits) {
    if (auto reg = find_io_region(address)) {
        if (reg->write_cb)
            reg->write_cb(address, value);
        return;
    }

    int target_width = (width_bits > 0) ? width_bits : (word_size_bytes_ * 8);
    int target_bytes = (target_width + 7) / 8;

    for (int i = 0; i < target_bytes; ++i) {
        check_access(address + i, false, true, false);
    }

    word_t mask = mask_for_width(target_width);
    value &= mask;

    for (int i = 0; i < target_bytes; ++i) {
        int shift = (endianness_ == Endianness::Little)
                        ? (i * 8)
                        : ((target_bytes - 1 - i) * 8);
        write_page_byte(pages_, address + i,
                       static_cast<uint8_t>((value >> shift) & 0xFF));
    }
}

uint8_t Memory::peek(word_t address) const {
    if (address >= memory_size_)
        return 0;
    return read_page_byte(pages_, address);
}

uint8_t Memory::peek_instruction(word_t address) const {
    if (address >= memory_size_)
        return 0;
    return read_page_byte(instruction_pages_, address);
}

std::vector<uint8_t> Memory::read_bytes(word_t address, size_t count) const {
    std::vector<uint8_t> result;
    result.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        if (!is_valid_address(address + i)) {
            throw std::runtime_error("Memory read out of bound");
        }
        result.push_back(read_page_byte(pages_, address + i));
    }
    return result;
}

void Memory::write_bytes(word_t address, const std::vector<uint8_t> &data) {
    for (size_t i = 0; i < data.size(); ++i) {
        if (!is_valid_address(address + i)) {
            throw std::runtime_error("Memory write out of bounds");
        }
        if (arch_ == MemoryArch::Harvard) {
            write_page_byte(instruction_pages_, address + i, data[i]);
        } else {
            write_page_byte(pages_, address + i, data[i]);
        }
    }
}

void Memory::load_program(const std::vector<uint8_t> &machine_code,
                          word_t start_address) {
    write_bytes(start_address, machine_code);
}

bool Memory::is_valid_address(word_t address) const {
    if (address >= memory_size_)
        return false;
    word_t end = address + static_cast<word_t>(word_size_bytes_) - 1;
    if (end < address) // overflow past the 128-bit address space
        return false;
    return end < memory_size_;
}

void Memory::reset() {
    pages_.clear();
    instruction_pages_.clear();
}

void Memory::map_io_region(word_t start, word_t end, MMIO_ReadCallback r_cb,
                           MMIO_WriteCallback w_cb) {
    io_regions_.push_back({start, end, r_cb, w_cb});
}

void Memory::reset_io_hooks() { io_regions_.clear(); }

const MMIORegion *Memory::find_io_region(word_t address) const {
    for (const MMIORegion &r : io_regions_) {
        if (address >= r.start && address <= r.end) {
            return &r;
        }
    }

    return nullptr;
}

void Memory::map_port_region(word_t start, word_t end,
                             MMIO_ReadCallback r_cb, MMIO_WriteCallback w_cb) {
    port_regions_.push_back({start, end, r_cb, w_cb});
}

word_t Memory::port_read(word_t port) {
    for (const auto &r : port_regions_) {
        if (port >= r.start && port <= r.end) {
            return r.read_cb ? r.read_cb(port) : 0;
        }
    }
    return 0;
}

void Memory::port_write(word_t port, word_t value) {
    for (const auto &r : port_regions_) {
        if (port >= r.start && port <= r.end) {
            if (r.write_cb)
                r.write_cb(port, value);
            return;
        }
    }
}

Memory::Snapshot Memory::capture_snapshot() const {
    return Snapshot{pages_, instruction_pages_};
}

void Memory::restore_snapshot(const Snapshot &snap) {
    pages_ = snap.pages;
    instruction_pages_ = snap.instruction_pages;
}
