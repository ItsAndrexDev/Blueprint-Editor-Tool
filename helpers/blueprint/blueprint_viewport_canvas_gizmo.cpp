#include "blueprint_viewport_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace BlueprintViewport::Internal {
ViewportCanvas::AxisHandle ViewportCanvas::axisHandle(int signedAxis) const {
    AxisHandle handle;
    float centerDepth;
    float axisDepth;
    handle.center = screen(selectedCenter, centerDepth);
    const ImVec2 unitEnd = screen(selectedCenter + axis(signedAxis), axisDepth);
    const float dx = unitEnd.x - handle.center.x;
    const float dy = unitEnd.y - handle.center.y;
    const float projectedPixels = std::sqrt(dx * dx + dy * dy);

    if (projectedPixels > 1e-3f)
        handle.screenAxis = {dx / projectedPixels, dy / projectedPixels};
    else {
        // Keep the handle visible and usable when its world axis faces the camera.
        const int axisNumber = std::abs(signedAxis);
        handle.screenAxis = axisNumber == 1   ? ImVec2(1.f, 0.f)
                            : axisNumber == 2 ? ImVec2(0.f, -1.f)
                                              : ImVec2(.707f, -.707f);
        if (signedAxis < 0) {
            handle.screenAxis.x = -handle.screenAxis.x;
            handle.screenAxis.y = -handle.screenAxis.y;
        }
    }

    handle.end = ImVec2(handle.center.x + handle.screenAxis.x * 38.f,
                        handle.center.y + handle.screenAxis.y * 38.f);
    handle.worldPerPixel = std::min(.5f, 1.f / std::max(.02f, projectedPixels));
    return handle;
}

int ViewportCanvas::axisAtMouse(ImVec2 point) const {
    if (state.logicManipulation || !hasSelectedOutline || state.selection < 0 ||
        state.selection >= static_cast<int>(blocks.size()) || blocks[state.selection].isJoint)
        return 0;

    int hitAxis = 0;
    float nearest = 12.f;
    for (int axisNumber = 1; axisNumber <= 3; ++axisNumber) {
        for (int sign : {-1, 1}) {
            const int signedAxis = axisNumber * sign;
            const AxisHandle handle = axisHandle(signedAxis);
            const ImVec2 center = handle.center;
            const ImVec2 end = handle.end;
            const float vx = end.x - center.x;
            const float vy = end.y - center.y;
            const float lengthSquared = vx * vx + vy * vy;

            if (lengthSquared < 16.f)
                continue;

            const float t = std::clamp(
                ((point.x - center.x) * vx + (point.y - center.y) * vy) / lengthSquared, 0.f, 1.f);
            const float dx = point.x - (center.x + t * vx);
            const float dy = point.y - (center.y + t * vy);
            const float distance = std::sqrt(dx * dx + dy * dy);

            if (t > .08f && distance < nearest) {
                nearest = distance;
                hitAxis = signedAxis;
            }
        }
    }

    return hitAxis;
}

void ViewportCanvas::drawSelectionGizmo() {
    const int hoveredAxis = hovered ? axisAtMouse(mouse) : 0;
    if (hoveredAxis && !state.dragAxis)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (hasSelectedOutline) {
        static constexpr int edges[][2] = {{0, 1},
                                           {1, 2},
                                           {2, 3},
                                           {3, 0},
                                           {4, 5},
                                           {5, 6},
                                           {6, 7},
                                           {7, 4},
                                           {0, 4},
                                           {1, 5},
                                           {2, 6},
                                           {3, 7}};
        for (const auto& selectionOutline : selectedOutlines) {
            const bool active = selectionOutline.first == state.selection;
            const ImU32 outlineColor =
                active ? IM_COL32(102, 210, 255, 255) : IM_COL32(255, 205, 96, 255);
            for (const auto& edge : edges)
                dl->AddLine(selectionOutline.second[edge[0]],
                            selectionOutline.second[edge[1]],
                            outlineColor,
                            active ? 2.f : 1.7f);
        }

        if (!state.logicManipulation) {
            const ImU32 colors[3] = {IM_COL32(255, 112, 112, 255),
                                     IM_COL32(120, 235, 155, 255),
                                     IM_COL32(115, 175, 255, 255)};
            const char* positive[3] = {"+X", "+Y", "+Z"};
            const char* negative[3] = {"-X", "-Y", "-Z"};
            auto drawArrow = [&](ImVec2 from,
                                 ImVec2 to,
                                 ImU32 color,
                                 const char* label,
                                 int signedAxis) {
                const float dx = to.x - from.x, dy = to.y - from.y,
                            len = std::sqrt(dx * dx + dy * dy);
                if (len < 8.f)
                    return;
                const bool active = state.dragAxis == signedAxis, hot = hoveredAxis == signedAxis;
                const ImU32 visible = active ? IM_COL32(255, 245, 115, 255)
                                      : hot  ? IM_COL32(255, 255, 255, 255)
                                             : color;
                const float ux = dx / len, uy = dy / len, head = std::min(9.f, len * .34f),
                            wing = 4.5f;
                dl->AddLine(from, to, IM_COL32(10, 13, 18, 230), active ? 7.f : 5.f);
                dl->AddLine(from, to, visible, active ? 4.f : hot ? 3.5f : 2.5f);
                dl->AddLine(to,
                            ImVec2(to.x - ux * head - uy * wing, to.y - uy * head + ux * wing),
                            IM_COL32(10, 13, 18, 230),
                            active ? 7.f : 5.f);
                dl->AddLine(to,
                            ImVec2(to.x - ux * head - uy * wing, to.y - uy * head + ux * wing),
                            visible,
                            active ? 4.f
                            : hot  ? 3.5f
                                   : 2.5f);
                dl->AddLine(to,
                            ImVec2(to.x - ux * head + uy * wing, to.y - uy * head - ux * wing),
                            IM_COL32(10, 13, 18, 230),
                            active ? 7.f : 5.f);
                dl->AddLine(to,
                            ImVec2(to.x - ux * head + uy * wing, to.y - uy * head - ux * wing),
                            visible,
                            active ? 4.f
                            : hot  ? 3.5f
                                   : 2.5f);
                const ImVec2 labelPos(to.x + ux * 7.f - 10.f, to.y + uy * 7.f - 8.f);
                dl->AddText(ImVec2(labelPos.x + 1, labelPos.y + 1), IM_COL32(0, 0, 0, 255), label);
                dl->AddText(labelPos, visible, label);
            };
            for (int i = 0; i < 3; i++) {
                const AxisHandle positiveHandle = axisHandle(i + 1),
                                 negativeHandle = axisHandle(-(i + 1));
                drawArrow(positiveHandle.center, positiveHandle.end, colors[i], positive[i], i + 1);
                drawArrow(
                    negativeHandle.center, negativeHandle.end, colors[i], negative[i], -(i + 1));
            }
            if (state.dragAxis) {
                const int a = std::abs(state.dragAxis) - 1;
                const char* names[3] = {"X", "Y", "Z"};
                char indicator[64];
                snprintf(indicator,
                         sizeof(indicator),
                         "MOVING  %c%s  ·  drag along this axis",
                         state.dragAxis > 0 ? '+' : '-',
                         names[a]);
                dl->AddRectFilled(ImVec2(origin.x + 12, origin.y + 12),
                                  ImVec2(origin.x + 230, origin.y + 40),
                                  IM_COL32(16, 20, 26, 235),
                                  5.f);
                dl->AddText(
                    ImVec2(origin.x + 21, origin.y + 19), IM_COL32(255, 245, 115, 255), indicator);
            }
        }
    }
}

} // namespace BlueprintViewport::Internal
