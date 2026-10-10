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
	ImGui::BulletText("Open the Blueprint Loader tab and choose a blueprint. Use the search box to find one by name.");
	ImGui::BulletText("The 3D view shows the parts in your creation. Click a part to select it and edit it in the Inspector.");
	ImGui::BulletText("Click Save Blueprint after making changes. The edited blueprint is saved in its existing folder.");
	ImGui::Spacing();
	ImGui::Text("Selection and movement");
	ImGui::BulletText("Click a part to select it. Hold Shift and click more parts to add them to your selection.");
	ImGui::BulletText("Drag one of the +X, -X, +Y, -Y, +Z, or -Z arrows to move all selected parts together along that direction.");
	ImGui::BulletText("The game uses Z as up. The arrow you pull controls the only direction of movement.");
	ImGui::BulletText("For block dimensions, local Z is Height and local Y is Depth. Orientation controls label the upright axis as local Z.");
	ImGui::BulletText("Position fields, movement buttons, and axis arrows move the whole selection. Rotation edits the active part. Color changes apply to every selected part.");
	ImGui::BulletText("With one part selected, Dimensions lets you resize built-in blocks. With multiple parts selected, it shows their combined world-space Width (X), Depth (Y), and Height (Z).");
	ImGui::BulletText("Press Delete to remove all selected parts.");
	ImGui::Spacing();
	ImGui::Text("Camera and part appearance");
	ImGui::BulletText("Right-drag to orbit the camera, middle-drag to pan, and scroll to zoom. Reset view returns to the default camera.");
	ImGui::BulletText("Use the Orientation controls to rotate a part. Built-in blocks can be resized by Width, Height, and Depth.");
	ImGui::BulletText("Click the color square to open the color picker, or type a six-digit hex color such as 3E9FFE. With multiple parts selected, this changes all their colors.");
	ImGui::Spacing();
	ImGui::Text("Shortcuts");
	ImGui::BulletText("Shift + click: add a part to the current selection.");
	ImGui::BulletText("Ctrl + S: save the edited blueprint.");
	ImGui::BulletText("Delete: remove the selected part or parts.");
	ImGui::BulletText("Right mouse + drag: orbit. Middle mouse + drag: pan. Mouse wheel: zoom.");
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
