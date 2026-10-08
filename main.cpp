#include "gui_manager.hpp"
#include "helpers/scrap_parser.hpp"
#include "menu_helper.hpp"
#include <fstream>
#include <iostream>

#define WINDOW_WIDTH 1280
#define WINDOW_HEIGHT 720




std::vector<BlueprintItem> loadedBlueprints;
std::vector<Parser::Block> blockList;
BlueprintItem selectedItem;
json blueprint;
json items;
ProgramStatus programStatus = BLUEPRINT_SELECT;

void renderImgui() {
	switch (programStatus)
	{
	case BLUEPRINT_SELECT:
		renderBlueprintLoader(loadedBlueprints, programStatus, blueprint, selectedItem, blockList);

		break;
	case BLUEPRINT_EDIT:
		renderBlueprintEditor(programStatus, blueprint, selectedItem, blockList);

		break;
	default:
		break;
	}
}

void loadBlueprintTextures() {
	std::vector<Parser::BlueprintPaths> blueprintFolders = Parser::getBlueprintFolders();
	for (auto& bp : blueprintFolders) {
		BlueprintItem item;
		item.paths = bp;
		item.name = bp.folderPath.filename().string(); // Fallback to directory name

		// Parse human-readable name from description.json
		if (std::filesystem::exists(bp.descriptionPath)) {
			std::ifstream descFile(bp.descriptionPath);
			if (descFile.is_open()) {
				try {
					json descJson;
					descFile >> descJson;
					if (descJson.contains("name") && descJson["name"].is_string()) {
						item.name = descJson["name"].get<std::string>();
					}
				}
				catch (...) {}
			}
		}

		// Load PNG icon texture
		int dummyW, dummyH;
		item.textureID = GuiManager::LoadTextureFromFile(bp.iconPath.string().c_str(), &dummyW, &dummyH);

		loadedBlueprints.push_back(item);
	}
}

int main() {
	GuiManager::Gui gui(WINDOW_WIDTH, WINDOW_HEIGHT, "Scrap Mechanic Blueprint Editor");
	// Load blueprint folders and read names + icons
	loadBlueprintTextures();



	// Main loop
	while (!glfwWindowShouldClose(gui.window)) {
		GuiManager::newFrame();
		gui.SetupDockSpace();

		// 3. Process UI windows
		renderImgui();

		GuiManager::endFrame(gui.window, gui.io);

	}

	// Cleanup textures
	for (auto& item : loadedBlueprints) {
		if (item.textureID != 0) {
			glDeleteTextures(1, &item.textureID);
		}
	}

	return 0;
}