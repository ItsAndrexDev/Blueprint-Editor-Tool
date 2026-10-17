#include "scrap_parser_internal.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

using Parser::Internal::flatChildIndex;
using Parser::Internal::integer;

bool Parser::captureSelectionClipboard(const json& blueprint,
                                       const std::vector<Block>& blocks,
                                       const std::vector<int>& selection,
                                       bool copyBlockState,
                                       SelectionClipboard& clipboard) {
    clipboard = {};
    clipboard.copyBlockState = copyBlockState;
    if (!blueprint.is_object() || !blueprint.contains("bodies") || !blueprint["bodies"].is_array())
        return false;
    std::unordered_set<int> selectedBlocks, selectedChildFlats, selectedJointIndices;
    std::unordered_map<int, size_t> selectedChildrenPerBody;
    for (int index : selection) {
        if (index < 0 || index >= (int)blocks.size() || !selectedBlocks.insert(index).second)
            continue;
        const auto& block = blocks[index];
        if (block.isJoint) {
            if (blueprint.contains("joints") && blueprint["joints"].is_array() &&
                block.jointIndex >= 0 && block.jointIndex < (int)blueprint["joints"].size())
                selectedJointIndices.insert(block.jointIndex);
            continue;
        }
        const int flat = flatChildIndex(blueprint, block.bodyIndex, block.childIndex);
        if (flat < 0 || block.bodyIndex < 0 || block.bodyIndex >= (int)blueprint["bodies"].size())
            continue;
        const auto& body = blueprint["bodies"][block.bodyIndex];
        if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() ||
            block.childIndex < 0 || block.childIndex >= (int)body["childs"].size())
            continue;
        const auto& child = body["childs"][block.childIndex];
        if (!child.is_object())
            continue;
        selectedChildFlats.insert(flat);
        ++selectedChildrenPerBody[block.bodyIndex];
        clipboard.children.push_back({block.bodyIndex, flat, false, child});
    }
    for (auto& copiedChild : clipboard.children) {
        const auto& sourceBody = blueprint["bodies"][copiedChild.sourceBodyIndex];
        copiedChild.sourceBodyFullySelected =
            sourceBody.contains("childs") && sourceBody["childs"].is_array() &&
            selectedChildrenPerBody[copiedChild.sourceBodyIndex] == sourceBody["childs"].size();
    }
    if (blueprint.contains("joints") && blueprint["joints"].is_array()) {
        for (int jointIndex = 0; jointIndex < (int)blueprint["joints"].size(); ++jointIndex) {
            const auto& joint = blueprint["joints"][jointIndex];
            if (!joint.is_object())
                continue;
            const int childA = integer(joint, "childA", -1), childB = integer(joint, "childB", -1);
            const bool jointSelected = selectedJointIndices.contains(jointIndex);
            const bool internalToSelection =
                selectedChildFlats.contains(childA) && selectedChildFlats.contains(childB);
            if (!jointSelected && !internalToSelection)
                continue;
            clipboard.joints.push_back({childA, childB, joint});
        }
    }
    return !clipboard.empty();
}
