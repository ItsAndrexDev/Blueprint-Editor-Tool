#include "scrap_parser.hpp"

#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace {
using IVec3 = std::array<int, 3>;

int integer(const json& object, const char* key, int fallback = 0) {
	if (!object.is_object() || !object.contains(key) || !object[key].is_number_integer()) return fallback;
	try {
		const auto value = object[key].get<std::int64_t>();
		return static_cast<int>(std::clamp<std::int64_t>(value, -1000000, 1000000));
	} catch (...) { return fallback; }
}

std::int64_t integer64(const json& object, const char* key, std::int64_t fallback = -1) {
	if (!object.is_object() || !object.contains(key) || !object[key].is_number_integer()) return fallback;
	try { return object[key].get<std::int64_t>(); } catch (...) { return fallback; }
}

Parser::Position position(const json& value, Parser::Position fallback = {}) {
	return { integer(value, "x", fallback.x), integer(value, "y", fallback.y), integer(value, "z", fallback.z) };
}

IVec3 axis(int code) {
	switch (code) {
	case 1: return { 1, 0, 0 }; case -1: return { -1, 0, 0 };
	case 2: return { 0, 1, 0 }; case -2: return { 0, -1, 0 };
	case 3: return { 0, 0, 1 }; case -3: return { 0, 0, -1 };
	default: return { 1, 0, 0 };
	}
}

IVec3 cross(const IVec3& a, const IVec3& b) {
	return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
}

int dot(const IVec3& a, const IVec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

struct Basis { IVec3 x, y, z; };
Basis basis(int xCode, int zCode) {
	Basis result{ axis(xCode), {}, axis(zCode) };
	result.y = cross(result.z, result.x);
	return result;
}

IVec3 toLocal(const IVec3& world, const Basis& frame) {
	return { dot(world, frame.x), dot(world, frame.y), dot(world, frame.z) };
}
IVec3 toWorld(const IVec3& local, const Basis& frame) {
	return {
		frame.x[0] * local[0] + frame.y[0] * local[1] + frame.z[0] * local[2],
		frame.x[1] * local[0] + frame.y[1] * local[1] + frame.z[1] * local[2],
		frame.x[2] * local[0] + frame.y[2] * local[1] + frame.z[2] * local[2]
	};
}

int flatChildIndex(const json& blueprint, int bodyIndex, int childIndex) {
	if (!blueprint.contains("bodies") || !blueprint["bodies"].is_array() || bodyIndex < 0 || bodyIndex >= (int)blueprint["bodies"].size()) return -1;
	int flat = childIndex;
	for (int i = 0; i < bodyIndex; ++i) {
		const auto& body = blueprint["bodies"][i];
		if (body.is_object() && body.contains("childs") && body["childs"].is_array()) flat += (int)body["childs"].size();
	}
	return flat;
}

void writePosition(json& object, const std::string& key, const Parser::Position& p) {
	if (!object.contains(key) || !object[key].is_object()) object[key] = json::object();
	object[key]["x"] = p.x; object[key]["y"] = p.y; object[key]["z"] = p.z;
}

bool childTransformChanged(const json& child, const Parser::Block& block) {
	const json empty = json::object();
	const auto& p = child.contains("pos") && child["pos"].is_object() ? child["pos"] : empty;
	return integer(p, "x") != block.pos.x || integer(p, "y") != block.pos.y || integer(p, "z") != block.pos.z ||
		integer(child, "xaxis", 1) != block.xaxis || integer(child, "zaxis", 3) != block.zaxis;
}

void moveAttachedEndpoint(json& joint, const char* side, int flatIndex,
	const json& originalChild, const Parser::Block& edited) {
	const char* childKey = side[0] == 'A' ? "childA" : "childB";
	if (!joint.is_object() || !joint.contains(childKey) || !joint[childKey].is_number_integer() || joint[childKey].get<int>() != flatIndex) return;
	const std::string posKey = std::string("pos") + side;
	if (!joint.contains(posKey) || !joint[posKey].is_object()) return;
	const Parser::Position oldPos = position(originalChild.value("pos", json::object()));
	const Parser::Position endpoint = position(joint[posKey]);
	const auto oldFrame = basis(integer(originalChild, "xaxis", 1), integer(originalChild, "zaxis", 3));
	const auto newFrame = basis(edited.xaxis, edited.zaxis);
	const IVec3 relative{ endpoint.x - oldPos.x, endpoint.y - oldPos.y, endpoint.z - oldPos.z };
	const auto relocated = toWorld(toLocal(relative, oldFrame), newFrame);
	writePosition(joint, posKey, { edited.pos.x + relocated[0], edited.pos.y + relocated[1], edited.pos.z + relocated[2] });
}

bool validAxis(int value) { return value == 1 || value == -1 || value == 2 || value == -2 || value == 3 || value == -3; }
void validateOrientation(Parser::Block& block) {
	if (!validAxis(block.xaxis)) block.xaxis = 1;
	if (!validAxis(block.zaxis) || std::abs(dot(axis(block.xaxis), axis(block.zaxis))) == 1) block.zaxis = 3;
}
}

std::vector<Parser::Block> Parser::parseBlueprint(const json& source) {
	std::vector<Block> result;
	if (!source.is_object() || !source.contains("bodies") || !source["bodies"].is_array()) return result;
	for (int bodyIndex = 0; bodyIndex < (int)source["bodies"].size(); ++bodyIndex) {
		const auto& body = source["bodies"][bodyIndex];
		if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array()) continue;
		for (int childIndex = 0; childIndex < (int)body["childs"].size(); ++childIndex) {
			const auto& child = body["childs"][childIndex];
			if (!child.is_object() || !child.contains("shapeId") || !child["shapeId"].is_string()) continue;
			result.push_back(parseBlock(child, bodyIndex, childIndex));
		}
	}
	if (source.contains("joints") && source["joints"].is_array()) {
		for (int i = 0; i < (int)source["joints"].size(); ++i) {
			const auto& joint = source["joints"][i];
			if (!joint.is_object() || !joint.contains("shapeId") || !joint["shapeId"].is_string() ||
				!joint.contains("posA") || !joint["posA"].is_object() || !joint.contains("posB") || !joint["posB"].is_object()) continue;
			Block block;
			block.isJoint = true; block.jointIndex = i;
			block.jointPosA = position(joint["posA"]); block.jointPosB = position(joint["posB"]);
			// Joint renderables are placed from posA in Scrap Mechanic's model
			// transform. posB describes the opposite connection on the attached part.
			block.pos = block.jointPosA;
			block.shapeID = joint["shapeId"].get<std::string>();
			block.xaxis = integer(joint, "xaxisA", 1); block.zaxis = integer(joint, "zaxisA", 3);
			block.jointChildA = integer(joint, "childA", -1); block.jointChildB = integer(joint, "childB", -1);
			block.jointId = integer64(joint, "id");
			if (joint.contains("color") && joint["color"].is_string()) { block.color = joint["color"].get<std::string>(); block.hasColor = true; }
			validateOrientation(block);
			result.push_back(std::move(block));
		}
	}
	return result;
}

Parser::Block Parser::parseBlock(const json& source, int bodyIndex, int childIndex) {
	Block block;
	block.bodyIndex = bodyIndex; block.childIndex = childIndex;
	if (!source.is_object()) return block;
	block.pos = position(source.value("pos", json::object()));
	if (source.contains("shapeId") && source["shapeId"].is_string()) block.shapeID = source["shapeId"].get<std::string>();
	block.xaxis = integer(source, "xaxis", 1); block.zaxis = integer(source, "zaxis", 3);
	if (source.contains("bounds") && source["bounds"].is_object()) {
		block.hasBounds = true;
		block.bounds = { std::max(1, integer(source["bounds"], "x", 1)), std::max(1, integer(source["bounds"], "y", 1)), std::max(1, integer(source["bounds"], "z", 1)) };
	}
	if (source.contains("color") && source["color"].is_string()) { block.color = source["color"].get<std::string>(); block.hasColor = true; }
	validateOrientation(block);
	return block;
}

void Parser::applyBlockListToNode(const std::vector<Block>& blocks, json& blueprint) {
	if (!blueprint.is_object() || !blueprint.contains("bodies") || !blueprint["bodies"].is_array()) return;
	const bool hasJointArray = blueprint.contains("joints") && blueprint["joints"].is_array();
	for (const auto& block : blocks) {
		if (block.isJoint) continue;
		if (block.bodyIndex < 0 || block.childIndex < 0 || block.bodyIndex >= (int)blueprint["bodies"].size()) continue;
		auto& body = blueprint["bodies"][block.bodyIndex];
		if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() || block.childIndex >= (int)body["childs"].size()) continue;
		auto& child = body["childs"][block.childIndex];
		if (!child.is_object()) continue;
		const bool moved = childTransformChanged(child, block);
		const int flat = flatChildIndex(blueprint, block.bodyIndex, block.childIndex);
		if (moved && flat >= 0 && hasJointArray) for (auto& joint : blueprint["joints"]) {
			moveAttachedEndpoint(joint, "A", flat, child, block);
			moveAttachedEndpoint(joint, "B", flat, child, block);
		}
		writePosition(child, "pos", block.pos);
		child["xaxis"] = block.xaxis; child["zaxis"] = block.zaxis; child["shapeId"] = block.shapeID;
		if (block.hasBounds) writePosition(child, "bounds", block.bounds);
		else child.erase("bounds");
		if (block.hasColor) child["color"] = block.color; else child.erase("color");
	}
	for (const auto& block : blocks) {
		if (!block.isJoint || block.jointIndex < 0 || block.jointIndex >= (int)blueprint["joints"].size()) continue;
		auto& joint = blueprint["joints"][block.jointIndex];
		if (!joint.is_object()) continue;
		const int dx = block.pos.x - block.jointPosA.x, dy = block.pos.y - block.jointPosA.y, dz = block.pos.z - block.jointPosA.z;
		if (dx || dy || dz) {
			for (const char* endpoint : { "A", "B" }) {
				const std::string key = std::string("pos") + endpoint;
				const auto original = endpoint[0] == 'A' ? block.jointPosA : block.jointPosB;
				const auto current = joint.contains(key) && joint[key].is_object() ? position(joint[key], original) : original;
				writePosition(joint, key, { current.x + dx, current.y + dy, current.z + dz });
			}
		}
		if (block.hasColor) joint["color"] = block.color;
	}
}

bool Parser::removeJointFromNode(json& blueprint, int jointIndex) {
	if (!blueprint.is_object() || !blueprint.contains("joints") || !blueprint["joints"].is_array() || jointIndex < 0 || jointIndex >= (int)blueprint["joints"].size()) return false;
	const auto removed = blueprint["joints"][jointIndex];
	std::unordered_set<std::int64_t> ids;
	if (removed.is_object() && removed.contains("id") && removed["id"].is_number_integer()) ids.insert(removed["id"].get<std::int64_t>());
	if (!ids.empty() && blueprint.contains("bodies") && blueprint["bodies"].is_array()) for (auto& body : blueprint["bodies"]) {
		if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array()) continue;
		for (auto& child : body["childs"]) {
			if (!child.is_object() || !child.contains("joints") || !child["joints"].is_array()) continue;
			auto& refs = child["joints"];
			refs.erase(std::remove_if(refs.begin(), refs.end(), [&](const json& ref) { return ref.is_object() && ref.contains("id") && ref["id"].is_number_integer() && ids.contains(ref["id"].get<std::int64_t>()); }), refs.end());
			if (refs.empty()) child.erase("joints");
		}
	}
	blueprint["joints"].erase(blueprint["joints"].begin() + jointIndex);
	return true;
}

bool Parser::removeBlockFromNode(json& blueprint, int bodyIndex, int childIndex) {
	if (!blueprint.is_object() || !blueprint.contains("bodies") || !blueprint["bodies"].is_array() || bodyIndex < 0 || bodyIndex >= (int)blueprint["bodies"].size()) return false;
	auto& bodies = blueprint["bodies"];
	auto& body = bodies[bodyIndex];
	if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() || childIndex < 0 || childIndex >= (int)body["childs"].size()) return false;
	const int flat = flatChildIndex(blueprint, bodyIndex, childIndex);
	std::unordered_set<std::int64_t> removedIds;
	const auto& child = body["childs"][childIndex];
	if (child.is_object() && child.contains("joints") && child["joints"].is_array()) for (const auto& ref : child["joints"])
		if (ref.is_object() && ref.contains("id") && ref["id"].is_number_integer()) removedIds.insert(ref["id"].get<std::int64_t>());
	if (blueprint.contains("joints") && blueprint["joints"].is_array()) {
		auto& joints = blueprint["joints"];
		for (const auto& joint : joints) if (joint.is_object() &&
			((joint.contains("childA") && joint["childA"].is_number_integer() && joint["childA"].get<int>() == flat) ||
			 (joint.contains("childB") && joint["childB"].is_number_integer() && joint["childB"].get<int>() == flat)) &&
			joint.contains("id") && joint["id"].is_number_integer()) removedIds.insert(joint["id"].get<std::int64_t>());
		joints.erase(std::remove_if(joints.begin(), joints.end(), [&](const json& joint) {
			return joint.is_object() && ((joint.contains("childA") && joint["childA"].is_number_integer() && joint["childA"].get<int>() == flat) ||
				(joint.contains("childB") && joint["childB"].is_number_integer() && joint["childB"].get<int>() == flat) ||
				(joint.contains("id") && joint["id"].is_number_integer() && removedIds.contains(joint["id"].get<std::int64_t>())));
		}), joints.end());
		for (auto& joint : joints) if (joint.is_object()) for (const char* key : { "childA", "childB" })
			if (joint.contains(key) && joint[key].is_number_integer() && joint[key].get<int>() > flat) joint[key] = joint[key].get<int>() - 1;
	}
	for (auto& currentBody : bodies) if (currentBody.is_object() && currentBody.contains("childs") && currentBody["childs"].is_array()) for (auto& currentChild : currentBody["childs"]) {
		if (!currentChild.is_object() || !currentChild.contains("joints") || !currentChild["joints"].is_array()) continue;
		auto& refs = currentChild["joints"];
		refs.erase(std::remove_if(refs.begin(), refs.end(), [&](const json& ref) { return ref.is_object() && ref.contains("id") && ref["id"].is_number_integer() && removedIds.contains(ref["id"].get<std::int64_t>()); }), refs.end());
		if (refs.empty()) currentChild.erase("joints");
	}
	body["childs"].erase(body["childs"].begin() + childIndex);
	return true;
}

bool Parser::saveBlueprint(const std::string& path, const json& blueprint) {
	const fs::path target(path);
	fs::path temporary = target; temporary += L".tmp";
	{
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output) return false;
		output << blueprint.dump(4); output.flush();
		if (!output.good()) { output.close(); std::error_code ignored; fs::remove(temporary, ignored); return false; }
	}
	if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		std::error_code ignored; fs::remove(temporary, ignored); return false;
	}
	return true;
}

std::string Parser::findBlockNameByShapeID(const std::string& shapeID, const json& items) {
	if (items.is_object() && items.contains(shapeID) && items[shapeID].is_object() && items[shapeID].contains("title") && items[shapeID]["title"].is_string()) return items[shapeID]["title"].get<std::string>();
	return {};
}

std::string Parser::findShapeIDByBlockName(const std::string& name, const json& items) {
	if (!items.is_object()) return {};
	for (const auto& [uuid, data] : items.items()) if (data.is_object() && data.contains("title") && data["title"].is_string() && data["title"].get<std::string>() == name) return uuid;
	return {};
}
