#include "scrap_parser.hpp"




Parser::Block Parser::parseBlock(const json& blockJson, int bodyIndex, int childIndex) {
	Parser::Block block;
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

void Parser::applyBlockListToNode(const std::vector<Parser::Block>& blockVector, json& blueprintJson) {

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

bool Parser::saveBlueprint(const std::string& filepath, const json& blueprint) {
	std::ofstream output(filepath);
	if (!output.is_open()) {
		std::cerr << "Failed to open " << filepath << " for writing!" << std::endl;
		return false;
	}

	output << blueprint.dump(4);
	output.close();
	return true;
}
std::string Parser::findBlockNameByShapeID(const std::string& shapeID, const json& items) {
	if (items.contains(shapeID) && items[shapeID].contains("title")) {
		return items[shapeID]["title"];
	}
	return "";
}

std::string Parser::findShapeIDByBlockName(const std::string& blockName, const json& items) {
	for (const auto& [uuid, item_data] : items.items()) {
		if (item_data.contains("title") && item_data["title"] == blockName) {
			return uuid;
		}
	}
	return "";
}


