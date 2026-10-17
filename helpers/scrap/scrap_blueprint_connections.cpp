#include "scrap_parser_internal.hpp"

#include <cstdint>

#include <algorithm>

bool Parser::addDirectedConnection(json& blueprint,
                                   const std::vector<Block>& blocks,
                                   int sourceIndex,
                                   int targetIndex) {
    if (sourceIndex < 0 || targetIndex < 0 || sourceIndex >= (int)blocks.size() ||
        targetIndex >= (int)blocks.size())
        return false;
    auto controllerFor = [&](const Block& block) -> json* {
        if (block.isJoint || block.bodyIndex < 0 || block.childIndex < 0 ||
            !blueprint.is_object() || !blueprint.contains("bodies") ||
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
    if (!source || !target || !source->contains("id") || !target->contains("id") ||
        !(*source)["id"].is_number_integer() || !(*target)["id"].is_number_integer())
        return false;
    std::int64_t sourceId, targetId;
    try {
        sourceId = (*source)["id"].get<std::int64_t>();
        targetId = (*target)["id"].get<std::int64_t>();
    } catch (...) {
        return false;
    }
    if (sourceId < 0 || targetId < 0 || (sourceIndex != targetIndex && sourceId == targetId))
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
