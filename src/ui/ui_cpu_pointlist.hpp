#pragma once
#include "ui_common.hpp"
#include <vector>

struct PointList {
    std::vector<ImVec2> points;

    void add(ImVec2 p) { points.push_back(p); }

    void draw(ImDrawList *draw_list, ImU32 base_color, float intensity) const {
        if (points.size() < 2)
            return;

        // Backdrop separator line to distinguish intersecting buses
        ImU32 bg_color = IM_COL32(12, 14, 18, 220);
        for (size_t i = 0; i < points.size() - 1; ++i) {
            draw_list->AddLine(points[i], points[i + 1], bg_color, 8.0f);
        }

        // Ambient glow when signal is actively propagating
        if (intensity > 0.05f) {
            ImU32 aura_color = (base_color & 0x00FFFFFF) |
                               (static_cast<int>(60 * intensity) << 24);
            float aura_width = 8.0f + 6.0f * intensity;
            for (size_t i = 0; i < points.size() - 1; ++i) {
                draw_list->AddLine(points[i], points[i + 1], aura_color,
                                   aura_width);
            }
        }

        // Primary bus core
        ImU32 core_color = base_color;
        float core_width = 2.0f + 1.5f * intensity;
        for (size_t i = 0; i < points.size() - 1; ++i) {
            draw_list->AddLine(points[i], points[i + 1], core_color,
                               core_width);
        }

        // Filament centerpiece for active data transfers
        if (intensity > 0.2f) {
            ImU32 filament_color =
                IM_COL32(255, 255, 255, static_cast<int>(180 * intensity));
            for (size_t i = 0; i < points.size() - 1; ++i) {
                draw_list->AddLine(points[i], points[i + 1], filament_color,
                                   1.0f);
            }
        }
    }

    void draw_flow(ImDrawList *draw_list, float intensity,
                   ImU32 flow_color = IM_COL32(255, 255, 255, 255)) const {
        if (intensity <= 0.05f || points.size() < 2)
            return;

        float total_len = 0.0f;
        std::vector<float> segment_lens;
        for (size_t i = 0; i < points.size() - 1; ++i) {
            ImVec2 d = ImVec2(points[i + 1].x - points[i].x,
                              points[i + 1].y - points[i].y);
            float l = sqrt(d.x * d.x + d.y * d.y);
            segment_lens.push_back(l);
            total_len += l;
        }

        float t = static_cast<float>(SDL_GetTicks() % 1000) / 1000.0f;

        auto get_point_at = [&](float fraction) -> ImVec2 {
            if (fraction <= 0.0f)
                return points.front();
            if (fraction >= 1.0f)
                return points.back();
            float target_dist = total_len * fraction;
            float accumulated_dist = 0.0f;
            for (size_t i = 0; i < segment_lens.size(); ++i) {
                if (target_dist <= accumulated_dist + segment_lens[i]) {
                    float seg_factor =
                        (target_dist - accumulated_dist) / segment_lens[i];
                    return ImVec2(
                        points[i].x +
                            (points[i + 1].x - points[i].x) * seg_factor,
                        points[i].y +
                            (points[i + 1].y - points[i].y) * seg_factor);
                }
                accumulated_dist += segment_lens[i];
            }
            return points.back();
        };

        for (int p_idx = 0; p_idx < 3; ++p_idx) {
            float offset = t + static_cast<float>(p_idx) / 3.0f;
            if (offset > 1.0f)
                offset -= 1.0f;

            for (int tail = 0; tail < 5; ++tail) {
                float tail_offset =
                    offset - (static_cast<float>(tail) * 0.015f);
                if (tail_offset < 0.0f)
                    tail_offset += 1.0f;

                ImVec2 p = get_point_at(tail_offset);
                float size = 4.0f - tail * 0.6f;
                if (size < 1.0f)
                    size = 1.0f;

                int alpha =
                    static_cast<int>(255 * intensity * (1.0f - tail * 0.2f));
                if (alpha < 0)
                    alpha = 0;

                ImU32 p_col;
                if (tail == 0) {
                    p_col = IM_COL32(255, 255, 255, alpha);
                } else {
                    p_col = (flow_color & 0x00FFFFFF) | (alpha << 24);
                }

                draw_list->AddCircleFilled(p, size, p_col);
            }
        }
    }

    ImVec2 get_midpoint() const {
        if (points.empty())
            return ImVec2(0, 0);
        if (points.size() == 2)
            return ImVec2((points[0].x + points[1].x) / 2.0f,
                          (points[0].y + points[1].y) / 2.0f);

        size_t mid_idx = points.size() / 2;
        return ImVec2((points[mid_idx - 1].x + points[mid_idx].x) / 2.0f,
                      (points[mid_idx - 1].y + points[mid_idx].y) / 2.0f);
    }
};
