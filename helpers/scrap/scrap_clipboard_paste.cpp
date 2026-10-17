#include "scrap_parser_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

using Parser::Internal::flatChildIndex;
using Parser::Internal::integer64;
using Parser::Internal::position;
using Parser::Internal::writePosition;

bool Parser::pasteSelectionClipboard(json& blueprint,
                                     const SelectionClipboard& clipboard,
                                     Position offset,
                                     PasteSelectionResult& result) {
    result = {};
    if (clipboard.empty() || !blueprint.is_object() || !blueprint.contains("bodies") ||
        !blueprint["bodies"].is_array())
        return false;
    if (!clipboard.joints.empty() && blueprint.contains("joints") &&
        !blueprint["joints"].is_array() && !blueprint["joints"].is_null())
        return false;

    std::map<int, std::vector<size_t>> childrenBySourceBody;
    std::unordered_map<int, size_t> clipboardChildBySourceFlat;
    for (size_t i = 0; i < clipboard.children.size(); ++i) {
        const auto& child = clipboard.children[i];
        if (child.sourceBodyIndex < 0 || child.sourceFlatIndex < 0 || !child.data.is_object() ||
            !child.data.contains("shapeId") || !child.data["shapeId"].is_string())
            return false;
        childrenBySourceBody[child.sourceBodyIndex].push_back(i);
        clipboardChildBySourceFlat.emplace(child.sourceFlatIndex, i);
    }
    for (const auto& joint : clipboard.joints)
        if (!joint.data.is_object())
            return false;

    std::unordered_set<std::int64_t> usedIds;
    auto rememberId = [&](const json& value) {
        if (!value.is_number_integer())
            return;
        try {
            const auto id = value.get<std::int64_t>();
            usedIds.insert(id);
        } catch (...) {
        }
    };
    auto rememberControllerId = [&](const json& owner) {
        if (!owner.is_object())
            return;
        if (owner.contains("controller") && owner["controller"].is_object()) {
            const auto& controller = owner["controller"];
            if (controller.contains("id"))
                rememberId(controller["id"]);
            for (const char* key : {"controllers", "joints"})
                if (controller.contains(key) && controller[key].is_array())
                    for (const auto& reference : controller[key])
                        if (reference.is_object() && reference.contains("id"))
                            rememberId(reference["id"]);
        }
        if (owner.contains("joints") && owner["joints"].is_array())
            for (const auto& reference : owner["joints"])
                if (reference.is_object() && reference.contains("id"))
                    rememberId(reference["id"]);
    };
    if (blueprint.contains("bodies") && blueprint["bodies"].is_array())
        for (const auto& body : blueprint["bodies"]) {
            if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array())
                continue;
            for (const auto& child : body["childs"])
                rememberControllerId(child);
        }
    if (blueprint.contains("joints") && blueprint["joints"].is_array())
        for (const auto& joint : blueprint["joints"]) {
            if (!joint.is_object())
                continue;
            if (joint.contains("id"))
                rememberId(joint["id"]);
            rememberControllerId(joint);
        }
    for (const auto& child : clipboard.children)
        if (child.data.is_object() && child.data.contains("controller") &&
            child.data["controller"].is_object() && child.data["controller"].contains("id"))
            rememberId(child.data["controller"]["id"]);
    for (const auto& joint : clipboard.joints) {
        if (joint.data.contains("id"))
            rememberId(joint.data["id"]);
        if (joint.data.is_object() && joint.data.contains("controller") &&
            joint.data["controller"].is_object() && joint.data["controller"].contains("id"))
            rememberId(joint.data["controller"]["id"]);
    }
    std::int64_t maximumId = 0;
    for (const auto id : usedIds)
        if (id > maximumId)
            maximumId = id;
    std::int64_t nextId = maximumId == std::numeric_limits<std::int64_t>::max() ? 1 : maximumId + 1;
    auto allocateId = [&]() {
        while (usedIds.contains(nextId))
            nextId = nextId == std::numeric_limits<std::int64_t>::max() ? 1 : nextId + 1;
        const std::int64_t allocated = nextId;
        usedIds.insert(allocated);
        nextId = nextId == std::numeric_limits<std::int64_t>::max() ? 1 : nextId + 1;
        return allocated;
    };
    auto translate = [](int value, int delta) {
        const long long translated = (long long)value + (long long)delta;
        return (int)std::clamp(translated,
                               (long long)std::numeric_limits<int>::min(),
                               (long long)std::numeric_limits<int>::max());
    };
    auto setControllerId = [&](json& owner, std::unordered_map<std::int64_t, std::int64_t>& remap) {
        if (!owner.is_object() || !owner.contains("controller") || !owner["controller"].is_object())
            return;
        auto& controller = owner["controller"];
        std::int64_t oldId = -1;
        if (controller.contains("id") && controller["id"].is_number_integer())
            try {
                oldId = controller["id"].get<std::int64_t>();
            } catch (...) {
            }
        const std::int64_t newId = allocateId();
        controller["id"] = newId;
        if (oldId >= 0)
            remap.try_emplace(oldId, newId);
    };
    std::unordered_map<std::int64_t, std::int64_t> controllerIdRemap, jointIdRemap;
    std::vector<json> clonedChildren;
    clonedChildren.reserve(clipboard.children.size());
    for (const auto& clipboardChild : clipboard.children) {
        json child = clipboardChild.data;
        if (child.contains("pos") && child["pos"].is_object()) {
            auto p = position(child["pos"]);
            writePosition(
                child,
                "pos",
                {translate(p.x, offset.x), translate(p.y, offset.y), translate(p.z, offset.z)});
        }
        setControllerId(child, controllerIdRemap);
        clonedChildren.push_back(std::move(child));
    }
    std::vector<json> clonedJoints;
    clonedJoints.reserve(clipboard.joints.size());
    for (const auto& clipboardJoint : clipboard.joints) {
        json joint = clipboardJoint.data;
        std::int64_t oldJointId = -1;
        if (joint.contains("id") && joint["id"].is_number_integer())
            try {
                oldJointId = joint["id"].get<std::int64_t>();
            } catch (...) {
            }
        const std::int64_t newJointId = allocateId();
        joint["id"] = newJointId;
        if (oldJointId >= 0)
            jointIdRemap.try_emplace(oldJointId, newJointId);
        setControllerId(joint, controllerIdRemap);
        clonedJoints.push_back(std::move(joint));
    }

    json staged = blueprint;
    PasteSelectionResult stagedResult;
    std::unordered_map<int, int> newFlatBySourceFlat;
    const int originalBodyCount = (int)staged["bodies"].size();
    const size_t originalJointCount =
        staged.contains("joints") && staged["joints"].is_array() ? staged["joints"].size() : 0;
    std::vector<int> originalBodyStarts(originalBodyCount),
        originalBodyChildCounts(originalBodyCount);
    int originalChildCount = 0;
    for (int bodyIndex = 0; bodyIndex < originalBodyCount; ++bodyIndex) {
        originalBodyStarts[bodyIndex] = originalChildCount;
        const auto& body = staged["bodies"][bodyIndex];
        if (body.is_object() && body.contains("childs") && body["childs"].is_array())
            originalBodyChildCounts[bodyIndex] = (int)body["childs"].size();
        originalChildCount += originalBodyChildCounts[bodyIndex];
    }
    std::unordered_map<int, int> appendedToExistingBody;
    std::unordered_map<int, std::pair<int, int>> newChildLocationBySourceFlat;
    for (const auto& [sourceBody, childIndices] : childrenBySourceBody) {
        if (childIndices.empty())
            continue;
        const bool copyWholeBody = clipboard.children[childIndices.front()].sourceBodyFullySelected;
        const bool sourceBodyExists = sourceBody >= 0 && sourceBody < originalBodyCount &&
                                      staged["bodies"][sourceBody].is_object() &&
                                      staged["bodies"][sourceBody].contains("childs") &&
                                      staged["bodies"][sourceBody]["childs"].is_array();
        const bool appendToSourceBody = !copyWholeBody && sourceBodyExists;
        int targetBodyIndex = -1;
        json detachedBody;
        json* destinationChildren = nullptr;
        if (appendToSourceBody) {
            targetBodyIndex = sourceBody;
            destinationChildren = &staged["bodies"][targetBodyIndex]["childs"];
            appendedToExistingBody[targetBodyIndex] += (int)childIndices.size();
        } else {
            targetBodyIndex = (int)staged["bodies"].size();
            if (sourceBodyExists)
                detachedBody = staged["bodies"][sourceBody];
            else
                detachedBody = json::object();
            detachedBody["childs"] = json::array();
            staged["bodies"].push_back(std::move(detachedBody));
            destinationChildren = &staged["bodies"][targetBodyIndex]["childs"];
        }
        for (size_t clipboardChildIndex : childIndices) {
            const int newChildIndex = (int)destinationChildren->size();
            const auto& source = clipboard.children[clipboardChildIndex];
            newChildLocationBySourceFlat.emplace(
                source.sourceFlatIndex, std::pair<int, int>{targetBodyIndex, newChildIndex});
            destinationChildren->push_back(std::move(clonedChildren[clipboardChildIndex]));
            stagedResult.children.emplace_back(targetBodyIndex, newChildIndex);
        }
    }
    // Child endpoints in joints use one flattened index across every body's childs array.
    // Appending into an earlier body shifts the original indices of all later bodies.
    auto remapOriginalFlatIndex = [&](int originalFlatIndex) {
        if (originalFlatIndex < 0 || originalFlatIndex >= originalChildCount)
            return originalFlatIndex;
        int sourceBody = -1;
        for (int bodyIndex = 0; bodyIndex < originalBodyCount; ++bodyIndex)
            if (originalFlatIndex >= originalBodyStarts[bodyIndex] &&
                originalFlatIndex <
                    originalBodyStarts[bodyIndex] + originalBodyChildCounts[bodyIndex]) {
                sourceBody = bodyIndex;
                break;
            }
        if (sourceBody < 0)
            return originalFlatIndex;
        int remapped = originalFlatIndex;
        for (const auto& [bodyIndex, appendedCount] : appendedToExistingBody)
            if (bodyIndex < sourceBody)
                remapped += appendedCount;
        return remapped;
    };
    for (size_t jointIndex = 0; jointIndex < originalJointCount; ++jointIndex) {
        auto& joint = staged["joints"][jointIndex];
        if (!joint.is_object())
            continue;
        for (const char* endpoint : {"childA", "childB"})
            if (joint.contains(endpoint) && joint[endpoint].is_number_integer())
                try {
                    const int childIndex = joint[endpoint].get<int>();
                    joint[endpoint] = remapOriginalFlatIndex(childIndex);
                } catch (...) {
                }
    }
    for (const auto& [sourceFlat, location] : newChildLocationBySourceFlat) {
        const int newFlat = flatChildIndex(staged, location.first, location.second);
        if (newFlat < 0)
            return false;
        newFlatBySourceFlat.emplace(sourceFlat, newFlat);
    }

    auto remapControllerState = [&](json& owner) {
        if (!owner.is_object() || !owner.contains("controller") || !owner["controller"].is_object())
            return;
        auto& controller = owner["controller"];
        const std::int64_t id = controller.value("id", (std::int64_t)-1);
        if (!clipboard.copyBlockState) {
            controller = {{"id", id}, {"controllers", nullptr}, {"joints", nullptr}};
            return;
        }
        if (controller.contains("controllers") && controller["controllers"].is_array()) {
            auto& connections = controller["controllers"];
            for (auto& connection : connections) {
                if (!connection.is_object() || !connection.contains("id") ||
                    !connection["id"].is_number_integer())
                    continue;
                try {
                    const auto targetId = connection["id"].get<std::int64_t>();
                    auto cloned = controllerIdRemap.find(targetId);
                    if (cloned != controllerIdRemap.end())
                        connection["id"] = cloned->second;
                } catch (...) {
                }
            }
        }
        if (controller.contains("joints") && controller["joints"].is_array()) {
            auto& configuredJoints = controller["joints"];
            configuredJoints.erase(
                std::remove_if(configuredJoints.begin(),
                               configuredJoints.end(),
                               [&](json& reference) {
                                   if (!reference.is_object() || !reference.contains("id") ||
                                       !reference["id"].is_number_integer())
                                       return false;
                                   try {
                                       auto cloned =
                                           jointIdRemap.find(reference["id"].get<std::int64_t>());
                                       if (cloned == jointIdRemap.end())
                                           return true;
                                       reference["id"] = cloned->second;
                                       return false;
                                   } catch (...) {
                                       return false;
                                   }
                               }),
                configuredJoints.end());
        }
    };
    // Blueprint connections are stored only on the source controller. When a
    // copied target has an incoming wire from a source outside the selection,
    // duplicate that reference on the original source so the pasted target keeps
    // the same input. Sources copied in this operation already get their links
    // remapped to the pasted targets above, so leave their originals untouched.
    auto duplicateIncomingControllerReferences = [&](json& owner) {
        if (!clipboard.copyBlockState || !owner.is_object() || !owner.contains("controller") ||
            !owner["controller"].is_object())
            return;
        auto& controller = owner["controller"];
        const std::int64_t sourceId = integer64(controller, "id");
        if (controllerIdRemap.contains(sourceId) || !controller.contains("controllers") ||
            !controller["controllers"].is_array())
            return;
        auto& connections = controller["controllers"];
        const auto originalConnections = connections;
        for (const auto& connection : originalConnections) {
            if (!connection.is_object() || !connection.contains("id") ||
                !connection["id"].is_number_integer())
                continue;
            std::int64_t targetId = -1;
            try {
                targetId = connection["id"].get<std::int64_t>();
            } catch (...) {
                continue;
            }
            auto clonedTarget = controllerIdRemap.find(targetId);
            if (clonedTarget == controllerIdRemap.end())
                continue;
            bool alreadyConnected = false;
            for (const auto& existing : connections) {
                if (!existing.is_object() || !existing.contains("id") ||
                    !existing["id"].is_number_integer())
                    continue;
                try {
                    alreadyConnected = existing["id"].get<std::int64_t>() == clonedTarget->second;
                } catch (...) {
                    continue;
                }
                if (alreadyConnected)
                    break;
            }
            if (alreadyConnected)
                continue;
            json clonedConnection = connection;
            clonedConnection["id"] = clonedTarget->second;
            connections.push_back(std::move(clonedConnection));
        }
    };
    auto remapChildJointReferences = [&](json& child) {
        if (!child.is_object() || !child.contains("joints") || !child["joints"].is_array())
            return;
        auto& references = child["joints"];
        references.erase(
            std::remove_if(references.begin(),
                           references.end(),
                           [&](json& reference) {
                               if (!reference.is_object() || !reference.contains("id") ||
                                   !reference["id"].is_number_integer())
                                   return false;
                               try {
                                   auto cloned =
                                       jointIdRemap.find(reference["id"].get<std::int64_t>());
                                   if (cloned == jointIdRemap.end())
                                       return true;
                                   reference["id"] = cloned->second;
                                   return false;
                               } catch (...) {
                                   return false;
                               }
                           }),
            references.end());
        if (references.empty())
            child.erase("joints");
    };
    auto ensureJointReference = [](json& child, std::int64_t id) {
        if (!child.is_object())
            return;
        if (!child.contains("joints") || !child["joints"].is_array())
            child["joints"] = json::array();
        auto& references = child["joints"];
        for (const auto& reference : references)
            if (reference.is_object() && reference.contains("id") &&
                reference["id"].is_number_integer())
                try {
                    if (reference["id"].get<std::int64_t>() == id)
                        return;
                } catch (...) {
                }
        references.push_back({{"id", id}});
    };
    auto childByFlatIndex = [&](int flat) -> json* {
        for (int bodyIndex = 0; bodyIndex < (int)staged["bodies"].size(); ++bodyIndex) {
            auto& body = staged["bodies"][bodyIndex];
            if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array())
                continue;
            for (int childIndex = 0; childIndex < (int)body["childs"].size(); ++childIndex)
                if (flatChildIndex(staged, bodyIndex, childIndex) == flat)
                    return &body["childs"][childIndex];
        }
        return nullptr;
    };
    std::unordered_set<int> pastedFlatIndices;
    for (const auto& [sourceFlat, pastedFlat] : newFlatBySourceFlat) {
        (void)sourceFlat;
        pastedFlatIndices.insert(pastedFlat);
    }
    for (int bodyIndex = 0; bodyIndex < (int)staged["bodies"].size(); ++bodyIndex) {
        auto& body = staged["bodies"][bodyIndex];
        if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array())
            continue;
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
    for (auto& joint : clonedJoints)
        remapControllerState(joint);
    std::vector<std::pair<int, std::int64_t>> originalEndpointReferences;
    for (size_t i = 0; i < clipboard.joints.size(); ++i) {
        const auto& source = clipboard.joints[i];
        auto& joint = clonedJoints[i];
        for (const auto& endpoint : {std::pair<const char*, int>{"A", source.sourceChildA},
                                     std::pair<const char*, int>{"B", source.sourceChildB}}) {
            const char* childKey = endpoint.first == std::string("A") ? "childA" : "childB";
            const char* posKey = endpoint.first == std::string("A") ? "posA" : "posB";
            auto copiedChild = clipboardChildBySourceFlat.find(endpoint.second);
            if (copiedChild != clipboardChildBySourceFlat.end()) {
                auto pastedFlat = newFlatBySourceFlat.find(endpoint.second);
                if (pastedFlat == newFlatBySourceFlat.end())
                    return false;
                joint[childKey] = pastedFlat->second;
                if (joint.contains(posKey) && joint[posKey].is_object()) {
                    const auto p = position(joint[posKey]);
                    writePosition(joint,
                                  posKey,
                                  {translate(p.x, offset.x),
                                   translate(p.y, offset.y),
                                   translate(p.z, offset.z)});
                }
                auto* pastedChild = childByFlatIndex(pastedFlat->second);
                if (!pastedChild)
                    return false;
                ensureJointReference(*pastedChild, joint["id"].get<std::int64_t>());
            } else {
                const int targetChild = remapOriginalFlatIndex(endpoint.second);
                joint[childKey] = targetChild;
                if (endpoint.second >= 0 && endpoint.second < originalChildCount)
                    originalEndpointReferences.emplace_back(targetChild,
                                                            joint["id"].get<std::int64_t>());
            }
        }
    }
    for (const auto& [flat, jointId] : originalEndpointReferences) {
        json* child = childByFlatIndex(flat);
        if (!child)
            return false;
        ensureJointReference(*child, jointId);
    }
    if (!clonedJoints.empty()) {
        if (!staged.contains("joints") || staged["joints"].is_null())
            staged["joints"] = json::array();
        if (!staged["joints"].is_array())
            return false;
        for (auto& joint : clonedJoints) {
            stagedResult.joints.push_back((int)staged["joints"].size());
            staged["joints"].push_back(std::move(joint));
        }
    }
    blueprint = std::move(staged);
    result = std::move(stagedResult);
    return true;
}
