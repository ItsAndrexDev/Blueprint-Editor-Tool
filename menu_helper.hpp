#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include <fstream>
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
	std::vector<Parser::Block>& blocks) {
	ImGui::Begin("Blueprint Loader");
	ImGui::TextWrapped("Select a blueprint to load:");
	static char searchBuffer[128] = "";
	ImGui::InputText("Search...", searchBuffer, sizeof(searchBuffer));
	ImGui::Separator();

	int index = -1;
	for (auto& item : loadedPrints) {
		index++;
		if (item.name.find(searchBuffer) == std::string::npos) continue;
		ImGui::PushID(index);

		ImTextureID texID = (ImTextureID)(intptr_t)item.textureID;

		if (ImGui::ImageButton("##bpIcon", texID, ImVec2(64, 64))) {
			std::ifstream blueprintFile(item.paths.folderPath / "blueprint.json");
			if (blueprintFile.is_open()) {
				try {
					blueprintFile >> outBlueprint;
					status = BLUEPRINT_EDIT;
					std::cout << "Selected Blueprint: " << item.name << std::endl;
					itemSelected = item; // Store the selected blueprint
					blocks = Parser::parseBlueprint(outBlueprint); // Parse blocks from the blueprint
				}
				catch (const std::exception& e) {
					std::cerr << "Failed to parse blueprint: " << e.what() << std::endl;
				}
			}
		}

		ImGui::SameLine();
		ImGui::BeginGroup();
		ImGui::TextWrapped(item.name.c_str());
		ImGui::EndGroup();

		ImGui::PopID();
		ImGui::Separator();
	}

	ImGui::End();   // moved outside the loop
}
void renderBlueprintEditor(ProgramStatus& status, json& blueprint, BlueprintItem& itemSelected, std::vector<Parser::Block>& blocks) {
	ImGui::Begin("Blueprint Editor");
	ImGui::TextWrapped(("Editing: " + itemSelected.name).c_str());
	ImGui::Text("Block Count: %d", Parser::getBlockCount(blocks));
	if (ImGui::Button("Back to Selection")) {
		status = BLUEPRINT_SELECT;
	}
	// Additional editing UI can be added here
	ImGui::End();
}