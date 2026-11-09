#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <Windows.h>
#include <objbase.h>
#include <shobjidl.h>
#undef RGB
#include "helpers/ui/gui_manager.hpp"
#include "helpers/ui/help_page.hpp"
#include "helpers/blueprint/blueprint_viewport.hpp"
#include "helpers/scrap/scrap_parser.hpp"
#include "helpers/ui/menu_helper.hpp"
#include "resource.h"
#include <GLFW/glfw3native.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

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

namespace {
std::filesystem::path gameDirectorySettingsPath() {
    wchar_t* appData = nullptr;
    size_t length = 0;
    if (_wdupenv_s(&appData, &length, L"APPDATA") != 0 || !appData)
        return {};
    std::filesystem::path path = std::filesystem::path(appData) / L"Blueprint Editor" /
                                 L"game_directory.txt";
    free(appData);
    return path;
}

std::filesystem::path loadSavedGameDirectory() {
    const auto settings = gameDirectorySettingsPath();
    if (settings.empty())
        return {};
    std::ifstream input(settings, std::ios::binary);
    std::string value((std::istreambuf_iterator<char>(input)), {});
    if (value.empty())
        return {};
    std::u8string utf8Value;
    utf8Value.reserve(value.size());
    for (unsigned char byte : value)
        utf8Value.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(utf8Value);
}

bool saveGameDirectory(const std::filesystem::path& directory) {
    const auto settings = gameDirectorySettingsPath();
    if (settings.empty())
        return false;
    std::error_code error;
    std::filesystem::create_directories(settings.parent_path(), error);
    if (error)
        return false;
    std::ofstream output(settings, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    const auto value = directory.u8string();
    output.write(reinterpret_cast<const char*>(value.data()),
                 static_cast<std::streamsize>(value.size()));
    output.flush();
    return output.good();
}

bool isScrapMechanicGameDirectory(const std::filesystem::path& directory) {
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
        return false;
    const auto dataShapes = directory / L"Data" / L"Objects" / L"Database" / L"ShapeSets";
    const auto survivalShapes =
        directory / L"Survival" / L"Objects" / L"Database" / L"ShapeSets";
    return std::filesystem::is_directory(dataShapes, error) ||
           std::filesystem::is_directory(survivalShapes, error);
}

std::filesystem::path chooseGameDirectory() {
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
        return {};
    const bool uninitializeCom = SUCCEEDED(comResult);

    IFileOpenDialog* dialog = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog,
                                      nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&dialog));
    if (FAILED(result)) {
        if (uninitializeCom)
            CoUninitialize();
        return {};
    }

    DWORD options = 0;
    result = dialog->GetOptions(&options);
    if (SUCCEEDED(result))
        result = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                                    FOS_PATHMUSTEXIST);
    if (SUCCEEDED(result))
        result = dialog->SetTitle(
            L"Select the Scrap Mechanic game folder containing Data and Survival");
    if (SUCCEEDED(result))
        result = dialog->Show(nullptr);

    std::filesystem::path directory;
    if (SUCCEEDED(result)) {
        IShellItem* selected = nullptr;
        result = dialog->GetResult(&selected);
        if (SUCCEEDED(result)) {
            PWSTR path = nullptr;
            result = selected->GetDisplayName(SIGDN_FILESYSPATH, &path);
            if (SUCCEEDED(result) && path)
                directory = path;
            CoTaskMemFree(path);
            selected->Release();
        }
    }

    dialog->Release();
    if (uninitializeCom)
        CoUninitialize();
    return directory;
}

bool initializeGameDirectory() {
    auto directory = loadSavedGameDirectory();
    while (!isScrapMechanicGameDirectory(directory)) {
        if (!directory.empty())
            MessageBoxW(nullptr,
                        L"That folder does not contain Scrap Mechanic's game data. Select the "
                        L"game folder that contains Data and/or Survival.",
                        L"Scrap Mechanic folder not found",
                        MB_OK | MB_ICONWARNING);
        directory = chooseGameDirectory();
        if (directory.empty())
            return false;
        if (!isScrapMechanicGameDirectory(directory))
            continue;
        if (!saveGameDirectory(directory))
            MessageBoxW(nullptr,
                        L"The game folder was selected, but the setting could not be saved. "
                        L"You may be asked again next time.",
                        L"Could not save setting",
                        MB_OK | MB_ICONWARNING);
    }
    BlueprintViewport::setGameDirectory(directory);
    return true;
}
} // namespace

void renderImgui() {
    if (programStatus == BLUEPRINT_SELECT && refreshBlueprintsRequested) {
        refreshBlueprintsRequested = false;
        loadBlueprintTextures();
    }
    switch (programStatus) {
    case BLUEPRINT_SELECT:
        renderBlueprintLoader(loadedBlueprints,
                              programStatus,
                              blueprint,
                              selectedItem,
                              blockList,
                              refreshBlueprintsRequested,
                              blueprintLoaderDockId);

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
        if (item.textureID != 0)
            glDeleteTextures(1, &item.textureID);
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
                } catch (...) {
                }
            }
        }

        // Load PNG icon texture
        int dummyW, dummyH;
        item.textureID =
            GuiManager::LoadTextureFromFile(bp.iconPath.string().c_str(), &dummyW, &dummyH);

        loadedBlueprints.push_back(item);
    }
}

bool loadEmbeddedItems(json& outItems) {
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(IDR_ITEMS_JSON), RT_RCDATA);
    if (!resource) {
        std::cerr << "Embedded items.json resource was not found. Error " << GetLastError()
                  << std::endl;
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
    } catch (const std::exception& e) {
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
        if (!report.detail.empty())
            std::cout << " (" << report.detail << ")";
        std::cout << '\n';
        return report.loaded ? 0 : 1;
    }
    if (argc == 3 && std::string(argv[1]) == "--inspect-shape") {
        openDiagnosticConsole();
        const auto report = BlueprintViewport::inspectShape(argv[2]);
        std::cout << (report.loaded ? "loaded" : "failed") << ": " << report.vertices
                  << " vertices, " << report.triangles << " triangles";
        if (!report.detail.empty())
            std::cout << " (" << report.detail << ")";
        std::cout << '\n';
        return report.loaded ? 0 : 1;
    }
    if (!initializeGameDirectory())
        return 0;
    GuiManager::Gui gui(WINDOW_WIDTH, WINDOW_HEIGHT, "Scrap Mechanic Blueprint Editor");
    if (!gui.isReady()) {
        std::cerr << "Failed to initialize the OpenGL 3.3 editor window. Update your graphics "
                     "driver or check that a compatible GPU is available."
                  << std::endl;
        return 1;
    }
    HWND hwnd = glfwGetWin32Window(gui.window);
    if (hwnd) {
        HINSTANCE hInstance = GetModuleHandleW(nullptr);
        HICON hIconBig = reinterpret_cast<HICON>(LoadImageW(
            hInstance, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR));
        HICON hIconSmall = reinterpret_cast<HICON>(LoadImageW(
            hInstance, MAKEINTRESOURCEW(IDI_ICON1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));

        if (hIconBig)
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIconBig));
        if (hIconSmall)
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIconSmall));
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
