#include "blueprint_viewport_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace BlueprintViewport::Internal {
namespace {
float pointSegmentDistanceSquared(const ImVec2 point, const ImVec2 start, const ImVec2 end) {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float lengthSquared = dx * dx + dy * dy;
    const float t = lengthSquared > 1e-6f
                        ? std::clamp(((point.x - start.x) * dx + (point.y - start.y) * dy) /
                                         lengthSquared,
                                     0.f,
                                     1.f)
                        : 0.f;
    const float offsetX = point.x - (start.x + dx * t);
    const float offsetY = point.y - (start.y + dy * t);
    return offsetX * offsetX + offsetY * offsetY;
}
} // namespace

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
        int connectionTargetPick = -1;
        float closestTargetDepth = -std::numeric_limits<float>::max();
        int nearbyConnectionTargetPick = -1;
        float nearestConnectionTargetDistanceSquared = 64.f;
        std::vector<bool> connectionTargets;
        if (state.logicManipulation && state.logicSource >= 0) {
            connectionTargets.resize(blocks.size());
            for (size_t index = 0; index < blocks.size(); ++index)
                connectionTargets[index] =
                    actions.connectionKind(state.logicSource, (int)index) != ConnectionKind::None;
        }
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
                const bool insideTriangle = wa >= -1e-4f && wb >= -1e-4f && wc >= -1e-4f;
                if (!connectionTargets.empty() && t.block >= 0 &&
                    t.block < (int)connectionTargets.size() && connectionTargets[t.block]) {
                    if (insideTriangle) {
                        const float depth = wa * t.vertexDepth[0] + wb * t.vertexDepth[1] +
                                            wc * t.vertexDepth[2];
                        if (depth > closestTargetDepth) {
                            closestTargetDepth = depth;
                            connectionTargetPick = t.block;
                        }
                    } else {
                        const float distanceSquared = std::min(
                            {pointSegmentDistanceSquared(mouse, a, b),
                             pointSegmentDistanceSquared(mouse, b, c),
                             pointSegmentDistanceSquared(mouse, c, a)});
                        if (distanceSquared < nearestConnectionTargetDistanceSquared) {
                            nearestConnectionTargetDistanceSquared = distanceSquared;
                            nearbyConnectionTargetPick = t.block;
                        }
                    }
                }
                if (insideTriangle) {
                    const float depth =
                        wa * t.vertexDepth[0] + wb * t.vertexDepth[1] + wc * t.vertexDepth[2];
                    if (depth > closestDepth) {
                        closestDepth = depth;
                        picked = t.block;
                    }
                }
            }
        // Joint meshes and small interactive parts can overlap their attachment blocks.
        // Prefer any compatible target surface under the cursor so an adjacent block
        // cannot intercept a connection click.
        if (connectionTargetPick >= 0)
            picked = connectionTargetPick;
        else if (nearbyConnectionTargetPick >= 0)
            picked = nearbyConnectionTargetPick;
        if (state.logicManipulation) {
            if (hitLogicEdge >= 0) {
                const auto& edge = logicEdges[hitLogicEdge];
                const bool removed = edge.mechanical
                                         ? Parser::removeJointConnectionById(
                                               blueprint, blocks, edge.source, edge.targetId)
                                         : Parser::removeConnectionById(
                                               blueprint, blocks, edge.source, edge.targetId);
                if (removed)
                    notify("Connection removed");
                if (state.logicSource == edge.source)
                    state.logicSource = -1;
            } else if (picked < 0) {
                state.logicSource = -1;
                state.selection = -1;
                state.selectionSet.clear();
                notify("Selection cleared");
            } else if (state.logicSource < 0 && !actions.canUseAsConnectionSource(picked))
                notify(actions.isPistonPart(picked)
                           ? "Pistons can receive signals, but cannot be sources"
                           : "That part cannot start a connection");
            else if (state.logicSource < 0) {
                state.logicSource = picked;
                state.selection = picked;
                state.selectionSet.clear();
                state.selectionSet.insert(picked);
                notify("Connection source selected");
            } else if (ImGui::GetIO().KeyShift) {
                if (actions.canUseAsConnectionSource(picked)) {
                    state.logicSource = picked;
                    state.selection = picked;
                    state.selectionSet.clear();
                    state.selectionSet.insert(picked);
                    notify("Connection source changed");
                } else
                    notify(actions.isPistonPart(picked)
                               ? "Pistons can receive signals, but cannot be sources"
                               : "That part cannot start a connection");
            } else if (picked == state.logicSource) {
                const std::string itemName = actions.partDisplayName(picked);
                if (actions.connectionKind(picked, picked) == ConnectionKind::Logic &&
                    actions.addConnection(picked, picked))
                    notify("Signal wired from " + itemName + " to itself");
                else
                    notify("No self-connection is available for " + itemName);
            } else {
                const auto kind = actions.connectionKind(state.logicSource, picked);
                if (kind == ConnectionKind::None)
                    notify("Those parts cannot connect in that direction");
                else if (actions.addConnection(state.logicSource, picked))
                    notify(kind == ConnectionKind::Joint ? "Mechanical connection created"
                                                         : "Signal connection created");
                else
                    notify("Connection was not added; it may exist already or reach a part limit");
            }
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
