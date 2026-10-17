#include "blueprint_viewport_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace BlueprintViewport::Internal {
void ViewportCanvas::handleInput() {
    const bool clickedLogicHint = state.logicManipulation && mouse.x >= logicHintMin.x &&
                                  mouse.x <= logicHintMax.x && mouse.y >= logicHintMin.y &&
                                  mouse.y <= logicHintMax.y;
    if (hovered && state.logicManipulation)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (hovered && !clickedLogicHint && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const int hitAxis = axisAtMouse(mouse);
        int hitLogicEdge = -1;
        float nearestLogicEdge = 5.5f;
        if (state.logicManipulation)
            for (size_t edgeIndex = 0; edgeIndex < logicEdges.size(); ++edgeIndex) {
                const auto& edge = logicEdges[edgeIndex];
                for (size_t point = 1; point < edge.path.size(); ++point) {
                    const ImVec2 from = edge.path[point - 1], to = edge.path[point];
                    const float vx = to.x - from.x, vy = to.y - from.y,
                                lengthSquared = vx * vx + vy * vy;
                    if (lengthSquared < 1.f)
                        continue;
                    const float t = std::clamp(((mouse.x - from.x) * vx + (mouse.y - from.y) * vy) /
                                                   lengthSquared,
                                               0.f,
                                               1.f);
                    const float dx = mouse.x - (from.x + vx * t), dy = mouse.y - (from.y + vy * t),
                                distance = std::sqrt(dx * dx + dy * dy);
                    if (distance < nearestLogicEdge) {
                        nearestLogicEdge = distance;
                        hitLogicEdge = (int)edgeIndex;
                    }
                }
            }
        int picked = -1;
        float closestDepth = -std::numeric_limits<float>::max();
        if (hitAxis)
            picked = state.selection;
        else
            for (const auto& t : triangles) {
                const auto& a = t.p[0];
                const auto& b = t.p[1];
                const auto& c = t.p[2];
                const float denom = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
                if (std::abs(denom) < 1e-6f)
                    continue;
                const float wa =
                    ((b.y - c.y) * (mouse.x - c.x) + (c.x - b.x) * (mouse.y - c.y)) / denom;
                const float wb =
                    ((c.y - a.y) * (mouse.x - c.x) + (a.x - c.x) * (mouse.y - c.y)) / denom;
                const float wc = 1.f - wa - wb;
                if (wa >= -1e-4f && wb >= -1e-4f && wc >= -1e-4f) {
                    const float depth =
                        wa * t.vertexDepth[0] + wb * t.vertexDepth[1] + wc * t.vertexDepth[2];
                    if (depth > closestDepth) {
                        closestDepth = depth;
                        picked = t.block;
                    }
                }
            }
        if (state.logicManipulation) {
            if (hitLogicEdge >= 0) {
                const auto& edge = logicEdges[hitLogicEdge];
                if (Parser::removeConnectionById(blueprint, blocks, edge.source, edge.targetId))
                    notify("Logic connection removed");
                if (state.logicSource == edge.source)
                    state.logicSource = -1;
            } else if (picked < 0) {
                state.logicSource = -1;
                state.selection = -1;
                state.selectionSet.clear();
                notify("Selection cleared");
            } else if (actions.controllerIdForPart(picked) < 0)
                notify("That part cannot be connected");
            else if (state.logicSource < 0) {
                state.logicSource = picked;
                state.selection = picked;
                state.selectionSet.clear();
                state.selectionSet.insert(picked);
                notify("Logic source selected");
            } else if (ImGui::GetIO().KeyShift) {
                state.logicSource = picked;
                state.selection = picked;
                state.selectionSet.clear();
                state.selectionSet.insert(picked);
                notify("Logic source changed");
            } else if (picked == state.logicSource) {
                const std::string itemName = actions.partDisplayName(picked);
                if (Parser::addDirectedConnection(blueprint, blocks, picked, picked))
                    notify("Wired " + itemName + " into itself");
                else
                    notify("Already wired " + itemName + " into itself");
            } else if (Parser::addDirectedConnection(blueprint, blocks, state.logicSource, picked))
                notify("Logic connection created");
            else
                notify("These parts are already connected");
        } else if (!hitAxis) {
            if (ImGui::GetIO().KeyShift) {
                if (picked >= 0) {
                    state.selection = picked;
                    actions.addSelectionGroup(picked);
                }
            } else {
                state.selection = picked;
                state.selectionSet.clear();
                if (picked >= 0)
                    actions.addSelectionGroup(picked);
            }
        }
        if (!state.logicManipulation && state.stackSelection && state.selection >= 0 &&
            state.selection < (int)blocks.size() && !blocks[state.selection].isJoint) {
            const Parser::Position target = blocks[state.selection].pos;
            for (int index : state.selectionSet)
                if (index >= 0 && index < (int)blocks.size() && !blocks[index].isJoint)
                    blocks[index].pos = target;
            actions.synchronizeJointPositions();
        }
        state.dragSelection.clear();
        if (hitAxis) {
            state.dragSelection.assign(state.selectionSet.begin(), state.selectionSet.end());
            if (state.dragSelection.empty() && state.selection >= 0)
                state.dragSelection.push_back(state.selection);
        }
        state.dragAxis = hitAxis;
        state.lastDragMouse = mouse;
        state.dragRemainder = 0.f;
        state.dragWorldPerPixel = 0.f;
        state.dragScreenAxis = {0, 0};
        if (hitAxis) {
            const AxisHandle handle = axisHandle(hitAxis);
            state.dragScreenAxis = handle.screenAxis;
            state.dragWorldPerPixel = handle.worldPerPixel;
        }
    }
    const float rightDragThreshold = ImGui::GetIO().MouseDragThreshold;
    const bool rightClick = ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
                            ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] <
                                rightDragThreshold * rightDragThreshold;
    if (hovered && state.logicManipulation && rightClick) {
        state.logicSource = -1;
        state.selection = -1;
        state.selectionSet.clear();
        state.dragSelection.clear();
        state.dragAxis = 0;
        notify("Selection cleared");
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        state.dragSelection.clear();
        state.dragAxis = 0;
    }
    if (!state.dragSelection.empty() && state.dragAxis &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 delta =
            ImVec2(mouse.x - state.lastDragMouse.x, mouse.y - state.lastDragMouse.y);
        state.lastDragMouse = mouse;
        const int signedAxis = state.dragAxis;
        if (state.dragWorldPerPixel > 0.f) {
            state.dragRemainder +=
                (delta.x * state.dragScreenAxis.x + delta.y * state.dragScreenAxis.y) *
                state.dragWorldPerPixel;
            const int steps = (int)std::trunc(state.dragRemainder);
            if (steps) {
                actions.moveSelection(signedAxis == 1    ? steps
                                      : signedAxis == -1 ? -steps
                                                         : 0,
                                      signedAxis == 2    ? steps
                                      : signedAxis == -2 ? -steps
                                                         : 0,
                                      signedAxis == 3    ? steps
                                      : signedAxis == -3 ? -steps
                                                         : 0);
                state.dragRemainder -= steps;
            }
        }
    }
}

} // namespace BlueprintViewport::Internal
