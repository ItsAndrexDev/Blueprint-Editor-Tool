#include "scrap_parser.hpp"

#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <unordered_map>
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

bool Parser::captureSelectionClipboard(const json& blueprint, const std::vector<Block>& blocks,
	const std::vector<int>& selection, bool copyBlockState, SelectionClipboard& clipboard) {
	clipboard = {};
	clipboard.copyBlockState = copyBlockState;
	if (!blueprint.is_object() || !blueprint.contains("bodies") || !blueprint["bodies"].is_array())return false;
	std::unordered_set<int> selectedBlocks, selectedChildFlats, selectedJointIndices;
	for (int index : selection) {
		if (index < 0 || index >= (int)blocks.size() || !selectedBlocks.insert(index).second)continue;
		const auto& block = blocks[index];
		if (block.isJoint) {
			if (blueprint.contains("joints") && blueprint["joints"].is_array() && block.jointIndex >= 0 && block.jointIndex < (int)blueprint["joints"].size())
				selectedJointIndices.insert(block.jointIndex);
			continue;
		}
		const int flat = flatChildIndex(blueprint, block.bodyIndex, block.childIndex);
		if (flat < 0 || block.bodyIndex < 0 || block.bodyIndex >= (int)blueprint["bodies"].size())continue;
		const auto& body = blueprint["bodies"][block.bodyIndex];
		if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() || block.childIndex < 0 || block.childIndex >= (int)body["childs"].size())continue;
		const auto& child = body["childs"][block.childIndex];
		if (!child.is_object())continue;
		selectedChildFlats.insert(flat);
		clipboard.children.push_back({ block.bodyIndex,flat,child });
	}
	if (blueprint.contains("joints") && blueprint["joints"].is_array()) {
		for (int jointIndex = 0; jointIndex < (int)blueprint["joints"].size(); ++jointIndex) {
			const auto& joint = blueprint["joints"][jointIndex];
			if (!joint.is_object())continue;
			const int childA = integer(joint, "childA", -1), childB = integer(joint, "childB", -1);
			const bool jointSelected = selectedJointIndices.contains(jointIndex);
			const bool internalToSelection = selectedChildFlats.contains(childA) && selectedChildFlats.contains(childB);
			if (!jointSelected && !internalToSelection)continue;
			clipboard.joints.push_back({ childA,childB,joint });
		}
	}
	return !clipboard.empty();
}

bool Parser::pasteSelectionClipboard(json& blueprint, const SelectionClipboard& clipboard, Position offset,
	PasteSelectionResult& result) {
	result = {};
	if (clipboard.empty() || !blueprint.is_object() || !blueprint.contains("bodies") || !blueprint["bodies"].is_array())return false;
	if (!clipboard.joints.empty() && blueprint.contains("joints") && !blueprint["joints"].is_array() && !blueprint["joints"].is_null())return false;

	std::map<int, std::vector<size_t>> childrenBySourceBody;
	std::unordered_map<int, size_t> clipboardChildBySourceFlat;
	for (size_t i = 0; i < clipboard.children.size(); ++i) {
		const auto& child = clipboard.children[i];
		if (child.sourceBodyIndex < 0 || child.sourceFlatIndex < 0 || !child.data.is_object() || !child.data.contains("shapeId") || !child.data["shapeId"].is_string())return false;
		childrenBySourceBody[child.sourceBodyIndex].push_back(i);
		clipboardChildBySourceFlat.emplace(child.sourceFlatIndex, i);
	}
	for (const auto& joint : clipboard.joints)if (!joint.data.is_object())return false;

	std::unordered_set<std::int64_t> usedIds;
	auto rememberId = [&](const json& value) {
		if (!value.is_number_integer())return;
		try { const auto id = value.get<std::int64_t>(); usedIds.insert(id); } catch (...) {}
	};
	auto rememberControllerId = [&](const json& owner) {
		if (!owner.is_object())return;
		if (owner.contains("controller") && owner["controller"].is_object()) {
			const auto& controller = owner["controller"];
			if (controller.contains("id"))rememberId(controller["id"]);
			for (const char* key : {"controllers", "joints"})
				if (controller.contains(key) && controller[key].is_array())
					for (const auto& reference : controller[key])
						if (reference.is_object() && reference.contains("id"))rememberId(reference["id"]);
		}
		if (owner.contains("joints") && owner["joints"].is_array())
			for (const auto& reference : owner["joints"])
				if (reference.is_object() && reference.contains("id"))rememberId(reference["id"]);
	};
	if (blueprint.contains("bodies") && blueprint["bodies"].is_array())for (const auto& body : blueprint["bodies"]) {
		if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array())continue;
		for (const auto& child : body["childs"])rememberControllerId(child);
	}
	if (blueprint.contains("joints") && blueprint["joints"].is_array())for (const auto& joint : blueprint["joints"]) {
		if (!joint.is_object())continue;
		if (joint.contains("id"))rememberId(joint["id"]);
		rememberControllerId(joint);
	}
	for (const auto& child : clipboard.children)
		if (child.data.is_object() && child.data.contains("controller") && child.data["controller"].is_object() && child.data["controller"].contains("id"))rememberId(child.data["controller"]["id"]);
	for (const auto& joint : clipboard.joints) {
		if (joint.data.contains("id"))rememberId(joint.data["id"]);
		if (joint.data.is_object() && joint.data.contains("controller") && joint.data["controller"].is_object() && joint.data["controller"].contains("id"))rememberId(joint.data["controller"]["id"]);
	}
	std::int64_t maximumId = 0;
	for (const auto id : usedIds)if (id > maximumId)maximumId = id;
	std::int64_t nextId = maximumId == std::numeric_limits<std::int64_t>::max() ? 1 : maximumId + 1;
	auto allocateId = [&]() {
		while (usedIds.contains(nextId))nextId = nextId == std::numeric_limits<std::int64_t>::max() ? 1 : nextId + 1;
		const std::int64_t allocated = nextId;
		usedIds.insert(allocated);
		nextId = nextId == std::numeric_limits<std::int64_t>::max() ? 1 : nextId + 1;
		return allocated;
	};
	auto translate = [](int value, int delta) {
		const long long translated = (long long)value + (long long)delta;
		return (int)std::clamp(translated, (long long)std::numeric_limits<int>::min(), (long long)std::numeric_limits<int>::max());
	};
	auto setControllerId = [&](json& owner, std::unordered_map<std::int64_t, std::int64_t>& remap) {
		if (!owner.is_object() || !owner.contains("controller") || !owner["controller"].is_object())return;
		auto& controller = owner["controller"];
		std::int64_t oldId = -1;
		if (controller.contains("id") && controller["id"].is_number_integer())try { oldId = controller["id"].get<std::int64_t>(); } catch (...) {}
		const std::int64_t newId = allocateId();
		controller["id"] = newId;
		if (oldId >= 0)remap.try_emplace(oldId, newId);
	};
	std::unordered_map<std::int64_t, std::int64_t> controllerIdRemap, jointIdRemap;
	std::vector<json> clonedChildren;
	clonedChildren.reserve(clipboard.children.size());
	for (const auto& clipboardChild : clipboard.children) {
		json child = clipboardChild.data;
		if (child.contains("pos") && child["pos"].is_object()) {
			auto p = position(child["pos"]);
			writePosition(child, "pos", { translate(p.x, offset.x),translate(p.y, offset.y),translate(p.z, offset.z) });
		}
		setControllerId(child, controllerIdRemap);
		clonedChildren.push_back(std::move(child));
	}
	std::vector<json> clonedJoints;
	clonedJoints.reserve(clipboard.joints.size());
	for (const auto& clipboardJoint : clipboard.joints) {
		json joint = clipboardJoint.data;
		std::int64_t oldJointId = -1;
		if (joint.contains("id") && joint["id"].is_number_integer())try { oldJointId = joint["id"].get<std::int64_t>(); } catch (...) {}
		const std::int64_t newJointId = allocateId();
		joint["id"] = newJointId;
		if (oldJointId >= 0)jointIdRemap.try_emplace(oldJointId, newJointId);
		setControllerId(joint, controllerIdRemap);
		clonedJoints.push_back(std::move(joint));
	}

	json staged = blueprint;
	PasteSelectionResult stagedResult;
	std::unordered_map<int, int> newFlatBySourceFlat;
	int existingChildCount = 0;
	for (const auto& body : staged["bodies"])if (body.is_object() && body.contains("childs") && body["childs"].is_array())existingChildCount += (int)body["childs"].size();
	const int originalChildCount = existingChildCount;
	for (const auto& [sourceBody, childIndices] : childrenBySourceBody) {
		(void)sourceBody;
		const int newBodyIndex = (int)staged["bodies"].size();
		json newBody = json::object();
		newBody["childs"] = json::array();
		for (size_t clipboardChildIndex : childIndices) {
			const int newChildIndex = (int)newBody["childs"].size();
			const int newFlatIndex = existingChildCount + newChildIndex;
			const auto& source = clipboard.children[clipboardChildIndex];
			newFlatBySourceFlat.emplace(source.sourceFlatIndex, newFlatIndex);
			newBody["childs"].push_back(std::move(clonedChildren[clipboardChildIndex]));
			stagedResult.children.emplace_back(newBodyIndex, newChildIndex);
		}
		existingChildCount += (int)newBody["childs"].size();
		staged["bodies"].push_back(std::move(newBody));
	}

	auto remapControllerState = [&](json& owner) {
		if (!owner.is_object() || !owner.contains("controller") || !owner["controller"].is_object())return;
		auto& controller = owner["controller"];
		const std::int64_t id = controller.value("id", (std::int64_t)-1);
		if (!clipboard.copyBlockState) {
			controller = { {"id", id}, {"controllers", nullptr}, {"joints", nullptr} };
			return;
		}
		if (controller.contains("controllers") && controller["controllers"].is_array()) {
			auto& connections = controller["controllers"];
			for (auto& connection : connections) {
				if (!connection.is_object() || !connection.contains("id") || !connection["id"].is_number_integer())continue;
				try {
					const auto targetId = connection["id"].get<std::int64_t>();
					auto cloned = controllerIdRemap.find(targetId);
					if (cloned != controllerIdRemap.end())connection["id"] = cloned->second;
				} catch (...) {}
			}
		}
		if (controller.contains("joints") && controller["joints"].is_array()) {
			auto& configuredJoints = controller["joints"];
			configuredJoints.erase(std::remove_if(configuredJoints.begin(), configuredJoints.end(), [&](json& reference) {
				if (!reference.is_object() || !reference.contains("id") || !reference["id"].is_number_integer())return false;
				try {
					auto cloned = jointIdRemap.find(reference["id"].get<std::int64_t>());
					if (cloned == jointIdRemap.end())return true;
					reference["id"] = cloned->second;
					return false;
				} catch (...) { return false; }
			}), configuredJoints.end());
		}
	};
	// Blueprint connections are stored only on the source controller. When a
	// copied target has an incoming wire from a source outside the selection,
	// duplicate that reference on the original source so the pasted target keeps
	// the same input. Sources copied in this operation already get their links
	// remapped to the pasted targets above, so leave their originals untouched.
	auto duplicateIncomingControllerReferences = [&](json& owner) {
		if (!clipboard.copyBlockState || !owner.is_object() ||
			!owner.contains("controller") || !owner["controller"].is_object())return;
		auto& controller = owner["controller"];
		const std::int64_t sourceId = integer64(controller, "id");
		if (controllerIdRemap.contains(sourceId) || !controller.contains("controllers") ||
			!controller["controllers"].is_array())return;
		auto& connections = controller["controllers"];
		const auto originalConnections = connections;
		for (const auto& connection : originalConnections) {
			if (!connection.is_object() || !connection.contains("id") ||
				!connection["id"].is_number_integer())continue;
			std::int64_t targetId = -1;
			try { targetId = connection["id"].get<std::int64_t>(); } catch (...) { continue; }
			auto clonedTarget = controllerIdRemap.find(targetId);
			if (clonedTarget == controllerIdRemap.end())continue;
			bool alreadyConnected = false;
			for (const auto& existing : connections) {
				if (!existing.is_object() || !existing.contains("id") ||
					!existing["id"].is_number_integer())continue;
				try {
					alreadyConnected = existing["id"].get<std::int64_t>() == clonedTarget->second;
				} catch (...) {
					continue;
				}
				if (alreadyConnected)break;
			}
			if (alreadyConnected)continue;
			json clonedConnection = connection;
			clonedConnection["id"] = clonedTarget->second;
			connections.push_back(std::move(clonedConnection));
		}
	};
	auto remapChildJointReferences = [&](json& child) {
		if (!child.is_object() || !child.contains("joints") || !child["joints"].is_array())return;
		auto& references = child["joints"];
		references.erase(std::remove_if(references.begin(), references.end(), [&](json& reference) {
			if (!reference.is_object() || !reference.contains("id") || !reference["id"].is_number_integer())return false;
			try {
				auto cloned = jointIdRemap.find(reference["id"].get<std::int64_t>());
				if (cloned == jointIdRemap.end())return true;
				reference["id"] = cloned->second;
				return false;
			} catch (...) { return false; }
		}), references.end());
		if (references.empty())child.erase("joints");
	};
	auto ensureJointReference = [](json& child, std::int64_t id) {
		if (!child.is_object())return;
		if (!child.contains("joints") || !child["joints"].is_array())child["joints"] = json::array();
		auto& references = child["joints"];
		for (const auto& reference : references)if (reference.is_object() && reference.contains("id") && reference["id"].is_number_integer())
			try { if (reference["id"].get<std::int64_t>() == id)return; } catch (...) {}
		references.push_back({ {"id", id} });
	};
	auto childByFlatIndex = [&](int flat) -> json* {
		for (int bodyIndex = 0; bodyIndex < (int)staged["bodies"].size(); ++bodyIndex) {
			auto& body = staged["bodies"][bodyIndex];
			if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array())continue;
			for (int childIndex = 0; childIndex < (int)body["childs"].size(); ++childIndex)
				if (flatChildIndex(staged, bodyIndex, childIndex) == flat)return &body["childs"][childIndex];
		}
		return nullptr;
	};
	std::unordered_set<int> pastedFlatIndices;
	for (const auto& [sourceFlat, pastedFlat] : newFlatBySourceFlat) { (void)sourceFlat; pastedFlatIndices.insert(pastedFlat); }
	for (int bodyIndex = 0; bodyIndex < (int)staged["bodies"].size(); ++bodyIndex) {
		auto& body = staged["bodies"][bodyIndex];
		if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array())continue;
		for (int childIndex = 0; childIndex < (int)body["childs"].size(); ++childIndex) {
			auto& child = body["childs"][childIndex];
			if (!pastedFlatIndices.contains(flatChildIndex(staged, bodyIndex, childIndex))) {
				duplicateIncomingControllerReferences(child);
				continue;
			}
			remapChildJointReferences(child);
			remapControllerState(child);
		}
	}
	if (staged.contains("joints") && staged["joints"].is_array())
		for (size_t jointIndex = 0; jointIndex < staged["joints"].size(); ++jointIndex)
			duplicateIncomingControllerReferences(staged["joints"][jointIndex]);
	for (auto& joint : clonedJoints)remapControllerState(joint);
	std::vector<std::pair<int, std::int64_t>> originalEndpointReferences;
	for (size_t i = 0; i < clipboard.joints.size(); ++i) {
		const auto& source = clipboard.joints[i];
		auto& joint = clonedJoints[i];
		for (const auto& endpoint : { std::pair<const char*, int>{"A",source.sourceChildA},std::pair<const char*, int>{"B",source.sourceChildB} }) {
			const char* childKey = endpoint.first == std::string("A") ? "childA" : "childB";
			const char* posKey = endpoint.first == std::string("A") ? "posA" : "posB";
			auto copiedChild = clipboardChildBySourceFlat.find(endpoint.second);
			if (copiedChild != clipboardChildBySourceFlat.end()) {
				auto pastedFlat = newFlatBySourceFlat.find(endpoint.second);
				if (pastedFlat == newFlatBySourceFlat.end())return false;
				joint[childKey] = pastedFlat->second;
				if (joint.contains(posKey) && joint[posKey].is_object()) {
					const auto p = position(joint[posKey]);
					writePosition(joint, posKey, { translate(p.x, offset.x),translate(p.y, offset.y),translate(p.z, offset.z) });
				}
				auto* pastedChild = childByFlatIndex(pastedFlat->second);
				if (!pastedChild)return false;
				ensureJointReference(*pastedChild, joint["id"].get<std::int64_t>());
			}
			else {
				joint[childKey] = endpoint.second;
				if (endpoint.second >= 0 && endpoint.second < originalChildCount)
					originalEndpointReferences.emplace_back(endpoint.second, joint["id"].get<std::int64_t>());
			}
		}
	}
	for (const auto& [flat, jointId] : originalEndpointReferences) {
		json* child = childByFlatIndex(flat);
		if (!child)return false;
		ensureJointReference(*child, jointId);
	}
	if (!clonedJoints.empty()) {
		if (!staged.contains("joints") || staged["joints"].is_null())staged["joints"] = json::array();
		if (!staged["joints"].is_array())return false;
		for (auto& joint : clonedJoints) {
			stagedResult.joints.push_back((int)staged["joints"].size());
			staged["joints"].push_back(std::move(joint));
		}
	}
	blueprint = std::move(staged);
	result = std::move(stagedResult);
	return true;
}

bool Parser::addDirectedConnection(json& blueprint, const std::vector<Block>& blocks, int sourceIndex, int targetIndex) {
	if (sourceIndex < 0 || targetIndex < 0 || sourceIndex >= (int)blocks.size() || targetIndex >= (int)blocks.size()) return false;
	auto controllerFor = [&](const Block& block) -> json* {
		if (block.isJoint || block.bodyIndex < 0 || block.childIndex < 0 || !blueprint.is_object() || !blueprint.contains("bodies") || !blueprint["bodies"].is_array() || block.bodyIndex >= (int)blueprint["bodies"].size()) return nullptr;
		auto& body = blueprint["bodies"][block.bodyIndex];
		if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() || block.childIndex >= (int)body["childs"].size()) return nullptr;
		auto& child = body["childs"][block.childIndex];
		if (!child.is_object() || !child.contains("controller") || !child["controller"].is_object()) return nullptr;
		return &child["controller"];
	};
	json* source = controllerFor(blocks[sourceIndex]);
	json* target = controllerFor(blocks[targetIndex]);
	if (!source || !target || !source->contains("id") || !target->contains("id") || !(*source)["id"].is_number_integer() || !(*target)["id"].is_number_integer()) return false;
	std::int64_t sourceId, targetId;
	try { sourceId = (*source)["id"].get<std::int64_t>(); targetId = (*target)["id"].get<std::int64_t>(); } catch (...) { return false; }
	if (sourceId < 0 || targetId < 0 || (sourceIndex != targetIndex && sourceId == targetId)) return false;
	if (!source->contains("controllers") || (*source)["controllers"].is_null()) (*source)["controllers"] = json::array();
	if (!(*source)["controllers"].is_array()) return false;
	for (const auto& connection : (*source)["controllers"]) if (connection.is_object() && connection.contains("id") && connection["id"].is_number_integer())
		try { if (connection["id"].get<std::int64_t>() == targetId) return false; } catch (...) {}
	(*source)["controllers"].push_back({ { "id", targetId } });
	return true;
}

bool Parser::removeConnectionById(json& blueprint, const std::vector<Block>& blocks, int sourceIndex, std::int64_t targetControllerId) {
	if (sourceIndex < 0 || sourceIndex >= (int)blocks.size() || targetControllerId < 0) return false;
	auto& sourceBlock = blocks[sourceIndex];
	if (sourceBlock.isJoint || sourceBlock.bodyIndex < 0 || sourceBlock.childIndex < 0 || !blueprint.is_object() || !blueprint.contains("bodies") || !blueprint["bodies"].is_array() || sourceBlock.bodyIndex >= (int)blueprint["bodies"].size()) return false;
	auto& body = blueprint["bodies"][sourceBlock.bodyIndex];
	if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() || sourceBlock.childIndex >= (int)body["childs"].size()) return false;
	auto& child = body["childs"][sourceBlock.childIndex];
	if (!child.is_object() || !child.contains("controller") || !child["controller"].is_object()) return false;
	auto& controller = child["controller"];
	if (!controller.contains("controllers") || !controller["controllers"].is_array()) return false;
	auto& connections = controller["controllers"];
	const auto oldSize = connections.size();
	connections.erase(std::remove_if(connections.begin(), connections.end(), [&](const json& connection) {
		if (!connection.is_object() || !connection.contains("id") || !connection["id"].is_number_integer()) return false;
		try { return connection["id"].get<std::int64_t>() == targetControllerId; } catch (...) { return false; }
	}), connections.end());
	return connections.size() != oldSize;
}

bool Parser::removeBlockFromNode(json& blueprint, int bodyIndex, int childIndex) {
	if (!blueprint.is_object() || !blueprint.contains("bodies") || !blueprint["bodies"].is_array() || bodyIndex < 0 || bodyIndex >= (int)blueprint["bodies"].size()) return false;
	auto& bodies = blueprint["bodies"];
	auto& body = bodies[bodyIndex];
	if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() || childIndex < 0 || childIndex >= (int)body["childs"].size()) return false;
	const int flat = flatChildIndex(blueprint, bodyIndex, childIndex);
	std::unordered_set<std::int64_t> removedIds;
	std::unordered_set<std::int64_t> removedControllerIds;
	const auto& child = body["childs"][childIndex];
	if (child.is_object() && child.contains("controller") && child["controller"].is_object() &&
		child["controller"].contains("id") && child["controller"]["id"].is_number_integer())
		try { removedControllerIds.insert(child["controller"]["id"].get<std::int64_t>()); } catch (...) {}
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
		if (!removedControllerIds.empty() && currentChild.is_object() && currentChild.contains("controller") && currentChild["controller"].is_object() &&
			currentChild["controller"].contains("controllers") && currentChild["controller"]["controllers"].is_array()) {
			auto& connections = currentChild["controller"]["controllers"];
			connections.erase(std::remove_if(connections.begin(), connections.end(), [&](const json& connection) {
				if (!connection.is_object() || !connection.contains("id") || !connection["id"].is_number_integer()) return false;
				try { return removedControllerIds.contains(connection["id"].get<std::int64_t>()); } catch (...) { return false; }
			}), connections.end());
		}
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
