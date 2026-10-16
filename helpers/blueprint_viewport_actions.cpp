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
constexpr const char *logicGateShapeId = "9f0f56e8-2c31-4d83-996c-d00a9b296c3f";
constexpr const char *controllerShapeId = "12862001-666c-4bdd-8326-a1f43610d28b";
}

EditingActions::EditingActions(json &blueprint, const std::filesystem::path &blueprintPath,
                               std::vector<Parser::Block> &blocks, const json &items)
    : blueprint_(blueprint), blueprintPath_(blueprintPath), blocks_(blocks), items_(items) {}

bool EditingActions::isSelected(int index) const {
  return index == state.selection || state.selectionSet.contains(index);
}

namespace {
bool isPhysicalController(const Parser::Block &part, const std::string &displayName) {
  return !part.isJoint &&
         (lower(part.shapeID) == controllerShapeId || lower(displayName) == "controller");
}
}

json *EditingActions::controllerForPart(int index) {
  if (index < 0 || index >= (int)blocks_.size() || blocks_[index].isJoint)
    return nullptr;
  const auto &part = blocks_[index];
  if (part.bodyIndex < 0 || part.childIndex < 0 || !blueprint_.is_object() || !blueprint_.contains("bodies") ||
      !blueprint_["bodies"].is_array() || part.bodyIndex >= (int)blueprint_["bodies"].size())
    return nullptr;
  auto &body = blueprint_["bodies"][part.bodyIndex];
  if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() ||
      part.childIndex >= (int)body["childs"].size())
    return nullptr;
  auto &child = body["childs"][part.childIndex];
  return child.is_object() && child.contains("controller") && child["controller"].is_object() ? &child["controller"]
                                                                                              : nullptr;
}

std::int64_t EditingActions::controllerIdForPart(int index) {
  json *controller = controllerForPart(index);
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
  const auto &part = blocks_[index];
  if (part.bodyIndex < 0 || part.childIndex < 0 || !blueprint_.is_object() ||
      !blueprint_.contains("bodies") || !blueprint_["bodies"].is_array() ||
      part.bodyIndex >= (int)blueprint_["bodies"].size())
    return 0;
  const auto &body = blueprint_["bodies"][part.bodyIndex];
  if (!body.is_object() || !body.contains("childs") || !body["childs"].is_array() ||
      part.childIndex >= (int)body["childs"].size())
    return 0;
  const auto &child = body["childs"][part.childIndex];
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
    json *controller = controllerForPart(index);
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
  json *controller = controllerForPart(index);
  if (!controller || !controller->contains("joints") || !(*controller)["joints"].is_array() ||
      !blueprint_.is_object() || !blueprint_.contains("joints") || !blueprint_["joints"].is_array())
    return result;

  for (const auto &reference : (*controller)["joints"]) {
    if (!reference.is_object() || !reference.contains("id") || !reference["id"].is_number_integer() ||
        !reference.contains("frames") || !reference["frames"].is_array())
      continue;
    std::int64_t jointId = -1;
    try {
      jointId = reference["id"].get<std::int64_t>();
    } catch (...) {
      continue;
    }
    const json *joint = nullptr;
    for (const auto &candidate : blueprint_["joints"])
      if (candidate.is_object() && candidate.contains("id") && candidate["id"].is_number_integer()) {
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
    for (const auto &frame : reference["frames"]) {
      int angle = 0;
      if (frame.is_object() && frame.contains("targetAngle") && frame["targetAngle"].is_number())
        try {
          const double value = frame["targetAngle"].get<double>();
          angle = (int)std::clamp(std::llround(value), (long long)std::numeric_limits<int>::min(),
                                  (long long)std::numeric_limits<int>::max());
        } catch (...) {
        }
      settings.frameAngles.push_back(angle);
    }
    result.push_back(std::move(settings));
  }
  std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
    return left.connectionIndex != right.connectionIndex ? left.connectionIndex < right.connectionIndex
                                                         : left.jointId < right.jointId;
  });
  return result;
}

bool EditingActions::setControllerBearingAngle(int index, std::int64_t jointId, int frameIndex, int angle) {
  if (index < 0 || index >= (int)blocks_.size() ||
      !isPhysicalController(blocks_[index], partDisplayName(index)) || frameIndex < 0)
    return false;
  json *controller = controllerForPart(index);
  if (!controller || !controller->contains("joints") || !(*controller)["joints"].is_array())
    return false;
  for (auto &reference : (*controller)["joints"]) {
    if (!reference.is_object() || !reference.contains("id") || !reference["id"].is_number_integer())
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
    auto &frames = reference["frames"];
    if (!frames[frameIndex].is_object())
      frames[frameIndex] = json::object();
    frames[frameIndex]["targetAngle"] = angle;
    if (!frames[0].is_object())
      frames[0] = json::object();
    long long totalAngle = 0;
    for (const auto &frame : frames)
      if (frame.is_object() && frame.contains("targetAngle") && frame["targetAngle"].is_number())
        try {
          const double value = frame["targetAngle"].get<double>();
          if (std::isfinite(value))
            totalAngle += (long long)std::clamp(std::llround(value), (long long)-1000000000,
                                                (long long)1000000000);
        } catch (...) {
        }
    reference["startAngle"] = frames[0].value("targetAngle", 0);
    reference["endAngle"] = (int)std::clamp(totalAngle, (long long)std::numeric_limits<int>::min(),
                                            (long long)std::numeric_limits<int>::max());
    return true;
  }
  return false;
}

bool EditingActions::setControllerBearingReverse(int index, std::int64_t jointId, bool reverse) {
  if (index < 0 || index >= (int)blocks_.size() ||
      !isPhysicalController(blocks_[index], partDisplayName(index)))
    return false;
  json *controller = controllerForPart(index);
  if (!controller || !controller->contains("joints") || !(*controller)["joints"].is_array())
    return false;
  for (auto &reference : (*controller)["joints"]) {
    if (!reference.is_object() || !reference.contains("id") || !reference["id"].is_number_integer())
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

void EditingActions::selectPart(int index, bool additive) {
  if (index < 0 || index >= (int)blocks_.size())
    return;
  if (state.logicManipulation) {
    if (controllerIdForPart(index) < 0) {
      BlueprintViewport::notify("That part cannot be used as a logic source");
      return;
    }
    state.selection = index;
    state.selectionSet.clear();
    state.selectionSet.insert(index);
    state.logicSource = index;
    BlueprintViewport::notify("Logic source selected from the parts list");
    return;
  }
  if (!additive)
    state.selectionSet.clear();
  state.selection = index;
  addSelectionGroup(index);
  state.dragSelection.clear();
  state.dragAxis = 0;
}

void EditingActions::selectAllOfType(const std::string &shapeId, bool additive) {
  const std::string wanted = lower(shapeId);
  if (!additive) {
    state.selectionSet.clear();
    state.selection = -1;
  }
  int firstMatch = -1;
  size_t added = 0;
  for (int index = 0; index < (int)blocks_.size(); ++index) {
    const auto &part = blocks_[index];
    if (lower(part.shapeID) == wanted) {
      if (firstMatch < 0)
        firstMatch = index;
      added += state.selectionSet.insert(index).second ? 1 : 0;
    }
  }
  if (firstMatch >= 0)
    state.selection = firstMatch;
  state.stackSelection = false;
  state.dragSelection.clear();
  state.dragAxis = 0;
  BlueprintViewport::notify("Added " + std::to_string(added) + " part(s) of this type; " +
                            std::to_string(state.selectionSet.size()) + " selected total");
}

void EditingActions::addTurretSeatSelection(int index) {
  if (index < 0 || index >= (int)blocks_.size())
    return;
  const auto &part = blocks_[index];
  constexpr std::string_view sphereId = "42b4c02e-2de5-431e-981a-f42cb7829e68";
  constexpr std::string_view baseId = "44afe29b-73a8-4892-94c9-f885b238a971";
  const auto partId = lower(part.shapeID);
  const bool isSphere = partId == sphereId, isBase = partId == baseId;
  if (!isSphere && !isBase)
    return;
  int mate = -1, nearestZ = std::numeric_limits<int>::max();
  for (size_t candidate = 0; candidate < blocks_.size(); ++candidate) {
    if ((int)candidate == index)
      continue;
    const auto &other = blocks_[candidate];
    if (lower(other.shapeID) != (isSphere ? baseId : sphereId) || other.xaxis != part.xaxis ||
        other.zaxis != part.zaxis)
      continue;
    if (other.pos.x != part.pos.x || other.pos.y != part.pos.y)
      continue;
    const int dz = std::abs(other.pos.z - part.pos.z);
    if (dz <= 1 && dz < nearestZ) {
      nearestZ = dz;
      mate = (int)candidate;
    }
  }
  if (mate >= 0)
    state.selectionSet.insert(mate);
}

void EditingActions::addSelectionGroup(int index) {
  if (index < 0 || index >= (int)blocks_.size())
    return;
  state.selectionSet.insert(index);
  addTurretSeatSelection(index);
}

void EditingActions::synchronizeJointPositions() {
  Parser::applyBlockListToNode(blocks_, blueprint_);
  if (!blueprint_.contains("joints") || !blueprint_["joints"].is_array())
    return;
  auto readPosition = [](const json &value) {
    auto component = [&](const char *key) {
      if (!value.is_object() || !value.contains(key) || !value[key].is_number_integer())
        return 0;
      try {
        return value[key].get<int>();
      } catch (...) {
        return 0;
      }
    };
    return Parser::Position{component("x"), component("y"), component("z")};
  };
  for (auto &block : blocks_) {
    if (!block.isJoint || block.jointIndex < 0 || block.jointIndex >= (int)blueprint_["joints"].size())
      continue;
    const auto &joint = blueprint_["joints"][block.jointIndex];
    if (!joint.is_object() || !joint.contains("posA") || !joint.contains("posB"))
      continue;
    block.jointPosA = readPosition(joint["posA"]);
    block.jointPosB = readPosition(joint["posB"]);
    block.pos = block.jointPosA;
  }
}

void EditingActions::moveSelection(int x, int y, int z) {
  if (state.selectionSet.empty() && state.selection >= 0)
    state.selectionSet.insert(state.selection);
  if (state.stackSelection && state.selection >= 0 && state.selection < (int)blocks_.size() &&
      !blocks_[state.selection].isJoint) {
    const auto &active = blocks_[state.selection];
    const Parser::Position target{active.pos.x + x, active.pos.y + y, active.pos.z + z};
    for (int index : state.selectionSet)
      if (index >= 0 && index < (int)blocks_.size() && !blocks_[index].isJoint)
        blocks_[index].pos = target;
  } else {
    for (int index : state.selectionSet)
      if (index >= 0 && index < (int)blocks_.size() && !blocks_[index].isJoint) {
        blocks_[index].pos.x += x;
        blocks_[index].pos.y += y;
        blocks_[index].pos.z += z;
      }
  }
  synchronizeJointPositions();
}

V3 EditingActions::selectedWorldSize() const {
  const float infinity = std::numeric_limits<float>::infinity();
  V3 minimum{infinity, infinity, infinity}, maximum{-infinity, -infinity, -infinity};
  bool found = false;
  for (size_t index = 0; index < blocks_.size(); index++)
    if (isSelected((int)index)) {
      const auto &part = blocks_[index];
      const V3 size = shapeSize(part);
      const auto placement = placementTransform(part, size);
      const V3 center = placement.translation;
      const V3 half = size * .5f;
      for (int mask = 0; mask < 8; mask++) {
        const V3 corner =
            center +
            orient({mask & 1 ? half.x : -half.x, mask & 2 ? half.y : -half.y, mask & 4 ? half.z : -half.z}, part);
        minimum.x = std::min(minimum.x, corner.x);
        minimum.y = std::min(minimum.y, corner.y);
        minimum.z = std::min(minimum.z, corner.z);
        maximum.x = std::max(maximum.x, corner.x);
        maximum.y = std::max(maximum.y, corner.y);
        maximum.z = std::max(maximum.z, corner.z);
      }
      found = true;
    }
  return found ? maximum - minimum : V3{};
}

void EditingActions::setSelectionColor(const std::string &color) {
  bool applied = false;
  for (int index : state.selectionSet)
    if (index >= 0 && index < (int)blocks_.size()) {
      blocks_[index].color = color;
      blocks_[index].hasColor = true;
      applied = true;
    }
  if (!applied && state.selection >= 0 && state.selection < (int)blocks_.size()) {
    blocks_[state.selection].color = color;
    blocks_[state.selection].hasColor = true;
  }
}

void EditingActions::saveBlueprint() {
  Parser::applyBlockListToNode(blocks_, blueprint_);
  if (Parser::saveBlueprint(blueprintPath_.string(), blueprint_)) {
    state.error.clear();
    BlueprintViewport::notify("Blueprint saved");
  } else
    state.error = "Could not save blueprint. Check file permissions.";
}

void EditingActions::restoreHistory(std::size_t cursor) {
  if (cursor >= state.blueprintHistory.size())
    return;
  state.historyCursor = cursor;
  blueprint_ = state.blueprintHistory[cursor];
  blocks_ = Parser::parseBlueprint(blueprint_);
  state.selection = -1;
  state.selectionSet.clear();
  state.dragSelection.clear();
  state.dragAxis = 0;
  state.stackSelection = false;
  state.historyActionInProgress = false;
  state.error.clear();
}

void EditingActions::undo() {
  if (state.historyCursor > 0) {
    restoreHistory(state.historyCursor - 1);
    BlueprintViewport::notify("Edit undone");
  }
}

void EditingActions::redo() {
  if (state.historyCursor + 1 < state.blueprintHistory.size()) {
    restoreHistory(state.historyCursor + 1);
    BlueprintViewport::notify("Edit redone");
  }
}

bool EditingActions::removeSelection() {
  std::vector<std::pair<int, int>> children;
  std::vector<int> joints;
  for (size_t i = 0; i < blocks_.size(); ++i)
    if (isSelected((int)i)) {
      if (blocks_[i].isJoint)
        joints.push_back(blocks_[i].jointIndex);
      else
        children.emplace_back(blocks_[i].bodyIndex, blocks_[i].childIndex);
    }
  if (children.empty() && joints.empty())
    return false;
  Parser::applyBlockListToNode(blocks_, blueprint_);
  std::sort(joints.begin(), joints.end(), std::greater<int>());
  bool removed = false;
  for (int joint : joints)
    removed = Parser::removeJointFromNode(blueprint_, joint) || removed;
  std::sort(children.begin(), children.end(),
            [](const auto &a, const auto &b) { return a.first != b.first ? a.first > b.first : a.second > b.second; });
  for (const auto &child : children)
    removed = Parser::removeBlockFromNode(blueprint_, child.first, child.second) || removed;
  if (removed) {
    blocks_ = Parser::parseBlueprint(blueprint_);
    state.selection = -1;
    state.selectionSet.clear();
    state.dragSelection.clear();
    state.dragAxis = 0;
    state.error = "Selected parts removed. Save Blueprint to write the change to disk.";
  }
  return removed;
}

void EditingActions::copySelectedObjects() {
  std::vector<int> selected;
  selected.reserve(state.selectionSet.size() + (state.selection >= 0 ? 1u : 0u));
  for (int index : state.selectionSet)
    if (index >= 0 && index < (int)blocks_.size())
      selected.push_back(index);
  if (state.selection >= 0 && state.selection < (int)blocks_.size() &&
      std::find(selected.begin(), selected.end(), state.selection) == selected.end())
    selected.push_back(state.selection);
  std::sort(selected.begin(), selected.end());
  if (selected.empty()) {
    BlueprintViewport::notify("Select objects to copy");
    return;
  }
  Parser::applyBlockListToNode(blocks_, blueprint_);
  Parser::SelectionClipboard copied;
  if (!Parser::captureSelectionClipboard(blueprint_, blocks_, selected, state.copyBlockState, copied)) {
    BlueprintViewport::notify("Nothing selected could be copied");
    return;
  }
  state.clipboard = std::move(copied);
  state.clipboardBlueprint = blueprintPath_;
  state.clipboardPasteCount = 0;
  BlueprintViewport::notify("Copied " + std::to_string(state.clipboard.objectCount()) + " object(s)");
}

void EditingActions::pasteCopiedObjects() {
  if (state.clipboard.empty() || state.clipboardBlueprint != blueprintPath_) {
    BlueprintViewport::notify("Clipboard is empty for this blueprint");
    return;
  }
  Parser::applyBlockListToNode(blocks_, blueprint_);
  const int pasteOffset = (int)std::min<std::size_t>(state.clipboardPasteCount + 1, 1000000);
  Parser::SelectionClipboard pasteClipboard = state.clipboard;
  // Treat the option as a paste-time choice so users can copy first and then
  // decide whether this paste should duplicate controller state and wiring.
  pasteClipboard.copyBlockState = state.copyBlockState;
  Parser::PasteSelectionResult pasted;
  if (!Parser::pasteSelectionClipboard(blueprint_, pasteClipboard, {pasteOffset, 0, 0}, pasted)) {
    BlueprintViewport::notify("Could not paste the copied objects");
    return;
  }
  blocks_ = Parser::parseBlueprint(blueprint_);
  state.selection = -1;
  state.selectionSet.clear();
  for (int index = 0; index < (int)blocks_.size(); ++index) {
    const auto &block = blocks_[index];
    if (block.isJoint) {
      if (std::find(pasted.joints.begin(), pasted.joints.end(), block.jointIndex) != pasted.joints.end())
        state.selectionSet.insert(index);
    } else if (std::find(pasted.children.begin(), pasted.children.end(),
                         std::pair<int, int>{block.bodyIndex, block.childIndex}) != pasted.children.end())
      state.selectionSet.insert(index);
  }
  if (!state.selectionSet.empty())
    state.selection = *std::min_element(state.selectionSet.begin(), state.selectionSet.end());
  state.stackSelection = false;
  state.logicSource = -1;
  state.dragSelection.clear();
  state.dragAxis = 0;
  ++state.clipboardPasteCount;
  const size_t copiedCount = pasted.children.size() + pasted.joints.size();
  BlueprintViewport::notify("Pasted " + std::to_string(copiedCount) + " object(s) at +X " +
                            std::to_string(pasteOffset) +
                            (state.copyBlockState ? " with block state" : " without block state"));
}
} // namespace BlueprintViewport::Internal
