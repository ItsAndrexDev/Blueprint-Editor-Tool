#include "scrap_parser_internal.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <utility>

using Parser::Internal::flatChildIndex;
using Parser::Internal::integer;
using Parser::Internal::integer64;
using Parser::Internal::position;
using Parser::Internal::writePosition;

namespace {
using IVec3 = std::array<int, 3>;

IVec3 axis(int code) {
    switch (code) {
    case 1:
        return {1, 0, 0};
    case -1:
        return {-1, 0, 0};
    case 2:
        return {0, 1, 0};
    case -2:
        return {0, -1, 0};
    case 3:
        return {0, 0, 1};
    case -3:
        return {0, 0, -1};
    default:
        return {1, 0, 0};
    }
}

IVec3 cross(const IVec3& a, const IVec3& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

int dot(const IVec3& a, const IVec3& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

struct Basis {
    IVec3 x, y, z;
};
Basis basis(int xCode, int zCode) {
    Basis result{axis(xCode), {}, axis(zCode)};
    result.y = cross(result.z, result.x);
    return result;
}

IVec3 toLocal(const IVec3& world, const Basis& frame) {
    return {dot(world, frame.x), dot(world, frame.y), dot(world, frame.z)};
}
IVec3 toWorld(const IVec3& local, const Basis& frame) {
    return {frame.x[0] * local[0] + frame.y[0] * local[1] + frame.z[0] * local[2],
            frame.x[1] * local[0] + frame.y[1] * local[1] + frame.z[1] * local[2],
            frame.x[2] * local[0] + frame.y[2] * local[1] + frame.z[2] * local[2]};
}

bool childTransformChanged(const json& child, const Parser::Block& block) {
    const json empty = json::object();
    const auto& p = child.contains("pos") && child["pos"].is_object() ? child["pos"] : empty;
    return integer(p, "x") != block.pos.x || integer(p, "y") != block.pos.y ||
           integer(p, "z") != block.pos.z || integer(child, "xaxis", 1) != block.xaxis ||
           integer(child, "zaxis", 3) != block.zaxis;
}

void moveAttachedEndpoint(json& joint,
                          const char* side,
                          int flatIndex,
                          const json& originalChild,
                          const Parser::Block& edited) {
    const char* childKey = side[0] == 'A' ? "childA" : "childB";
    if (!joint.is_object() || !joint.contains(childKey) || !joint[childKey].is_number_integer() ||
        joint[childKey].get<int>() != flatIndex)
        return;
    const std::string posKey = std::string("pos") + side;
    if (!joint.contains(posKey) || !joint[posKey].is_object())
        return;
    const Parser::Position oldPos = position(originalChild.value("pos", json::object()));
    const Parser::Position endpoint = position(joint[posKey]);
    const auto oldFrame =
        basis(integer(originalChild, "xaxis", 1), integer(originalChild, "zaxis", 3));
    const auto newFrame = basis(edited.xaxis, edited.zaxis);
    const IVec3 relative{endpoint.x - oldPos.x, endpoint.y - oldPos.y, endpoint.z - oldPos.z};
    const auto relocated = toWorld(toLocal(relative, oldFrame), newFrame);
    writePosition(
        joint,
        posKey,
        {edited.pos.x + relocated[0], edited.pos.y + relocated[1], edited.pos.z + relocated[2]});
}

bool validAxis(int value) {
    return value == 1 || value == -1 || value == 2 || value == -2 || value == 3 || value == -3;
}
void validateOrientation(Parser::Block& block) {
    if (!validAxis(block.xaxis))
        block.xaxis = 1;
    if (!validAxis(block.zaxis) || std::abs(dot(axis(block.xaxis), axis(block.zaxis))) == 1)
        block.zaxis = 3;
}
} // namespace

std::vector<Parser::Block> Parser::parseBlueprint(const json& source) {
    std::vector<Block> result;
    if (!source.is_object() || !source.contains("bodies") || !source["bodies"].is_array())
        return result;
    for (int bodyIndex = 0; bodyIndex < (int)source["bodies"].size(); ++bodyIndex) {
        const auto& body = source["bodies"][bodyIndex];
        if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array())
            continue;
        for (int childIndex = 0; childIndex < (int)body["childs"].size(); ++childIndex) {
            const auto& child = body["childs"][childIndex];
            if (!child.is_object() || !child.contains("shapeId") || !child["shapeId"].is_string())
                continue;
            result.push_back(parseBlock(child, bodyIndex, childIndex));
        }
    }
    if (source.contains("joints") && source["joints"].is_array()) {
        for (int i = 0; i < (int)source["joints"].size(); ++i) {
            const auto& joint = source["joints"][i];
            if (!joint.is_object() || !joint.contains("shapeId") || !joint["shapeId"].is_string() ||
                !joint.contains("posA") || !joint["posA"].is_object() || !joint.contains("posB") ||
                !joint["posB"].is_object())
                continue;
            Block block;
            block.isJoint = true;
            block.jointIndex = i;
            block.jointPosA = position(joint["posA"]);
            block.jointPosB = position(joint["posB"]);
            // Joint renderables are placed from posA in Scrap Mechanic's model
            // transform. posB describes the opposite connection on the attached part.
            block.pos = block.jointPosA;
            block.shapeID = joint["shapeId"].get<std::string>();
            block.xaxis = integer(joint, "xaxisA", 1);
            block.zaxis = integer(joint, "zaxisA", 3);
            block.jointChildA = integer(joint, "childA", -1);
            block.jointChildB = integer(joint, "childB", -1);
            block.jointId = integer64(joint, "id");
            if (joint.contains("color") && joint["color"].is_string()) {
                block.color = joint["color"].get<std::string>();
                block.hasColor = true;
            }
            validateOrientation(block);
            result.push_back(std::move(block));
        }
    }
    return result;
}

Parser::Block Parser::parseBlock(const json& source, int bodyIndex, int childIndex) {
    Block block;
    block.bodyIndex = bodyIndex;
    block.childIndex = childIndex;
    if (!source.is_object())
        return block;
    block.pos = position(source.value("pos", json::object()));
    if (source.contains("shapeId") && source["shapeId"].is_string())
        block.shapeID = source["shapeId"].get<std::string>();
    block.xaxis = integer(source, "xaxis", 1);
    block.zaxis = integer(source, "zaxis", 3);
    if (source.contains("bounds") && source["bounds"].is_object()) {
        block.hasBounds = true;
        block.bounds = {std::max(1, integer(source["bounds"], "x", 1)),
                        std::max(1, integer(source["bounds"], "y", 1)),
                        std::max(1, integer(source["bounds"], "z", 1))};
    }
    if (source.contains("color") && source["color"].is_string()) {
        block.color = source["color"].get<std::string>();
        block.hasColor = true;
    }
    validateOrientation(block);
    return block;
}

void Parser::applyBlockListToNode(const std::vector<Block>& blocks, json& blueprint) {
    if (!blueprint.is_object() || !blueprint.contains("bodies") || !blueprint["bodies"].is_array())
        return;
    const bool hasJointArray = blueprint.contains("joints") && blueprint["joints"].is_array();
    for (const auto& block : blocks) {
        if (block.isJoint)
            continue;
        if (block.bodyIndex < 0 || block.childIndex < 0 ||
            block.bodyIndex >= (int)blueprint["bodies"].size())
            continue;
        auto& body = blueprint["bodies"][block.bodyIndex];
        if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() ||
            block.childIndex >= (int)body["childs"].size())
            continue;
        auto& child = body["childs"][block.childIndex];
        if (!child.is_object())
            continue;
        const bool moved = childTransformChanged(child, block);
        const int flat = flatChildIndex(blueprint, block.bodyIndex, block.childIndex);
        if (moved && flat >= 0 && hasJointArray)
            for (auto& joint : blueprint["joints"]) {
                moveAttachedEndpoint(joint, "A", flat, child, block);
                moveAttachedEndpoint(joint, "B", flat, child, block);
            }
        writePosition(child, "pos", block.pos);
        child["xaxis"] = block.xaxis;
        child["zaxis"] = block.zaxis;
        child["shapeId"] = block.shapeID;
        if (block.hasBounds)
            writePosition(child, "bounds", block.bounds);
        else
            child.erase("bounds");
        if (block.hasColor)
            child["color"] = block.color;
        else
            child.erase("color");
    }
    for (const auto& block : blocks) {
        if (!block.isJoint || block.jointIndex < 0 ||
            block.jointIndex >= (int)blueprint["joints"].size())
            continue;
        auto& joint = blueprint["joints"][block.jointIndex];
        if (!joint.is_object())
            continue;
        const int dx = block.pos.x - block.jointPosA.x, dy = block.pos.y - block.jointPosA.y,
                  dz = block.pos.z - block.jointPosA.z;
        if (dx || dy || dz) {
            for (const char* endpoint : {"A", "B"}) {
                const std::string key = std::string("pos") + endpoint;
                const auto original = endpoint[0] == 'A' ? block.jointPosA : block.jointPosB;
                const auto current = joint.contains(key) && joint[key].is_object()
                                         ? position(joint[key], original)
                                         : original;
                writePosition(joint, key, {current.x + dx, current.y + dy, current.z + dz});
            }
        }
        if (block.hasColor)
            joint["color"] = block.color;
    }
}
