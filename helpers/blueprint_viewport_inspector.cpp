#include "blueprint_viewport_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace BlueprintViewport::Internal {
void renderInspector(const InspectorContext &context) {
  auto &blocks = context.blocks;
  const auto &items = context.items;
  const float inspector = context.width;
  const bool coarsePreview = context.coarsePreview;
  const auto &moveSelection = context.moveSelection;
  const auto &synchronizeJointPositions = context.synchronizeJointPositions;
  const auto &selectedWorldSize = context.selectedWorldSize;
  const auto &setSelectionColor = context.setSelectionColor;
  const auto &removeSelection = context.removeSelection;

  ImGui::BeginChild("BlockInspector", ImVec2(inspector, 0), true);
  ImGui::PushTextWrapPos(0.f);
  ImGui::Text("INSPECTOR");
  ImGui::Separator();
  if (state.selection < 0 || state.selection >= (int)blocks.size())
    ImGui::TextDisabled("Click a block in the viewport to edit it.");
  else {
    auto &b = blocks[state.selection];
    const bool multiSelection = state.selectionSet.size() > 1;
    std::string title = multiSelection ? "Multi-selection" : Parser::findBlockNameByShapeID(b.shapeID, items);
    if (title.empty() && !multiSelection) {
      const auto shape = state.catalog.find(lower(b.shapeID));
      if (shape != state.catalog.end())
        title = displayNameFromShapeSet(shape->second.name);
    }
    if (title.empty())
      title = "Unknown part";
    ImGui::TextWrapped("%s", title.c_str());
    if (multiSelection)
      ImGui::TextDisabled("%zu parts selected · movement and color apply to all", state.selectionSet.size());
    else if (b.isJoint)
      ImGui::TextDisabled("Joint %d  ·  render pivot at attachment A", b.jointIndex + 1);
    else
      ImGui::TextDisabled("Part %d of %d  ·  body %d", state.selection + 1, (int)blocks.size(), b.bodyIndex + 1);
    if (!b.isJoint &&
        lower(b.shapeID) == "9f0f56e8-2c31-4d83-996c-d00a9b296c3f") {
      const int mode = context.logicGateMode ? context.logicGateMode(state.selection) : 0;
      ImGui::TextColored(ImVec4(.40f, .88f, .94f, 1.f), "Logic setting: %s",
                         logicGateModeName(mode));
    }
    if (multiSelection && !b.isJoint) {
      if (ImGui::Button("Stack selected parts at active position")) {
        const Parser::Position target = b.pos;
        for (int index : state.selectionSet)
          if (index >= 0 && index < (int)blocks.size() && !blocks[index].isJoint)
            blocks[index].pos = target;
        synchronizeJointPositions();
      }
      ImGui::TextDisabled("When enabled, movement keeps all selected parts at that position.");
    }
    ImGui::Spacing();
    ImGui::Text("Position");
    if (b.isJoint) {
      ImGui::TextDisabled("A  (%d, %d, %d)", b.jointPosA.x, b.jointPosA.y, b.jointPosA.z);
      ImGui::TextDisabled("B  (%d, %d, %d)", b.jointPosB.x, b.jointPosB.y, b.jointPosB.z);
    } else {
      int positionX = b.pos.x, positionY = b.pos.y, positionZ = b.pos.z;
      auto positionDelta = [](int target, int current) {
        const long long delta = (long long)target - (long long)current;
        return (int)std::clamp(delta, (long long)std::numeric_limits<int>::min(),
                               (long long)std::numeric_limits<int>::max());
      };
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputInt("X##pos", &positionX, 0, 0))
        moveSelection(positionDelta(positionX, b.pos.x), 0, 0);
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputInt("Y##pos", &positionY, 0, 0))
        moveSelection(0, positionDelta(positionY, b.pos.y), 0);
      ImGui::SetNextItemWidth(-1);
      if (ImGui::InputInt("Z##pos", &positionZ, 0, 0))
        moveSelection(0, 0, positionDelta(positionZ, b.pos.z));
      const float movementButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * .5f;
      if (ImGui::Button("Left -X", ImVec2(movementButtonWidth, 0)))
        moveSelection(-1, 0, 0);
      ImGui::SameLine();
      if (ImGui::Button("Right +X", ImVec2(movementButtonWidth, 0)))
        moveSelection(1, 0, 0);
      if (ImGui::Button("Forward +Y", ImVec2(movementButtonWidth, 0)))
        moveSelection(0, 1, 0);
      ImGui::SameLine();
      if (ImGui::Button("Back -Y", ImVec2(movementButtonWidth, 0)))
        moveSelection(0, -1, 0);
      if (ImGui::Button("Up +Z", ImVec2(movementButtonWidth, 0)))
        moveSelection(0, 0, 1);
      ImGui::SameLine();
      if (ImGui::Button("Down -Z", ImVec2(movementButtonWidth, 0)))
        moveSelection(0, 0, -1);
    }
    if (b.isJoint) {
      ImGui::Spacing();
      ImGui::Text("Joint orientation");
      ImGui::TextDisabled("Orientation follows the connected bodies.");
    } else {
      ImGui::Spacing();
      ImGui::Text("Orientation (Z is up)");
      static constexpr int axisValues[6] = {-3, -1, -2, 1, 2, 3};
      auto axisIndex = [&](int v) {
        for (int i = 0; i < 6; i++)
          if (axisValues[i] == v)
            return i;
        return 3;
      };
      int xi = axisIndex(b.xaxis), zi = axisIndex(b.zaxis);
      ImGui::Text("Local X axis");
      ImGui::SetNextItemWidth(-1);
      if (ImGui::Combo("##localXAxis", &xi, "-Z\0-X\0-Y\0+X\0+Y\0+Z\0")) {
        int candidate = axisValues[xi];
        if (std::abs(dot(axis(candidate), axis(b.zaxis))) < .5f)
          b.xaxis = candidate;
        else
          xi = axisIndex(b.xaxis);
      }
      ImGui::Text("Local Z axis (up)");
      ImGui::SetNextItemWidth(-1);
      if (ImGui::Combo("##localZAxis", &zi, "-Z\0-X\0-Y\0+X\0+Y\0+Z\0")) {
        int candidate = axisValues[zi];
        if (std::abs(dot(axis(candidate), axis(b.xaxis))) < .5f)
          b.zaxis = candidate;
        else
          zi = axisIndex(b.zaxis);
      }
      const float rotationButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * .5f;
      if (ImGui::Button("Rotate +90°", ImVec2(rotationButtonWidth, 0))) {
        setOrientation(b, rotateAxisAroundZ(b.xaxis, 1), rotateAxisAroundZ(b.zaxis, 1));
      }
      ImGui::SameLine();
      if (ImGui::Button("Rotate -90°", ImVec2(rotationButtonWidth, 0))) {
        setOrientation(b, rotateAxisAroundZ(b.xaxis, -1), rotateAxisAroundZ(b.zaxis, -1));
      }
      ImGui::TextWrapped("Rotate around local Z (up).");
      if (ImGui::Button("Reset rotation", ImVec2(-1, 0))) {
        setOrientation(b, 1, 3);
      }
    }
    ImGui::Spacing();
    ImGui::Text("Dimensions");
    const bool canResize = state.blockShapes.contains(lower(b.shapeID));
    if (multiSelection) {
      const V3 overall = selectedWorldSize();
      ImGui::TextDisabled("Overall size of selected parts");
      ImGui::Text("Width (X): %.1f", overall.x);
      ImGui::Text("Depth (Y): %.1f", overall.y);
      ImGui::Text("Height (Z, up): %.1f", overall.z);
    } else if (canResize) {
      bool dimensionsChanged = false;
      ImGui::Text("Width");
      ImGui::SetNextItemWidth(-1);
      dimensionsChanged |= ImGui::InputInt("##dimWidth", &b.bounds.x);
      // Blueprint bounds are stored in local X/Y/Z order. With Scrap
      // Mechanic's Z-up basis, local Z is height and local Y is depth.
      ImGui::Text("Height");
      ImGui::SetNextItemWidth(-1);
      dimensionsChanged |= ImGui::InputInt("##dimHeight", &b.bounds.z);
      ImGui::Text("Depth");
      ImGui::SetNextItemWidth(-1);
      dimensionsChanged |= ImGui::InputInt("##dimDepth", &b.bounds.y);
      b.bounds.x = std::clamp(b.bounds.x, 1, 256);
      b.bounds.y = std::clamp(b.bounds.y, 1, 256);
      b.bounds.z = std::clamp(b.bounds.z, 1, 256);
      if (dimensionsChanged)
        b.hasBounds = b.bounds.x != 1 || b.bounds.y != 1 || b.bounds.z != 1;
    } else {
      const V3 dimensions = shapeSize(b);
      ImGui::TextDisabled("Part footprint: %.0f × %.0f × %.0f (fixed by game)", dimensions.x, dimensions.y,
                          dimensions.z);
    }
    const int colorInt = displayColor(b);
    char hex[16];
    snprintf(hex, sizeof(hex), "%06X", colorInt);
    const ImVec4 previewColor(((colorInt >> 16) & 255) / 255.f, ((colorInt >> 8) & 255) / 255.f,
                              (colorInt & 255) / 255.f, 1.f);
    ImGui::TextUnformatted(multiSelection ? "Color · all selected parts" : "Color");
    ImGui::PushID(state.selection);
    if (ImGui::ColorButton("##colorPreview", previewColor, ImGuiColorEditFlags_NoTooltip, ImVec2(24, 22)))
      ImGui::OpenPopup("ColorPickerPopup");
    ImGui::SameLine();
    ImGui::TextDisabled("Click square to choose");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##hex", hex, sizeof(hex), ImGuiInputTextFlags_CharsHexadecimal) && strlen(hex) == 6) {
      const int value = (int)strtoul(hex, nullptr, 16);
      char normalized[7];
      snprintf(normalized, sizeof(normalized), "%06x", value);
      setSelectionColor(normalized);
    }
    ImGui::TextDisabled("Hex · #RRGGBB");
    if (ImGui::BeginPopup("ColorPickerPopup")) {
      float pickerColor[3] = {previewColor.x, previewColor.y, previewColor.z};
      if (ImGui::ColorPicker3("##colorPicker", pickerColor,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoSidePreview |
                                  ImGuiColorEditFlags_NoLabel)) {
        const int red = (int)(pickerColor[0] * 255.f + 0.5f);
        const int green = (int)(pickerColor[1] * 255.f + 0.5f);
        const int blue = (int)(pickerColor[2] * 255.f + 0.5f);
        char normalized[7];
        snprintf(normalized, sizeof(normalized), "%02x%02x%02x", red, green, blue);
        setSelectionColor(normalized);
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
    ImGui::Spacing();
    auto m = meshFor(b.shapeID);
    if (coarsePreview && m && m->loaded)
      ImGui::TextDisabled("Large scene · real mesh detail reduced");
    else if (m && m->loaded)
      ImGui::TextColored(ImVec4(.45f, .84f, .65f, 1), "Game model loaded");
    else if (state.blockShapes.contains(lower(b.shapeID)))
      ImGui::TextDisabled("Built-in block · cuboid preview");
    else {
      ImGui::TextDisabled("Box preview · model unavailable");
      if (m && !m->detail.empty())
        ImGui::TextWrapped("%s", m->detail.c_str());
    }
    ImGui::Spacing();
    const char *removeLabel = state.selectionSet.size() > 1 ? "Remove selected parts" : "Remove selected part";
    if (ImGui::Button(removeLabel, ImVec2(-1, 0)) && !removeSelection())
      state.error = "Could not remove the selected parts from the blueprint.";
  }
  ImGui::Separator();
  ImGui::Text("Scene");
  size_t loadedParts = 0, blockParts = 0, missingParts = 0;
  for (const auto &b : blocks) {
    auto id = lower(b.shapeID);
    auto m = meshFor(b.shapeID);
    if (m && m->loaded)
      loadedParts++;
    else if (state.blockShapes.contains(id))
      blockParts++;
    else
      missingParts++;
  }
  ImGui::Text("%zu parts", blocks.size());
  ImGui::Text("%zu mesh assets · %zu built-in block previews", loadedParts, blockParts);
  if (missingParts)
    ImGui::TextColored(ImVec4(1.f, .68f, .34f, 1.f), "%zu parts using fallback boxes", missingParts);
  if (coarsePreview)
    ImGui::TextDisabled("Large scene shows reduced real-mesh detail for responsiveness.");
  if (!state.renderError.empty())
    ImGui::TextColored(ImVec4(1.f, .48f, .36f, 1.f), "3D renderer: %s", state.renderError.c_str());
  if (!state.error.empty())
    ImGui::TextWrapped("%s", state.error.c_str());
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
  ImGui::TextWrapped("Shift-click adds to selection. Drag an axis arrow to "
                     "move selected parts along it. Right-drag to orbit, "
                     "middle-drag to pan, and scroll to zoom.");
  ImGui::PopStyleColor();
  ImGui::PopTextWrapPos();
  ImGui::EndChild();
}
} // namespace BlueprintViewport::Internal
