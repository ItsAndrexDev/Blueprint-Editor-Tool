#include "blueprint_viewport_internal.hpp"

#include "imgui/imgui.h"

namespace BlueprintViewport::Internal {
void renderSceneDetails(const InspectorContext& context) {
    ImGui::Separator();
    ImGui::Text("Scene");

    size_t loadedParts = 0;
    size_t blockParts = 0;
    size_t missingParts = 0;

    for (const auto& block : context.blocks) {
        const auto shapeId = lower(block.shapeID);
        const auto mesh = meshFor(block.shapeID);

        if (mesh && mesh->loaded)
            ++loadedParts;
        else if (state.blockShapes.contains(shapeId))
            ++blockParts;
        else
            ++missingParts;
    }

    ImGui::Text("%zu parts", context.blocks.size());
    ImGui::Text("%zu mesh assets · %zu built-in block previews", loadedParts, blockParts);

    if (missingParts)
        ImGui::TextColored(
            ImVec4(1.f, .68f, .34f, 1.f), "%zu parts using fallback boxes", missingParts);

    if (context.coarsePreview)
        ImGui::TextDisabled("Large scene shows reduced real-mesh detail for responsiveness.");

    if (!state.renderError.empty())
        ImGui::TextColored(
            ImVec4(1.f, .48f, .36f, 1.f), "3D renderer: %s", state.renderError.c_str());

    if (!state.error.empty())
        ImGui::TextWrapped("%s", state.error.c_str());

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("Shift-click adds to selection. Drag an axis arrow to "
                       "move selected parts along it. Right-drag to orbit, "
                       "middle-drag to pan, and scroll to zoom.");
    ImGui::PopStyleColor();
}
} // namespace BlueprintViewport::Internal
