#include "scrap_parser_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_set>

using Parser::Internal::flatChildIndex;

bool Parser::removeJointFromNode(json& blueprint, int jointIndex) {
    if (!blueprint.is_object() || !blueprint.contains("joints") ||
        !blueprint["joints"].is_array() || jointIndex < 0 ||
        jointIndex >= (int)blueprint["joints"].size())
        return false;
    const auto removed = blueprint["joints"][jointIndex];
    std::unordered_set<std::int64_t> ids;
    if (removed.is_object() && removed.contains("id") && removed["id"].is_number_integer())
        ids.insert(removed["id"].get<std::int64_t>());
    if (!ids.empty() && blueprint.contains("bodies") && blueprint["bodies"].is_array())
        for (auto& body : blueprint["bodies"]) {
            if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array())
                continue;
            for (auto& child : body["childs"]) {
                if (!child.is_object() || !child.contains("joints") || !child["joints"].is_array())
                    continue;
                auto& refs = child["joints"];
                refs.erase(std::remove_if(refs.begin(),
                                          refs.end(),
                                          [&](const json& ref) {
                                              return ref.is_object() && ref.contains("id") &&
                                                     ref["id"].is_number_integer() &&
                                                     ids.contains(ref["id"].get<std::int64_t>());
                                          }),
                           refs.end());
                if (refs.empty())
                    child.erase("joints");
            }
        }
    blueprint["joints"].erase(blueprint["joints"].begin() + jointIndex);
    return true;
}

bool Parser::removeBlockFromNode(json& blueprint, int bodyIndex, int childIndex) {
    if (!blueprint.is_object() || !blueprint.contains("bodies") ||
        !blueprint["bodies"].is_array() || bodyIndex < 0 ||
        bodyIndex >= (int)blueprint["bodies"].size())
        return false;
    auto& bodies = blueprint["bodies"];
    auto& body = bodies[bodyIndex];
    if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() ||
        childIndex < 0 || childIndex >= (int)body["childs"].size())
        return false;
    const int flat = flatChildIndex(blueprint, bodyIndex, childIndex);
    std::unordered_set<std::int64_t> removedIds;
    std::unordered_set<std::int64_t> removedControllerIds;
    const auto& child = body["childs"][childIndex];
    if (child.is_object() && child.contains("controller") && child["controller"].is_object() &&
        child["controller"].contains("id") && child["controller"]["id"].is_number_integer())
        try {
            removedControllerIds.insert(child["controller"]["id"].get<std::int64_t>());
        } catch (...) {
        }
    if (child.is_object() && child.contains("joints") && child["joints"].is_array())
        for (const auto& ref : child["joints"])
            if (ref.is_object() && ref.contains("id") && ref["id"].is_number_integer())
                removedIds.insert(ref["id"].get<std::int64_t>());
    if (blueprint.contains("joints") && blueprint["joints"].is_array()) {
        auto& joints = blueprint["joints"];
        for (const auto& joint : joints)
            if (joint.is_object() &&
                ((joint.contains("childA") && joint["childA"].is_number_integer() &&
                  joint["childA"].get<int>() == flat) ||
                 (joint.contains("childB") && joint["childB"].is_number_integer() &&
                  joint["childB"].get<int>() == flat)) &&
                joint.contains("id") && joint["id"].is_number_integer())
                removedIds.insert(joint["id"].get<std::int64_t>());
        joints.erase(
            std::remove_if(
                joints.begin(),
                joints.end(),
                [&](const json& joint) {
                    return joint.is_object() &&
                           ((joint.contains("childA") && joint["childA"].is_number_integer() &&
                             joint["childA"].get<int>() == flat) ||
                            (joint.contains("childB") && joint["childB"].is_number_integer() &&
                             joint["childB"].get<int>() == flat) ||
                            (joint.contains("id") && joint["id"].is_number_integer() &&
                             removedIds.contains(joint["id"].get<std::int64_t>())));
                }),
            joints.end());
        for (auto& joint : joints)
            if (joint.is_object())
                for (const char* key : {"childA", "childB"})
                    if (joint.contains(key) && joint[key].is_number_integer() &&
                        joint[key].get<int>() > flat)
                        joint[key] = joint[key].get<int>() - 1;
    }
    for (auto& currentBody : bodies)
        if (currentBody.is_object() && currentBody.contains("childs") &&
            currentBody["childs"].is_array())
            for (auto& currentChild : currentBody["childs"]) {
                if (!removedControllerIds.empty() && currentChild.is_object() &&
                    currentChild.contains("controller") && currentChild["controller"].is_object() &&
                    currentChild["controller"].contains("controllers") &&
                    currentChild["controller"]["controllers"].is_array()) {
                    auto& connections = currentChild["controller"]["controllers"];
                    connections.erase(
                        std::remove_if(connections.begin(),
                                       connections.end(),
                                       [&](const json& connection) {
                                           if (!connection.is_object() ||
                                               !connection.contains("id") ||
                                               !connection["id"].is_number_integer())
                                               return false;
                                           try {
                                               return removedControllerIds.contains(
                                                   connection["id"].get<std::int64_t>());
                                           } catch (...) {
                                               return false;
                                           }
                                       }),
                        connections.end());
                }
                if (!currentChild.is_object() || !currentChild.contains("joints") ||
                    !currentChild["joints"].is_array())
                    continue;
                auto& refs = currentChild["joints"];
                refs.erase(std::remove_if(refs.begin(),
                                          refs.end(),
                                          [&](const json& ref) {
                                              return ref.is_object() && ref.contains("id") &&
                                                     ref["id"].is_number_integer() &&
                                                     removedIds.contains(
                                                         ref["id"].get<std::int64_t>());
                                          }),
                           refs.end());
                if (refs.empty())
                    currentChild.erase("joints");
            }
    body["childs"].erase(body["childs"].begin() + childIndex);
    return true;
}
