#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <utility>
#include "helpers/blueprint_viewport.hpp"
struct BlueprintItem {
	std::string name;
	GLuint textureID;
	Parser::BlueprintPaths paths;
};
enum ProgramStatus
{
	BLUEPRINT_SELECT,
	BLUEPRINT_EDIT
};
// Takes the output json by reference instead of returning a reference to a local
void renderBlueprintLoader(std::vector<BlueprintItem>& loadedPrints,
	ProgramStatus& status,
	json& outBlueprint,
	BlueprintItem& itemSelected,
	std::vector<Parser::Block>& blocks,
	bool& refreshRequested,
	ImGuiID& helpDockId) {
	ImGui::Begin("Blueprint Loader");
	helpDockId = ImGui::GetWindowDockID();
	ImGui::TextWrapped("Select a blueprint to load:");
	static char searchBuffer[128] = "";
	const ImGuiStyle& style = ImGui::GetStyle();
	const float refreshWidth = ImGui::CalcTextSize("Refresh").x + style.FramePadding.x * 2.0f;
	const float searchWidth = std::max(64.0f, ImGui::GetContentRegionAvail().x - refreshWidth - style.ItemSpacing.x);
	ImGui::SetNextItemWidth(searchWidth);
	ImGui::InputTextWithHint("##BlueprintSearch", "Search blueprints...", searchBuffer, sizeof(searchBuffer));
	ImGui::SameLine();
	if (ImGui::Button("Refresh")) refreshRequested = true;
	ImGui::SameLine();
	ImGui::TextDisabled("%zu blueprints", loadedPrints.size());
	ImGui::Separator();
	std::vector<size_t> visibleItems;
	visibleItems.reserve(loadedPrints.size());
	for (size_t i = 0; i < loadedPrints.size(); ++i) {
		if (loadedPrints[i].name.find(searchBuffer) != std::string::npos) visibleItems.push_back(i);
	}
	ImGuiListClipper clipper;
	clipper.Begin(static_cast<int>(visibleItems.size()), 102.0f);
	while (clipper.Step()) for (int visibleIndex = clipper.DisplayStart; visibleIndex < clipper.DisplayEnd; ++visibleIndex) {
		auto& item = loadedPrints[visibleItems[visibleIndex]];
		ImGui::PushID(static_cast<int>(visibleItems[visibleIndex]));

		ImTextureID texID = (ImTextureID)(intptr_t)item.textureID;

		if (ImGui::ImageButton("##bpIcon", texID, ImVec2(88, 88))) {
			std::ifstream blueprintFile(item.paths.folderPath / "blueprint.json");
			if (blueprintFile.is_open()) {
				try {
					json candidateBlueprint;
					blueprintFile >> candidateBlueprint;
					auto candidateBlocks = Parser::parseBlueprint(candidateBlueprint);
					outBlueprint = std::move(candidateBlueprint);
					blocks = std::move(candidateBlocks);
					itemSelected = item;
					BlueprintViewport::resetSelection();
					BlueprintViewport::notify("Blueprint loaded: " + item.name);
					status = BLUEPRINT_EDIT;
					std::cout << "Selected Blueprint: " << item.name << std::endl;
				}
				catch (const std::exception& e) {
					std::cerr << "Failed to parse blueprint: " << e.what() << std::endl;
				}
			}
			else {
				std::cerr << "Failed to open blueprint: " << item.paths.folderPath / "blueprint.json" << std::endl;
			}
		}

		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::TextWrapped("%s", item.name.c_str());
		ImGui::EndGroup();

		ImGui::Separator();
		ImGui::PopID();
	}

	ImGui::End();   // moved outside the loop
}
void renderBlueprintEditor(ProgramStatus& status, json& blueprint, BlueprintItem& itemSelected, std::vector<Parser::Block>& blocks, const json& items) {
	bool backToSelectionRequested = false;
	BlueprintViewport::render(blueprint, itemSelected.paths.folderPath / "blueprint.json", blocks, items,
		itemSelected.name, backToSelectionRequested);
	if (backToSelectionRequested) status = BLUEPRINT_SELECT;
}


void renderHelpPage(ImGuiID loaderDockId) {
	if (loaderDockId != 0) ImGui::SetNextWindowDockID(loaderDockId, ImGuiCond_FirstUseEver);
	ImGui::Begin("Help");
	ImGui::Text("Scrap Mechanic Blueprint Editor");
	ImGui::TextWrapped("A quick guide to finding, editing, and saving your creations.");
	ImGui::Separator();
	ImGui::Text("Getting started");
	ImGui::BulletText("In Blueprint Loader, type in Search blueprints... to filter by name, press Refresh to rescan, and select a blueprint image to open it.");
	ImGui::BulletText("Back to Selection returns from the editor to the blueprint list.");
	ImGui::BulletText("Save Blueprint writes your current edits to the blueprint's existing folder. Undo and Redo only change the open editor history; save to write those changes to disk.");
	ImGui::Spacing();
	ImGui::Text("Editor controls");
	ImGui::BulletText("Click a part to select it. Shift-click adds parts to the selection. The cyan outline marks the active part; other selected parts also have outlines. Click empty space in normal mode to clear the selection.");
	ImGui::BulletText("Move the selection with the Inspector's X/Y/Z position fields, Left/Right/Forward/Back/Up/Down buttons, or by dragging an axis arrow. Scrap Mechanic uses Z as up; X and Y are horizontal.");
	ImGui::BulletText("For a multi-selection, Stack selected parts at active position snaps selected non-joint parts to the active part's position. Movement then moves them together.");
	ImGui::BulletText("The Inspector edits the active part's orientation using the Local X and Local Z (up) controls. Rotate +90° and Rotate -90° turn around local Z; Reset rotation restores the default. Joints follow their connected bodies.");
	ImGui::BulletText("A single supported built-in block can be resized with Width, Height, and Depth. Other parts show their fixed footprint. A multi-selection shows its combined world-space Width (X), Depth (Y), and Height (Z).");
	ImGui::BulletText("Click the color square to open the picker or type a six-digit hex value such as 3E9FFE. Color changes apply to all selected parts.");
	ImGui::BulletText("Click Remove selected part(s) or press Delete to remove the selection. Undo can restore removed parts.");
	ImGui::BulletText("The Scene section reports loaded models and fallback previews. Large blueprints may show reduced real-mesh detail to keep the viewport responsive.");
	ImGui::Spacing();
	ImGui::Text("Copy, logic, and gate settings");
	ImGui::BulletText("Copy and Paste duplicate selected parts and joints. Joints between copied parts are included. Pasted parts are selected and offset one additional grid unit along +X with each paste; copied joints follow them.");
	ImGui::BulletText("Copy block state (connections) is off by default. Turn it on before pasting to preserve controller settings and logic connections. Links between copied parts target their pasted copies; copied parts keep links to original targets, and unselected sources also link to pasted targets. When off, copied controller settings reset and logic links are cleared.");
	ImGui::BulletText("Gate Labels: Off/On is off by default. Turn it on to show each logic gate's AND, OR, XOR, NAND, NOR, or XNOR setting in the viewport. A selected gate's Inspector also shows its setting.");
	ImGui::BulletText("Start Logic Editing, click a controller part as the source, then click controller targets to create directed connections. Click the source again to connect it to itself; Shift-click another controller to change sources. Click empty space to clear the source and selection. Click a connection arrow to remove it. Stop Logic Editing or press Esc to exit.");
	ImGui::Spacing();
	ImGui::Text("View and shortcuts");
	ImGui::BulletText("Right-drag over the viewport to orbit, middle-drag to pan, and scroll to zoom. Reset view restores the default camera.");
	ImGui::BulletText("Save Blueprint / Ctrl+S: save. Undo / Ctrl+Z: undo. Redo / Ctrl+Y: redo.");
	ImGui::BulletText("Copy / Ctrl+C: copy the selection. Paste / Ctrl+V: paste copied objects with the +X offset described above.");
	ImGui::BulletText("Delete: remove the selection. Shift-click: add a part to the selection. Esc: exit Logic Editing.");
	ImGui::BulletText("Keyboard shortcuts work while Blueprint Editor is focused and no field, UI item, or popup is taking input. Mouse gestures work over the 3D viewport.");
	ImGui::Spacing();
	ImGui::TextWrapped("If the editor reports a Survival-only part, Scrap Mechanic Creative mode cannot spawn that part. Remove it or use a Survival world.");
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::TextWrapped("Developed By Andrex. This editor is open-source and available on GitHub");
	ImGui::TextWrapped("If you discover a bug or have a feature request, please do so on discord @consthater");
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::TextWrapped("This editor is not affiliated with Axolot Games or Scrap Mechanic. It is a fan-made tool for editing blueprints.");
	ImGui::End();
}
