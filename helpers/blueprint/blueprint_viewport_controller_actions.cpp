#include "blueprint_viewport_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BlueprintViewport::Internal {
namespace {
constexpr const char* logicGateShapeId = "9f0f56e8-2c31-4d83-996c-d00a9b296c3f";
constexpr const char* controllerShapeId = "12862001-666c-4bdd-8326-a1f43610d28b";

bool isPhysicalController(const Parser::Block& part, const std::string& displayName) {
    return !part.isJoint &&
           (lower(part.shapeID) == controllerShapeId || lower(displayName) == "controller");
}
} // namespace

json* EditingActions::controllerForPart(int index) {
    if (index < 0 || index >= (int)blocks_.size() || blocks_[index].isJoint)
        return nullptr;
    const auto& part = blocks_[index];
    if (part.bodyIndex < 0 || part.childIndex < 0 || !blueprint_.is_object() ||
        !blueprint_.contains("bodies") || !blueprint_["bodies"].is_array() ||
        part.bodyIndex >= (int)blueprint_["bodies"].size())
        return nullptr;
    auto& body = blueprint_["bodies"][part.bodyIndex];
    if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() ||
        part.childIndex >= (int)body["childs"].size())
        return nullptr;
    auto& child = body["childs"][part.childIndex];
    return child.is_object() && child.contains("controller") && child["controller"].is_object()
               ? &child["controller"]
               : nullptr;
}

std::int64_t EditingActions::controllerIdForPart(int index) {
    json* controller = controllerForPart(index);
    if (!controller || !controller->contains("id") || !(*controller)["id"].is_number_integer())
        return -1;
    try {
        return (*controller)["id"].get<std::int64_t>();
    } catch (...) {
        return -1;
    }
}

std::string EditingActions::partDisplayName(int index) const {
    if (index < 0 || index >= (int)blocks_.size())
        return "part";
    std::string name = Parser::findBlockNameByShapeID(blocks_[index].shapeID, items_);
    if (name.empty() || name == "Unknown part") {
        const auto shape = state.catalog.find(lower(blocks_[index].shapeID));
        if (shape != state.catalog.end() && !shape->second.name.empty())
            name = displayNameFromShapeSet(shape->second.name);
    }
    return name.empty() || name == "Unknown part" ? "part" : name;
}

int EditingActions::logicGateModeForPart(int index) const {
    if (index < 0 || index >= (int)blocks_.size() || blocks_[index].isJoint ||
        lower(blocks_[index].shapeID) != logicGateShapeId)
        return 0;
    const auto& part = blocks_[index];
    if (part.bodyIndex < 0 || part.childIndex < 0 || !blueprint_.is_object() ||
        !blueprint_.contains("bodies") || !blueprint_["bodies"].is_array() ||
        part.bodyIndex >= (int)blueprint_["bodies"].size())
        return 0;
    const auto& body = blueprint_["bodies"][part.bodyIndex];
    if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() ||
        part.childIndex >= (int)body["childs"].size())
        return 0;
    const auto& child = body["childs"][part.childIndex];
    if (!child.is_object() || !child.contains("controller") || !child["controller"].is_object() ||
        !child["controller"].contains("mode") || !child["controller"]["mode"].is_number_integer())
        return 0;
    try {
        const int mode = child["controller"]["mode"].get<int>();
        return mode >= 0 && mode <= 5 ? mode : 0;
    } catch (...) {
        return 0;
    }
}

void EditingActions::setSelectionLogicGateMode(int mode) {
    if (mode < 0 || mode > 5)
        return;
    std::unordered_set<int> selected = state.selectionSet;
    if (state.selection >= 0 && state.selection < (int)blocks_.size())
        selected.insert(state.selection);
    for (int index : selected) {
        if (index < 0 || index >= (int)blocks_.size() || blocks_[index].isJoint ||
            lower(blocks_[index].shapeID) != logicGateShapeId)
            continue;
        json* controller = controllerForPart(index);
        if (controller)
            (*controller)["mode"] = mode;
    }
}

std::vector<EditingActions::ControllerBearingSettings>
EditingActions::controllerBearingSettings(int index) {
    std::vector<ControllerBearingSettings> result;
    if (index < 0 || index >= (int)blocks_.size() ||
        !isPhysicalController(blocks_[index], partDisplayName(index)))
        return result;
    json* controller = controllerForPart(index);
    if (!controller || !controller->contains("joints") || !(*controller)["joints"].is_array() ||
        !blueprint_.is_object() || !blueprint_.contains("joints") ||
        !blueprint_["joints"].is_array())
        return result;

    for (const auto& reference : (*controller)["joints"]) {
        if (!reference.is_object() || !reference.contains("id") ||
            !reference["id"].is_number_integer() || !reference.contains("frames") ||
            !reference["frames"].is_array())
            continue;
        std::int64_t jointId = -1;
        try {
            jointId = reference["id"].get<std::int64_t>();
        } catch (...) {
            continue;
        }
        const json* joint = nullptr;
        for (const auto& candidate : blueprint_["joints"])
            if (candidate.is_object() && candidate.contains("id") &&
                candidate["id"].is_number_integer()) {
                try {
                    if (candidate["id"].get<std::int64_t>() == jointId) {
                        joint = &candidate;
                        break;
                    }
                } catch (...) {
                }
            }
        if (!joint || !joint->contains("shapeId") || !(*joint)["shapeId"].is_string())
            continue;
        const std::string shapeId = (*joint)["shapeId"].get<std::string>();
        std::string partName = Parser::findBlockNameByShapeID(shapeId, items_);
        if (lower(partName).find("bearing") == std::string::npos)
            continue;

        ControllerBearingSettings settings;
        settings.jointId = jointId;
        settings.connectionIndex = (int)result.size();
        if (reference.contains("index") && reference["index"].is_number_integer())
            try {
                settings.connectionIndex = reference["index"].get<int>();
            } catch (...) {
            }
        settings.name = std::move(partName);
        settings.reverse = reference.contains("reverse") && reference["reverse"].is_boolean()
                               ? reference["reverse"].get<bool>()
                               : false;
        settings.frameAngles.reserve(reference["frames"].size());
        for (const auto& frame : reference["frames"]) {
            int angle = 0;
            if (frame.is_object() && frame.contains("targetAngle") &&
                frame["targetAngle"].is_number())
                try {
                    const double value = frame["targetAngle"].get<double>();
                    angle = (int)std::clamp(std::llround(value),
                                            (long long)std::numeric_limits<int>::min(),
                                            (long long)std::numeric_limits<int>::max());
                } catch (...) {
                }
            settings.frameAngles.push_back(angle);
        }
        result.push_back(std::move(settings));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.connectionIndex != right.connectionIndex
                   ? left.connectionIndex < right.connectionIndex
                   : left.jointId < right.jointId;
    });
    return result;
}

bool EditingActions::setControllerBearingAngle(int index,
                                               std::int64_t jointId,
                                               int frameIndex,
                                               int angle) {
    if (index < 0 || index >= (int)blocks_.size() ||
        !isPhysicalController(blocks_[index], partDisplayName(index)) || frameIndex < 0)
        return false;
    json* controller = controllerForPart(index);
    if (!controller || !controller->contains("joints") || !(*controller)["joints"].is_array())
        return false;
    for (auto& reference : (*controller)["joints"]) {
        if (!reference.is_object() || !reference.contains("id") ||
            !reference["id"].is_number_integer())
            continue;
        try {
            if (reference["id"].get<std::int64_t>() != jointId)
                continue;
        } catch (...) {
            continue;
        }
        if (!reference.contains("frames") || !reference["frames"].is_array() ||
            frameIndex >= (int)reference["frames"].size())
            return false;
        auto& frames = reference["frames"];
        if (!frames[frameIndex].is_object())
            frames[frameIndex] = json::object();
        frames[frameIndex]["targetAngle"] = angle;
        if (!frames[0].is_object())
            frames[0] = json::object();
        long long totalAngle = 0;
        for (const auto& frame : frames)
            if (frame.is_object() && frame.contains("targetAngle") &&
                frame["targetAngle"].is_number())
                try {
                    const double value = frame["targetAngle"].get<double>();
                    if (std::isfinite(value))
                        totalAngle += (long long)std::clamp(
                            std::llround(value), (long long)-1000000000, (long long)1000000000);
                } catch (...) {
                }
        reference["startAngle"] = frames[0].value("targetAngle", 0);
        reference["endAngle"] = (int)std::clamp(totalAngle,
                                                (long long)std::numeric_limits<int>::min(),
                                                (long long)std::numeric_limits<int>::max());
        return true;
    }
    return false;
}

bool EditingActions::setControllerBearingReverse(int index, std::int64_t jointId, bool reverse) {
    if (index < 0 || index >= (int)blocks_.size() ||
        !isPhysicalController(blocks_[index], partDisplayName(index)))
        return false;
    json* controller = controllerForPart(index);
    if (!controller || !controller->contains("joints") || !(*controller)["joints"].is_array())
        return false;
    for (auto& reference : (*controller)["joints"]) {
        if (!reference.is_object() || !reference.contains("id") ||
            !reference["id"].is_number_integer())
            continue;
        try {
            if (reference["id"].get<std::int64_t>() == jointId) {
                reference["reverse"] = reverse;
                return true;
            }
        } catch (...) {
        }
    }
    return false;
}

} // namespace BlueprintViewport::Internal
