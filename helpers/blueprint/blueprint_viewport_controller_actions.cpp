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
constexpr const char* pistonShapeId = "260b4597-f1ac-409c-8e6b-90c998c5fe94";

bool isPhysicalController(const Parser::Block& part, const std::string& displayName) {
    if (part.isJoint)
        return false;
    const auto capabilities = state.shapeConnectionCapabilities.find(lower(part.shapeID));
    return lower(part.shapeID) == controllerShapeId ||
           (capabilities != state.shapeConnectionCapabilities.end() &&
            (capabilities->second & ShapeControllerJointOutput) != 0) ||
           lower(displayName).find("controller") == 0;
}
} // namespace

json* EditingActions::controllerForPart(int index) {
    if (index < 0 || index >= (int)blocks_.size())
        return nullptr;
    const auto& part = blocks_[index];
    if (part.isJoint) {
        if (lower(part.shapeID) != pistonShapeId || part.jointIndex < 0 ||
            !blueprint_.is_object() || !blueprint_.contains("joints") ||
            !blueprint_["joints"].is_array() ||
            part.jointIndex >= (int)blueprint_["joints"].size())
            return nullptr;
        auto& joint = blueprint_["joints"][part.jointIndex];
        return joint.is_object() && joint.contains("controller") &&
                       joint["controller"].is_object()
                   ? &joint["controller"]
                   : nullptr;
    }
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

bool EditingActions::isPistonPart(int index) const {
    if (index < 0 || index >= (int)blocks_.size())
        return false;
    return lower(blocks_[index].shapeID) == pistonShapeId ||
           lower(partDisplayName(index)) == "piston";
}

bool EditingActions::canUseAsLogicSource(int index) {
    if (index < 0 || index >= (int)blocks_.size() || isPistonPart(index))
        return false;
    const auto shape = state.shapeConnectionCapabilities.find(lower(blocks_[index].shapeID));
    if (shape == state.shapeConnectionCapabilities.end())
        return controllerIdForPart(index) >= 0;
    if (shape->second & ShapeLogicOutput)
        return true;
    json* controller = controllerForPart(index);
    return controller && controller->contains("controllers") &&
           (*controller)["controllers"].is_array();
}

bool EditingActions::canUseAsJointSource(int index) {
    if (index < 0 || index >= (int)blocks_.size() || blocks_[index].isJoint ||
        controllerIdForPart(index) < 0)
        return false;
    const auto shape = state.shapeConnectionCapabilities.find(lower(blocks_[index].shapeID));
    if (shape == state.shapeConnectionCapabilities.end()) {
        json* controller = controllerForPart(index);
        return controller && controller->contains("joints") &&
               (*controller)["joints"].is_array();
    }
    return (shape->second & (ShapeEngineJointOutput | ShapeControllerJointOutput |
                             ShapeSteeringJointOutput)) != 0;
}

bool EditingActions::canUseAsConnectionSource(int index) {
    return canUseAsLogicSource(index) || canUseAsJointSource(index);
}

ConnectionKind EditingActions::connectionKind(int sourceIndex, int targetIndex) {
    if (sourceIndex < 0 || targetIndex < 0 || sourceIndex >= (int)blocks_.size() ||
        targetIndex >= (int)blocks_.size())
        return ConnectionKind::None;
    const auto sourceShape =
        state.shapeConnectionCapabilities.find(lower(blocks_[sourceIndex].shapeID));
    const auto targetShape =
        state.shapeConnectionCapabilities.find(lower(blocks_[targetIndex].shapeID));
    const auto sourceCapabilities = sourceShape == state.shapeConnectionCapabilities.end()
                                       ? 0u
                                       : sourceShape->second;
    const auto targetCapabilities = targetShape == state.shapeConnectionCapabilities.end()
                                       ? 0u
                                       : targetShape->second;
    const bool targetBearing = (targetCapabilities & ShapeBearingJoint) != 0;
    const bool targetSuspension = (targetCapabilities & ShapeSuspensionJoint) != 0;
    const bool targetPiston = (targetCapabilities & ShapePistonJoint) != 0;
    const bool controller = (sourceCapabilities & ShapeControllerJointOutput) != 0;
    const bool engine = (sourceCapabilities & ShapeEngineJointOutput) != 0;
    const bool driverSeat = (sourceCapabilities & ShapeSteeringJointOutput) != 0;
    if (controller && isPistonPart(targetIndex)) {
        json* source = controllerForPart(sourceIndex);
        const auto targetId = controllerIdForPart(targetIndex);
        if (source && targetId >= 0 && source->contains("controllers") &&
            (*source)["controllers"].is_array())
            for (const auto& connection : (*source)["controllers"])
                if (connection.is_object() && connection.contains("id") &&
                    connection["id"].is_number_integer())
                    try {
                        if (connection["id"].get<std::int64_t>() == targetId)
                            return ConnectionKind::Logic;
                    } catch (...) {
                    }
        if (source && source->contains("joints") && (*source)["joints"].is_array())
            for (const auto& connection : (*source)["joints"])
                if (connection.is_object() && connection.contains("id") &&
                    connection["id"].is_number_integer())
                    try {
                        if (connection["id"].get<std::int64_t>() ==
                            blocks_[targetIndex].jointId)
                            return ConnectionKind::Joint;
                    } catch (...) {
                    }
        // Scrap Mechanic stores a Controller-to-Piston sequence under
        // controller.controllers, with `setting` frames, not as a joint-angle link.
        return ConnectionKind::Logic;
    }
    if (isPistonPart(targetIndex) && !isPistonPart(sourceIndex) &&
        canUseAsConnectionSource(sourceIndex))
        return ConnectionKind::Logic;
    if (canUseAsJointSource(sourceIndex) && blocks_[targetIndex].isJoint &&
        blocks_[targetIndex].jointId >= 0 &&
        ((controller && (targetBearing || targetPiston)) || (engine && targetBearing) ||
         (driverSeat && (targetBearing || targetSuspension))))
        return ConnectionKind::Joint;

    if (!canUseAsLogicSource(sourceIndex))
        return ConnectionKind::None;
    if (isPistonPart(targetIndex))
        return ConnectionKind::Logic;
    if (controllerIdForPart(targetIndex) < 0)
        return ConnectionKind::None;

    const bool seatSource = (sourceCapabilities & ShapeSeatOutput) != 0;
    if ((targetCapabilities & ShapeButtonSwitchTarget) != 0)
        return seatSource ? ConnectionKind::Logic : ConnectionKind::None;
    if (seatSource) {
        const bool driverSeatTarget =
            (targetCapabilities & (ShapeEngineTarget | ShapeThrusterTarget |
                                   ShapeControllerTarget)) != 0;
        return driverSeat && driverSeatTarget ? ConnectionKind::Logic : ConnectionKind::None;
    }
    if ((targetCapabilities & ShapeSeatOutput) != 0)
        return ConnectionKind::None;
    return ConnectionKind::Logic;
}

bool EditingActions::addConnection(int sourceIndex, int targetIndex) {
    switch (connectionKind(sourceIndex, targetIndex)) {
    case ConnectionKind::Logic: {
        const auto shape =
            state.shapeConnectionCapabilities.find(lower(blocks_[sourceIndex].shapeID));
        const bool controllerPistonSequence =
            shape != state.shapeConnectionCapabilities.end() &&
            (shape->second & ShapeControllerJointOutput) != 0 && isPistonPart(targetIndex);
        return Parser::addDirectedConnection(
            blueprint_, blocks_, sourceIndex, targetIndex, controllerPistonSequence);
    }
    case ConnectionKind::Joint: {
        const auto shape =
            state.shapeConnectionCapabilities.find(lower(blocks_[sourceIndex].shapeID));
        if (shape != state.shapeConnectionCapabilities.end() &&
            (shape->second & ShapeGasEngineBearingLimit) != 0) {
            json* controller = controllerForPart(sourceIndex);
            if (controller && controller->contains("joints") &&
                (*controller)["joints"].is_array() &&
                (*controller)["joints"].size() >= 5)
                return false;
        }
        const bool controllerSequence = shape != state.shapeConnectionCapabilities.end() &&
                                        (shape->second & ShapeControllerJointOutput) != 0;
        return Parser::addJointConnection(
            blueprint_, blocks_, sourceIndex, targetIndex, controllerSequence);
    }
    case ConnectionKind::None:
        return false;
    }
    return false;
}

float EditingActions::controllerTimePerFrame(int index) {
    if (index < 0 || index >= (int)blocks_.size() ||
        !isPhysicalController(blocks_[index], partDisplayName(index)))
        return 1.f;
    json* controller = controllerForPart(index);
    if (!controller || !controller->contains("timePerFrame") ||
        !(*controller)["timePerFrame"].is_number())
        return 1.f;
    try {
        const double value = (*controller)["timePerFrame"].get<double>();
        return std::isfinite(value) && value >= 0.0 && value <= std::numeric_limits<float>::max()
                   ? static_cast<float>(value)
                   : 1.f;
    } catch (...) {
        return 1.f;
    }
}

bool EditingActions::setControllerTimePerFrame(int index, float seconds) {
    if (index < 0 || index >= (int)blocks_.size() || !std::isfinite(seconds) || seconds < 0.f ||
        !isPhysicalController(blocks_[index], partDisplayName(index)))
        return false;
    json* controller = controllerForPart(index);
    if (!controller)
        return false;
    (*controller)["timePerFrame"] = seconds;
    return true;
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

std::vector<EditingActions::ControllerJointSettings>
EditingActions::controllerJointSettings(int index) {
    std::vector<ControllerJointSettings> result;
    if (index < 0 || index >= (int)blocks_.size() ||
        !isPhysicalController(blocks_[index], partDisplayName(index)))
        return result;
    json* controller = controllerForPart(index);
    if (!controller || !blueprint_.is_object() || !blueprint_.contains("joints") ||
        !blueprint_["joints"].is_array())
        return result;

    auto appendSettings = [&](const json& reference, bool controllerPistonSequence) {
        if (!reference.is_object() || !reference.contains("id") ||
            !reference["id"].is_number_integer() || !reference.contains("frames") ||
            !reference["frames"].is_array())
            return;
        std::int64_t referenceId = -1;
        try {
            referenceId = reference["id"].get<std::int64_t>();
        } catch (...) {
            return;
        }
        const json* joint = nullptr;
        for (const auto& candidate : blueprint_["joints"]) {
            if (!candidate.is_object())
                continue;
            const json* identity = nullptr;
            if (controllerPistonSequence) {
                if (candidate.contains("controller") && candidate["controller"].is_object() &&
                    candidate["controller"].contains("id"))
                    identity = &candidate["controller"]["id"];
            } else if (candidate.contains("id")) {
                identity = &candidate["id"];
            }
            if (!identity || !identity->is_number_integer())
                continue;
            try {
                if (identity->get<std::int64_t>() == referenceId) {
                    joint = &candidate;
                    break;
                }
            } catch (...) {
            }
        }
        if (!joint || !joint->contains("id") || !(*joint)["id"].is_number_integer() ||
            !joint->contains("shapeId") || !(*joint)["shapeId"].is_string())
            return;
        const std::string shapeId = (*joint)["shapeId"].get<std::string>();
        const auto shape = state.shapeConnectionCapabilities.find(lower(shapeId));
        if (shape == state.shapeConnectionCapabilities.end())
            return;
        const bool piston = (shape->second & ShapePistonJoint) != 0;
        if (controllerPistonSequence ? !piston
                                     : (shape->second & (ShapeBearingJoint | ShapePistonJoint)) == 0)
            return;
        std::int64_t actualJointId = -1;
        try {
            actualJointId = (*joint)["id"].get<std::int64_t>();
        } catch (...) {
            return;
        }
        if (std::any_of(result.begin(), result.end(), [&](const auto& item) {
                return item.jointId == actualJointId;
            }))
            return;

        std::string partName = Parser::findBlockNameByShapeID(shapeId, items_);
        if (partName.empty() || partName == "Unknown part") {
            const auto catalogEntry = state.catalog.find(lower(shapeId));
            if (catalogEntry != state.catalog.end())
                partName = displayNameFromShapeSet(catalogEntry->second.name);
        }
        ControllerJointSettings settings;
        settings.jointId = actualJointId;
        settings.referenceId = referenceId;
        settings.connectionIndex = (int)result.size();
        settings.piston = piston;
        settings.controllerSequence = controllerPistonSequence;
        settings.frameValueMaximum = controllerPistonSequence ? 15 : 16;
        if (reference.contains("index") && reference["index"].is_number_integer())
            try {
                settings.connectionIndex = reference["index"].get<int>();
            } catch (...) {
            }
        settings.name = std::move(partName);
        settings.reverse = reference.contains("reverse") && reference["reverse"].is_boolean()
                               ? reference["reverse"].get<bool>()
                               : false;
        const char* frameKey = controllerPistonSequence
                                   ? "setting"
                                   : (piston ? "targetLength" : "targetAngle");
        settings.frameValues.reserve(reference["frames"].size());
        for (const auto& frame : reference["frames"]) {
            int value = 0;
            if (frame.is_object() && frame.contains(frameKey) && frame[frameKey].is_number())
                try {
                    const double storedValue = frame[frameKey].get<double>();
                    value = (int)std::clamp(std::llround(storedValue),
                                            (long long)std::numeric_limits<int>::min(),
                                            (long long)std::numeric_limits<int>::max());
                } catch (...) {
                }
            settings.frameValues.push_back(value);
        }
        result.push_back(std::move(settings));
    };

    if (controller->contains("joints") && (*controller)["joints"].is_array())
        for (const auto& reference : (*controller)["joints"])
            appendSettings(reference, false);
    if (controller->contains("controllers") && (*controller)["controllers"].is_array())
        for (const auto& reference : (*controller)["controllers"])
            appendSettings(reference, true);
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.connectionIndex != right.connectionIndex
                   ? left.connectionIndex < right.connectionIndex
                   : left.jointId < right.jointId;
    });
    return result;
}

bool EditingActions::setControllerJointFrameValue(int index,
                                                  std::int64_t referenceId,
                                                  int frameIndex,
                                                  int value) {
    if (index < 0 || index >= (int)blocks_.size() ||
        !isPhysicalController(blocks_[index], partDisplayName(index)) || frameIndex < 0)
        return false;
    json* controller = controllerForPart(index);
    if (!controller)
        return false;
    if (!blueprint_.is_object() || !blueprint_.contains("joints") ||
        !blueprint_["joints"].is_array())
        return false;
    auto updateReference = [&](json& reference, bool controllerPistonSequence) {
        if (!reference.is_object() || !reference.contains("id") ||
            !reference["id"].is_number_integer())
            return false;
        try {
            if (reference["id"].get<std::int64_t>() != referenceId)
                return false;
        } catch (...) {
            return false;
        }
        if (!reference.contains("frames") || !reference["frames"].is_array() ||
            frameIndex >= (int)reference["frames"].size())
            return false;
        bool piston = false;
        for (const auto& joint : blueprint_["joints"]) {
            if (!joint.is_object())
                continue;
            const json* identity = nullptr;
            if (controllerPistonSequence) {
                if (joint.contains("controller") && joint["controller"].is_object() &&
                    joint["controller"].contains("id"))
                    identity = &joint["controller"]["id"];
            } else if (joint.contains("id")) {
                identity = &joint["id"];
            }
            if (!identity || !identity->is_number_integer() || !joint.contains("shapeId") ||
                !joint["shapeId"].is_string())
                continue;
            try {
                if (identity->get<std::int64_t>() == referenceId) {
                    const auto shape = state.shapeConnectionCapabilities.find(
                        lower(joint["shapeId"].get<std::string>()));
                    piston = shape != state.shapeConnectionCapabilities.end() &&
                             (shape->second & ShapePistonJoint) != 0;
                    break;
                }
            } catch (...) {
            }
        }
        if (controllerPistonSequence && !piston)
            return false;
        auto& frames = reference["frames"];
        if (!frames[frameIndex].is_object())
            frames[frameIndex] = json::object();
        const char* frameKey = controllerPistonSequence
                                   ? "setting"
                                   : (piston ? "targetLength" : "targetAngle");
        const int maxValue = controllerPistonSequence ? 15 : 16;
        frames[frameIndex][frameKey] = piston ? std::clamp(value, 0, maxValue) : value;
        if (!frames[0].is_object())
            frames[0] = json::object();
        if (!piston) {
            long long totalAngle = 0;
            for (const auto& frame : frames)
                if (frame.is_object() && frame.contains(frameKey) &&
                    frame[frameKey].is_number())
                    try {
                        const double storedValue = frame[frameKey].get<double>();
                        if (std::isfinite(storedValue))
                            totalAngle += (long long)std::clamp(
                                std::llround(storedValue),
                                (long long)-1000000000,
                                (long long)1000000000);
                    } catch (...) {
                    }
            reference["startAngle"] = frames[0].value(frameKey, 0);
            reference["endAngle"] = (int)std::clamp(
                totalAngle,
                (long long)std::numeric_limits<int>::min(),
                (long long)std::numeric_limits<int>::max());
        }
        return true;
    };
    if (controller->contains("joints") && (*controller)["joints"].is_array())
        for (auto& reference : (*controller)["joints"])
            if (updateReference(reference, false))
                return true;
    if (controller->contains("controllers") && (*controller)["controllers"].is_array())
        for (auto& reference : (*controller)["controllers"])
            if (updateReference(reference, true))
                return true;
    return false;
}

bool EditingActions::setControllerJointReverse(int index, std::int64_t jointId, bool reverse) {
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
