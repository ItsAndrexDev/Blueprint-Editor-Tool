#include "blueprint_viewport_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace BlueprintViewport::Internal {
ViewportCanvas::ViewportCanvas(json& blueprintData,
                               std::vector<Parser::Block>& blockList,
                               EditingActions& editingActions)
    : blueprint(blueprintData), blocks(blockList), actions(editingActions) {
}

ImVec2 ViewportCanvas::viewportOrigin() const {
    return origin;
}

ImVec2 ViewportCanvas::viewportSize() const {
    return size;
}

ImVec2 ViewportCanvas::screen(V3 world, float& depth) const {
    const V3 relative = world - focus;
    // Reflect the horizontal axis to match the in-game blueprint preview.
    const float horizontal = camera.cy * relative.x - camera.sy * relative.y;
    const float horizontalDepth = camera.sy * relative.x + camera.cy * relative.y;
    const float vertical = camera.cp * relative.z - camera.sp * horizontalDepth;
    depth = camera.sp * relative.z + camera.cp * horizontalDepth;
    return {camera.center.x - camera.scale * horizontal, camera.center.y - camera.scale * vertical};
}

bool ViewportCanvas::render(float viewportWidth, float inspectorWidth) {
    ImGui::BeginChild("BlueprintViewport",
                      ImVec2(std::max(120.f, viewportWidth - inspectorWidth - 8.f), 0),
                      true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    origin = ImGui::GetCursorScreenPos();
    size = ImGui::GetContentRegionAvail();
    size.x = std::max(size.x, 120.f);
    size.y = std::max(size.y, 120.f);
    dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(
        origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(25, 28, 33, 255));
    ImGui::InvisibleButton("##scene",
                           size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    hovered = ImGui::IsItemHovered();
    mouse = ImGui::GetIO().MousePos;
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        state.yaw += d.x * .008f;
        state.pitch = std::clamp(state.pitch + d.y * .008f, -1.35f, 1.35f);
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        state.pan.x += d.x;
        state.pan.y += d.y;
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0)
        state.zoom = std::clamp(state.zoom * std::pow(1.12f, ImGui::GetIO().MouseWheel), .12f, 5.f);

    setupCameraAndGrid();
    buildSceneGeometry();
    renderSceneGeometry();
    drawLogicGateLabels();
    buildLogicEdges();
    drawSelectionGizmo();
    drawLogicConnections();
    handleInput();
    ImGui::EndChild();
    return coarsePreview;
}

} // namespace BlueprintViewport::Internal
