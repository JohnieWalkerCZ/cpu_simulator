#pragma once
#include "ui_common.hpp"
#include "ui_cpu_pointlist.hpp"
#include <algorithm>
#include <string>
#include <vector>

// ============================================================================
// HELPER ROUTINES & BLENDING
// ============================================================================

/**
 * @brief Blends a base color with a glow color based on a given intensity.
 *
 * @param base Base color (inactive state representation)
 * @param glow Glow color (active state representation)
 * @param intensity Value from 0.0f to 1.0f indicating the glow dominance
 * @return ImU32 Blended 32-bit color
 */
inline ImU32 MixGlowColor(ImU32 base, ImU32 glow, float intensity) {
    int r = static_cast<int>(((base >> 0) & 0xFF) * (1.0f - intensity) +
                             ((glow >> 0) & 0xFF) * intensity);
    int g = static_cast<int>(((base >> 8) & 0xFF) * (1.0f - intensity) +
                             ((glow >> 8) & 0xFF) * intensity);
    int b = static_cast<int>(((base >> 16) & 0xFF) * (1.0f - intensity) +
                             ((glow >> 16) & 0xFF) * intensity);
    return IM_COL32(r, g, b, 255);
}

// ============================================================================
// BLOCK RENDERING FUNCTIONS (SCHEMATIC BOXES)
// ============================================================================

/**
 * @brief Draws a standard highlighted outline box block.
 */
inline void DrawBlock(ImDrawList *draw_list, ImVec2 pos, ImVec2 size,
                      const char *label, ImU32 base_col, ImU32 glow_col,
                      float intensity, ImU32 col_text, ImU32 col_box,
                      float zoom) {
    ImU32 border_col = MixGlowColor(base_col, glow_col, intensity);
    draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                             col_box, 4.0f * zoom);
    draw_list->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), border_col,
                       4.0f * zoom, 0, 2.0f * zoom);

    ImVec2 text_size = ImGui::CalcTextSize(label);
    text_size.x *= zoom;
    text_size.y *= zoom;
    ImVec2 text_pos = ImVec2(pos.x + (size.x - text_size.x) / 2.0f,
                             pos.y + (size.y - text_size.y) / 2.0f);
    draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * zoom, text_pos,
                       col_text, label);
}

/**
 * @brief Draws the Decoder block with interactive scanning animations during
 * DECODE state.
 */

inline void DrawDecoderBlock(ImDrawList *draw_list, ImVec2 ctrl_pos,
                             ImVec2 box_size,
                             const DecodedInstruction &cur_inst,
                             ExecutionState exec_state, float ir_bus,
                             ImU32 col_box, ImU32 col_text, ImU32 col_inactive,
                             ImU32 col_active, float zoom) {
    if (exec_state == ExecutionState::DECODE) {
        ImU32 col_decoder_active_bg = IM_COL32(0, 45, 5, 200);
        draw_list->AddRectFilled(
            ctrl_pos, ImVec2(ctrl_pos.x + box_size.x, ctrl_pos.y + box_size.y),
            col_decoder_active_bg, 4.0f * zoom);
        draw_list->AddRect(
            ctrl_pos, ImVec2(ctrl_pos.x + box_size.x, ctrl_pos.y + box_size.y),
            IM_COL32(0, 255, 0, 255), 4.0f * zoom, 0, 2.0f * zoom);

        float scan_t = static_cast<float>(SDL_GetTicks() % 1000) / 1000.0f;
        float scan_y = ctrl_pos.y + 2.0f * zoom +
                       (box_size.y - 4.0f * zoom) *
                           (0.5f - 0.5f * cos(scan_t * 2.0f * 3.1415926535f));
        draw_list->AddLine(
            ImVec2(ctrl_pos.x + 2.0f * zoom, scan_y),
            ImVec2(ctrl_pos.x + box_size.x - 2.0f * zoom, scan_y),
            IM_COL32(0, 255, 0, 255), 2.0f * zoom);
    } else if (exec_state == ExecutionState::FETCH && ir_bus > 0.1f) {
        ImU32 col_decoder_fetch_bg =
            IM_COL32(40, 0, 40, static_cast<int>(180.0f * ir_bus));
        draw_list->AddRectFilled(
            ctrl_pos, ImVec2(ctrl_pos.x + box_size.x, ctrl_pos.y + box_size.y),
            col_decoder_fetch_bg, 4.0f * zoom);
        draw_list->AddRect(
            ctrl_pos, ImVec2(ctrl_pos.x + box_size.x, ctrl_pos.y + box_size.y),
            IM_COL32(255, 0, 255, static_cast<int>(255.0f * ir_bus)),
            4.0f * zoom, 0, 2.0f * zoom);
    } else {
        draw_list->AddRectFilled(
            ctrl_pos, ImVec2(ctrl_pos.x + box_size.x, ctrl_pos.y + box_size.y),
            col_box, 4.0f * zoom);
        draw_list->AddRect(
            ctrl_pos, ImVec2(ctrl_pos.x + box_size.x, ctrl_pos.y + box_size.y),
            MixGlowColor(col_inactive, col_active, ir_bus), 4.0f * zoom, 0,
            2.0f * zoom);
    }

    bool show_inst_name = (exec_state == ExecutionState::DECODE ||
                           exec_state == ExecutionState::EXECUTE_UOPS) &&
                          cur_inst.is_valid;
    std::string ctrl_text = show_inst_name ? cur_inst.name : "DECODER";
    ImVec2 ctrl_lbl_pos = ImGui::CalcTextSize(ctrl_text.c_str());
    ctrl_lbl_pos.x *= zoom;
    ctrl_lbl_pos.y *= zoom;
    draw_list->AddText(
        ImGui::GetFont(), ImGui::GetFontSize() * zoom,
        ImVec2(ctrl_pos.x + (box_size.x - ctrl_lbl_pos.x) / 2.0f,
               ctrl_pos.y + (box_size.y - ctrl_lbl_pos.y) / 2.0f),
        col_text, ctrl_text.c_str());
}

/**
 * @brief Draws the standard ALU block polygon.
 */
inline void DrawALUBlock(ImDrawList *draw_list, float alu_cx, float alu_top_y,
                         ImVec2 alu_poly[7], float alu_path, ImU32 col_box,
                         ImU32 col_text, ImU32 col_inactive, ImU32 col_active,
                         const std::string &active_op, float zoom) {
    draw_list->AddConvexPolyFilled(alu_poly, 7, col_box);
    draw_list->AddPolyline(alu_poly, 7,
                           MixGlowColor(col_inactive, col_active, alu_path),
                           ImDrawFlags_Closed, 2.0f * zoom);
    ImVec2 alu_lbl_pos = ImGui::CalcTextSize(active_op.c_str());
    alu_lbl_pos.x *= zoom;
    alu_lbl_pos.y *= zoom;
    draw_list->AddText(
        ImGui::GetFont(), ImGui::GetFontSize() * zoom,
        ImVec2(alu_cx - alu_lbl_pos.x / 2.0f, alu_top_y + 20.0f * zoom),
        col_text, active_op.c_str());
}

/**
 * @brief Draws the Unified Memory segment including ROM, RAM, and Stack spaces.
 */

inline void DrawMemoryBlock(ImDrawList *draw_list, ImVec2 mem_pos, float mem_w,
                            float mem_h, float data_bus, ImU32 col_box,
                            ImU32 col_text, ImU32 col_inactive,
                            ImU32 col_active, float zoom) {
    draw_list->AddRectFilled(mem_pos,
                             ImVec2(mem_pos.x + mem_w, mem_pos.y + mem_h),
                             col_box, 4.0f * zoom);
    draw_list->AddRect(mem_pos, ImVec2(mem_pos.x + mem_w, mem_pos.y + mem_h),
                       MixGlowColor(col_inactive, col_active, data_bus),
                       4.0f * zoom, 0, 2.0f * zoom);

    draw_list->AddText(
        ImGui::GetFont(), ImGui::GetFontSize() * zoom,
        ImVec2(mem_pos.x + 10.0f * zoom, mem_pos.y + 15.0f * zoom), col_text,
        "Instruction ROM");
    draw_list->AddLine(ImVec2(mem_pos.x, mem_pos.y + (mem_h * 0.33f)),
                       ImVec2(mem_pos.x + mem_w, mem_pos.y + (mem_h * 0.33f)),
                       col_inactive, 1.0f * zoom);
    draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * zoom,
                       ImVec2(mem_pos.x + 10.0f * zoom,
                              mem_pos.y + (mem_h * 0.33f) + 15.0f * zoom),
                       col_text, "Data RAM");
    draw_list->AddLine(ImVec2(mem_pos.x, mem_pos.y + (mem_h * 0.66f)),
                       ImVec2(mem_pos.x + mem_w, mem_pos.y + (mem_h * 0.66f)),
                       col_inactive, 1.0f * zoom);
    draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * zoom,
                       ImVec2(mem_pos.x + 10.0f * zoom,
                              mem_pos.y + (mem_h * 0.66f) + 15.0f * zoom),
                       col_text, "Stack Frame");
}

/**
 * @brief Draws the detailed Register File block, showcasing nested Tree levels
 * and physical bit representations.
 */
inline void DrawRegisterFileBlock(
    ImDrawList *draw_list, ImVec2 reg_pos, float reg_file_w, float reg_file_h,
    CPU &cpu, GUIState &gui, float row_h, float pitch, float box_w,
    const std::vector<int> &reg_depths, std::vector<float> &reg_drawn_y,
    int dest_idx, int src_idx, int addr_idx, ImU32 col_box, ImU32 col_text,
    ImU32 col_inactive, ImU32 col_active, ImU32 col_read_glow,
    ImU32 col_write_glow, ImU32 col_addr_glow, ImU32 col_pc_glow) {
    auto &regs = cpu.get_registers();
    const auto &reg_defs = regs.get_defs();
    size_t reg_count = reg_defs.size();
    auto &h = gui.highlighter;
    auto &exec = cpu.get_executor();

    draw_list->AddRectFilled(
        reg_pos, ImVec2(reg_pos.x + reg_file_w, reg_pos.y + reg_file_h),
        col_box, 4.0f);
    draw_list->AddRect(
        reg_pos, ImVec2(reg_pos.x + reg_file_w, reg_pos.y + reg_file_h),
        MixGlowColor(col_inactive, col_active, h.reg_file_path), 4.0f, 0, 2.0f);

    for (size_t i = 0; i < reg_count; ++i) {
        float r_y = reg_pos.y + i * row_h;
        reg_drawn_y[i] = r_y + row_h / 2.0f;

        ImVec2 r_min(reg_pos.x, r_y);
        ImVec2 r_max(reg_pos.x + reg_file_w, r_y + row_h);

        const auto &def = reg_defs[i];
        int depth = reg_depths[i];

        bool is_pc_active =
            ((def.role == "program_counter" || def.role == "pc") &&
             (exec.get_state() == ExecutionState::FETCH ||
              exec.get_state() == ExecutionState::DECODE));

        bool is_dest_active = false;
        bool is_src_active = false;
        bool is_addr_active = false;

        bool is_exec_or_done =
            (exec.get_state() == ExecutionState::EXECUTE_UOPS ||
             exec.get_state() == ExecutionState::DONE);
        if (is_exec_or_done) {
            int phys_idx = static_cast<int>(i);
            if (phys_idx == dest_idx)
                is_dest_active = true;
            if (phys_idx == src_idx)
                is_src_active = true;
            if (phys_idx == addr_idx)
                is_addr_active = true;
        }

        // Apply distinct color glows for reads, writes, addresses, and PC
        if (is_pc_active && h.reg_file_path > 0.1f) {
            draw_list->AddRectFilled(r_min, r_max, col_pc_glow, 2.0f);
            draw_list->AddRect(
                r_min, r_max,
                IM_COL32(255, 0, 255,
                         static_cast<int>(255.0f * h.reg_file_path)),
                2.0f, 0, 1.5f);
        } else if (is_dest_active && h.reg_file_path > 0.1f) {
            draw_list->AddRectFilled(r_min, r_max, col_write_glow, 2.0f);
            draw_list->AddRect(
                r_min, r_max,
                IM_COL32(255, 120, 0,
                         static_cast<int>(255.0f * h.reg_file_path)),
                2.0f, 0, 1.5f);
        } else if (is_addr_active && h.reg_file_path > 0.1f) {
            draw_list->AddRectFilled(r_min, r_max, col_addr_glow, 2.0f);
            draw_list->AddRect(
                r_min, r_max,
                IM_COL32(0, 180, 230,
                         static_cast<int>(255.0f * h.reg_file_path)),
                2.0f, 0, 1.5f);
        } else if (is_src_active && h.reg_file_path > 0.1f) {
            draw_list->AddRectFilled(r_min, r_max, col_read_glow, 2.0f);
            draw_list->AddRect(
                r_min, r_max,
                IM_COL32(0, 255, 0, static_cast<int>(255.0f * h.reg_file_path)),
                2.0f, 0, 1.5f);
        }

        // Draw horizontal row separators strictly in the text margin area
        if (i > 0) {
            draw_list->AddLine(r_min, ImVec2(reg_pos.x + 115.0f, r_min.y),
                               col_inactive, 1.0f);
        }

        if (depth > 0 && def.parent_index != -1) {
            float parent_cy = reg_drawn_y[def.parent_index];
            float tree_line_x =
                r_min.x + 8.0f * gui.zoom + (depth - 1) * 12.0f * gui.zoom;
            draw_list->AddLine(ImVec2(tree_line_x, parent_cy),
                               ImVec2(tree_line_x, r_min.y + 12.0f * gui.zoom),
                               IM_COL32(100, 100, 100, 150), 1.5f * gui.zoom);
            draw_list->AddLine(ImVec2(tree_line_x, r_min.y + 12.0f * gui.zoom),
                               ImVec2(tree_line_x + 8.0f * gui.zoom,
                                      r_min.y + 12.0f * gui.zoom),
                               IM_COL32(100, 100, 100, 150), 1.5f * gui.zoom);
        }

        uint64_t val = 0;
        try {
            val = regs.read(def.name);
        } catch (...) {
        }
        std::string val_str = FormatHexValue(
            val, def.role == "program_counter" || def.role == "pc" ||
                         def.role == "stack_pointer" || def.role == "sp"
                     ? cpu.get_config().addr_width
                     : cpu.get_config().data_width);

        char cell_buf[64];
        snprintf(cell_buf, sizeof(cell_buf), "%s: %s", def.name.c_str(),
                 val_str.c_str());

        float text_indent = depth * 12.0f * gui.zoom;
        ImVec2 text_pos =
            ImVec2(r_min.x + 8.0f * gui.zoom + text_indent,
                   r_min.y + (gui.show_register_bits
                                  ? (row_h - 14.0f * gui.zoom) / 2.0f
                                  : (row_h - 15.0f * gui.zoom) / 2.0f));
        text_pos.x = std::floor(text_pos.x);
        text_pos.y = std::floor(text_pos.y);

        draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * gui.zoom,
                           text_pos, col_text, cell_buf);

        if (gui.show_register_bits) {
            float bit_area_right = r_max.x - 10.0f;
            int grid_width = GetRegisterGridWidth(def, reg_defs);

            for (int phys_bit = 0; phys_bit < grid_width; ++phys_bit) {
                int bit_idx = -1;
                for (int b = 0; b < def.width; ++b) {
                    if (def.bit_mapping[b] == phys_bit) {
                        bit_idx = b;
                        break;
                    }
                }

                float b_x = bit_area_right - (phys_bit + 1) * pitch;
                ImVec2 b_min(b_x, r_min.y);
                ImVec2 b_max(b_x + box_w, r_min.y + row_h);

                if (bit_idx != -1) {
                    bool is_set = (val >> bit_idx) & 1;
                    ImU32 b_col = is_set ? IM_COL32(0, 255, 0, 255)
                                         : IM_COL32(50, 50, 50, 255);
                    ImU32 b_border = is_set ? IM_COL32(100, 255, 100, 255)
                                            : IM_COL32(30, 30, 30, 255);

                    draw_list->AddRectFilled(b_min, b_max, b_col, 0.0f);
                    draw_list->AddRect(b_min, b_max, b_border, 0.0f, 0, 1.0f);
                } else {
                    ImU32 b_col = IM_COL32(20, 20, 20, 150);
                    ImU32 b_border = IM_COL32(35, 35, 35, 100);
                    draw_list->AddRectFilled(b_min, b_max, b_col, 0.0f);
                    draw_list->AddRect(b_min, b_max, b_border, 0.0f, 0, 1.0f);
                }
            }
        }
    }
}

// ============================================================================
// MODULAR TRACE RENDERING ROUTINES (BUSES/WIRES)
// ============================================================================

/**
 * @brief Draws the Address Bus trace. Only renders when active.
 */
inline void DrawAddressBus(ImDrawList *draw_list, BusHighlighter &h,
                           ImVec2 reg_pos, float reg_file_w, float pc_y,
                           ImVec2 mem_pos, ImU32 col_inactive, ImU32 col_abus,
                           float zoom) {
    if (h.address_bus <= 0.01f)
        return;
    PointList ab_path;
    ab_path.add(ImVec2(reg_pos.x + reg_file_w, pc_y));
    ab_path.add(ImVec2(mem_pos.x, pc_y));

    ab_path.draw(draw_list, MixGlowColor(col_inactive, col_abus, h.address_bus),
                 h.address_bus);
    ab_path.draw_flow(draw_list, h.address_bus, col_abus);
    DrawDataCapsule(draw_list, ab_path.points[0], ab_path.points[1],
                    h.address_val, col_abus, h.address_bus, zoom);
}

/**
 * @brief Draws the Data Bus trace based on the current routing state. Only
 * renders when active.
 */
inline void DrawDataBus(ImDrawList *draw_list, BusHighlighter &h,
                        ImVec2 reg_pos, float reg_file_w, float reg_file_h,
                        ImVec2 ctrl_pos, ImVec2 mem_pos, float db_reg_y,
                        ImU32 col_inactive, ImU32 col_dbus, float zoom) {
    if (h.data_bus <= 0.01f)
        return;
    PointList db_path;

    switch (h.data_bus_route) {
    case DataBusRoute::DECODER_TO_REG:
        db_path.add(
            ImVec2(ctrl_pos.x + 30.0f * zoom, mem_pos.y - 40.0f * zoom));
        db_path.add(
            ImVec2(ctrl_pos.x + 30.0f * zoom, reg_pos.y - 20.0f * zoom));
        db_path.add(ImVec2(reg_pos.x - 40.0f * zoom, reg_pos.y - 20.0f * zoom));
        db_path.add(ImVec2(reg_pos.x - 40.0f * zoom, db_reg_y));
        db_path.add(ImVec2(reg_pos.x, db_reg_y));
        break;

    case DataBusRoute::REG_TO_REG:
        db_path.add(ImVec2(reg_pos.x + reg_file_w, db_reg_y));
        db_path.add(ImVec2(reg_pos.x + reg_file_w + 40.0f * zoom, db_reg_y));
        db_path.add(ImVec2(reg_pos.x + reg_file_w + 40.0f * zoom,
                           reg_pos.y + reg_file_h + 40.0f * zoom));
        db_path.add(ImVec2(reg_pos.x - 40.0f * zoom,
                           reg_pos.y + reg_file_h + 40.0f * zoom));
        db_path.add(ImVec2(reg_pos.x - 40.0f * zoom, db_reg_y));
        db_path.add(ImVec2(reg_pos.x, db_reg_y));
        break;

    case DataBusRoute::REG_TO_MEM:
        db_path.add(ImVec2(reg_pos.x + reg_file_w, db_reg_y));
        db_path.add(ImVec2(reg_pos.x + reg_file_w + 45.0f * zoom, db_reg_y));
        db_path.add(ImVec2(reg_pos.x + reg_file_w + 45.0f * zoom,
                           reg_pos.y + reg_file_h + 60.0f * zoom));
        db_path.add(ImVec2(mem_pos.x - 45.0f * zoom,
                           reg_pos.y + reg_file_h + 60.0f * zoom));
        db_path.add(ImVec2(mem_pos.x - 45.0f * zoom, db_reg_y));
        db_path.add(ImVec2(mem_pos.x, db_reg_y));
        break;

    default: // MEM_TO_REG
        db_path.add(ImVec2(mem_pos.x, db_reg_y));
        db_path.add(ImVec2(ctrl_pos.x + 185.0f * zoom, db_reg_y));
        db_path.add(ImVec2(ctrl_pos.x + 185.0f * zoom,
                           reg_pos.y + reg_file_h + 60.0f * zoom));
        db_path.add(ImVec2(reg_pos.x - 45.0f * zoom,
                           reg_pos.y + reg_file_h + 60.0f * zoom));
        db_path.add(ImVec2(reg_pos.x - 45.0f * zoom, db_reg_y));
        db_path.add(ImVec2(reg_pos.x, db_reg_y));
        break;
    }

    db_path.draw(draw_list, MixGlowColor(col_inactive, col_dbus, h.data_bus),
                 h.data_bus);
    db_path.draw_flow(draw_list, h.data_bus, col_dbus);
    DrawDataCapsule(draw_list, db_path.get_midpoint(), db_path.get_midpoint(),
                    h.data_val, col_dbus, h.data_bus);
}

/**
 * @brief Draws the Instruction Bus trace. Only renders when active.
 */
inline void DrawInstructionBus(ImDrawList *draw_list, BusHighlighter &h,
                               ImVec2 mem_pos, ImVec2 ctrl_pos,
                               ImU32 col_inactive, ImU32 col_irbus,
                               float zoom) {
    if (h.ir_bus <= 0.01f)
        return;
    PointList ir_path;
    ir_path.add(ImVec2(mem_pos.x, mem_pos.y + 40.0f * zoom));
    ir_path.add(ImVec2(ctrl_pos.x + 160.0f * zoom, mem_pos.y + 40.0f * zoom));
    ir_path.add(ImVec2(ctrl_pos.x + 160.0f * zoom, ctrl_pos.y + 25.0f * zoom));
    ir_path.add(ImVec2(ctrl_pos.x + 140.0f * zoom, ctrl_pos.y + 25.0f * zoom));

    ir_path.draw(draw_list, MixGlowColor(col_inactive, col_irbus, h.ir_bus),
                 h.ir_bus);
    ir_path.draw_flow(draw_list, h.ir_bus, col_irbus);
    DrawDataCapsule(draw_list, ir_path.points[1], ir_path.points[2], h.data_val,
                    col_irbus, h.ir_bus);
}

/**
 * @brief Draws the ALU Input wires (A and B side). Only renders when active.
 */

inline void DrawALUInputWires(ImDrawList *draw_list, BusHighlighter &h,
                              ImVec2 reg_pos, float reg_file_w,
                              const std::vector<float> &reg_drawn_y,
                              float alu_cx, float alu_top_y, ImVec2 ctrl_pos,
                              float ctrl_box_h, ImU32 col_inactive,
                              ImU32 col_alubus, float zoom) {
    if (h.alu_path <= 0.01f)
        return;

    PointList alu_a_path;
    if (h.alu_a_is_reg) {
        float start_y = reg_drawn_y[h.alu_a_reg_row];
        alu_a_path.add(ImVec2(reg_pos.x + reg_file_w, start_y));
        alu_a_path.add(ImVec2(alu_cx - 30.0f * zoom, start_y));
        alu_a_path.add(ImVec2(alu_cx - 30.0f * zoom, alu_top_y));
    } else {
        alu_a_path.add(
            ImVec2(ctrl_pos.x + 50.0f * zoom, ctrl_pos.y + ctrl_box_h));
        alu_a_path.add(
            ImVec2(ctrl_pos.x + 50.0f * zoom, alu_top_y - 20.0f * zoom));
        alu_a_path.add(ImVec2(alu_cx - 30.0f * zoom, alu_top_y - 20.0f * zoom));
        alu_a_path.add(ImVec2(alu_cx - 30.0f * zoom, alu_top_y));
    }
    alu_a_path.draw(draw_list,
                    MixGlowColor(col_inactive, col_alubus, h.alu_path),
                    h.alu_path);
    alu_a_path.draw_flow(draw_list, h.alu_path, col_alubus);
    DrawDataCapsule(draw_list, alu_a_path.points[0], alu_a_path.points[1],
                    h.alu_a_val, col_alubus, h.alu_path);

    PointList alu_b_path;
    if (h.alu_b_is_reg) {
        float start_y = reg_drawn_y[h.alu_b_reg_row];
        alu_b_path.add(ImVec2(reg_pos.x + reg_file_w, start_y));
        alu_b_path.add(ImVec2(alu_cx + 30.0f * zoom, start_y));
        alu_b_path.add(ImVec2(alu_cx + 30.0f * zoom, alu_top_y));
    } else {
        alu_b_path.add(
            ImVec2(ctrl_pos.x + 110.0f * zoom, ctrl_pos.y + ctrl_box_h));
        alu_b_path.add(
            ImVec2(ctrl_pos.x + 110.0f * zoom, alu_top_y - 10.0f * zoom));
        alu_b_path.add(ImVec2(alu_cx + 30.0f * zoom, alu_top_y - 10.0f * zoom));
        alu_b_path.add(ImVec2(alu_cx + 30.0f * zoom, alu_top_y));
    }
    alu_b_path.draw(draw_list,
                    MixGlowColor(col_inactive, col_alubus, h.alu_path),
                    h.alu_path);
    alu_b_path.draw_flow(draw_list, h.alu_path, col_alubus);
    DrawDataCapsule(draw_list, alu_b_path.points[0], alu_b_path.points[1],
                    h.alu_b_val, col_alubus, h.alu_path);
}

/**
 * @brief Draws the ALU Output wire trace. Only renders when active.
 */
inline void DrawALUOutputWire(ImDrawList *draw_list, BusHighlighter &h,
                              float alu_cx, ImVec2 alu_poly_out, ImVec2 reg_pos,
                              float reg_file_h, float db_reg_y,
                              ImU32 col_inactive, ImU32 col_alubus,
                              float zoom) {
    if (h.alu_path <= 0.01f)
        return;
    PointList alu_out_path;
    alu_out_path.add(alu_poly_out);
    alu_out_path.add(ImVec2(alu_cx, reg_pos.y + reg_file_h + 20.0f * zoom));
    alu_out_path.add(ImVec2(reg_pos.x - 30.0f * zoom,
                            reg_pos.y + reg_file_h + 20.0f * zoom));
    alu_out_path.add(ImVec2(reg_pos.x - 30.0f * zoom, db_reg_y));
    alu_out_path.add(ImVec2(reg_pos.x, db_reg_y));

    alu_out_path.draw(draw_list,
                      MixGlowColor(col_inactive, col_alubus, h.alu_path),
                      h.alu_path);
    alu_out_path.draw_flow(draw_list, h.alu_path, col_alubus);
    DrawDataCapsule(draw_list, alu_out_path.points[1], alu_out_path.points[2],
                    h.alu_out_val, col_alubus, h.alu_path);
}

/**
 * @brief Draws the MMIO Connection wire. Only renders when active.
 */
inline void DrawMMIOConnection(ImDrawList *draw_list, BusHighlighter &h,
                               ImVec2 mem_pos, ImVec2 mmio_pos,
                               ImU32 col_inactive, ImU32 col_active,
                               float zoom) {
    if (h.mmio_path <= 0.01f)
        return;
    PointList mmio_path;
    mmio_path.add(ImVec2(mem_pos.x + 50.0f * zoom, mem_pos.y));
    mmio_path.add(ImVec2(mmio_pos.x + 50.0f * zoom, mmio_pos.y + 50.0f * zoom));

    mmio_path.draw(draw_list,
                   MixGlowColor(col_inactive, col_active, h.mmio_path),
                   h.mmio_path);
    mmio_path.draw_flow(draw_list, h.mmio_path, col_active);
}

/**
 * @brief Wrapper drawing delegator orchestrating the layout buses.
 */

inline void DrawSignalBuses(ImDrawList *draw_list, CPU &cpu, GUIState &gui,
                            ImVec2 reg_pos, float reg_file_w, float reg_file_h,
                            ImVec2 ctrl_pos, ImVec2 mem_pos, ImVec2 mmio_pos,
                            float alu_cx, float alu_top_y, ImVec2 alu_poly_out,
                            float pc_y, float db_reg_y,
                            const std::vector<float> &reg_drawn_y,
                            ImU32 col_inactive, ImU32 col_abus, ImU32 col_dbus,
                            ImU32 col_irbus, ImU32 col_alubus,
                            ImU32 col_active) {
    auto &h = gui.highlighter;
    float zoom = gui.zoom;
    float ctrl_box_h = 50.0f * zoom;

    // 1. Address Bus (Yellow)
    DrawAddressBus(draw_list, gui.highlighter, reg_pos, reg_file_w, pc_y,
                   mem_pos, col_inactive, col_abus, zoom);

    // 2. Data Bus (Cyan)
    DrawDataBus(draw_list, gui.highlighter, reg_pos, reg_file_w, reg_file_h,
                ctrl_pos, mem_pos, db_reg_y, col_inactive, col_dbus, zoom);

    // 3. Instruction Bus (Magenta)
    DrawInstructionBus(draw_list, gui.highlighter, mem_pos, ctrl_pos,
                       col_inactive, col_irbus, zoom);

    // 4 & 5. ALU Inputs Wires
    DrawALUInputWires(draw_list, h, reg_pos, reg_file_w, reg_drawn_y, alu_cx,
                      alu_top_y, ctrl_pos, ctrl_box_h, col_inactive, col_alubus,
                      zoom);

    // 6. ALU Output Wire
    DrawALUOutputWire(draw_list, h, alu_cx, alu_poly_out, reg_pos, reg_file_h,
                      db_reg_y, col_inactive, col_alubus, zoom);

    // 7. MMIO Connection
    DrawMMIOConnection(draw_list, h, mem_pos, mmio_pos, col_inactive,
                       col_active, zoom);
}

// ============================================================================
// CONTROL ROUTINE DIAGRAMS
// ============================================================================

/**
 * @brief Sub-drawing routine for Control Line Signals (Marching Ants). Only
 * renders active paths.
 */
inline void DrawControlBuses(ImDrawList *draw_list, ImVec2 reg_pos,
                             float reg_file_h, ImVec2 ctrl_pos, ImVec2 box_size,
                             float alu_cx, float alu_top_y, ImVec2 mmio_pos,
                             ImVec2 mem_pos, GUIState &gui, ImU32 col_ctrl,
                             ImU32 col_inactive, ImU32 col_active) {
    auto &h = gui.highlighter;
    float zoom = gui.zoom;
    float origin_y = reg_pos.y - 110.0f * zoom;

    auto draw_marching_dashes = [&](ImVec2 start, ImVec2 end, ImU32 col,
                                    float intensity) {
        ImVec2 dir = ImVec2(end.x - start.x, end.y - start.y);
        float len = sqrt(dir.x * dir.x + dir.y * dir.y);
        if (len == 0.0f)
            return;
        dir.x /= len;
        dir.y /= len;

        float dash_len = 6.0f * zoom;
        float gap_len = 4.0f * zoom;
        float total_period = dash_len + gap_len;

        float speed =
            10.0f * (gui.clock_speed > 10.0f ? 10.0f / gui.clock_speed : 1.0f);
        if (intensity > 0.05f)
            speed *= (1.0f + 2.5f * intensity);

        float time_sec = static_cast<float>(SDL_GetTicks()) / 1000.0f;
        float shift = fmod(time_sec * speed, total_period);

        if (intensity > 0.05f) {
            ImU32 glow_col =
                (col & 0x00FFFFFF) | (static_cast<int>(45 * intensity) << 24);
            draw_list->AddLine(start, end, glow_col,
                               (3.5f + 2.0f * intensity) * zoom);
        }

        float curr_dist = -total_period + shift;
        while (curr_dist < len) {
            float s_dist = std::max(0.0f, curr_dist);
            float e_dist = std::min(len, curr_dist + dash_len);
            if (e_dist > s_dist) {
                ImVec2 p1 =
                    ImVec2(start.x + dir.x * s_dist, start.y + dir.y * s_dist);
                ImVec2 p2 =
                    ImVec2(start.x + dir.x * e_dist, start.y + dir.y * e_dist);
                draw_list->AddLine(p1, p2, col,
                                   (intensity > 0.05f ? 1.8f : 1.1f) * zoom);
            }
            curr_dist += total_period;
        }
    };

    if (h.ctrl_reg_bus > 0.01f) {
        ImU32 col_reg_ctrl =
            MixGlowColor(col_ctrl, IM_COL32(0, 180, 230, 255), h.ctrl_reg_bus);
        col_reg_ctrl = (col_reg_ctrl & 0x00FFFFFF) |
                       (static_cast<int>(100 * (1.0f - h.ctrl_reg_bus) +
                                         255 * h.ctrl_reg_bus)
                        << 24);
        draw_marching_dashes(
            ImVec2(ctrl_pos.x, ctrl_pos.y + 25.0f * zoom),
            ImVec2(reg_pos.x + 70.0f * zoom, ctrl_pos.y + 25.0f * zoom),
            col_reg_ctrl, h.ctrl_reg_bus);
        draw_marching_dashes(
            ImVec2(reg_pos.x + 70.0f * zoom, ctrl_pos.y + 25.0f * zoom),
            ImVec2(reg_pos.x + 70.0f * zoom, reg_pos.y), col_reg_ctrl,
            h.ctrl_reg_bus);
    }

    if (h.ctrl_alu_bus > 0.01f) {
        ImU32 col_alu_ctrl =
            MixGlowColor(col_ctrl, IM_COL32(0, 255, 0, 255), h.ctrl_alu_bus);
        col_alu_ctrl = (col_alu_ctrl & 0x00FFFFFF) |
                       (static_cast<int>(100 * (1.0f - h.ctrl_alu_bus) +
                                         255 * h.ctrl_alu_bus)
                        << 24);
        draw_marching_dashes(ImVec2(alu_cx, ctrl_pos.y + box_size.y),
                             ImVec2(alu_cx, alu_top_y), col_alu_ctrl,
                             h.ctrl_alu_bus);
    }

    float max_right_ctrl = std::max(h.ctrl_mem_bus, h.ctrl_periph_bus);
    if (max_right_ctrl > 0.01f) {
        ImU32 glow_color = (h.ctrl_periph_bus > h.ctrl_mem_bus)
                               ? IM_COL32(230, 180, 0, 255)
                               : IM_COL32(210, 30, 210, 255);
        ImU32 col_right_ctrl =
            MixGlowColor(col_ctrl, glow_color, max_right_ctrl);
        col_right_ctrl = (col_right_ctrl & 0x00FFFFFF) |
                         (static_cast<int>(100 * (1.0f - max_right_ctrl) +
                                           255 * max_right_ctrl)
                          << 24);

        draw_marching_dashes(
            ImVec2(ctrl_pos.x + 130.0f * zoom, ctrl_pos.y),
            ImVec2(ctrl_pos.x + 130.0f * zoom, origin_y + 5.0f * zoom),
            col_right_ctrl, max_right_ctrl);
        draw_marching_dashes(
            ImVec2(ctrl_pos.x + 130.0f * zoom, origin_y + 5.0f * zoom),
            ImVec2(mem_pos.x + 50.0f * zoom, origin_y + 5.0f * zoom),
            col_right_ctrl, max_right_ctrl);

        if (h.ctrl_periph_bus > 0.01f) {
            ImU32 col_periph_ctrl = MixGlowColor(
                col_ctrl, IM_COL32(230, 180, 0, 255), h.ctrl_periph_bus);
            col_periph_ctrl =
                (col_periph_ctrl & 0x00FFFFFF) |
                (static_cast<int>(100 * (1.0f - h.ctrl_periph_bus) +
                                  255 * h.ctrl_periph_bus)
                 << 24);
            draw_marching_dashes(
                ImVec2(mem_pos.x + 50.0f * zoom, origin_y + 5.0f * zoom),
                ImVec2(mem_pos.x + 50.0f * zoom, mmio_pos.y), col_periph_ctrl,
                h.ctrl_periph_bus);
        }

        if (h.ctrl_mem_bus > 0.01f) {
            ImU32 col_mem_ctrl = MixGlowColor(
                col_ctrl, IM_COL32(210, 30, 210, 255), h.ctrl_mem_bus);
            col_mem_ctrl = (col_mem_ctrl & 0x00FFFFFF) |
                           (static_cast<int>(100 * (1.0f - h.ctrl_mem_bus) +
                                             255 * h.ctrl_mem_bus)
                            << 24);
            draw_marching_dashes(
                ImVec2(mem_pos.x + 50.0f * zoom, origin_y + 5.0f * zoom),
                ImVec2(mem_pos.x + 120.0f * zoom, origin_y + 5.0f * zoom),
                col_mem_ctrl, h.ctrl_mem_bus);
            draw_marching_dashes(
                ImVec2(mem_pos.x + 120.0f * zoom, origin_y + 5.0f * zoom),
                ImVec2(mem_pos.x + 120.0f * zoom, mem_pos.y + 1.0f * zoom),
                col_mem_ctrl, h.ctrl_mem_bus);
        }
    }
}

// ============================================================================
// SYSTEM SCHEMATIC WINDOW ORCHESTRATOR
// ============================================================================

inline void UI_SystemSchematic(CPU &cpu, GUIState &gui) {
    ImGui::Begin("System Schematic", nullptr,
                 ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);

    BusHighlighter &h = gui.highlighter;
    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin.y += 30.0f;

    if (ImGui::IsWindowHovered()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            gui.pan.x += delta.x;
            gui.pan.y += delta.y;
        }
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            float old_zoom = gui.zoom;
            gui.zoom += wheel * 0.05f;
            if (gui.zoom < 0.3f)
                gui.zoom = 0.3f;
            if (gui.zoom > 3.0f)
                gui.zoom = 3.0f;
        }
    }

    auto &exec = cpu.get_executor();
    auto &regs = cpu.get_registers();
    auto &config = cpu.get_config();
    const auto &reg_defs = regs.get_defs();
    size_t reg_count = reg_defs.size();

    // 1. Calculate register sub-hierarchy depths and grid parameters
    std::vector<int> reg_depths(reg_count, 0);
    int max_grid_width = 8;
    for (size_t i = 0; i < reg_count; ++i) {
        int gw = GetRegisterGridWidth(reg_defs[i], reg_defs);
        if (gw > max_grid_width)
            max_grid_width = gw;
        int parent = reg_defs[i].parent_index;
        int depth = 0;
        while (parent != -1) {
            depth++;
            parent = reg_defs[parent].parent_index;
        }
        reg_depths[i] = gui.show_subreg_hierarchy ? depth : 0;
    }

    // Apply Zoom to dimensions
    float zoom = gui.zoom;
    float box_w =
        (max_grid_width > 32 ? 8.0f : (max_grid_width > 16 ? 12.0f : 16.0f)) *
        zoom;
    float pitch = box_w;

    float reg_file_w =
        (gui.show_register_bits
             ? (110.0f * zoom) + max_grid_width * pitch + (15.0f * zoom)
             : 140.0f * zoom);
    float row_h = (gui.show_register_bits ? 26.0f : 35.0f) * zoom;
    float reg_file_h = reg_count * row_h;
    ImVec2 box_size = ImVec2(140.0f * zoom, 50.0f * zoom);

    auto Trans = [&](ImVec2 p) -> ImVec2 {
        return ImVec2((p.x - origin.x) * zoom + origin.x + gui.pan.x,
                      (p.y - origin.y) * zoom + origin.y + gui.pan.y);
    };

    // 2. Base coordinates in "world space"
    float col1_x = origin.x + 40.0f;
    float col2_x = col1_x + (reg_file_w / zoom) + 65.0f;
    float col3_x = col2_x + 140.0f + 65.0f;

    // Apply Transformation immediately, keeping base items in screen space
    ImVec2 reg_pos = Trans(ImVec2(col1_x, origin.y + 110.0f));
    ImVec2 ctrl_pos = Trans(ImVec2(col2_x, origin.y + 20.0f));
    ImVec2 mem_pos = Trans(ImVec2(col3_x, origin.y + 110.0f));
    ImVec2 mmio_pos = Trans(ImVec2(col3_x, origin.y + 20.0f));

    // Calculate scaled ALU geometry
    float alu_cx = ctrl_pos.x + 70.0f * zoom;
    float alu_top_y = reg_pos.y + reg_file_h / 2.0f - 30.0f * zoom;
    if (alu_top_y < ctrl_pos.y + box_size.y + 40.0f * zoom) {
        alu_top_y = ctrl_pos.y + box_size.y + 40.0f * zoom;
    }

    ImVec2 alu_poly[7] = {
        ImVec2(alu_cx - 50.0f * zoom, alu_top_y),
        ImVec2(alu_cx - 10.0f * zoom, alu_top_y),
        ImVec2(alu_cx, alu_top_y + 15.0f * zoom),
        ImVec2(alu_cx + 10.0f * zoom, alu_top_y),
        ImVec2(alu_cx + 50.0f * zoom, alu_top_y),
        ImVec2(alu_cx + 25.0f * zoom, alu_top_y + 60.0f * zoom),
        ImVec2(alu_cx - 25.0f * zoom, alu_top_y + 60.0f * zoom)};
    ImVec2 alu_poly_out = ImVec2(alu_cx, alu_top_y + 60.0f * zoom);

    // Color definitions
    ImU32 col_inactive = IM_COL32(60, 60, 60, 255);
    ImU32 col_active = IM_COL32(0, 255, 0, 255);

    ImU32 col_abus = IM_COL32(230, 180, 0, 255);   // Default Yellow
    ImU32 col_dbus = IM_COL32(0, 180, 230, 255);   // Default Cyan
    ImU32 col_alubus = IM_COL32(30, 210, 30, 255); // Default Green
    ImU32 col_irbus = IM_COL32(210, 30, 210, 255); // Default Magenta

    if (gui.active_theme == UITheme::RETRO_AMBER) {
        col_abus = IM_COL32(250, 100, 0, 255);   // Deep Orange
        col_dbus = IM_COL32(255, 160, 0, 255);   // Dark Gold/Amber
        col_alubus = IM_COL32(200, 80, 0, 255);  // Burnt Orange
        col_irbus = IM_COL32(255, 200, 50, 255); // Bright Yellow
        col_active = IM_COL32(255, 160, 0, 255);
    } else if (gui.active_theme == UITheme::GREEN_PHOSPHOR) {
        col_abus = IM_COL32(0, 180, 50, 255);     // Dark Green
        col_dbus = IM_COL32(0, 255, 100, 255);    // Phosphor Lime
        col_alubus = IM_COL32(0, 130, 30, 255);   // Deep emerald
        col_irbus = IM_COL32(100, 255, 100, 255); // Light Mint Green
        col_active = IM_COL32(0, 255, 100, 255);
    }
    ImU32 col_box = IM_COL32(30, 30, 30, 255);
    ImU32 col_text = IM_COL32(220, 220, 220, 255);
    ImU32 col_ctrl = IM_COL32(230, 230, 230, 100);

    // 3. Resolve current registers indexes associated with micro-op action
    // details
    int dest_idx = -1;
    int src_idx = -1;
    int addr_idx = -1;
    const auto &cur_inst = exec.get_current_inst();
    if (cur_inst.is_valid) {
        if (cur_inst.regs.count("dest"))
            dest_idx = cur_inst.regs.at("dest");
        if (cur_inst.regs.count("src"))
            src_idx = cur_inst.regs.at("src");
        if (cur_inst.regs.count("addr_reg"))
            addr_idx = cur_inst.regs.at("addr_reg");
    }

    float pulse = 0.4f + 0.3f * sin(static_cast<float>(SDL_GetTicks() % 800) /
                                    800.0f * 2.0f * 3.1415926535f);
    ImU32 col_read_glow =
        IM_COL32(0, 180, 0, static_cast<int>(255 * pulse * h.reg_file_path));
    ImU32 col_write_glow =
        IM_COL32(230, 100, 0, static_cast<int>(255 * pulse * h.reg_file_path));
    ImU32 col_addr_glow =
        IM_COL32(0, 150, 200, static_cast<int>(255 * pulse * h.reg_file_path));
    ImU32 col_pc_glow =
        IM_COL32(180, 0, 180, static_cast<int>(255 * pulse * h.reg_file_path));

    std::vector<float> reg_drawn_y(reg_count, 0.0f);

    std::string current_alu_op_name = "ALU";
    if (exec.get_state() == ExecutionState::EXECUTE_UOPS) {
        const auto &current_inst = exec.get_current_inst();
        for (const auto &inst : config.instructions) {
            if (inst.opcode == current_inst.opcode) {
                size_t idx = exec.get_current_uop_index();
                if (idx < inst.microcode.size()) {
                    const auto &uop = inst.microcode[idx];
                    if (uop.action == "alu" && uop.args.count("op")) {
                        current_alu_op_name = uop.args.at("op");
                    }
                }
                break;
            }
        }
    }

    // 4. Draw Schematic Block Elements
    DrawRegisterFileBlock(draw_list, reg_pos, reg_file_w, reg_file_h, cpu, gui,
                          row_h, pitch, box_w, reg_depths, reg_drawn_y,
                          dest_idx, src_idx, addr_idx, col_box, col_text,
                          col_inactive, col_active, col_read_glow,
                          col_write_glow, col_addr_glow, col_pc_glow);

    DrawDecoderBlock(draw_list, ctrl_pos, box_size, cur_inst, exec.get_state(),
                     h.ir_bus, col_box, col_text, col_inactive, col_active,
                     zoom);

    DrawALUBlock(draw_list, alu_cx, alu_top_y, alu_poly, h.alu_path, col_box,
                 col_text, col_inactive, col_active, current_alu_op_name, zoom);

    float mem_w = 160.0f * gui.zoom;
    float mem_h = reg_file_h > 240.0f ? reg_file_h : 240.0f;
    DrawMemoryBlock(draw_list, mem_pos, mem_w, mem_h, h.data_bus, col_box,
                    col_text, col_inactive, col_active, zoom);

    DrawBlock(draw_list, mmio_pos, box_size, "Peripherals", col_inactive,
              col_active, h.mmio_path, col_text, col_box, zoom);

    // 5. Draw Dynamic Signal Bus Wire paths (Only active buses draw
    // values/lines)
    int pc_reg_idx = regs.find_by_role("program_counter");
    if (pc_reg_idx == -1)
        pc_reg_idx = regs.find_by_role("pc");
    float pc_y = (pc_reg_idx != -1 && pc_reg_idx < reg_drawn_y.size())
                     ? reg_drawn_y[pc_reg_idx]
                     : reg_pos.y + 20.0f;

    float db_reg_y = reg_pos.y + 140.0f;
    int active_reg_idx = -1;
    if (dest_idx != -1)
        active_reg_idx = dest_idx;
    else if (src_idx != -1)
        active_reg_idx = src_idx;
    else if (addr_idx != -1)
        active_reg_idx = addr_idx;

    if (active_reg_idx != -1 && active_reg_idx < reg_drawn_y.size()) {
        db_reg_y = reg_drawn_y[active_reg_idx];
    }

    DrawSignalBuses(draw_list, cpu, gui, reg_pos, reg_file_w, reg_file_h,
                    ctrl_pos, mem_pos, mmio_pos, alu_cx, alu_top_y,
                    alu_poly_out, pc_y, db_reg_y, reg_drawn_y, col_inactive,
                    col_abus, col_dbus, col_irbus, col_alubus, col_active);

    DrawControlBuses(draw_list, reg_pos, reg_file_h, ctrl_pos, box_size, alu_cx,
                     alu_top_y, mmio_pos, mem_pos, gui, col_ctrl, col_inactive,
                     col_active);

    ImGui::SetCursorScreenPos(reg_pos);
    if (ImGui::InvisibleButton("##RegFileFocusBtn",
                               ImVec2(reg_file_w, reg_file_h))) {
        ImGui::SetWindowFocus("Register File");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Click to focus Register File panel");

    ImGui::SetCursorScreenPos(mem_pos);
    if (ImGui::InvisibleButton("##MemoryFocusBtn", ImVec2(mem_w, mem_h))) {
        ImGui::SetWindowFocus("Memory Explorer");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Click to focus Memory Explorer panel");

    ImGui::SetCursorScreenPos(ctrl_pos);
    if (ImGui::InvisibleButton("##DecoderFocusBtn", box_size)) {
        ImGui::SetWindowFocus("Micro-op Pipeline");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Click to focus Micro-op Pipeline panel");

    ImGui::End();
}
