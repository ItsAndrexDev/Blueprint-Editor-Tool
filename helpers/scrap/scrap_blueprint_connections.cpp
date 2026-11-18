#include "scrap_parser_internal.hpp"

#include <cstdint>
#include <limits>

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace {
constexpr const char* pistonShapeId = "260b4597-f1ac-409c-8e6b-90c998c5fe94";

bool isPistonShape(const std::string& shapeId) {
    if (shapeId.size() != std::char_traits<char>::length(pistonShapeId))
        return false;
    return std::equal(shapeId.begin(),
                       shapeId.end(),
                       pistonShapeId,
                       [](unsigned char left, unsigned char right) {
                           return std::tolower(left) == std::tolower(right);
                       });
}
}

bool Parser::addDirectedConnection(json& blueprint,
                                   const std::vector<Block>& blocks,
                                   int sourceIndex,
                                   int targetIndex) {
    if (sourceIndex < 0 || targetIndex < 0 || sourceIndex >= (int)blocks.size() ||
        targetIndex >= (int)blocks.size())
        return false;
    if (isPistonShape(blocks[sourceIndex].shapeID))
        return false;
    auto controllerFor = [&](const Block& block) -> json* {
        if (!blueprint.is_object())
            return nullptr;
        if (block.isJoint) {
            if (!isPistonShape(block.shapeID) || block.jointIndex < 0 ||
                !blueprint.contains("joints") || !blueprint["joints"].is_array() ||
                block.jointIndex >= (int)blueprint["joints"].size())
                return nullptr;
            auto& joint = blueprint["joints"][block.jointIndex];
            return joint.is_object() && joint.contains("controller") &&
                           joint["controller"].is_object()
                       ? &joint["controller"]
                       : nullptr;
        }
        if (block.bodyIndex < 0 || block.childIndex < 0 || !blueprint.contains("bodies") ||
            !blueprint["bodies"].is_array() || block.bodyIndex >= (int)blueprint["bodies"].size())
            return nullptr;
        auto& body = blueprint["bodies"][block.bodyIndex];
        if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() ||
            block.childIndex >= (int)body["childs"].size())
            return nullptr;
        auto& child = body["childs"][block.childIndex];
        if (!child.is_object() || !child.contains("controller") || !child["controller"].is_object())
            return nullptr;
        return &child["controller"];
    };
    json* source = controllerFor(blocks[sourceIndex]);
    json* target = controllerFor(blocks[targetIndex]);
    const auto& targetBlock = blocks[targetIndex];
    if (!target && targetBlock.isJoint && isPistonShape(targetBlock.shapeID) &&
        targetBlock.jointIndex >= 0 && blueprint.contains("joints") &&
        blueprint["joints"].is_array() &&
        targetBlock.jointIndex < (int)blueprint["joints"].size()) {
        auto& joint = blueprint["joints"][targetBlock.jointIndex];
        if (joint.is_object()) {
            joint["controller"] = json::object();
            target = &joint["controller"];
        }
    }
    if (!source || !target || !source->contains("id") || !(*source)["id"].is_number_integer())
        return false;
    std::int64_t sourceId;
    try {
        sourceId = (*source)["id"].get<std::int64_t>();
    } catch (...) {
        return false;
    }
    if (sourceId < 0)
        return false;
    auto readId = [](const json& value, std::unordered_set<std::int64_t>& ids) {
        if (!value.is_number_integer())
            return;
        try {
            const auto id = value.get<std::int64_t>();
            if (id >= 0)
                ids.insert(id);
        } catch (...) {
        }
    };
    std::unordered_set<std::int64_t> usedIds;
    if (blueprint.contains("bodies") && blueprint["bodies"].is_array())
        for (const auto& body : blueprint["bodies"])
            if (body.is_object() && body.contains("childs") && body["childs"].is_array())
                for (const auto& child : body["childs"])
                    if (child.is_object() && child.contains("controller") &&
                        child["controller"].is_object() && child["controller"].contains("id"))
                        readId(child["controller"]["id"], usedIds);
    if (blueprint.contains("joints") && blueprint["joints"].is_array())
        for (const auto& joint : blueprint["joints"])
            if (joint.is_object()) {
                if (joint.contains("id"))
                    readId(joint["id"], usedIds);
                if (joint.contains("controller") && joint["controller"].is_object() &&
                    joint["controller"].contains("id"))
                    readId(joint["controller"]["id"], usedIds);
            }
    readId((*source)["id"], usedIds);
    std::int64_t targetId = -1;
    if (target->contains("id") && (*target)["id"].is_number_integer())
        try {
            targetId = (*target)["id"].get<std::int64_t>();
        } catch (...) {
        }
    if (targetId < 0) {
        std::int64_t candidate = 1;
        while (usedIds.contains(candidate) && candidate < std::numeric_limits<std::int64_t>::max())
            ++candidate;
        if (usedIds.contains(candidate))
            return false;
        targetId = candidate;
        (*target)["id"] = targetId;
    }
    if (sourceIndex != targetIndex && sourceId == targetId)
        return false;
    if (!source->contains("controllers") || (*source)["controllers"].is_null())
        (*source)["controllers"] = json::array();
    if (!(*source)["controllers"].is_array())
        return false;
    for (const auto& connection : (*source)["controllers"])
        if (connection.is_object() && connection.contains("id") &&
            connection["id"].is_number_integer())
            try {
                if (connection["id"].get<std::int64_t>() == targetId)
                    return false;
            } catch (...) {
            }
    (*source)["controllers"].push_back({{"id", targetId}});
    return true;
}

bool Parser::addJointConnection(json& blueprint,
                                const std::vector<Block>& blocks,
                                int sourceIndex,
                                int targetIndex,
                                bool controllerSequence) {
    if (sourceIndex < 0 || targetIndex < 0 || sourceIndex >= (int)blocks.size() ||
        targetIndex >= (int)blocks.size() || blocks[sourceIndex].isJoint ||
        !blocks[targetIndex].isJoint || !blueprint.is_object() ||
        !blueprint.contains("bodies") || !blueprint["bodies"].is_array() ||
        !blueprint.contains("joints") || !blueprint["joints"].is_array())
        return false;

    const auto& sourceBlock = blocks[sourceIndex];
    const auto& targetBlock = blocks[targetIndex];
    if (sourceBlock.bodyIndex < 0 || sourceBlock.childIndex < 0 ||
        sourceBlock.bodyIndex >= (int)blueprint["bodies"].size() || targetBlock.jointIndex < 0 ||
        targetBlock.jointIndex >= (int)blueprint["joints"].size())
        return false;

    auto& body = blueprint["bodies"][sourceBlock.bodyIndex];
    if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() ||
        sourceBlock.childIndex >= (int)body["childs"].size())
        return false;
    auto& child = body["childs"][sourceBlock.childIndex];
    if (!child.is_object() || !child.contains("controller") ||
        !child["controller"].is_object() || !child["controller"].contains("id") ||
        !child["controller"]["id"].is_number_integer())
        return false;

    auto& joint = blueprint["joints"][targetBlock.jointIndex];
    if (!joint.is_object() || !joint.contains("id") || !joint["id"].is_number_integer())
        return false;
    std::int64_t targetJointId = -1;
    try {
        targetJointId = joint["id"].get<std::int64_t>();
    } catch (...) {
        return false;
    }
    if (targetJointId < 0)
        return false;

    auto& controller = child["controller"];
    if (!controller.contains("joints") || controller["joints"].is_null())
        controller["joints"] = json::array();
    if (!controller["joints"].is_array())
        return false;
    auto& connections = controller["joints"];
    for (const auto& connection : connections)
        if (connection.is_object() && connection.contains("id") &&
            connection["id"].is_number_integer())
            try {
                if (connection["id"].get<std::int64_t>() == targetJointId)
                    return false;
            } catch (...) {
            }
    std::int64_t order = static_cast<std::int64_t>(connections.size());
    for (const auto& connection : connections)
        if (connection.is_object() && connection.contains("index") &&
            connection["index"].is_number_integer())
            try {
                const auto existingIndex = connection["index"].get<std::int64_t>();
                if (existingIndex >= 0 && existingIndex < std::numeric_limits<std::int64_t>::max())
                    order = std::max(order, existingIndex + 1);
            } catch (...) {
            }
    const bool piston = joint.contains("shapeId") && joint["shapeId"].is_string() &&
                        isPistonShape(joint["shapeId"].get<std::string>());
    json connection{{"id", targetJointId}, {"index", order}, {"reverse", false}};
    if (!piston) {
        connection["startAngle"] = 0;
        connection["endAngle"] = 0;
    }
    if (controllerSequence) {
        connection["frames"] = json::array();
        for (int frame = 0; frame < 11; ++frame)
            connection["frames"].push_back(
                piston ? json{{"targetLength", 0}} : json{{"targetAngle", 0}});
    }
    connections.push_back(std::move(connection));
    return true;
}

bool Parser::removeConnectionById(json& blueprint,
                                  const std::vector<Block>& blocks,
                                  int sourceIndex,
                                  std::int64_t targetControllerId) {
    if (sourceIndex < 0 || sourceIndex >= (int)blocks.size() || targetControllerId < 0)
        return false;
    auto& sourceBlock = blocks[sourceIndex];
    if (sourceBlock.isJoint || sourceBlock.bodyIndex < 0 || sourceBlock.childIndex < 0 ||
        !blueprint.is_object() || !blueprint.contains("bodies") ||
        !blueprint["bodies"].is_array() || sourceBlock.bodyIndex >= (int)blueprint["bodies"].size())
        return false;
    auto& body = blueprint["bodies"][sourceBlock.bodyIndex];
    if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() ||
        sourceBlock.childIndex >= (int)body["childs"].size())
        return false;
    auto& child = body["childs"][sourceBlock.childIndex];
    if (!child.is_object() || !child.contains("controller") || !child["controller"].is_object())
        return false;
    auto& controller = child["controller"];
    if (!controller.contains("controllers") || !controller["controllers"].is_array())
        return false;
    auto& connections = controller["controllers"];
    const auto oldSize = connections.size();
    connections.erase(
        std::remove_if(connections.begin(),
                       connections.end(),
                       [&](const json& connection) {
                           if (!connection.is_object() || !connection.contains("id") ||
                               !connection["id"].is_number_integer())
                               return false;
                           try {
                               return connection["id"].get<std::int64_t>() == targetControllerId;
                           } catch (...) {
                               return false;
                           }
                       }),
        connections.end());
    return connections.size() != oldSize;
}

bool Parser::removeJointConnectionById(json& blueprint,
                                       const std::vector<Block>& blocks,
                                       int sourceIndex,
                                       std::int64_t targetJointId) {
    if (sourceIndex < 0 || sourceIndex >= (int)blocks.size() || targetJointId < 0)
        return false;
    const auto& sourceBlock = blocks[sourceIndex];
    if (sourceBlock.isJoint || sourceBlock.bodyIndex < 0 || sourceBlock.childIndex < 0 ||
        !blueprint.is_object() || !blueprint.contains("bodies") ||
        !blueprint["bodies"].is_array() || sourceBlock.bodyIndex >= (int)blueprint["bodies"].size())
        return false;
    auto& body = blueprint["bodies"][sourceBlock.bodyIndex];
    if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() ||
        sourceBlock.childIndex >= (int)body["childs"].size())
        return false;
    auto& child = body["childs"][sourceBlock.childIndex];
    if (!child.is_object() || !child.contains("controller") || !child["controller"].is_object() ||
        !child["controller"].contains("joints") || !child["controller"]["joints"].is_array())
        return false;
    auto& connections = child["controller"]["joints"];
    const auto oldSize = connections.size();
    connections.erase(
        std::remove_if(connections.begin(),
                       connections.end(),
                       [&](const json& connection) {
                           if (!connection.is_object() || !connection.contains("id") ||
                               !connection["id"].is_number_integer())
                               return false;
                           try {
                               return connection["id"].get<std::int64_t>() == targetJointId;
                           } catch (...) {
                               return false;
                           }
                       }),
        connections.end());
    return connections.size() != oldSize;
}
