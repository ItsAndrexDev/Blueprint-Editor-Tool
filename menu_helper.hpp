#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include <algorithm>
#include <fstream>
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
	bool& refreshRequested) {
	ImGui::Begin("Blueprint Loader");
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
	BlueprintViewport::render(blueprint, itemSelected.paths.folderPath / "blueprint.json", blocks, items);
	ImGui::Begin("Blueprint");
	ImGui::TextWrapped("Editing: %s", itemSelected.name.c_str());
	if (ImGui::Button("Back to Selection")) status = BLUEPRINT_SELECT;
	ImGui::End();
}
