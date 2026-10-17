#include "blueprint_viewport_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <string>

namespace BlueprintViewport::Internal {
void renderInspector(const InspectorContext& context) {
    constexpr const char* controllerShapeId = "12862001-666c-4bdd-8326-a1f43610d28b";
    constexpr const char* logicGateShapeId = "9f0f56e8-2c31-4d83-996c-d00a9b296c3f";
    auto& blocks = context.blocks;
    const auto& items = context.items;
    const float inspector = context.width;
    const bool coarsePreview = context.coarsePreview;
    const auto& moveSelection = context.moveSelection;
    const auto& synchronizeJointPositions = context.synchronizeJointPositions;
    const auto& selectedWorldSize = context.selectedWorldSize;
    const auto& setSelectionColor = context.setSelectionColor;
    const auto& removeSelection = context.removeSelection;

    ImGui::BeginChild("BlockInspector", ImVec2(inspector, 0), true);
    ImGui::PushTextWrapPos(0.f);
    ImGui::Text("INSPECTOR");
    ImGui::Separator();
    if (ImGui::Button("Find parts...", ImVec2(-1, 0)))
        ImGui::OpenPopup("FindPartsPopup");
    ImGui::SetNextWindowSize(ImVec2(400.f, 460.f), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("FindPartsPopup")) {
        static char partSearch[128] = "";
        ImGui::Text("Find parts by type");
        ImGui::TextDisabled("Select a row, or select every part in a type group.");
        ImGui::TextDisabled(
            "Hold Shift while selecting Select All to add a type to your current selection.");
        ImGui::SetNextItemWidth(-1.f);
        ImGui::InputTextWithHint(
            "##PartSearch", "Search part names...", partSearch, sizeof(partSearch));
        ImGui::Spacing();

        struct PartGroup {
            std::string shapeId;
            std::string name;
            std::vector<int> indices;
        };
        std::map<std::string, PartGroup> groupsByShape;
        for (int index = 0; index < (int)blocks.size(); ++index) {
            const auto& part = blocks[index];
            const std::string shape = lower(part.shapeID);
            auto& group = groupsByShape[shape];
            group.shapeId = shape;
            if (group.name.empty()) {
                group.name = context.partDisplayName
                                 ? context.partDisplayName(index)
                                 : Parser::findBlockNameByShapeID(part.shapeID, items);
                if (group.name.empty() || group.name == "part") {
                    const auto catalogEntry = state.catalog.find(shape);
                    if (catalogEntry != state.catalog.end() && !catalogEntry->second.name.empty())
                        group.name = displayNameFromShapeSet(catalogEntry->second.name);
                }
                if (group.name.empty() || group.name == "part")
                    group.name =
                        "Unknown part · " +
                        part.shapeID.substr(0, std::min<std::size_t>(8, part.shapeID.size()));
            }
            group.indices.push_back(index);
        }

        const std::string query = lower(partSearch);
        std::vector<const PartGroup*> visibleGroups;
        int visibleTypes = 0;
        int visibleParts = 0;
        for (const auto& [shape, group] : groupsByShape) {
            if (query.empty() || lower(group.name).find(query) != std::string::npos ||
                shape.find(query) != std::string::npos) {
                ++visibleTypes;
                visibleParts += static_cast<int>(group.indices.size());
                visibleGroups.push_back(&group);
            }
        }
        std::sort(visibleGroups.begin(),
                  visibleGroups.end(),
                  [](const PartGroup* left, const PartGroup* right) {
                      const std::string leftName = lower(left->name);
                      const std::string rightName = lower(right->name);
                      return leftName != rightName ? leftName < rightName
                                                   : left->shapeId < right->shapeId;
                  });
        ImGui::TextDisabled("%d types · %d parts", visibleTypes, visibleParts);
        ImGui::BeginChild("PartSearchResults", ImVec2(0.f, 0.f), true);
        if (visibleTypes == 0) {
            ImGui::TextDisabled("No parts match this search.");
        } else {
            for (const PartGroup* groupPointer : visibleGroups) {
                const PartGroup& group = *groupPointer;
                const std::string& shape = group.shapeId;
                ImGui::PushID(shape.c_str());
                const std::string header =
                    group.name + " (" + std::to_string(group.indices.size()) + ")";
                const ImGuiTreeNodeFlags flags =
                    ImGuiTreeNodeFlags_Framed |
                    (query.empty() ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_DefaultOpen);
                const bool expanded = ImGui::TreeNodeEx("##partType", flags, "%s", header.c_str());
                if (expanded) {
                    ImGui::BeginDisabled(state.logicManipulation);
                    if (ImGui::Button("Select All##type", ImVec2(-1.f, 0.f)))
                        if (context.selectAllOfType)
                            context.selectAllOfType(group.shapeId, ImGui::GetIO().KeyShift);
                    ImGui::EndDisabled();
                    if (state.logicManipulation && ImGui::IsItemHovered())
                        ImGui::SetTooltip("Stop Logic Editing before selecting a group.");
                    for (int index : group.indices) {
                        const auto& part = blocks[index];
                        const std::string label = std::string(part.isJoint ? "Joint " : "Part ") +
                                                  std::to_string(index + 1) + "  ·  (" +
                                                  std::to_string(part.pos.x) + ", " +
                                                  std::to_string(part.pos.y) + ", " +
                                                  std::to_string(part.pos.z) + ")";
                        ImGui::PushID(index);
                        const bool selected =
                            index == state.selection || state.selectionSet.contains(index);
                        if (ImGui::Selectable(
                                label.c_str(), selected, ImGuiSelectableFlags_NoAutoClosePopups) &&
                            context.selectPart)
                            context.selectPart(index, ImGui::GetIO().KeyShift);
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    if (state.selection < 0 || state.selection >= (int)blocks.size())
        ImGui::TextDisabled("Click a block in the viewport to edit it.");
    else {
        auto& b = blocks[state.selection];
        const bool multiSelection = state.selectionSet.size() > 1;
        std::string title =
            multiSelection ? "Multi-selection" : Parser::findBlockNameByShapeID(b.shapeID, items);
        if (title.empty() && !multiSelection) {
            const auto shape = state.catalog.find(lower(b.shapeID));
            if (shape != state.catalog.end())
                title = displayNameFromShapeSet(shape->second.name);
        }
        if (title.empty())
            title = "Unknown part";
        ImGui::TextWrapped("%s", title.c_str());
        if (multiSelection)
            ImGui::TextDisabled("%zu parts selected · movement and color apply to all",
                                state.selectionSet.size());
        else if (b.isJoint)
            ImGui::TextDisabled("Joint %d  ·  render pivot at attachment A", b.jointIndex + 1);
        else
            ImGui::TextDisabled("Part %d of %d  ·  body %d",
                                state.selection + 1,
                                (int)blocks.size(),
                                b.bodyIndex + 1);
        int logicGateSettingsPart = -1;
        int selectedLogicGateCount = 0;
        for (int index : state.selectionSet)
            if (index >= 0 && index < (int)blocks.size() && !blocks[index].isJoint &&
                lower(blocks[index].shapeID) == logicGateShapeId) {
                ++selectedLogicGateCount;
                if (logicGateSettingsPart < 0)
                    logicGateSettingsPart = index;
            }
        if (state.selection >= 0 && state.selection < (int)blocks.size() &&
            !blocks[state.selection].isJoint &&
            lower(blocks[state.selection].shapeID) == logicGateShapeId) {
            logicGateSettingsPart = state.selection;
            if (!state.selectionSet.contains(state.selection))
                ++selectedLogicGateCount;
        }
        if (logicGateSettingsPart >= 0) {
            ImGui::Spacing();
            ImGui::SeparatorText("Logic gate settings");
            if (selectedLogicGateCount > 1)
                ImGui::TextDisabled("Applies to %d selected logic gates.", selectedLogicGateCount);
            int mode = context.logicGateMode ? context.logicGateMode(logicGateSettingsPart) : 0;
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::Combo("Gate operation", &mode, "AND\0OR\0XOR\0NAND\0NOR\0XNOR\0") &&
                context.setSelectionLogicGateMode)
                context.setSelectionLogicGateMode(mode);
            ImGui::TextDisabled("Choose how this gate combines its logic inputs.");
        }
        const std::string selectedPartName = context.partDisplayName
                                                 ? context.partDisplayName(state.selection)
                                                 : Parser::findBlockNameByShapeID(b.shapeID, items);
        if (!b.isJoint &&
            (lower(b.shapeID) == controllerShapeId || lower(selectedPartName) == "controller")) {
            static int controllerSettingsPart = -1;
            ImGui::Spacing();
            ImGui::SeparatorText("Controller settings");
            ImGui::TextDisabled("Edit the bearing rotation steps for this controller.");
            if (ImGui::Button("Edit bearing rotations...", ImVec2(-1.f, 0.f))) {
                controllerSettingsPart = state.selection;
                ImGui::OpenPopup("ControllerRotationEditor");
            }
            ImGui::SetNextWindowSize(ImVec2(860.f, 620.f), ImGuiCond_Appearing);
            if (ImGui::BeginPopupModal(
                    "ControllerRotationEditor", nullptr, ImGuiWindowFlags_NoResize)) {
                std::string controllerName = context.partDisplayName
                                                 ? context.partDisplayName(controllerSettingsPart)
                                                 : "Controller";
                ImGui::TextWrapped("%s · connected bearing rotations", controllerName.c_str());
                ImGui::TextDisabled("Frame 0 is the starting angle. Frames 1–10 set the rotation "
                                    "steps in degrees.");
                ImGui::Separator();
                const auto bearings =
                    context.controllerBearingSettings
                        ? context.controllerBearingSettings(controllerSettingsPart)
                        : std::vector<EditingActions::ControllerBearingSettings>{};
                ImGui::BeginChild("ControllerRotationList",
                                  ImVec2(0.f, -ImGui::GetFrameHeightWithSpacing() - 6.f),
                                  true);
                if (bearings.empty()) {
                    ImGui::TextDisabled(
                        "No connected bearings with saved rotation settings were found.");
                    ImGui::TextWrapped("Connect a bearing to this Controller in Scrap Mechanic, "
                                       "then reopen the blueprint here.");
                } else {
                    for (const auto& bearing : bearings) {
                        ImGui::PushID(std::to_string(bearing.jointId).c_str());
                        ImGui::Text("%s · connection %d",
                                    bearing.name.c_str(),
                                    bearing.connectionIndex + 1);
                        ImGui::SameLine();
                        bool reverse = bearing.reverse;
                        if (ImGui::Checkbox("Reverse direction", &reverse) &&
                            context.setControllerBearingReverse)
                            context.setControllerBearingReverse(
                                controllerSettingsPart, bearing.jointId, reverse);
                        if (bearing.frameAngles.empty()) {
                            ImGui::TextDisabled("No rotation frames are stored for this bearing.");
                        } else if (ImGui::BeginTable("RotationFrames",
                                                     5,
                                                     ImGuiTableFlags_BordersInnerV |
                                                         ImGuiTableFlags_RowBg |
                                                         ImGuiTableFlags_SizingStretchSame)) {
                            for (int firstFrame = 0; firstFrame < (int)bearing.frameAngles.size();
                                 firstFrame += 5) {
                                ImGui::TableNextRow();
                                for (int column = 0; column < 5; ++column) {
                                    const int frameIndex = firstFrame + column;
                                    if (frameIndex >= (int)bearing.frameAngles.size())
                                        break;
                                    ImGui::TableSetColumnIndex(column);
                                    if (frameIndex == 0)
                                        ImGui::TextUnformatted("Frame 0 · Start");
                                    else
                                        ImGui::Text("Frame %d", frameIndex);
                                    ImGui::PushID(frameIndex);
                                    int angle = bearing.frameAngles[frameIndex];
                                    ImGui::SetNextItemWidth(-1.f);
                                    if (ImGui::InputInt("##targetAngle", &angle, 0, 0) &&
                                        context.setControllerBearingAngle)
                                        context.setControllerBearingAngle(controllerSettingsPart,
                                                                          bearing.jointId,
                                                                          frameIndex,
                                                                          angle);
                                    ImGui::PopID();
                                }
                            }
                            ImGui::EndTable();
                        }
                        ImGui::Spacing();
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
                if (ImGui::Button("Done", ImVec2(120.f, 0.f)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        if (multiSelection && !b.isJoint) {
            if (ImGui::Button("Stack selected parts at active position")) {
                const Parser::Position target = b.pos;
                for (int index : state.selectionSet)
                    if (index >= 0 && index < (int)blocks.size() && !blocks[index].isJoint)
                        blocks[index].pos = target;
                synchronizeJointPositions();
            }
            ImGui::TextDisabled(
                "Aligns the selected non-joint parts to the active part's position once.");
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
                return (int)std::clamp(delta,
                                       (long long)std::numeric_limits<int>::min(),
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
            const float movementButtonWidth =
                (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * .5f;
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
            const float rotationButtonWidth =
                (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * .5f;
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
            ImGui::TextDisabled("Part footprint: %.0f × %.0f × %.0f (fixed by game)",
                                dimensions.x,
                                dimensions.y,
                                dimensions.z);
        }
        const int colorInt = displayColor(b);
        char hex[16];
        snprintf(hex, sizeof(hex), "%06X", colorInt);
        const ImVec4 previewColor(((colorInt >> 16) & 255) / 255.f,
                                  ((colorInt >> 8) & 255) / 255.f,
                                  (colorInt & 255) / 255.f,
                                  1.f);
        ImGui::TextUnformatted(multiSelection ? "Color · all selected parts" : "Color");
        ImGui::PushID(state.selection);
        if (ImGui::ColorButton(
                "##colorPreview", previewColor, ImGuiColorEditFlags_NoTooltip, ImVec2(24, 22)))
            ImGui::OpenPopup("ColorPickerPopup");
        ImGui::SameLine();
        ImGui::TextDisabled("Click square to choose");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##hex", hex, sizeof(hex), ImGuiInputTextFlags_CharsHexadecimal) &&
            strlen(hex) == 6) {
            const int value = (int)strtoul(hex, nullptr, 16);
            char normalized[7];
            snprintf(normalized, sizeof(normalized), "%06x", value);
            setSelectionColor(normalized);
        }
        ImGui::TextDisabled("Hex · #RRGGBB");
        if (ImGui::BeginPopup("ColorPickerPopup")) {
            float pickerColor[3] = {previewColor.x, previewColor.y, previewColor.z};
            if (ImGui::ColorPicker3("##colorPicker",
                                    pickerColor,
                                    ImGuiColorEditFlags_NoInputs |
                                        ImGuiColorEditFlags_NoSidePreview |
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
        const char* removeLabel =
            state.selectionSet.size() > 1 ? "Remove selected parts" : "Remove selected part";
        if (ImGui::Button(removeLabel, ImVec2(-1, 0)) && !removeSelection())
            state.error = "Could not remove the selected parts from the blueprint.";
    }
    renderSceneDetails(context);
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
}
} // namespace BlueprintViewport::Internal
