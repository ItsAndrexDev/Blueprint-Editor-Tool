#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <filesystem>
using json = nlohmann::json;
namespace fs = std::filesystem;
namespace Parser {
	struct BlueprintPaths {
		fs::path folderPath;
		fs::path descriptionPath;
		fs::path iconPath;
	};

	struct Position {
		int x = 0, y = 0, z = 0;
	};

	struct Block {
		Position bounds{ 1, 1, 1 }; // Default 1x1x1 for Scrap Mechanic blocks
		std::string color = "ffffff";
		Position pos{ 0, 0, 0 };
		std::string shapeID = "";
		int xaxis = 1;
		int zaxis = 3;

		// Indices to track the block's position in the blueprint structure
		int bodyIndex = 0;
		int childIndex = 0;
	};

	Block parseBlock(const json& blockJson, int bodyIndex, int childIndex);

	void applyBlockListToNode(const std::vector<Block>& blockVector, json& blueprintJson);

	bool saveBlueprint(const std::string& filepath, const json& blueprint);
	std::string findBlockNameByShapeID(const std::string& shapeID, const json& items);

	std::string findShapeIDByBlockName(const std::string& blockName, const json& items);

	inline int getBlockCount(std::vector<Block>& blockList, const std::string& shapeID = "") {
		int count = 0;

		for (const auto& block : blockList) {
			if (shapeID.empty() || block.shapeID == shapeID) // If shapeID is empty, count all blocks, else count only those matching the shapeID
				count += block.bounds.x * block.bounds.y * block.bounds.z; // Count all blocks using their bounds
		}
		return count;
	}


	inline std::vector<BlueprintPaths> getBlueprintFolders() { // ai slop
		std::vector<BlueprintPaths> blueprints;

		// 1. Get %APPDATA%
		char* appdataBuf = nullptr;
		size_t len = 0;
		if (_dupenv_s(&appdataBuf, &len, "APPDATA") != 0 || appdataBuf == nullptr) {
			return blueprints;
		}
		fs::path appdata(appdataBuf);
		free(appdataBuf);

		// 2. Find the User_<ID>/Blueprints directory
		fs::path userDir = appdata / "Axolot Games" / "Scrap Mechanic" / "User";
		fs::path blueprintsDir;

		if (fs::exists(userDir) && fs::is_directory(userDir)) {
			for (const auto& entry : fs::directory_iterator(userDir)) {
				if (entry.is_directory() && entry.path().filename().string().rfind("User_", 0) == 0) {
					fs::path candidate = entry.path() / "Blueprints";
					if (fs::exists(candidate)) {
						blueprintsDir = candidate;
						break;
					}
				}
			}
		}

		if (blueprintsDir.empty()) return blueprints;

		// 3. Scan all blueprint subfolders
		for (const auto& entry : fs::directory_iterator(blueprintsDir)) {
			if (entry.is_directory()) {
				BlueprintPaths bp;
				bp.folderPath = entry.path();
				bp.descriptionPath = entry.path() / "description.json";
				bp.iconPath = entry.path() / "icon.png";

				blueprints.push_back(bp);
			}
		}

		return blueprints;
	}


}


