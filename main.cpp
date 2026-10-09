#define NOMINMAX
#include <Windows.h>
#undef RGB
#include "gui_manager.hpp"
#include "helpers/blueprint_viewport.hpp"
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
bool refreshBlueprintsRequested = false;
void loadBlueprintTextures();

void renderImgui() {
	if (programStatus == BLUEPRINT_SELECT && refreshBlueprintsRequested) {
		refreshBlueprintsRequested = false;
		loadBlueprintTextures();
	}
	switch (programStatus)
	{
	case BLUEPRINT_SELECT:
		renderBlueprintLoader(loadedBlueprints, programStatus, blueprint, selectedItem, blockList, refreshBlueprintsRequested);

		break;
	case BLUEPRINT_EDIT:
		renderBlueprintEditor(programStatus, blueprint, selectedItem, blockList, items);

		break;
	default:
		break;
	}
}

void loadBlueprintTextures() {
	for (auto& item : loadedBlueprints) {
		if (item.textureID != 0) glDeleteTextures(1, &item.textureID);
	}
	loadedBlueprints.clear();
	std::vector<Parser::BlueprintPaths> blueprintFolders = Parser::getBlueprintFolders();
	loadedBlueprints.reserve(blueprintFolders.size());
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

int main(int argc, char** argv) {
	if (argc == 3 && std::string(argv[1]) == "--inspect-model") {
		const auto report = BlueprintViewport::inspectModelFile(std::filesystem::path(argv[2]));
		std::cout << (report.loaded ? "loaded" : "failed") << ": " << report.vertices
			<< " vertices, " << report.triangles << " triangles";
		if (!report.detail.empty()) std::cout << " (" << report.detail << ")";
		std::cout << '\n';
		return report.loaded ? 0 : 1;
	}
	if (argc == 3 && std::string(argv[1]) == "--inspect-shape") {
		const auto report = BlueprintViewport::inspectShape(argv[2]);
		std::cout << (report.loaded ? "loaded" : "failed") << ": " << report.vertices
			<< " vertices, " << report.triangles << " triangles";
		if (!report.detail.empty()) std::cout << " (" << report.detail << ")";
		std::cout << '\n';
		return report.loaded ? 0 : 1;
	}
	GuiManager::Gui gui(WINDOW_WIDTH, WINDOW_HEIGHT, "Scrap Mechanic Blueprint Editor");
	if (!gui.isReady()) {
		std::cerr << "Failed to initialize the OpenGL 3.3 editor window. Update your graphics driver or check that a compatible GPU is available." << std::endl;
		return 1;
	}
	{
		wchar_t executablePath[MAX_PATH]{};
		const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
		std::filesystem::path itemsPath = pathLength > 0 && pathLength < MAX_PATH
			? std::filesystem::path(executablePath).parent_path() / "items.json"
			: std::filesystem::current_path() / "items.json";
		std::ifstream itemsFile(itemsPath);
		if (itemsFile.is_open()) {
			try { itemsFile >> items; }
			catch (const std::exception& e) { std::cerr << "Failed to load items.json: " << e.what() << std::endl; }
		}
	}
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

	BlueprintViewport::shutdownRenderer();

	// Cleanup textures
	for (auto& item : loadedBlueprints) {
		if (item.textureID != 0) {
			glDeleteTextures(1, &item.textureID);
		}
	}

	return 0;
}
