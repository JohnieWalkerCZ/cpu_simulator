#pragma once
#include "ui_common.hpp"
#include <algorithm>

inline void UI_StackFrameExplorer(CPU &cpu, GUIState &gui) {
    ImGui::Begin("Stack Frame Explorer");

    auto &regs = cpu.get_registers();
    auto &mem = cpu.get_memory();
    auto &config = cpu.get_config();
    int addr_width = config.addr_width;

    int sp_idx = regs.find_by_role("stack_pointer");
    if (sp_idx == -1)
        sp_idx = regs.find_by_role("sp");

    if (sp_idx == -1) {
        ImGui::TextWrapped(
            "This architecture has no stack pointer register defined.");
        ImGui::End();
        return;
    }

    uint64_t sp = regs.read(sp_idx);
    auto &sf = gui.stack_frames;

    ImGui::Text("SP = %s", FormatHexValue(sp, addr_width).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("   Depth: %zu frame(s)", sf.frame_boundaries.size());

    const MemorySegmentDef *stack_seg = nullptr;
    for (const auto &seg : config.memory_segments) {
        if (sp >= seg.start && sp <= seg.end) {
            stack_seg = &seg;
            break;
        }
    }

    if (stack_seg) {
        ImGui::TextDisabled(
            "Segment '%s': %s - %s", stack_seg->name.c_str(),
            FormatHexValue(stack_seg->start, addr_width).c_str(),
            FormatHexValue(stack_seg->end, addr_width).c_str());
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "Warning: SP is outside any defined memory segment.");
    }
    ImGui::Separator();

    if (sp >= mem.size()) {
        ImGui::TextWrapped("SP (%s) is outside addressable memory.",
                           FormatHexValue(sp, addr_width).c_str());
        ImGui::End();
        return;
    }

    uint64_t range_end = stack_seg ? stack_seg->end
                                   : std::min<uint64_t>(sp + 64, mem.size() - 1);
    if (range_end >= mem.size())
        range_end = mem.size() - 1;

    // Guard against pathologically large segments flooding the immediate-mode
    // row list -- the visualizer is meant for inspecting active stack depth,
    // not dumping an entire address space.
    const uint64_t max_rows = 2048;
    if (range_end - sp + 1 > max_rows)
        range_end = sp + max_rows - 1;

    // Boundaries closer to SP are visited first while walking down from SP.
    std::vector<uint64_t> boundaries;
    for (uint64_t b : sf.frame_boundaries) {
        if (b > sp && b <= range_end + 1)
            boundaries.push_back(b);
    }
    std::sort(boundaries.begin(), boundaries.end());

    ImGui::BeginChild("StackScroll");

    size_t boundary_idx = 0;
    uint64_t frame_top = sp;

    for (uint64_t addr = sp; addr <= range_end; ++addr) {
        while (boundary_idx < boundaries.size() &&
               addr == boundaries[boundary_idx]) {
            ImGui::Separator();
            ImGui::TextDisabled(
                "-- frame boundary @ %s --",
                FormatHexValue(boundaries[boundary_idx], addr_width).c_str());
            frame_top = boundaries[boundary_idx];
            boundary_idx++;
        }

        uint8_t val = mem.raw()[addr];
        uint64_t offset = addr - frame_top;
        bool is_sp = (addr == sp);

        ImGui::PushID(static_cast<int>(addr));
        if (is_sp)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.2f, 1.0f));

        ImGui::Text("%s   +%llu   %s", FormatHexValue(addr, addr_width).c_str(),
                   (unsigned long long)offset, FormatHexValue(val, 8).c_str());

        if (is_sp) {
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("<- SP");
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Address: %s\nByte: %s (%u)\nFrame offset: +%llu",
                              FormatHexValue(addr, addr_width).c_str(),
                              FormatHexValue(val, 8).c_str(), val,
                              (unsigned long long)offset);
        }
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::End();
}
