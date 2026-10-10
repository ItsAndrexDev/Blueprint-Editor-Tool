#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <Windows.h>
#undef RGB
#include "gui_manager.hpp"
#include <GLFW/glfw3native.h>
#include "helpers/blueprint_viewport.hpp"
#include "helpers/scrap_parser.hpp"
#include "menu_helper.hpp"
#include "resource.h"
#include <cstdio>
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
ImGuiID blueprintLoaderDockId = 0;
void loadBlueprintTextures();

void renderImgui() {
	if (programStatus == BLUEPRINT_SELECT && refreshBlueprintsRequested) {
		refreshBlueprintsRequested = false;
		loadBlueprintTextures();
	}
	switch (programStatus)
	{
	case BLUEPRINT_SELECT:
		renderBlueprintLoader(loadedBlueprints, programStatus, blueprint, selectedItem, blockList, refreshBlueprintsRequested, blueprintLoaderDockId);

		break;
	case BLUEPRINT_EDIT:
		renderBlueprintEditor(programStatus, blueprint, selectedItem, blockList, items);

		break;
	default:
		break;
	}
	renderHelpPage(blueprintLoaderDockId);
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

bool loadEmbeddedItems(json& outItems) {
	const HMODULE module = GetModuleHandleW(nullptr);
	const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_ITEMS_JSON), RT_RCDATA);
	if (!resource) {
		std::cerr << "Embedded items.json resource was not found. Error " << GetLastError() << std::endl;
		return false;
	}
	const DWORD resourceSize = SizeofResource(module, resource);
	const HGLOBAL loadedResource = LoadResource(module, resource);
	const auto* resourceData = static_cast<const char*>(LockResource(loadedResource));
	if (!loadedResource || !resourceData || resourceSize == 0) {
		std::cerr << "Embedded items.json resource could not be read." << std::endl;
		return false;
	}
	try {
		outItems = json::parse(resourceData, resourceData + resourceSize);
		return true;
	}
	catch (const std::exception& e) {
		std::cerr << "Embedded items.json is invalid: " << e.what() << std::endl;
		return false;
	}
}

void openDiagnosticConsole() {
	if (!AttachConsole(ATTACH_PARENT_PROCESS) && GetLastError() != ERROR_ACCESS_DENIED)
		AllocConsole();
	FILE* output = nullptr;
	FILE* errors = nullptr;
	freopen_s(&output, "CONOUT$", "w", stdout);
	freopen_s(&errors, "CONOUT$", "w", stderr);
}

int main(int argc, char** argv) {



	if (argc == 3 && std::string(argv[1]) == "--inspect-model") {
		openDiagnosticConsole();
		const auto report = BlueprintViewport::inspectModelFile(std::filesystem::path(argv[2]));
		std::cout << (report.loaded ? "loaded" : "failed") << ": " << report.vertices
			<< " vertices, " << report.triangles << " triangles";
		if (!report.detail.empty()) std::cout << " (" << report.detail << ")";
		std::cout << '\n';
		return report.loaded ? 0 : 1;
	}
	if (argc == 3 && std::string(argv[1]) == "--inspect-shape") {
		openDiagnosticConsole();
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
	HWND hwnd = glfwGetWin32Window(gui.window);
	if (hwnd) {
		HINSTANCE hInstance = GetModuleHandle(NULL);
		HICON hIconBig = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
		HICON hIconSmall = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

		if (hIconBig) SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
		if (hIconSmall) SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
	}
	loadEmbeddedItems(items);
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
