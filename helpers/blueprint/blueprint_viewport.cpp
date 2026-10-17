#define NOMINMAX
#include "blueprint_viewport.hpp"
#include "blueprint_viewport_canvas.hpp"
#include "blueprint_viewport_internal.hpp"
#include "imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BlueprintViewport {
namespace Internal {
State state;
}

using namespace Internal;

void shutdownRenderer() {
    Internal::releaseTextureCache();
    Internal::shutdownSceneRenderer();
}

void resetSelection() {
    state.selection = -1;
    state.selectionSet.clear();
    state.dragSelection.clear();
    state.dragAxis = 0;
    state.blueprintHistory.clear();
    state.historyCursor = 0;
    state.historyActionInProgress = false;
}

void notify(const std::string& message) {
    state.notification = message;
    state.notificationUntil = ImGui::GetTime() + 2.4;
}

void render(json& blueprint,
            const std::filesystem::path& blueprintPath,
            std::vector<Parser::Block>& blocks,
            const json& items,
            const std::string& blueprintName,
            bool& backToSelectionRequested) {
    loadCatalog();
    if (state.currentBlueprint != blueprintPath) {
        state.meshes.clear();
        state.meshFiles.clear();
        state.meshCacheBytes = 0;
        state.currentBlueprint = blueprintPath;
        state.selection = -1;
        state.selectionSet.clear();
        state.stackSelection = false;
        state.logicManipulation = false;
        state.logicSource = -1;
        state.showAllLogicConnections = false;
        state.dragSelection.clear();
        state.dragAxis = 0;
        state.pan = {0, 0};
        state.zoom = 1.f;
        state.blueprintHistory.clear();
        state.blueprintHistory.push_back(blueprint);
        Parser::applyBlockListToNode(blocks, state.blueprintHistory.back());
        state.historyCursor = 0;
        state.historyActionInProgress = false;
    }
    if (state.clipboardBlueprint != blueprintPath) {
        state.clipboard = {};
        state.clipboardBlueprint = blueprintPath;
        state.clipboardPasteCount = 0;
    }
    for (auto it = state.selectionSet.begin(); it != state.selectionSet.end();)
        if (*it < 0 || *it >= (int)blocks.size())
            it = state.selectionSet.erase(it);
        else
            ++it;
    if (state.selection < 0 || state.selection >= (int)blocks.size())
        state.selection = -1;
    if (state.selection >= 0)
        state.selectionSet.insert(state.selection);
    EditingActions actions(blueprint, blueprintPath, blocks, items);
    if (state.logicSource < 0 || state.logicSource >= (int)blocks.size() ||
        actions.controllerIdForPart(state.logicSource) < 0)
        state.logicSource = -1;
    auto isSelected = [&actions](int index) {
        return actions.isSelected(index);
    };
    auto controllerForPart = [&actions](int index) {
        return actions.controllerForPart(index);
    };
    auto controllerIdForPart = [&actions](int index) {
        return actions.controllerIdForPart(index);
    };
    auto partDisplayName = [&actions](int index) {
        return actions.partDisplayName(index);
    };
    auto addSelectionGroup = [&actions](int index) {
        actions.addSelectionGroup(index);
    };
    auto synchronizeJointPositions = [&actions]() {
        actions.synchronizeJointPositions();
    };
    auto moveSelection = [&actions](int x, int y, int z) {
        actions.moveSelection(x, y, z);
    };
    auto selectedWorldSize = [&actions]() {
        return actions.selectedWorldSize();
    };
    auto setSelectionColor = [&actions](const std::string& color) {
        actions.setSelectionColor(color);
    };
    auto removeSelection = [&actions]() {
        return actions.removeSelection();
    };
    auto logicGateModeForPart = [&actions](int index) {
        return actions.logicGateModeForPart(index);
    };
    auto setSelectionLogicGateMode = [&actions](int mode) {
        actions.setSelectionLogicGateMode(mode);
    };
    auto selectPart = [&actions](int index, bool additive) {
        actions.selectPart(index, additive);
    };
    auto selectAllOfType = [&actions](const std::string& shapeId, bool additive) {
        actions.selectAllOfType(shapeId, additive);
    };
    auto controllerBearingSettings = [&actions](int index) {
        return actions.controllerBearingSettings(index);
    };
    auto setControllerBearingAngle =
        [&actions](int index, std::int64_t jointId, int frame, int angle) {
            return actions.setControllerBearingAngle(index, jointId, frame, angle);
        };
    auto setControllerBearingReverse = [&actions](int index, std::int64_t jointId, bool reverse) {
        return actions.setControllerBearingReverse(index, jointId, reverse);
    };
    ImGui::Begin("Blueprint Editor");
    const ImGuiIO& io = ImGui::GetIO();
    const bool shortcutReady =
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput &&
        !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (shortcutReady && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        actions.undo();
    else if (shortcutReady && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
        actions.redo();
    else if (shortcutReady && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        actions.saveBlueprint();
    else if (shortcutReady && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
        actions.copySelectedObjects();
    else if (shortcutReady && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
        actions.pasteCopiedObjects();
    if (shortcutReady && state.logicManipulation && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        state.logicManipulation = false;
        state.logicSource = -1;
        state.showAllLogicConnections = false;
        notify("Logic manipulation disabled");
    }
    if (shortcutReady && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        actions.removeSelection();
    ImGui::TextWrapped("Editing: %s", blueprintName.c_str());
    if (ImGui::Button("Back to Selection"))
        backToSelectionRequested = true;
    ImGui::Spacing();
    ImGui::Text("3D blueprint editor");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu parts  |  %zu grid cells", blocks.size(), gridCellCount(blocks));
    ImGui::SameLine();
    if (ImGui::Button("Save Blueprint"))
        actions.saveBlueprint();
    ImGui::SameLine();
    ImGui::BeginDisabled(state.historyCursor == 0);
    if (ImGui::Button("Undo"))
        actions.undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state.historyCursor + 1 >= state.blueprintHistory.size());
    if (ImGui::Button("Redo"))
        actions.redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset view")) {
        state.yaw = .72f;
        state.pitch = .42f;
        state.zoom = 1.f;
        state.pan = {0, 0};
        notify("View reset");
    }
    ImGui::Spacing();
    const bool hasCopySelection = state.selection >= 0 || !state.selectionSet.empty();
    ImGui::BeginDisabled(!hasCopySelection);
    if (ImGui::Button("Copy"))
        actions.copySelectedObjects();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(state.clipboard.empty() || state.clipboardBlueprint != blueprintPath);
    if (ImGui::Button("Paste"))
        actions.pasteCopiedObjects();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Copy block state (connections)", &state.copyBlockState);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When enabled, Paste copies controller settings and wiring. Links "
                          "between copied parts point to their pasted copies. Copied parts "
                          "keep links to original targets, and unselected sources also link "
                          "to pasted targets. Toggle before paste.");
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+C / Ctrl+V");
    ImGui::Spacing();
    if (ImGui::Button(state.showLogicGateLabels ? "Gate Labels: On" : "Gate Labels: Off")) {
        state.showLogicGateLabels = !state.showLogicGateLabels;
        notify(state.showLogicGateLabels ? "Logic gate labels enabled"
                                         : "Logic gate labels disabled");
    }
    ImGui::SameLine();
    const bool logicButtonWasActive = state.logicManipulation;
    if (logicButtonWasActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.13f, .43f, .48f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.17f, .53f, .58f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.10f, .36f, .41f, 1.f));
    }
    const bool clickedLogicButton =
        ImGui::Button(state.logicManipulation ? "Stop Logic Editing" : "Start Logic Editing");
    if (logicButtonWasActive)
        ImGui::PopStyleColor(3);
    if (clickedLogicButton) {
        state.logicManipulation = !state.logicManipulation;
        state.logicSource = -1;
        if (!state.logicManipulation)
            state.showAllLogicConnections = false;
        state.dragSelection.clear();
        state.dragAxis = 0;
        notify(state.logicManipulation ? "Logic manipulation enabled"
                                       : "Logic manipulation disabled");
    }
    ImGui::SameLine();
    const bool showAllConnectionsButtonWasActive = state.showAllLogicConnections;
    if (showAllConnectionsButtonWasActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.13f, .43f, .48f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.17f, .53f, .58f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.10f, .36f, .41f, 1.f));
    }
    ImGui::BeginDisabled(!state.logicManipulation);
    const bool clickedShowAllConnections =
        ImGui::Button(state.showAllLogicConnections
                          ? "Show all connections while editing: On##ShowAllConnections"
                          : "Show all connections while editing: Off##ShowAllConnections");
    ImGui::EndDisabled();
    if (showAllConnectionsButtonWasActive)
        ImGui::PopStyleColor(3);
    if (clickedShowAllConnections)
        state.showAllLogicConnections = !state.showAllLogicConnections;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "When off, selecting a source shows only connections to or from it. "
            "With no source selected, all connections remain visible so you can choose one.");
    if (state.logicManipulation)
        ImGui::SameLine();
    if (state.logicManipulation)
        ImGui::TextWrapped(
            "Click a source, then targets. Click the source again to wire it into "
            "itself. Shift-click to switch sources; click empty space or right-click to "
            "clear selection. Click an arrow to remove a link. Esc exits.");
    float avail = ImGui::GetContentRegionAvail().x;
    float inspector = std::clamp(avail * .25f, 230.f, 330.f);
    ViewportCanvas canvas(blueprint, blocks, actions);
    const bool coarsePreview = canvas.render(avail, inspector);
    ImGui::SameLine();
    renderInspector(InspectorContext{blocks,
                                     items,
                                     inspector,
                                     coarsePreview,
                                     moveSelection,
                                     synchronizeJointPositions,
                                     selectedWorldSize,
                                     setSelectionColor,
                                     removeSelection,
                                     logicGateModeForPart,
                                     setSelectionLogicGateMode,
                                     partDisplayName,
                                     controllerBearingSettings,
                                     setControllerBearingAngle,
                                     setControllerBearingReverse,
                                     selectPart,
                                     selectAllOfType});
    json frameSnapshot = blueprint;
    Parser::applyBlockListToNode(blocks, frameSnapshot);
    if (state.blueprintHistory.empty()) {
        state.blueprintHistory.push_back(frameSnapshot);
        state.historyCursor = 0;
    } else if (frameSnapshot != state.blueprintHistory[state.historyCursor]) {
        const bool interactionActive = ImGui::GetIO().MouseDown[0] || ImGui::IsAnyItemActive();
        if (state.historyActionInProgress && interactionActive)
            state.blueprintHistory[state.historyCursor] = std::move(frameSnapshot);
        else {
            state.blueprintHistory.erase(state.blueprintHistory.begin() +
                                             (ptrdiff_t)state.historyCursor + 1,
                                         state.blueprintHistory.end());
            state.blueprintHistory.push_back(std::move(frameSnapshot));
            state.historyCursor = state.blueprintHistory.size() - 1;
            constexpr size_t maximumHistoryStates = 64;
            if (state.blueprintHistory.size() > maximumHistoryStates) {
                state.blueprintHistory.erase(state.blueprintHistory.begin());
                --state.historyCursor;
            }
        }
        state.historyActionInProgress = interactionActive;
    } else if (!ImGui::GetIO().MouseDown[0] && !ImGui::IsAnyItemActive())
        state.historyActionInProgress = false;
    if (!state.notification.empty() && ImGui::GetTime() < state.notificationUntil) {
        const ImVec2 textSize = ImGui::CalcTextSize(state.notification.c_str());
        const ImVec2 padding(12.f, 8.f);
        const ImVec2 toastSize(textSize.x + padding.x * 2.f, textSize.y + padding.y * 2.f);
        const ImVec2 canvasOrigin = canvas.viewportOrigin();
        const ImVec2 canvasSize = canvas.viewportSize();
        const ImVec2 toastPos(canvasOrigin.x + canvasSize.x - toastSize.x - 12.f,
                              canvasOrigin.y + 12.f);
        auto* foreground = ImGui::GetForegroundDrawList();
        foreground->AddRectFilled(toastPos,
                                  ImVec2(toastPos.x + toastSize.x, toastPos.y + toastSize.y),
                                  IM_COL32(37, 91, 66, 242),
                                  5.f);
        foreground->AddText(ImVec2(toastPos.x + padding.x, toastPos.y + padding.y),
                            IM_COL32(238, 250, 240, 255),
                            state.notification.c_str());
    } else if (ImGui::GetTime() >= state.notificationUntil)
        state.notification.clear();
    ImGui::End();
}
} // namespace BlueprintViewport
