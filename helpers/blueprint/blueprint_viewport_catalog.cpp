#define NOMINMAX
// clang-format off
#include <Windows.h>
#include "blueprint_viewport_internal.hpp"
// clang-format on

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>
#include <string_view>

namespace BlueprintViewport::Internal {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}
bool readFile(const std::filesystem::path& p, json& out) {
    std::ifstream f(p);
    if (!f)
        return false;
    try {
        f >> out;
        return out.is_object();
    } catch (...) {
        return false;
    }
}
std::filesystem::path steamRoot() {
    wchar_t steam[1024]{};
    DWORD n = (DWORD)std::size(steam);
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &key) ==
        ERROR_SUCCESS) {
        n = (DWORD)std::size(steam);
        RegQueryValueExW(key, L"SteamPath", nullptr, nullptr, (LPBYTE)steam, &n);
        RegCloseKey(key);
        if (n) {
            auto p = std::filesystem::path(steam);
            if (std::filesystem::exists(p / L"steamapps"))
                return p;
        }
    }
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &key) ==
        ERROR_SUCCESS) {
        n = (DWORD)std::size(steam);
        if (RegQueryValueExW(key, L"InstallPath", nullptr, nullptr, (LPBYTE)steam, &n) ==
            ERROR_SUCCESS) {
            RegCloseKey(key);
            auto p = std::filesystem::path(steam);
            if (std::filesystem::exists(p / L"steamapps"))
                return p;
        } else
            RegCloseKey(key);
    }
    wchar_t* pf = nullptr;
    size_t pfLength = 0;
    if (_wdupenv_s(&pf, &pfLength, L"PROGRAMFILES(X86)") == 0 && pf) {
        auto p = std::filesystem::path(pf) / L"Steam";
        free(pf);
        if (std::filesystem::exists(p / L"steamapps"))
            return p;
    }
    return {};
}

std::filesystem::path resolveGameAsset(const std::string& asset) {
    constexpr std::string_view gameToken = "$GAME_DATA/";
    constexpr std::string_view survivalToken = "$SURVIVAL_DATA/";
    if (asset.rfind(gameToken, 0) == 0)
        return state.game / L"Data" / std::filesystem::path(asset.substr(gameToken.size()));
    if (asset.rfind(survivalToken, 0) == 0)
        return state.game / L"Survival" / std::filesystem::path(asset.substr(survivalToken.size()));
    return {};
}

void loadCatalog() {
    if (state.catalogLoaded)
        return;
    state.catalogLoaded = true;
    auto steam = steamRoot();
    if (steam.empty()) {
        state.error = "Steam installation not found.";
        return;
    }
    auto libraries = steam / L"steamapps" / L"libraryfolders.vdf";
    std::vector<std::filesystem::path> roots{steam};
    std::ifstream lib(libraries);
    std::string line;
    const std::regex pathPattern(R"rx("path"\s+"([^"]+)")rx");
    while (std::getline(lib, line)) {
        std::smatch m;
        if (std::regex_search(line, m, pathPattern)) {
            std::string v = m[1].str();
            std::replace(v.begin(), v.end(), '\\', '/');
            auto p = std::filesystem::path(v);
            if (std::find(roots.begin(), roots.end(), p) == roots.end())
                roots.push_back(p);
        }
    }
    for (const auto& root : roots) {
        auto game = root / L"steamapps" / L"common" / L"Scrap Mechanic";
        const std::array<std::filesystem::path, 2> databases = {
            game / L"Data" / L"Objects" / L"Database" / L"ShapeSets",
            game / L"Survival" / L"Objects" / L"Database" / L"ShapeSets"};
        if (!std::filesystem::exists(databases[0]) && !std::filesystem::exists(databases[1]))
            continue;
        state.game = game;
        for (size_t databaseIndex = 0; databaseIndex < databases.size(); databaseIndex++) {
            const auto& db = databases[databaseIndex];
            if (!std::filesystem::exists(db))
                continue;
            std::error_code ec;
            for (auto it = std::filesystem::directory_iterator(db, ec);
                 !ec && it != std::filesystem::directory_iterator();
                 it.increment(ec)) {
                if (!it->is_regular_file() || lower(it->path().extension().string()) != ".shapeset")
                    continue;
                json set;
                if (!readFile(it->path(), set))
                    continue;
                if (set.contains("blockList") && set["blockList"].is_array())
                    for (const auto& block : set["blockList"]) {
                        if (!block.is_object() || !block.contains("uuid") ||
                            !block["uuid"].is_string())
                            continue;
                        const auto id = lower(block["uuid"].get<std::string>());
                        state.blockShapes.insert(id);
                        if (block.contains("color") && block["color"].is_string())
                            state.shapeDefaultColors[id] = block["color"].get<std::string>();
                    }
                if (!set.contains("partList") || !set["partList"].is_array())
                    continue;
                for (const auto& part : set["partList"]) {
                    if (!part.is_object() || !part.contains("uuid") || !part["uuid"].is_string())
                        continue;
                    const auto id = lower(part["uuid"].get<std::string>());
                    // Keep ShapeSet defaults available for parts whose blueprint has
                    // no valid paint color. Explicit blueprint colors, including black,
                    // still take precedence in displayColor().
                    if (part.contains("color") && part["color"].is_string())
                        state.shapeDefaultColors[id] = part["color"].get<std::string>();
                    // Register renderable parts from both game databases alike. The
                    // editor is a blueprint visualizer; Creative/Survival eligibility
                    // must never hide a part's installed game model.
                    // Parts without a resizable `box` still publish their
                    // placement footprint as `hull`. Using a unit fallback for
                    // those objects made assets such as the toilet render at
                    // one grid cell even though the game reserves a 3x4x3 hull.
                    const json* footprint = nullptr;
                    if (part.contains("box") && part["box"].is_object())
                        footprint = &part["box"];
                    else if (part.contains("hull") && part["hull"].is_object())
                        footprint = &part["hull"];
                    if (footprint) {
                        state.footprintAnchoredParts.insert(id);
                        auto readSize = [&](const char* axis) {
                            if (!footprint->contains(axis) || !(*footprint)[axis].is_number())
                                return 1.f;
                            return std::clamp((*footprint)[axis].get<float>(), 1.f, 256.f);
                        };
                        state.shapeSizes[id] = {readSize("x"), readSize("y"), readSize("z")};
                    } else if (part.contains("sphere") && part["sphere"].is_object()) {
                        // Spherical parts still occupy a full diameter-sized grid
                        // footprint. Treating them as the one-cell fallback shifts centered
                        // meshes by three cells (e.g. the gyro-seat bubble over its
                        // seven-cell base).
                        const auto& sphere = part["sphere"];
                        const float diameter =
                            sphere.contains("diameter") && sphere["diameter"].is_number()
                                ? std::clamp(sphere["diameter"].get<float>(), 1.f, 256.f)
                                : 1.f;
                        state.shapeSizes[id] = {diameter, diameter, diameter};
                    } else if (part.contains("cylinder") && part["cylinder"].is_object()) {
                        const auto& cylinder = part["cylinder"];
                        auto readCylinderSize = [&](const char* key) {
                            if (!cylinder.contains(key) || !cylinder[key].is_number())
                                return 1.f;
                            return std::clamp(cylinder[key].get<float>(), 1.f, 256.f);
                        };
                        const float diameter = readCylinderSize("diameter"),
                                    depth = readCylinderSize("depth");
                        V3 dimensions{diameter, diameter, diameter};
                        const std::string cylinderAxis =
                            cylinder.contains("axis") && cylinder["axis"].is_string()
                                ? lower(cylinder["axis"].get<std::string>())
                                : "z";
                        if (cylinderAxis == "x")
                            dimensions.x = depth;
                        else if (cylinderAxis == "y")
                            dimensions.y = depth;
                        else
                            dimensions.z = depth;
                        state.shapeSizes[id] = dimensions;
                    }
                    if (!part.contains("renderable"))
                        continue;
                    const json* rend = &part["renderable"];
                    json externalRenderable;
                    if (rend->is_string()) {
                        std::string renderable = rend->get<std::string>();
                        auto path = resolveGameAsset(renderable);
                        if (path.empty())
                            continue;
                        if (!readFile(path, externalRenderable))
                            continue;
                        rend = &externalRenderable;
                    }
                    if (!rend->is_object() || !rend->contains("lodList") ||
                        !(*rend)["lodList"].is_array() || (*rend)["lodList"].empty())
                        continue;
                    Entry entry;
                    if (part.contains("name") && part["name"].is_string())
                        entry.name = part["name"].get<std::string>();
                    const auto normalizedPartName = lower(entry.name);
                    // The water cannon's FBX pivot is displaced from its footprint. Keep
                    // the Totebot bobblehead pivots intact: those multi-piece models use
                    // authored head/plate offsets to remain separated when placed side by
                    // side.
                    entry.centerMeshOnFootprint =
                        normalizedPartName.find("watergun") != std::string::npos;
                    // Renderables describe diffuse images in either subMeshMap (newer
                    // interactive assets) or subMeshList (vehicle and legacy assets).
                    for (const auto& lod : (*rend)["lodList"]) {
                        if (!lod.is_object())
                            continue;
                        auto rememberMaterial = [&](const std::string& name, const json& material) {
                            if (!material.is_object() ||
                                (material.contains("hidden") && material["hidden"].is_boolean() &&
                                 material["hidden"].get<bool>()))
                                return;
                            std::string texture;
                            if (material.contains("textures") && material["textures"].is_object() &&
                                material["textures"].contains("diffuse") &&
                                material["textures"]["diffuse"].is_string())
                                texture = material["textures"]["diffuse"].get<std::string>();
                            else if (material.contains("textureList") &&
                                     material["textureList"].is_array() &&
                                     !material["textureList"].empty() &&
                                     material["textureList"][0].is_string())
                                texture = material["textureList"][0].get<std::string>();
                            if (!texture.empty()) {
                                auto resolved = resolveGameAsset(texture);
                                if (!resolved.empty())
                                    entry.materialTextures[lower(name)] = std::move(resolved);
                            }
                        };
                        if (lod.contains("subMeshMap") && lod["subMeshMap"].is_object())
                            for (auto material = lod["subMeshMap"].begin();
                                 material != lod["subMeshMap"].end();
                                 ++material)
                                rememberMaterial(material.key(), material.value());
                        auto readDiffuse = [&](const json& material) {
                            std::string texture;
                            if (material.is_object() && material.contains("hidden") &&
                                material["hidden"].is_boolean() && material["hidden"].get<bool>())
                                return;
                            if (material.is_object() && material.contains("textures") &&
                                material["textures"].is_object() &&
                                material["textures"].contains("diffuse") &&
                                material["textures"]["diffuse"].is_string())
                                texture = material["textures"]["diffuse"].get<std::string>();
                            else if (material.is_object() && material.contains("textureList") &&
                                     material["textureList"].is_array() &&
                                     !material["textureList"].empty() &&
                                     material["textureList"][0].is_string())
                                texture = material["textureList"][0].get<std::string>();
                            if (!texture.empty())
                                entry.diffuseTexture = resolveGameAsset(texture);
                        };
                        if (lod.contains("subMeshList") && lod["subMeshList"].is_array())
                            for (const auto& material : lod["subMeshList"]) {
                                readDiffuse(material);
                                if (!entry.diffuseTexture.empty())
                                    break;
                            }
                        if (lod.contains("subMeshList") && lod["subMeshList"].is_array())
                            for (size_t materialIndex = 0;
                                 materialIndex < lod["subMeshList"].size();
                                 ++materialIndex)
                                rememberMaterial(std::to_string(materialIndex),
                                                 lod["subMeshList"][materialIndex]);
                        if (entry.diffuseTexture.empty() && lod.contains("subMeshMap") &&
                            lod["subMeshMap"].is_object()) {
                            // Submesh maps are ordered by key, not by visual importance.
                            // Lights list light-cone/flare textures before the actual lamp
                            // body; using that first texture for the whole mesh makes the
                            // fixture disappear.
                            std::vector<std::pair<int, const json*>> materials;
                            for (auto it = lod["subMeshMap"].begin(); it != lod["subMeshMap"].end();
                                 ++it) {
                                int score = 0;
                                const auto key = lower(it.key());
                                if (key.find("mesh") != std::string::npos ||
                                    key.find("body") != std::string::npos ||
                                    key.find("turretseat05") != std::string::npos)
                                    score += 10;
                                if (key.find("glass") != std::string::npos ||
                                    key.find("lightcone") != std::string::npos ||
                                    key.find("flare") != std::string::npos)
                                    score -= 5;
                                materials.emplace_back(score, &it.value());
                            }
                            std::stable_sort(materials.begin(),
                                             materials.end(),
                                             [](const auto& a, const auto& b) {
                                                 return a.first > b.first;
                                             });
                            for (const auto& material : materials) {
                                readDiffuse(*material.second);
                                if (!entry.diffuseTexture.empty())
                                    break;
                            }
                        }
                        if (!entry.diffuseTexture.empty())
                            break;
                    }
                    // The renderable mesh is the part's complete static geometry. pose0
                    // is animated state data (beam cones and moving subparts included),
                    // so retain it only as fallback when mesh cannot be decoded.
                    for (const auto& lod : (*rend)["lodList"]) {
                        if (!lod.is_object())
                            continue;
                        auto addModel = [&](const char* key) {
                            if (!lod.contains(key) || !lod[key].is_string())
                                return;
                            auto model = resolveGameAsset(lod[key].get<std::string>());
                            if (model.empty() || !std::filesystem::is_regular_file(model))
                                return;
                            if (std::find(entry.models.begin(), entry.models.end(), model) ==
                                entry.models.end())
                                entry.models.push_back(std::move(model));
                        };
                        // The warehouse spotlight's static mesh is only the unlit fixture.
                        // Its active pose contains the assembled lamp housing; prefer that
                        // complete model for this one part while leaving other pose-driven
                        // lights static.
                        const bool warehouseSpotlight =
                            normalizedPartName.find("warehousespotlight") != std::string::npos ||
                            normalizedPartName.find("warehousespotligt") != std::string::npos;
                        if (warehouseSpotlight) {
                            addModel("pose0");
                            addModel("mesh");
                        } else {
                            addModel("mesh");
                            addModel("pose0");
                        }
                    }
                    if (entry.models.empty())
                        continue;
                    if (!entry.diffuseTexture.empty())
                        state.shapeDiffuseTextures[id] = entry.diffuseTexture;
                    state.catalog[id] = std::move(entry);
                }
            }
        }
        if (!state.catalog.empty() || !state.blockShapes.empty()) {
            state.catalogAvailable = true;
            break;
        }
    }
    if (!state.catalogAvailable)
        state.error = "Scrap Mechanic shape database was not found in the Steam library.";
}

} // namespace BlueprintViewport::Internal
