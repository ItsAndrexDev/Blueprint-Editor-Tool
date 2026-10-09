#include "scrap_parser.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <unordered_set>

namespace {
using Axis = std::array<int, 3>;

Axis axisVector(int value) {
	switch (value) {
	case 1: return { 1, 0, 0 };
	case -1: return { -1, 0, 0 };
	case 2: return { 0, 1, 0 };
	case -2: return { 0, -1, 0 };
	case 3: return { 0, 0, 1 };
	case -3: return { 0, 0, -1 };
	default: return { 1, 0, 0 };
	}
}

Axis cross(const Axis& a, const Axis& b) {
	return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

int dot(const Axis& a, const Axis& b) {
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

int readInteger(const json& node, const char* key, int fallback) {
	if (!node.is_object() || !node.contains(key) || !node[key].is_number_integer()) return fallback;
	try {
		const int64_t value = node[key].get<int64_t>();
		return static_cast<int>(std::clamp<int64_t>(value, -1000000, 1000000));
	}
	catch (...) { return fallback; }
}

struct Orientation {
	Axis x, y, z;
};

Orientation orientation(int xaxis, int zaxis) {
	Orientation out{ axisVector(xaxis), {}, axisVector(zaxis) };
	out.y = cross(out.z, out.x);
	return out;
}

Axis rotateVector(const Axis& v, const Orientation& from, const Orientation& to) {
	const Axis local{ dot(v, from.x), dot(v, from.y), dot(v, from.z) };
	return {
		to.x[0] * local[0] + to.y[0] * local[1] + to.z[0] * local[2],
		to.x[1] * local[0] + to.y[1] * local[1] + to.z[1] * local[2],
		to.x[2] * local[0] + to.y[2] * local[1] + to.z[2] * local[2]
	};
}

void updateJointEndpoint(json& joint, const char* endpoint, int childIndex,
	const json& oldChild, const Parser::Block& edited) {
	const bool sideA = endpoint[0] == 'A';
	const std::string childKey = sideA ? "childA" : "childB";
	if (!joint.contains(childKey) || !joint[childKey].is_number_integer() || joint[childKey].get<int>() != childIndex)
		return;
	const std::string posKey = std::string("pos") + endpoint;
	if (!oldChild.contains("pos") || !oldChild["pos"].is_object()) return;
	const auto oldPos = oldChild["pos"];
	const int ox = readInteger(oldPos, "x", 0), oy = readInteger(oldPos, "y", 0), oz = readInteger(oldPos, "z", 0);
	const int oldX = readInteger(oldChild, "xaxis", 1), oldZ = readInteger(oldChild, "zaxis", 3);
	const Orientation oldOrientation = orientation(oldX, oldZ);
	const Orientation newOrientation = orientation(edited.xaxis, edited.zaxis);
	if (joint.contains(posKey) && joint[posKey].is_object()) {
		const auto anchor = joint[posKey];
		const Axis relative{ readInteger(anchor, "x", 0) - ox, readInteger(anchor, "y", 0) - oy, readInteger(anchor, "z", 0) - oz };
		const Axis moved = rotateVector(relative, oldOrientation, newOrientation);
		joint[posKey]["x"] = edited.pos.x + moved[0];
		joint[posKey]["y"] = edited.pos.y + moved[1];
		joint[posKey]["z"] = edited.pos.z + moved[2];
	}
	// xaxisA/zaxisA and xaxisB/zaxisB describe the joint's own local frame. Keep
	// these values unchanged when editing its parent shape; only its anchor moves.
}

bool transformChanged(const json& child, const Parser::Block& edited) {
	if (!child.is_object()) return false;
	const json empty = json::object();
	const auto& pos = child.contains("pos") && child["pos"].is_object() ? child["pos"] : empty;
	return readInteger(pos, "x", 0) != edited.pos.x || readInteger(pos, "y", 0) != edited.pos.y ||
		readInteger(pos, "z", 0) != edited.pos.z || readInteger(child, "xaxis", 1) != edited.xaxis ||
		readInteger(child, "zaxis", 3) != edited.zaxis;
}
}


std::vector<Parser::Block> Parser::parseBlueprint(const json& blueprintJson)
{
	std::vector<Parser::Block> blocks;
	if (!blueprintJson.is_object() || !blueprintJson.contains("bodies") || !blueprintJson["bodies"].is_array())
		return blocks;
	int bodyIndex = -1;
	int childIndex = -1;
	for (const auto& body : blueprintJson["bodies"]) {
		bodyIndex += 1;
		childIndex = -1;
		if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array())
			continue;
		for (const auto& child : body["childs"]) {
			childIndex += 1;
			Block block = parseBlock(child, bodyIndex, childIndex);
			blocks.push_back(block);
		}
	}
	return blocks;
}

Parser::Block Parser::parseBlock(const json& blockJson, int bodyIndex, int childIndex) {
	Parser::Block block;
	block.bodyIndex = bodyIndex;
	block.childIndex = childIndex;

	if (blockJson.is_object() && blockJson.contains("pos") && blockJson["pos"].is_object()) {
		block.pos.x = readInteger(blockJson["pos"], "x", 0);
		block.pos.y = readInteger(blockJson["pos"], "y", 0);
		block.pos.z = readInteger(blockJson["pos"], "z", 0);
	}

	if (blockJson.is_object() && blockJson.contains("bounds") && blockJson["bounds"].is_object()) {
		block.hasBounds = true;
		block.bounds.x = std::clamp(readInteger(blockJson["bounds"], "x", 1), 1, 256);
		block.bounds.y = std::clamp(readInteger(blockJson["bounds"], "y", 1), 1, 256);
		block.bounds.z = std::clamp(readInteger(blockJson["bounds"], "z", 1), 1, 256);
	}
	else {
		block.bounds = { 1, 1, 1 };
	}

	if (blockJson.is_object()) {
		block.xaxis = readInteger(blockJson, "xaxis", 1);
		block.zaxis = readInteger(blockJson, "zaxis", 3);
		if (blockJson.contains("color") && blockJson["color"].is_string()) block.color = blockJson["color"].get<std::string>();
		if (blockJson.contains("shapeId") && blockJson["shapeId"].is_string()) block.shapeID = blockJson["shapeId"].get<std::string>();
	}
	auto validAxis = [](int axis) { return axis == 1 || axis == -1 || axis == 2 || axis == -2 || axis == 3 || axis == -3; };
	if (!validAxis(block.xaxis)) block.xaxis = 1;
	if (!validAxis(block.zaxis) || std::abs(dot(axisVector(block.xaxis), axisVector(block.zaxis))) == 1) block.zaxis = 3;

	return block;
}

void Parser::applyBlockListToNode(const std::vector<Parser::Block>& blockVector, json& blueprintJson) {
	// Scrap Mechanic stores joint child references as flat indices across all bodies.
	// Preserve joint axis metadata and move only anchors attached to shapes whose
	// transforms changed. This avoids rewriting unrelated joint data on every save.
	if (!blueprintJson.contains("bodies") || !blueprintJson["bodies"].is_array()) return;
	int flatIndex = 0;
	for (int bodyIndex = 0; bodyIndex < static_cast<int>(blueprintJson["bodies"].size()); ++bodyIndex) {
		auto& body = blueprintJson["bodies"][bodyIndex];
		if (!body.contains("childs") || !body["childs"].is_array()) continue;
		for (int childIndex = 0; childIndex < static_cast<int>(body["childs"].size()); ++childIndex, ++flatIndex) {
			auto& child = body["childs"][childIndex];
			for (const auto& block : blockVector) {
				if (block.bodyIndex != bodyIndex || block.childIndex != childIndex) continue;
				if (transformChanged(child, block) && blueprintJson.contains("joints") && blueprintJson["joints"].is_array()) {
					for (auto& joint : blueprintJson["joints"]) {
						updateJointEndpoint(joint, "A", flatIndex, child, block);
						updateJointEndpoint(joint, "B", flatIndex, child, block);
					}
				}
				break;
			}
		}
	}
	for (const auto& block : blockVector) {
		if (block.bodyIndex < 0 || block.childIndex < 0 || !blueprintJson.contains("bodies") ||
			!blueprintJson["bodies"].is_array() || block.bodyIndex >= static_cast<int>(blueprintJson["bodies"].size())) continue;
		auto& body = blueprintJson["bodies"][block.bodyIndex];
		if (!body.contains("childs") || !body["childs"].is_array() || block.childIndex >= static_cast<int>(body["childs"].size())) continue;
		auto& child = body["childs"][block.childIndex];
		if (!child.contains("pos") || !child["pos"].is_object()) child["pos"] = json::object();
		child["pos"]["x"] = block.pos.x;
		child["pos"]["y"] = block.pos.y;
		child["pos"]["z"] = block.pos.z;

		// Preserve an explicitly saved bounds field even for 1x1x1 blocks. The
		// game serializes these for ordinary blocks, and stripping it here changes
		// the source blueprint despite no resize being requested.
		if (block.hasBounds) {
			if (!child.contains("bounds") || !child["bounds"].is_object()) child["bounds"] = json::object();
			child["bounds"]["x"] = block.bounds.x;
			child["bounds"]["y"] = block.bounds.y;
			child["bounds"]["z"] = block.bounds.z;
		}
		else {
			blueprintJson["bodies"][block.bodyIndex]["childs"][block.childIndex].erase("bounds");
		}

		child["color"] = block.color;
		child["shapeId"] = block.shapeID;
		child["xaxis"] = block.xaxis;
		child["zaxis"] = block.zaxis;
	}

}

bool Parser::removeBlockFromNode(json& blueprintJson, int bodyIndex, int childIndex) {
	if (!blueprintJson.is_object() || !blueprintJson.contains("bodies") || !blueprintJson["bodies"].is_array() ||
		bodyIndex < 0 || childIndex < 0 || bodyIndex >= static_cast<int>(blueprintJson["bodies"].size())) return false;

	auto& bodies = blueprintJson["bodies"];
	auto& body = bodies[bodyIndex];
	if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() ||
		childIndex >= static_cast<int>(body["childs"].size())) return false;

	int flatIndex = childIndex;
	for (int i = 0; i < bodyIndex; ++i) {
		const auto& previousBody = bodies[i];
		if (previousBody.is_object() && previousBody.contains("childs") && previousBody["childs"].is_array())
			flatIndex += static_cast<int>(previousBody["childs"].size());
	}

	std::unordered_set<int64_t> removedJointIds;
	const auto& removedChild = body["childs"][childIndex];
	if (removedChild.is_object() && removedChild.contains("joints") && removedChild["joints"].is_array()) {
		for (const auto& ref : removedChild["joints"])
			if (ref.is_object() && ref.contains("id") && ref["id"].is_number_integer())
				removedJointIds.insert(ref["id"].get<int64_t>());
	}

	if (blueprintJson.contains("joints") && blueprintJson["joints"].is_array()) {
		auto& joints = blueprintJson["joints"];
		for (const auto& joint : joints) {
			if (!joint.is_object()) continue;
			const auto referencesRemovedChild = [&](const char* key) {
				return joint.contains(key) && joint[key].is_number_integer() && joint[key].get<int>() == flatIndex;
			};
			if (referencesRemovedChild("childA") || referencesRemovedChild("childB")) {
				if (joint.contains("id") && joint["id"].is_number_integer()) removedJointIds.insert(joint["id"].get<int64_t>());
			}
		}

		joints.erase(std::remove_if(joints.begin(), joints.end(), [&](const json& joint) {
			if (!joint.is_object()) return false;
			return (joint.contains("childA") && joint["childA"].is_number_integer() && joint["childA"].get<int>() == flatIndex) ||
				(joint.contains("childB") && joint["childB"].is_number_integer() && joint["childB"].get<int>() == flatIndex) ||
				(joint.contains("id") && joint["id"].is_number_integer() && removedJointIds.contains(joint["id"].get<int64_t>()));
		}), joints.end());
		for (auto& joint : joints) {
			if (!joint.is_object()) continue;
			for (const char* key : { "childA", "childB" }) {
				if (joint.contains(key) && joint[key].is_number_integer() && joint[key].get<int>() > flatIndex)
					joint[key] = joint[key].get<int>() - 1;
			}
		}

		for (auto& remainingBody : bodies) {
			if (!remainingBody.is_object() || !remainingBody.contains("childs") || !remainingBody["childs"].is_array()) continue;
			for (auto& child : remainingBody["childs"]) {
				if (!child.is_object() || !child.contains("joints") || !child["joints"].is_array()) continue;
				auto& refs = child["joints"];
				refs.erase(std::remove_if(refs.begin(), refs.end(), [&](const json& ref) {
					return ref.is_object() && ref.contains("id") && ref["id"].is_number_integer() && removedJointIds.contains(ref["id"].get<int64_t>());
				}), refs.end());
				if (refs.empty()) child.erase("joints");
			}
		}
	}

	body["childs"].erase(body["childs"].begin() + childIndex);
	return true;
}

bool Parser::saveBlueprint(const std::string& filepath, const json& blueprint) {
	const fs::path target(filepath);
	fs::path temporary = target;
	temporary += ".tmp";
	std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
	if (!output.is_open()) {
		std::cerr << "Failed to open " << temporary.string() << " for writing!" << std::endl;
		return false;
	}

	output << blueprint.dump(4);
	output.flush();
	const bool writeSucceeded = output.good();
	output.close();
	if (!writeSucceeded || output.fail()) {
		std::error_code ignored;
		fs::remove(temporary, ignored);
		std::cerr << "Failed to write complete blueprint to " << temporary.string() << std::endl;
		return false;
	}

	std::error_code replaceError;
	fs::rename(temporary, target, replaceError);
	if (replaceError) {
		std::error_code ignored;
		fs::remove(temporary, ignored);
		std::cerr << "Failed to replace blueprint " << target.string() << ": " << replaceError.message() << std::endl;
		return false;
	}
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


