#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

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

Block parseBlock(const json& blockJson, int bodyIndex, int childIndex) {
	Block block;
	block.bodyIndex = bodyIndex;
	block.childIndex = childIndex;

	if (blockJson.contains("pos")) {
		block.pos.x = blockJson["pos"].value("x", 0);
		block.pos.y = blockJson["pos"].value("y", 0);
		block.pos.z = blockJson["pos"].value("z", 0);
	}

	if (blockJson.contains("bounds")) {
		block.bounds.x = blockJson["bounds"].value("x", 1);
		block.bounds.y = blockJson["bounds"].value("y", 1);
		block.bounds.z = blockJson["bounds"].value("z", 1);
	}
	else {
		block.bounds = { 1, 1, 1 };
	}

	block.xaxis = blockJson.value("xaxis", 1);
	block.zaxis = blockJson.value("zaxis", 3);

	block.color = blockJson.value("color", "ffffff");
	block.shapeID = blockJson.value("shapeId", "");

	return block;
}

void applyBlockListToNode(const std::vector<Block>& blockVector, json& blueprintJson) {

	for (const auto& block : blockVector) { // pretty funky but it works.
		blueprintJson["bodies"][block.bodyIndex]["childs"][block.childIndex]["pos"]["x"] = block.pos.x;
		blueprintJson["bodies"][block.bodyIndex]["childs"][block.childIndex]["pos"]["y"] = block.pos.y;
		blueprintJson["bodies"][block.bodyIndex]["childs"][block.childIndex]["pos"]["z"] = block.pos.z;

		if (block.bounds.x != 1 || block.bounds.y != 1 || block.bounds.z != 1) {
			blueprintJson["bodies"][block.bodyIndex]["childs"][block.childIndex]["bounds"]["x"] = block.bounds.x;
			blueprintJson["bodies"][block.bodyIndex]["childs"][block.childIndex]["bounds"]["y"] = block.bounds.y;
			blueprintJson["bodies"][block.bodyIndex]["childs"][block.childIndex]["bounds"]["z"] = block.bounds.z;
		}
		else {
			blueprintJson["bodies"][block.bodyIndex]["childs"][block.childIndex].erase("bounds");
		}

		blueprintJson["bodies"][block.bodyIndex]["childs"][block.childIndex]["color"] = block.color;
		blueprintJson["bodies"][block.bodyIndex]["childs"][block.childIndex]["shapeId"] = block.shapeID;
		blueprintJson["bodies"][block.bodyIndex]["childs"][block.childIndex]["xaxis"] = block.xaxis;
		blueprintJson["bodies"][block.bodyIndex]["childs"][block.childIndex]["zaxis"] = block.zaxis;
	}

}

bool saveBlueprint(const std::string& filepath, const json& blueprint) {
	std::ofstream output(filepath);
	if (!output.is_open()) {
		std::cerr << "Failed to open " << filepath << " for writing!" << std::endl;
		return false;
	}

	output << blueprint.dump(4);
	output.close();
	return true;
}
std::string findBlockNameByShapeID(const std::string& shapeID, const json& items) {
	if (items.contains(shapeID) && items[shapeID].contains("title")) {
		return items[shapeID]["title"];
	}
	return "";
}

std::string findShapeIDByBlockName(const std::string& blockName, const json& items) {
	for (const auto& [uuid, item_data] : items.items()) {
		if (item_data.contains("title") && item_data["title"] == blockName) {
			return uuid;
		}
	}
	return "";
}

int getBlockCount(std::vector<Block>& blockList, const std::string& shapeID = "") {
	int count = 0;

	for (const auto& block : blockList) {
		if (shapeID.empty() || block.shapeID == shapeID) // If shapeID is empty, count all blocks, else count only those matching the shapeID
			count += block.bounds.x * block.bounds.y * block.bounds.z; // Count all blocks using their bounds
	}
	return count;
}

int main() {
	std::ifstream input("blueprint.json");
	if (!input.is_open()) {
		std::cerr << "Failed to open blueprint.json" << std::endl;
		return 1;
	}

	json blueprint;
	input >> blueprint;
	input.close();

	input.open("items.json");
	if (!input.is_open()) {
		std::cerr << "Failed to open items.json" << std::endl;
		return 1;
	}

	json items;
	input >> items;
	input.close();

	std::vector<Block> blockList;

	// Iterate through bodies array, then childs array
	int bodyIndex = -1;
	int childIndex = -1;
	if (blueprint.contains("bodies") && blueprint["bodies"].is_array()) {
		for (const auto& body : blueprint["bodies"]) {
			bodyIndex++;
			if (body.contains("childs") && body["childs"].is_array()) {
				for (const auto& blockJson : body["childs"]) {
					childIndex++;
					Block block = parseBlock(blockJson, bodyIndex, childIndex);
					blockList.push_back(block);
				}
			}
		}
	}
	// example: Recolor all blocks to white and move every "Toilet" up by 3 units in the z-axis
	for (auto& block : blockList) {
		block.color = "ffffff";
		if (block.shapeID == findShapeIDByBlockName("Toilet", items)) {
			block.pos.z += 3;
		}
	}
	applyBlockListToNode(blockList, blueprint);
	saveBlueprint("blueprint.json", blueprint);

	return 0;
}