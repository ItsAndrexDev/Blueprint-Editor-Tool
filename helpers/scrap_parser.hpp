#pragma once

#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Parser {
struct BlueprintPaths {
	fs::path folderPath;
	fs::path descriptionPath;
	fs::path iconPath;
};

struct Position { int x = 0, y = 0, z = 0; };

// A UI-facing record for either a body child or a serialized joint. The indices
// always refer back to their original JSON arrays so unknown blueprint fields
// remain intact when edits are saved.
struct Block {
	Position bounds{ 1, 1, 1 };
	bool hasBounds = false;
	std::string color = "ffffff";
	bool hasColor = false;
	Position pos{};
	std::string shapeID;
	int xaxis = 1;
	int zaxis = 3;
	int bodyIndex = -1;
	int childIndex = -1;
	bool isJoint = false;
	int jointIndex = -1;
	std::int64_t jointId = -1;
	int jointChildA = -1;
	int jointChildB = -1;
	Position jointPosA{};
	Position jointPosB{};
};

std::vector<Block> parseBlueprint(const json& blueprint);
Block parseBlock(const json& block, int bodyIndex, int childIndex);
void applyBlockListToNode(const std::vector<Block>& blocks, json& blueprint);
bool removeBlockFromNode(json& blueprint, int bodyIndex, int childIndex);
bool removeJointFromNode(json& blueprint, int jointIndex);
bool saveBlueprint(const std::string& filepath, const json& blueprint);
std::string findBlockNameByShapeID(const std::string& shapeID, const json& items);
std::string findShapeIDByBlockName(const std::string& blockName, const json& items);

inline int getBlockCount(const std::vector<Block>& blocks, const std::string& shapeID = "") {
	int count = 0;
	for (const auto& block : blocks) {
		if (!shapeID.empty() && block.shapeID != shapeID) continue;
		if (block.isJoint) { ++count; continue; }
		const auto volume = static_cast<long long>(std::max(1, block.bounds.x)) * std::max(1, block.bounds.y) * std::max(1, block.bounds.z);
		count += static_cast<int>(std::min<long long>(volume, 1000000));
	}
	return count;
}

inline std::vector<BlueprintPaths> getBlueprintFolders() {
	std::vector<BlueprintPaths> result;
	#ifdef _WIN32
	char* appDataBuffer = nullptr;
	size_t length = 0;
	if (_dupenv_s(&appDataBuffer, &length, "APPDATA") != 0 || !appDataBuffer) return result;
	const fs::path userRoot = fs::path(appDataBuffer) / "Axolot Games" / "Scrap Mechanic" / "User";
	free(appDataBuffer);
	std::error_code error;
	if (!fs::is_directory(userRoot, error)) return result;
	for (const auto& user : fs::directory_iterator(userRoot, error)) {
		if (error) break;
		if (!user.is_directory(error) || user.path().filename().string().rfind("User_", 0) != 0) continue;
		const auto root = user.path() / "Blueprints";
		if (!fs::is_directory(root, error)) continue;
		for (const auto& entry : fs::directory_iterator(root, error)) {
			if (error) break;
			if (!entry.is_directory(error)) continue;
			result.push_back({ entry.path(), entry.path() / "description.json", entry.path() / "icon.png" });
		}
		break;
	}
	#endif
	return result;
}
}
