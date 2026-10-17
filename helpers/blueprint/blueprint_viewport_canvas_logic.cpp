#include "blueprint_viewport_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BlueprintViewport::Internal {
void ViewportCanvas::buildLogicEdges() {
    if (state.logicManipulation) {
        std::unordered_map<std::int64_t, int> partByControllerId;
        std::unordered_map<std::int64_t, std::unordered_set<std::int64_t>> targetsByControllerId;
        for (int index = 0; index < (int)blocks.size(); ++index) {
            const auto id = actions.controllerIdForPart(index);
            if (id < 0)
                continue;
            partByControllerId.emplace(id, index);
            json* controller = actions.controllerForPart(index);
            if (!controller || !controller->contains("controllers") ||
                !(*controller)["controllers"].is_array())
                continue;
            for (const auto& connection : (*controller)["controllers"])
                if (connection.is_object() && connection.contains("id") &&
                    connection["id"].is_number_integer())
                    try {
                        targetsByControllerId[id].insert(connection["id"].get<std::int64_t>());
                    } catch (...) {
                    }
        }
        std::vector<ImVec2> partScreen(blocks.size());
        for (size_t index = 0; index < blocks.size(); ++index) {
            float depth;
            partScreen[index] = screen(blockCenter(blocks[index]), depth);
        }
        for (int source = 0; source < (int)blocks.size(); ++source) {
            const std::int64_t sourceId = actions.controllerIdForPart(source);
            auto outgoing = targetsByControllerId.find(sourceId);
            if (outgoing == targetsByControllerId.end())
                continue;
            for (std::int64_t targetId : outgoing->second) {
                auto targetPart = partByControllerId.find(targetId);
                if (targetPart == partByControllerId.end())
                    continue;
                const int target = targetPart->second;
                if (!state.showAllLogicConnections && state.logicSource >= 0 &&
                    source != state.logicSource && target != state.logicSource)
                    continue;
                if (target == source) {
                    const ImVec2 center = partScreen[source];
                    const ImVec2 start(center.x + 8.f, center.y - 5.f);
                    const ImVec2 controlA(center.x + 43.f, center.y - 47.f);
                    const ImVec2 controlB(center.x - 43.f, center.y - 47.f);
                    const ImVec2 end(center.x - 8.f, center.y - 5.f);
                    std::vector<ImVec2> loop;
                    loop.reserve(21);
                    for (int step = 0; step <= 20; ++step) {
                        const float t = step / 20.f, oneMinusT = 1.f - t;
                        const float a = oneMinusT * oneMinusT * oneMinusT;
                        const float b = 3.f * oneMinusT * oneMinusT * t;
                        const float c = 3.f * oneMinusT * t * t;
                        const float d = t * t * t;
                        loop.emplace_back(a * start.x + b * controlA.x + c * controlB.x + d * end.x,
                                          a * start.y + b * controlA.y + c * controlB.y +
                                              d * end.y);
                    }
                    logicEdges.push_back({source, target, targetId, std::move(loop)});
                    continue;
                }
                float dx = partScreen[target].x - partScreen[source].x,
                      dy = partScreen[target].y - partScreen[source].y;
                const float length = std::sqrt(dx * dx + dy * dy);
                if (length < 18.f)
                    continue;
                const float ux = dx / length, uy = dy / length;
                auto reverse = targetsByControllerId.find(targetId);
                const bool reciprocal =
                    reverse != targetsByControllerId.end() && reverse->second.contains(sourceId);
                const float side = reciprocal ? 4.f : 0.f;
                const float ox = -uy * side, oy = ux * side;
                logicEdges.push_back(
                    {source,
                     target,
                     targetId,
                     {{partScreen[source].x + ux * 8.f + ox, partScreen[source].y + uy * 8.f + oy},
                      {partScreen[target].x - ux * 15.f + ox,
                       partScreen[target].y - uy * 15.f + oy}}});
            }
        }
    }
}

void ViewportCanvas::drawLogicConnections() {
    if (state.logicManipulation) {
        dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
        for (const auto& edge : logicEdges) {
            if (edge.path.size() < 2)
                continue;
            const ImVec2 from = edge.path.front(), to = edge.path.back();
            const size_t tangentOffset = std::min<size_t>(4, edge.path.size() - 1);
            const ImVec2 previous = edge.path[edge.path.size() - 1 - tangentOffset];
            const float dx = to.x - previous.x, dy = to.y - previous.y,
                        length = std::sqrt(dx * dx + dy * dy);
            if (length < 8.f)
                continue;
            const float ux = dx / length, uy = dy / length, head = std::min(11.f, length * .38f),
                        wing = 5.5f;
            const ImU32 color = edge.source == state.logicSource ? IM_COL32(255, 190, 76, 255)
                                                                 : IM_COL32(83, 222, 235, 245);
            const ImVec2 left(to.x - ux * head - uy * wing, to.y - uy * head + ux * wing);
            const ImVec2 right(to.x - ux * head + uy * wing, to.y - uy * head - ux * wing);
            for (size_t point = 1; point < edge.path.size(); ++point) {
                dl->AddLine(edge.path[point - 1], edge.path[point], IM_COL32(10, 16, 22, 235), 7.f);
                dl->AddLine(edge.path[point - 1], edge.path[point], color, 3.f);
            }
            dl->AddLine(to, left, IM_COL32(10, 16, 22, 245), 7.f);
            dl->AddLine(to, left, color, 3.f);
            dl->AddLine(to, right, IM_COL32(10, 16, 22, 245), 7.f);
            dl->AddLine(to, right, color, 3.f);
            dl->AddCircleFilled(from, 5.f, IM_COL32(10, 16, 22, 245));
            dl->AddCircleFilled(from, 2.8f, color);
        }
        if (state.logicSource >= 0 && state.logicSource < (int)blocks.size()) {
            float sourceDepth;
            const ImVec2 sourcePoint = screen(blockCenter(blocks[state.logicSource]), sourceDepth);
            dl->AddCircle(sourcePoint, 12.f, IM_COL32(18, 24, 30, 255), 20, 4.f);
            dl->AddCircle(sourcePoint, 10.f, IM_COL32(255, 190, 76, 255), 20, 2.f);
            dl->AddCircleFilled(sourcePoint, 3.f, IM_COL32(255, 220, 145, 255));
            if (hovered) {
                const float dx = mouse.x - sourcePoint.x, dy = mouse.y - sourcePoint.y,
                            length = std::sqrt(dx * dx + dy * dy);
                if (length > 18.f) {
                    const float ux = dx / length, uy = dy / length;
                    for (float at = 14.f; at < length - 8.f; at += 12.f)
                        dl->AddLine(ImVec2(sourcePoint.x + ux * at, sourcePoint.y + uy * at),
                                    ImVec2(sourcePoint.x + ux * std::min(at + 6.f, length - 8.f),
                                           sourcePoint.y + uy * std::min(at + 6.f, length - 8.f)),
                                    IM_COL32(255, 190, 76, 190),
                                    2.f);
                    dl->AddCircleFilled(mouse, 4.f, IM_COL32(255, 190, 76, 230));
                }
            }
        }
        dl->PopClipRect();
        std::string guidance =
            state.logicSource >= 0
                ? "LOGIC SOURCE: part " + std::to_string(state.logicSource + 1) +
                      "  ·  click targets  ·  Shift-click changes source  ·  right-click "
                      "clears selection  ·  arrows remove  ·  Esc exits"
                : "LOGIC MODE  ·  click source, then targets  ·  click source to "
                  "wire into itself  ·  right-click clears selection  ·  arrows remove  ·  Esc "
                  "exits";
        const float hintWrapWidth = std::max(80.f, size.x - 36.f);
        const ImVec2 labelSize =
            ImGui::CalcTextSize(guidance.c_str(), nullptr, false, hintWrapWidth);
        const ImVec2 labelPos(origin.x + 12.f, origin.y + 12.f);
        logicHintMin = labelPos;
        logicHintMax = {std::min(origin.x + size.x - 8.f, labelPos.x + labelSize.x + 18.f),
                        labelPos.y + labelSize.y + 16.f};
        dl->AddRectFilled(logicHintMin, logicHintMax, IM_COL32(15, 28, 34, 238), 5.f);
        dl->AddText(ImGui::GetFont(),
                    ImGui::GetFontSize(),
                    ImVec2(labelPos.x + 9.f, labelPos.y + 8.f),
                    IM_COL32(140, 241, 245, 255),
                    guidance.c_str(),
                    nullptr,
                    hintWrapWidth);
    }
}

} // namespace BlueprintViewport::Internal
