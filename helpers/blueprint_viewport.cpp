#define NOMINMAX
#include "blueprint_viewport.hpp"
#include "../imgui/imgui.h"
#include "blueprint_viewport_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BlueprintViewport {
namespace Internal {
State state;
}

using namespace Internal;

namespace {
struct Camera {
  ImVec2 center{};
  float scale{}, cy{}, sy{}, cp{}, sp{};
};
ImVec2 project(V3 v, const Camera &c, float &depth) {
  // Scrap Mechanic blueprint coordinates are Z-up: X/Y lie on the ground.
  float xx = c.cy * v.x - c.sy * v.y, horizontalDepth = c.sy * v.x + c.cy * v.y;
  float yy = c.cp * v.z - c.sp * horizontalDepth;
  depth = c.sp * v.z + c.cp * horizontalDepth;
  // Game blueprints present increasing X toward screen-left in the
  // canonical view. Reflect the camera's horizontal screen axis here,
  // so geometry, grid, picking, and drag handles share that convention
  // without altering stored positions or part orientation.
  return {c.center.x - c.scale * xx, c.center.y - c.scale * yy};
}
V3 blockCenter(const Parser::Block &b) {
  const V3 size = shapeSize(b);
  const auto placement = placementTransform(b, {size.x, size.y, size.z});
  return {placement.translation.x, placement.translation.y, placement.translation.z};
}

void nudge(Parser::Block &b, int x, int y, int z) {
  b.pos.x += x;
  b.pos.y += y;
  b.pos.z += z;
}
} // namespace

void shutdownRenderer() {
  Internal::releaseTextureCache();
  Internal::shutdownSceneRenderer();
}

void resetSelection() {
  state.selection = -1;
  state.selectionSet.clear();
  state.dragSelection.clear();
  state.dragAxis = 0;
  state.blueprintHistory.clear();
  state.historyCursor = 0;
  state.historyActionInProgress = false;
}

void notify(const std::string &message) {
  state.notification = message;
  state.notificationUntil = ImGui::GetTime() + 2.4;
}

void render(json &blueprint, const std::filesystem::path &blueprintPath, std::vector<Parser::Block> &blocks,
            const json &items, const std::string &blueprintName, bool &backToSelectionRequested) {
  loadCatalog();
  if (state.currentBlueprint != blueprintPath) {
    state.meshes.clear();
    state.meshFiles.clear();
    state.meshCacheBytes = 0;
    state.currentBlueprint = blueprintPath;
    state.selection = -1;
    state.selectionSet.clear();
    state.stackSelection = false;
    state.logicManipulation = false;
    state.logicSource = -1;
    state.dragSelection.clear();
    state.dragAxis = 0;
    state.pan = {0, 0};
    state.zoom = 1.f;
    state.blueprintHistory.clear();
    state.blueprintHistory.push_back(blueprint);
    Parser::applyBlockListToNode(blocks, state.blueprintHistory.back());
    state.historyCursor = 0;
    state.historyActionInProgress = false;
  }
  if (state.clipboardBlueprint != blueprintPath) {
    state.clipboard = {};
    state.clipboardBlueprint = blueprintPath;
    state.clipboardPasteCount = 0;
  }
  for (auto it = state.selectionSet.begin(); it != state.selectionSet.end();)
    if (*it < 0 || *it >= (int)blocks.size())
      it = state.selectionSet.erase(it);
    else
      ++it;
  if (state.selection < 0 || state.selection >= (int)blocks.size())
    state.selection = -1;
  if (state.selection >= 0)
    state.selectionSet.insert(state.selection);
  EditingActions actions(blueprint, blueprintPath, blocks, items);
  if (state.logicSource < 0 || state.logicSource >= (int)blocks.size() ||
      actions.controllerIdForPart(state.logicSource) < 0)
    state.logicSource = -1;
  auto isSelected = [&actions](int index) { return actions.isSelected(index); };
  auto controllerForPart = [&actions](int index) { return actions.controllerForPart(index); };
  auto controllerIdForPart = [&actions](int index) { return actions.controllerIdForPart(index); };
  auto partDisplayName = [&actions](int index) { return actions.partDisplayName(index); };
  auto addSelectionGroup = [&actions](int index) { actions.addSelectionGroup(index); };
  auto synchronizeJointPositions = [&actions]() { actions.synchronizeJointPositions(); };
  auto moveSelection = [&actions](int x, int y, int z) { actions.moveSelection(x, y, z); };
  auto selectedWorldSize = [&actions]() { return actions.selectedWorldSize(); };
  auto setSelectionColor = [&actions](const std::string &color) { actions.setSelectionColor(color); };
  auto removeSelection = [&actions]() { return actions.removeSelection(); };
  auto logicGateModeForPart = [&blueprint, &blocks](int index) {
    if (index < 0 || index >= (int)blocks.size() || blocks[index].isJoint ||
        blocks[index].bodyIndex < 0 || blocks[index].childIndex < 0 ||
        !blueprint.is_object() || !blueprint.contains("bodies") ||
        !blueprint["bodies"].is_array() ||
        blocks[index].bodyIndex >= (int)blueprint["bodies"].size())
      return 0;
    const auto &body = blueprint["bodies"][blocks[index].bodyIndex];
    if (!body.is_object() || !body.contains("childs") ||
        !body["childs"].is_array() ||
        blocks[index].childIndex >= (int)body["childs"].size())
      return 0;
    const auto &child = body["childs"][blocks[index].childIndex];
    if (!child.is_object() || !child.contains("controller") ||
        !child["controller"].is_object() ||
        !child["controller"].contains("mode") ||
        !child["controller"]["mode"].is_number_integer())
      return 0;
    try {
      const int mode = child["controller"]["mode"].get<int>();
      return mode >= 0 && mode <= 5 ? mode : 0;
    } catch (...) {
      return 0;
    }
  };
  ImGui::Begin("Blueprint Editor");
  const ImGuiIO &io = ImGui::GetIO();
  const bool shortcutReady = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput &&
                             !ImGui::IsAnyItemActive() &&
                             !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
  if (shortcutReady && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
    actions.undo();
  else if (shortcutReady && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
    actions.redo();
  else if (shortcutReady && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
    actions.saveBlueprint();
  else if (shortcutReady && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
    actions.copySelectedObjects();
  else if (shortcutReady && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
    actions.pasteCopiedObjects();
  if (shortcutReady && state.logicManipulation && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    state.logicManipulation = false;
    state.logicSource = -1;
    notify("Logic manipulation disabled");
  }
  if (shortcutReady && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    actions.removeSelection();
  ImGui::TextWrapped("Editing: %s", blueprintName.c_str());
  if (ImGui::Button("Back to Selection"))
    backToSelectionRequested = true;
  ImGui::Spacing();
  ImGui::Text("3D blueprint editor");
  ImGui::SameLine();
  ImGui::TextDisabled("%zu parts  |  %zu grid cells", blocks.size(), gridCellCount(blocks));
  ImGui::SameLine();
  if (ImGui::Button("Save Blueprint"))
    actions.saveBlueprint();
  ImGui::SameLine();
  ImGui::BeginDisabled(state.historyCursor == 0);
  if (ImGui::Button("Undo"))
    actions.undo();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(state.historyCursor + 1 >= state.blueprintHistory.size());
  if (ImGui::Button("Redo"))
    actions.redo();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Reset view")) {
    state.yaw = .72f;
    state.pitch = .42f;
    state.zoom = 1.f;
    state.pan = {0, 0};
    notify("View reset");
  }
  ImGui::Spacing();
  const bool hasCopySelection = state.selection >= 0 || !state.selectionSet.empty();
  ImGui::BeginDisabled(!hasCopySelection);
  if (ImGui::Button("Copy"))
    actions.copySelectedObjects();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(state.clipboard.empty() || state.clipboardBlueprint != blueprintPath);
  if (ImGui::Button("Paste"))
    actions.pasteCopiedObjects();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::Checkbox("Copy block state (connections)", &state.copyBlockState);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("When enabled, Paste copies controller settings and wiring. Links "
                      "between copied parts point to their pasted copies. Copied parts "
                      "keep links to original targets, and unselected sources also link "
                      "to pasted targets. Toggle before paste.");
  ImGui::SameLine();
  ImGui::TextDisabled("Ctrl+C / Ctrl+V");
  ImGui::Spacing();
  if (ImGui::Button(state.showLogicGateLabels ? "Gate Labels: On"
                                               : "Gate Labels: Off")) {
    state.showLogicGateLabels = !state.showLogicGateLabels;
    notify(state.showLogicGateLabels ? "Logic gate labels enabled"
                                     : "Logic gate labels disabled");
  }
  ImGui::SameLine();
  const bool logicButtonWasActive = state.logicManipulation;
  if (logicButtonWasActive) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.13f, .43f, .48f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(.17f, .53f, .58f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(.10f, .36f, .41f, 1.f));
  }
  if (ImGui::Button(state.logicManipulation ? "Disable Logic manipulation" : "Enable Logic manipulation")) {
    state.logicManipulation = !state.logicManipulation;
    state.logicSource = -1;
    state.dragSelection.clear();
    state.dragAxis = 0;
    notify(state.logicManipulation ? "Logic manipulation enabled" : "Logic manipulation disabled");
  }
  if (state.logicManipulation)
    ImGui::SameLine();
  if (state.logicManipulation)
    ImGui::TextWrapped("Click a source, then targets. Click the source again to wire it into "
                       "itself. Shift-click to switch sources; click empty space to clear "
                       "selection. Click an arrow to remove a link. Esc exits.");
  if (logicButtonWasActive)
    ImGui::PopStyleColor(3);
  float avail = ImGui::GetContentRegionAvail().x;
  float inspector = std::clamp(avail * .25f, 230.f, 330.f);
  ImGui::BeginChild("BlueprintViewport", ImVec2(std::max(120.f, avail - inspector - 8), 0), true,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImVec2 origin = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
  size.x = std::max(size.x, 120.f);
  size.y = std::max(size.y, 120.f);
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(25, 28, 33, 255));
  ImGui::InvisibleButton("##scene", size,
                         ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  ImVec2 mouse = ImGui::GetIO().MousePos;
  if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
    ImVec2 d = ImGui::GetIO().MouseDelta;
    state.yaw += d.x * .008f;
    state.pitch = std::clamp(state.pitch + d.y * .008f, .08f, 1.35f);
  }
  if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
    ImVec2 d = ImGui::GetIO().MouseDelta;
    state.pan.x += d.x;
    state.pan.y += d.y;
  }
  if (hovered && ImGui::GetIO().MouseWheel != 0)
    state.zoom = std::clamp(state.zoom * std::pow(1.12f, ImGui::GetIO().MouseWheel), .12f, 5.f);

  float minY = std::numeric_limits<float>::max(), maxY = -minY, minX = minY, maxX = -minY, minZ = minY, maxZ = -minY;
  for (const auto &b : blocks) {
    const V3 center = blockCenter(b), dimensions = shapeSize(b), half = dimensions * .5f;
    for (int mask = 0; mask < 8; mask++) {
      const V3 c =
          center + orient({mask & 1 ? half.x : -half.x, mask & 2 ? half.y : -half.y, mask & 4 ? half.z : -half.z}, b);
      minX = std::min(minX, c.x);
      maxX = std::max(maxX, c.x);
      minY = std::min(minY, c.y);
      maxY = std::max(maxY, c.y);
      minZ = std::min(minZ, c.z);
      maxZ = std::max(maxZ, c.z);
    }
  }
  if (blocks.empty()) {
    minX = minY = minZ = -2;
    maxX = maxY = maxZ = 2;
  }
  V3 focus{(minX + maxX) * .5f, (minY + maxY) * .5f, (minZ + maxZ) * .5f};
  const float longest = std::max({maxX - minX, maxY - minY, maxZ - minZ, 1.f});
  const float fitScale = std::min(size.x, size.y) * .74f / std::max(8.f, longest * 1.65f);
  Camera camera;
  camera.center = {origin.x + size.x * .5f + state.pan.x, origin.y + size.y * .52f + state.pan.y};
  camera.scale = std::clamp(fitScale * state.zoom, .25f, 140.f);
  camera.cy = std::cos(state.yaw);
  camera.sy = std::sin(state.yaw);
  camera.cp = std::cos(state.pitch);
  camera.sp = std::sin(state.pitch);
  auto screen = [&](V3 v, float &d) { return project(v - focus, camera, d); };
  // Infinite feeling ground plane, with a restrained scale-aware grid.
  int span = (int)std::clamp(std::max(maxX - minX, maxY - minY) + 8.f, 12.f, 120.f);
  int stride = span > 48 ? 4 : 1;
  int gx0 = (int)std::floor(focus.x) - span / 2, gy0 = (int)std::floor(focus.y) - span / 2;
  for (int k = 0; k <= span; k += stride) {
    float d;
    auto a = screen({(float)(gx0 + k), (float)gy0, minZ}, d),
         b = screen({(float)(gx0 + k), (float)(gy0 + span), minZ}, d);
    dl->AddLine(a, b, IM_COL32(66, 72, 82, 90));
    a = screen({(float)gx0, (float)(gy0 + k), minZ}, d);
    b = screen({(float)(gx0 + span), (float)(gy0 + k), minZ}, d);
    dl->AddLine(a, b, IM_COL32(66, 72, 82, 90));
  }

  struct WorldTri {
    std::array<ImVec2, 3> p;
    std::array<float, 3> vertexDepth;
    std::array<ImU32, 3> color;
    std::array<std::array<float, 2>, 3> uv;
    GLuint texture{};
    int block;
  };
  std::vector<WorldTri> triangles;
  std::vector<std::pair<int, std::array<ImVec2, 8>>> selectedOutlines;
  bool hasSelectedOutline = false;
  V3 selectedCenter{};
  size_t total = 0;
  for (const auto &b : blocks) {
    auto m = meshFor(b.shapeID);
    total += m->loaded ? m->indices.size() / 3 : 12;
  }
  // Keep per-frame projection and streamed GPU memory bounded, but let normal
  // creations use the actual part meshes. The former 15k triangle cutoff was
  // low enough that a handful of detailed parts switched the entire scene to
  // boxes, which made ordinary creations appear to have no models at all.
  constexpr size_t maximumSceneTriangles = 120000;
  constexpr size_t maximumPreviewBlocks = 10000;
  const bool coarsePreview = total > maximumSceneTriangles;
  const size_t previewStride =
      coarsePreview ? std::max<size_t>(1, (blocks.size() + maximumPreviewBlocks - 1) / maximumPreviewBlocks) : 1;
  std::unordered_set<int> coarsePreviewParts;
  if (coarsePreview) {
    for (size_t index = 0; index < blocks.size(); index += previewStride)
      coarsePreviewParts.insert((int)index);
    std::vector<int> selectedParts;
    if (state.selection >= 0 && state.selection < (int)blocks.size())
      selectedParts.push_back(state.selection);
    for (int index : state.selectionSet)
      if (index >= 0 && index < (int)blocks.size() && index != state.selection)
        selectedParts.push_back(index);
    for (int index : selectedParts) {
      if (coarsePreviewParts.contains(index))
        continue;
      if (coarsePreviewParts.size() < maximumPreviewBlocks) {
        coarsePreviewParts.insert(index);
        continue;
      }
      auto replace = coarsePreviewParts.end();
      for (auto it = coarsePreviewParts.begin(); it != coarsePreviewParts.end(); ++it)
        if (!isSelected(*it) && (replace == coarsePreviewParts.end() || *it > *replace))
          replace = it;
      if (replace != coarsePreviewParts.end()) {
        coarsePreviewParts.erase(replace);
        coarsePreviewParts.insert(index);
      }
    }
  }
  const size_t perPartTriangleBudget =
      coarsePreview ? std::max<size_t>(12, maximumSceneTriangles / std::max<size_t>(1, coarsePreviewParts.size()))
                    : total;
  triangles.reserve(coarsePreview ? maximumSceneTriangles : total);
  for (size_t bi = 0; bi < blocks.size(); bi++) {
    if (coarsePreview && !coarsePreviewParts.contains((int)bi))
      continue;
    const auto &b = blocks[bi];
    auto m = meshFor(b.shapeID);
    bool imported = m && m->loaded && !m->indices.empty();
    const int col = displayColor(b);
    const auto diffusePath = state.shapeDiffuseTextures.find(lower(b.shapeID));
    const GLuint diffuseTexture =
        imported && diffusePath != state.shapeDiffuseTextures.end() ? diffuseTextureFor(diffusePath->second) : 0;
    float red = (float)((col >> 16) & 255) / 255.f, green = (float)((col >> 8) & 255) / 255.f,
          blue = (float)(col & 255) / 255.f;
    const V3 dimensions = shapeSize(b);
    const auto catalogEntry = state.catalog.find(lower(b.shapeID));
    const bool isLogicGate =
        lower(b.shapeID) == "9f0f56e8-2c31-4d83-996c-d00a9b296c3f";
    const int logicGateMode = isLogicGate ? logicGateModeForPart((int)bi) : 0;
    GLuint gateModeTexture = 0;
    if (isLogicGate && catalogEntry != state.catalog.end()) {
      const auto screenMaterial =
          catalogEntry->second.materialTextures.find("screen");
      if (screenMaterial != catalogEntry->second.materialTextures.end())
        gateModeTexture =
            logicGateModeTextureFor(screenMaterial->second, logicGateMode);
    }
    const bool centerMeshOnFootprint =
        catalogEntry != state.catalog.end() &&
        catalogEntry->second.centerMeshOnFootprint;
    // Fixed render meshes are authored in game-space units and their origins
    // are meaningful attachment pivots. Do not scale them to their collision
    // hull/cylinder or their geometry and pivots drift away from blueprint
    // positions. Explicit blueprint bounds are the only render-scale request.
    float sc[3] = {1.f, 1.f, 1.f};
    V3 lo{}, hi{};
    if (imported && m->hasBounds) {
      lo = m->boundsMin;
      hi = m->boundsMax;
      if (b.hasBounds) {
        sc[0] = dimensions.x / std::max(.001f, hi.x - lo.x);
        sc[1] = dimensions.y / std::max(.001f, hi.y - lo.y);
        sc[2] = dimensions.z / std::max(.001f, hi.z - lo.z);
      }
    } else {
      // Built-in blocks and missing-model fallbacks use a unit cube mesh.
      // Scale that cube to the blueprint's saved bounds / ShapeSet footprint
      // or multi-cell blocks silently collapse back to one grid cell.
      imported = false;
      sc[0] = dimensions.x;
      sc[1] = dimensions.y;
      sc[2] = dimensions.z;
    }
    const V3 center = blockCenter(b);
    const V3 meshCenter =
        imported && m->hasBounds ? (m->boundsMin + m->boundsMax) * .5f : V3{};
    // FBX model vertices are authored around their model pivot. Re-centering
    // each asset to its mesh AABB loses deliberate pivot offsets (notably the
    // thruster's nozzle/body offset) and disagrees with the game's placement
    // transform. Scale the authored coordinates, then place that pivot at the
    // centre of the part's grid footprint.
    auto world = [&](V3 v) {
      if (centerMeshOnFootprint)
        v = v - meshCenter;
      v.x *= sc[0];
      v.y *= sc[1];
      v.z *= sc[2];
      return orient(v, b) + center;
    };
    if (isSelected((int)bi)) {
      const V3 localMin = imported
                              ? lo - (centerMeshOnFootprint ? meshCenter : V3{})
                              : V3{-.5f, -.5f, -.5f};
      const V3 localMax = imported
                              ? hi - (centerMeshOnFootprint ? meshCenter : V3{})
                              : V3{.5f, .5f, .5f};
      const V3 corners[8] = {{localMin.x, localMin.y, localMin.z},
                             {localMax.x, localMin.y, localMin.z},
                             {localMax.x, localMax.y, localMin.z},
                             {localMin.x, localMax.y, localMin.z},
                             {localMin.x, localMin.y, localMax.z},
                             {localMax.x, localMin.y, localMax.z},
                             {localMax.x, localMax.y, localMax.z},
                             {localMin.x, localMax.y, localMax.z}};
      std::array<ImVec2, 8> outline{};
      for (int i = 0; i < 8; i++) {
        float d;
        outline[i] = screen(world(corners[i]), d);
      }
      selectedOutlines.emplace_back((int)bi, outline);
      if ((int)bi == state.selection) {
        selectedCenter = center;
        hasSelectedOutline = true;
      }
    }
    auto append =
        [&](V3 aa, V3 bb, V3 cc, V3 na, V3 nb, V3 nc,
            std::array<float, 2> ta = {}, std::array<float, 2> tb = {},
            std::array<float, 2> tc = {}, GLuint triangleTexture = UINT32_MAX,
            bool preserveTextureColors = false) {
          V3 a = world(aa), q = world(bb), r = world(cc);
          const V3 transformed[3] = {
              norm(orient({na.x / sc[0], na.y / sc[1], na.z / sc[2]}, b)),
              norm(orient({nb.x / sc[0], nb.y / sc[1], nb.z / sc[2]}, b)),
              norm(orient({nc.x / sc[0], nc.y / sc[1], nc.z / sc[2]}, b))};
          const V3 lightDir = norm(V3{-.4f, .5f, .82f});
          ImU32 colors[3];
          for (int i = 0; i < 3; i++) {
            float light =
                .88f + .12f * std::max(0.f, dot(transformed[i], lightDir));
            float shade =
                isSelected((int)bi) ? std::min(1.f, light * 1.04f) : light;
            const float tintRed = preserveTextureColors ? 1.f : red;
            const float tintGreen = preserveTextureColors ? 1.f : green;
            const float tintBlue = preserveTextureColors ? 1.f : blue;
            colors[i] = IM_COL32((int)(tintRed * shade * 255),
                                 (int)(tintGreen * shade * 255),
                                 (int)(tintBlue * shade * 255), 255);
          }
          float da, db, dc;
          auto pa = screen(a, da), pb = screen(q, db), pc = screen(r, dc);
          triangles.push_back(
              {{pa, pb, pc},
               {da, db, dc},
               {colors[0], colors[1], colors[2]},
               {ta, tb, tc},
               triangleTexture == UINT32_MAX ? diffuseTexture : triangleTexture,
               (int)bi});
        };
    if (imported) {
      const size_t sourceTriangles = m->indices.size() / 3;
      const size_t triangleStride =
          coarsePreview ? std::max<size_t>(
                              1, (sourceTriangles + perPartTriangleBudget - 1) /
                                     perPartTriangleBudget)
                        : 1;
      for (size_t tri = 0; tri < sourceTriangles; tri += triangleStride) {
        size_t ti = tri * 3;
        const auto ia = m->indices[ti], ib = m->indices[ti + 1],
                   ic = m->indices[ti + 2];
        if (ia >= m->vertices.size() || ib >= m->vertices.size() ||
            ic >= m->vertices.size())
          continue;
        const auto uvAt = [&](uint32_t i) {
          return i < m->uvs.size() ? m->uvs[i] : std::array<float, 2>{};
        };
        const bool logicGateScreen = isLogicGate &&
                                     tri < m->triangleMaterials.size() &&
                                     m->triangleMaterials[tri] == "screen";
        auto screenUvAt = [&](uint32_t i) {
          auto uv = uvAt(i);
          if (logicGateScreen) {
            const float localU = std::clamp(uv[0] * 6.f, 0.f, 1.f);
            const float localV = std::clamp(uv[1] * 6.f, 0.f, 1.f);
            if (gateModeTexture) {
              // The per-mode texture already contains just the lit icon.
              uv = {localU, localV};
            } else {
              // Keep a correct in-atlas fallback if a dedicated crop cannot
              // be uploaded: six modes span the first row, with their lit
              // variants in the second row.
              uv = {(logicGateMode + localU) / 6.f, (1.f + localV) / 6.f};
            }
          }
          return uv;
        };
        GLuint triangleTexture = UINT32_MAX;
        if (catalogEntry != state.catalog.end() &&
            tri < m->triangleMaterials.size()) {
          auto material = catalogEntry->second.materialTextures.find(
              m->triangleMaterials[tri]);
          if (material != catalogEntry->second.materialTextures.end())
            triangleTexture = diffuseTextureFor(material->second);
        }
        if (logicGateScreen && gateModeTexture)
          triangleTexture = gateModeTexture;
        append(m->vertices[ia], m->vertices[ib], m->vertices[ic],
               m->normals[ia], m->normals[ib], m->normals[ic], screenUvAt(ia),
               screenUvAt(ib), screenUvAt(ic), triangleTexture,
               logicGateScreen);
      }
    } else {
      V3 v[8] = {{-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f},
                 {-.5f, -.5f, .5f},  {.5f, -.5f, .5f},  {.5f, .5f, .5f},  {-.5f, .5f, .5f}};
      const int ix[] = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                        3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
      for (int t = 0; t < 36; t += 3) {
        const V3 a = v[ix[t]], q = v[ix[t + 1]], r = v[ix[t + 2]], n = norm(cross(q - a, r - a));
        append(a, q, r, n, n, n);
      }
    }
  }
  // Upload projected triangles to a small offscreen target with a real depth
  // buffer. Average-depth triangle sorting cannot correctly resolve large,
  // intersecting surfaces, which made floor blocks paint across other parts.
  float nearestDepth = -std::numeric_limits<float>::infinity();
  float farthestDepth = std::numeric_limits<float>::infinity();
  for (const auto &triangle : triangles)
    for (float depth : triangle.vertexDepth) {
      nearestDepth = std::max(nearestDepth, depth);
      farthestDepth = std::min(farthestDepth, depth);
    }
  const float depthRange = std::max(1e-5f, nearestDepth - farthestDepth);
  std::vector<SceneBatch> gpuBatches;
  std::unordered_map<GLuint, size_t> batchByTexture;
  for (const auto &triangle : triangles)
    for (size_t corner = 0; corner < 3; corner++) {
      const ImVec2 point = triangle.p[corner];
      const ImU32 packed = triangle.color[corner];
      const float x = 2.f * (point.x - origin.x) / size.x - 1.f;
      const float y = 1.f - 2.f * (point.y - origin.y) / size.y;
      const float z = 2.f * (nearestDepth - triangle.vertexDepth[corner]) / depthRange - 1.f;
      auto batch = batchByTexture.find(triangle.texture);
      if (batch == batchByTexture.end()) {
        batchByTexture[triangle.texture] = gpuBatches.size();
        gpuBatches.push_back({triangle.texture, {}});
        batch = batchByTexture.find(triangle.texture);
      }
      auto &vertices = gpuBatches[batch->second].vertices;
      vertices.push_back({x, y, z, (float)((packed >> IM_COL32_R_SHIFT) & 0xff) / 255.f,
                          (float)((packed >> IM_COL32_G_SHIFT) & 0xff) / 255.f,
                          (float)((packed >> IM_COL32_B_SHIFT) & 0xff) / 255.f, triangle.uv[corner][0],
                          triangle.uv[corner][1]});
    }
  if (triangles.empty())
    state.renderError.clear();
  const bool sceneRendered = triangles.empty() || drawDepthBufferedScene(gpuBatches, origin, size, dl);
  if (!sceneRendered) {
    const ImVec2 message{origin.x + 12.f, origin.y + 12.f};
    dl->AddRectFilled(message, ImVec2(message.x + std::min(size.x - 24.f, 500.f), message.y + 50.f),
                      IM_COL32(56, 31, 24, 240), 5.f);
    dl->AddText(ImVec2(message.x + 9.f, message.y + 9.f), IM_COL32(255, 205, 170, 255),
                "3D depth renderer could not start; see viewer diagnostics in "
                "the inspector.");
  }
  if (coarsePreview) {
    const ImVec2 label{origin.x + std::max(12.f, size.x - 390.f), origin.y + 12.f};
    dl->AddRectFilled(label, ImVec2(label.x + 378.f, label.y + 28.f), IM_COL32(16, 20, 26, 235), 5.f);
    dl->AddText(ImVec2(label.x + 8.f, label.y + 7.f), IM_COL32(255, 210, 135, 255),
                "Large creation: real meshes shown at reduced detail.");
  }
  if (state.showLogicGateLabels) {
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    for (size_t index = 0; index < blocks.size(); ++index) {
      if (coarsePreview && !coarsePreviewParts.contains((int)index))
        continue;
      if (lower(blocks[index].shapeID) !=
          "9f0f56e8-2c31-4d83-996c-d00a9b296c3f")
        continue;
      const char *mode = logicGateModeName(logicGateModeForPart((int)index));
      const ImVec2 textSize = ImGui::CalcTextSize(mode);
      float gateDepth;
      const ImVec2 center = screen(blockCenter(blocks[index]), gateDepth);
      (void)gateDepth;
      const ImVec2 textPosition(center.x - textSize.x * .5f,
                                center.y - textSize.y * .5f);
      constexpr float paddingX = 5.f, paddingY = 3.f;
      const ImVec2 badgeMin(textPosition.x - paddingX,
                            textPosition.y - paddingY);
      const ImVec2 badgeMax(textPosition.x + textSize.x + paddingX,
                            textPosition.y + textSize.y + paddingY);
      dl->AddRectFilled(badgeMin, badgeMax, IM_COL32(10, 20, 27, 238), 4.f);
      dl->AddRect(badgeMin, badgeMax, IM_COL32(70, 210, 226, 245), 4.f, 0, 1.f);
      dl->AddText(textPosition, IM_COL32(218, 251, 255, 255), mode);
    }
    dl->PopClipRect();
  }
  struct LogicEdge {
    int source{}, target{};
    std::int64_t targetId{};
    std::vector<ImVec2> path;
  };
  std::vector<LogicEdge> logicEdges;
  ImVec2 logicHintMin{}, logicHintMax{};
  if (state.logicManipulation) {
    std::unordered_map<std::int64_t, int> partByControllerId;
    std::unordered_map<std::int64_t, std::unordered_set<std::int64_t>> targetsByControllerId;
    for (int index = 0; index < (int)blocks.size(); ++index) {
      const auto id = controllerIdForPart(index);
      if (id < 0)
        continue;
      partByControllerId.emplace(id, index);
      json *controller = controllerForPart(index);
      if (!controller || !controller->contains("controllers") || !(*controller)["controllers"].is_array())
        continue;
      for (const auto &connection : (*controller)["controllers"])
        if (connection.is_object() && connection.contains("id") && connection["id"].is_number_integer())
          try {
            targetsByControllerId[id].insert(connection["id"].get<std::int64_t>());
          } catch (...) {
          }
    }
    std::vector<ImVec2> partScreen(blocks.size());
    for (size_t index = 0; index < blocks.size(); ++index) {
      float depth;
      partScreen[index] = screen(blockCenter(blocks[index]), depth);
    }
    for (int source = 0; source < (int)blocks.size(); ++source) {
      const std::int64_t sourceId = controllerIdForPart(source);
      auto outgoing = targetsByControllerId.find(sourceId);
      if (outgoing == targetsByControllerId.end())
        continue;
      for (std::int64_t targetId : outgoing->second) {
        auto targetPart = partByControllerId.find(targetId);
        if (targetPart == partByControllerId.end())
          continue;
        const int target = targetPart->second;
        if (target == source) {
          const ImVec2 center = partScreen[source];
          const ImVec2 start(center.x + 8.f, center.y - 5.f);
          const ImVec2 controlA(center.x + 43.f, center.y - 47.f);
          const ImVec2 controlB(center.x - 43.f, center.y - 47.f);
          const ImVec2 end(center.x - 8.f, center.y - 5.f);
          std::vector<ImVec2> loop;
          loop.reserve(21);
          for (int step = 0; step <= 20; ++step) {
            const float t = step / 20.f, oneMinusT = 1.f - t;
            const float a = oneMinusT * oneMinusT * oneMinusT;
            const float b = 3.f * oneMinusT * oneMinusT * t;
            const float c = 3.f * oneMinusT * t * t;
            const float d = t * t * t;
            loop.emplace_back(a * start.x + b * controlA.x + c * controlB.x + d * end.x,
                              a * start.y + b * controlA.y + c * controlB.y + d * end.y);
          }
          logicEdges.push_back({source, target, targetId, std::move(loop)});
          continue;
        }
        float dx = partScreen[target].x - partScreen[source].x, dy = partScreen[target].y - partScreen[source].y;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 18.f)
          continue;
        const float ux = dx / length, uy = dy / length;
        auto reverse = targetsByControllerId.find(targetId);
        const bool reciprocal = reverse != targetsByControllerId.end() && reverse->second.contains(sourceId);
        const float side = reciprocal ? 4.f : 0.f;
        const float ox = -uy * side, oy = ux * side;
        logicEdges.push_back({source,
                              target,
                              targetId,
                              {{partScreen[source].x + ux * 8.f + ox, partScreen[source].y + uy * 8.f + oy},
                               {partScreen[target].x - ux * 15.f + ox, partScreen[target].y - uy * 15.f + oy}}});
      }
    }
  }
  struct AxisHandle {
    ImVec2 center{}, end{}, screenAxis{};
    float worldPerPixel{};
  };
  auto getAxisHandle = [&](int signedAxis) {
    AxisHandle handle;
    float centerDepth, axisDepth;
    handle.center = screen(selectedCenter, centerDepth);
    const ImVec2 unitEnd = screen(selectedCenter + axis(signedAxis), axisDepth);
    float dx = unitEnd.x - handle.center.x, dy = unitEnd.y - handle.center.y;
    const float projectedPixels = std::sqrt(dx * dx + dy * dy);
    if (projectedPixels > 1e-3f)
      handle.screenAxis = {dx / projectedPixels, dy / projectedPixels};
    else {
      // At the singular camera angle the world axis points straight at the
      // camera. Keep its signed handle usable and visibly distinct.
      const int a = std::abs(signedAxis);
      handle.screenAxis = a == 1 ? ImVec2(1.f, 0.f) : a == 2 ? ImVec2(0.f, -1.f) : ImVec2(.707f, -.707f);
      if (signedAxis < 0) {
        handle.screenAxis.x = -handle.screenAxis.x;
        handle.screenAxis.y = -handle.screenAxis.y;
      }
    }
    handle.end = ImVec2(handle.center.x + handle.screenAxis.x * 38.f, handle.center.y + handle.screenAxis.y * 38.f);
    handle.worldPerPixel = std::min(.5f, 1.f / std::max(.02f, projectedPixels));
    return handle;
  };
  auto axisAtMouse = [&](ImVec2 point) {
    if (state.logicManipulation || !hasSelectedOutline || state.selection < 0 ||
        state.selection >= (int)blocks.size() || blocks[state.selection].isJoint)
      return 0;
    int hitAxis = 0;
    float nearest = 12.f;
    for (int a = 1; a <= 3; a++)
      for (int sign : {-1, 1}) {
        const int signedAxis = a * sign;
        const AxisHandle handle = getAxisHandle(signedAxis);
        const ImVec2 center = handle.center, end = handle.end;
        const float vx = end.x - center.x, vy = end.y - center.y, l2 = vx * vx + vy * vy;
        if (l2 < 16.f)
          continue;
        const float t = std::clamp(((point.x - center.x) * vx + (point.y - center.y) * vy) / l2, 0.f, 1.f);
        const float dx = point.x - (center.x + t * vx), dy = point.y - (center.y + t * vy),
                    distance = std::sqrt(dx * dx + dy * dy);
        if (t > .08f && distance < nearest) {
          nearest = distance;
          hitAxis = signedAxis;
        }
      }
    return hitAxis;
  };
  const int hoveredAxis = hovered ? axisAtMouse(mouse) : 0;
  if (hoveredAxis && !state.dragAxis)
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (hasSelectedOutline) {
    static constexpr int edges[][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                       {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto &selectionOutline : selectedOutlines) {
      const bool active = selectionOutline.first == state.selection;
      const ImU32 outlineColor = active ? IM_COL32(102, 210, 255, 255) : IM_COL32(255, 205, 96, 255);
      for (const auto &edge : edges)
        dl->AddLine(selectionOutline.second[edge[0]], selectionOutline.second[edge[1]], outlineColor,
                    active ? 2.f : 1.7f);
    }

    if (!state.logicManipulation) {
      const ImU32 colors[3] = {IM_COL32(255, 112, 112, 255), IM_COL32(120, 235, 155, 255),
                               IM_COL32(115, 175, 255, 255)};
      const char *positive[3] = {"+X", "+Y", "+Z"};
      const char *negative[3] = {"-X", "-Y", "-Z"};
      auto drawArrow = [&](ImVec2 from, ImVec2 to, ImU32 color, const char *label, int signedAxis) {
        const float dx = to.x - from.x, dy = to.y - from.y, len = std::sqrt(dx * dx + dy * dy);
        if (len < 8.f)
          return;
        const bool active = state.dragAxis == signedAxis, hot = hoveredAxis == signedAxis;
        const ImU32 visible = active ? IM_COL32(255, 245, 115, 255) : hot ? IM_COL32(255, 255, 255, 255) : color;
        const float ux = dx / len, uy = dy / len, head = std::min(9.f, len * .34f), wing = 4.5f;
        dl->AddLine(from, to, IM_COL32(10, 13, 18, 230), active ? 7.f : 5.f);
        dl->AddLine(from, to, visible, active ? 4.f : hot ? 3.5f : 2.5f);
        dl->AddLine(to, ImVec2(to.x - ux * head - uy * wing, to.y - uy * head + ux * wing), IM_COL32(10, 13, 18, 230),
                    active ? 7.f : 5.f);
        dl->AddLine(to, ImVec2(to.x - ux * head - uy * wing, to.y - uy * head + ux * wing), visible,
                    active ? 4.f
                    : hot  ? 3.5f
                           : 2.5f);
        dl->AddLine(to, ImVec2(to.x - ux * head + uy * wing, to.y - uy * head - ux * wing), IM_COL32(10, 13, 18, 230),
                    active ? 7.f : 5.f);
        dl->AddLine(to, ImVec2(to.x - ux * head + uy * wing, to.y - uy * head - ux * wing), visible,
                    active ? 4.f
                    : hot  ? 3.5f
                           : 2.5f);
        const ImVec2 labelPos(to.x + ux * 7.f - 10.f, to.y + uy * 7.f - 8.f);
        dl->AddText(ImVec2(labelPos.x + 1, labelPos.y + 1), IM_COL32(0, 0, 0, 255), label);
        dl->AddText(labelPos, visible, label);
      };
      for (int i = 0; i < 3; i++) {
        const AxisHandle positiveHandle = getAxisHandle(i + 1), negativeHandle = getAxisHandle(-(i + 1));
        drawArrow(positiveHandle.center, positiveHandle.end, colors[i], positive[i], i + 1);
        drawArrow(negativeHandle.center, negativeHandle.end, colors[i], negative[i], -(i + 1));
      }
      if (state.dragAxis) {
        const int a = std::abs(state.dragAxis) - 1;
        const char *names[3] = {"X", "Y", "Z"};
        char indicator[64];
        snprintf(indicator, sizeof(indicator), "MOVING  %c%s  ·  drag along this axis", state.dragAxis > 0 ? '+' : '-',
                 names[a]);
        dl->AddRectFilled(ImVec2(origin.x + 12, origin.y + 12), ImVec2(origin.x + 230, origin.y + 40),
                          IM_COL32(16, 20, 26, 235), 5.f);
        dl->AddText(ImVec2(origin.x + 21, origin.y + 19), IM_COL32(255, 245, 115, 255), indicator);
      }
    }
  }
  if (state.logicManipulation) {
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    for (const auto &edge : logicEdges) {
      if (edge.path.size() < 2)
        continue;
      const ImVec2 from = edge.path.front(), to = edge.path.back();
      const size_t tangentOffset = std::min<size_t>(4, edge.path.size() - 1);
      const ImVec2 previous = edge.path[edge.path.size() - 1 - tangentOffset];
      const float dx = to.x - previous.x, dy = to.y - previous.y, length = std::sqrt(dx * dx + dy * dy);
      if (length < 8.f)
        continue;
      const float ux = dx / length, uy = dy / length, head = std::min(11.f, length * .38f), wing = 5.5f;
      const ImU32 color = edge.source == state.logicSource ? IM_COL32(255, 190, 76, 255) : IM_COL32(83, 222, 235, 245);
      const ImVec2 left(to.x - ux * head - uy * wing, to.y - uy * head + ux * wing);
      const ImVec2 right(to.x - ux * head + uy * wing, to.y - uy * head - ux * wing);
      for (size_t point = 1; point < edge.path.size(); ++point) {
        dl->AddLine(edge.path[point - 1], edge.path[point], IM_COL32(10, 16, 22, 235), 7.f);
        dl->AddLine(edge.path[point - 1], edge.path[point], color, 3.f);
      }
      dl->AddLine(to, left, IM_COL32(10, 16, 22, 245), 7.f);
      dl->AddLine(to, left, color, 3.f);
      dl->AddLine(to, right, IM_COL32(10, 16, 22, 245), 7.f);
      dl->AddLine(to, right, color, 3.f);
      dl->AddCircleFilled(from, 5.f, IM_COL32(10, 16, 22, 245));
      dl->AddCircleFilled(from, 2.8f, color);
    }
    if (state.logicSource >= 0 && state.logicSource < (int)blocks.size()) {
      float sourceDepth;
      const ImVec2 sourcePoint = screen(blockCenter(blocks[state.logicSource]), sourceDepth);
      dl->AddCircle(sourcePoint, 12.f, IM_COL32(18, 24, 30, 255), 20, 4.f);
      dl->AddCircle(sourcePoint, 10.f, IM_COL32(255, 190, 76, 255), 20, 2.f);
      dl->AddCircleFilled(sourcePoint, 3.f, IM_COL32(255, 220, 145, 255));
      if (hovered) {
        const float dx = mouse.x - sourcePoint.x, dy = mouse.y - sourcePoint.y, length = std::sqrt(dx * dx + dy * dy);
        if (length > 18.f) {
          const float ux = dx / length, uy = dy / length;
          for (float at = 14.f; at < length - 8.f; at += 12.f)
            dl->AddLine(ImVec2(sourcePoint.x + ux * at, sourcePoint.y + uy * at),
                        ImVec2(sourcePoint.x + ux * std::min(at + 6.f, length - 8.f),
                               sourcePoint.y + uy * std::min(at + 6.f, length - 8.f)),
                        IM_COL32(255, 190, 76, 190), 2.f);
          dl->AddCircleFilled(mouse, 4.f, IM_COL32(255, 190, 76, 230));
        }
      }
    }
    dl->PopClipRect();
    std::string guidance = state.logicSource >= 0 ? "LOGIC SOURCE: part " + std::to_string(state.logicSource + 1) +
                                                        "  ·  click targets  ·  Shift-click changes source  ·  empty "
                                                        "space clears selection  ·  arrows remove  ·  Esc exits"
                                                  : "LOGIC MODE  ·  click source, then targets  ·  click source to "
                                                    "wire into itself  ·  arrows remove  ·  Esc exits";
    const float hintWrapWidth = std::max(80.f, size.x - 36.f);
    const ImVec2 labelSize = ImGui::CalcTextSize(guidance.c_str(), nullptr, false, hintWrapWidth);
    const ImVec2 labelPos(origin.x + 12.f, origin.y + 12.f);
    logicHintMin = labelPos;
    logicHintMax = {std::min(origin.x + size.x - 8.f, labelPos.x + labelSize.x + 18.f),
                    labelPos.y + labelSize.y + 16.f};
    dl->AddRectFilled(logicHintMin, logicHintMax, IM_COL32(15, 28, 34, 238), 5.f);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(labelPos.x + 9.f, labelPos.y + 8.f),
                IM_COL32(140, 241, 245, 255), guidance.c_str(), nullptr, hintWrapWidth);
  }
  const bool clickedLogicHint = state.logicManipulation && mouse.x >= logicHintMin.x && mouse.x <= logicHintMax.x &&
                                mouse.y >= logicHintMin.y && mouse.y <= logicHintMax.y;
  if (hovered && state.logicManipulation)
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  if (hovered && !clickedLogicHint && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const int hitAxis = axisAtMouse(mouse);
    int hitLogicEdge = -1;
    float nearestLogicEdge = 5.5f;
    if (state.logicManipulation)
      for (size_t edgeIndex = 0; edgeIndex < logicEdges.size(); ++edgeIndex) {
        const auto &edge = logicEdges[edgeIndex];
        for (size_t point = 1; point < edge.path.size(); ++point) {
          const ImVec2 from = edge.path[point - 1], to = edge.path[point];
          const float vx = to.x - from.x, vy = to.y - from.y, lengthSquared = vx * vx + vy * vy;
          if (lengthSquared < 1.f)
            continue;
          const float t = std::clamp(((mouse.x - from.x) * vx + (mouse.y - from.y) * vy) / lengthSquared, 0.f, 1.f);
          const float dx = mouse.x - (from.x + vx * t), dy = mouse.y - (from.y + vy * t),
                      distance = std::sqrt(dx * dx + dy * dy);
          if (distance < nearestLogicEdge) {
            nearestLogicEdge = distance;
            hitLogicEdge = (int)edgeIndex;
          }
        }
      }
    int picked = -1;
    float closestDepth = -std::numeric_limits<float>::max();
    if (hitAxis)
      picked = state.selection;
    else
      for (const auto &t : triangles) {
        const auto &a = t.p[0];
        const auto &b = t.p[1];
        const auto &c = t.p[2];
        const float denom = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (std::abs(denom) < 1e-6f)
          continue;
        const float wa = ((b.y - c.y) * (mouse.x - c.x) + (c.x - b.x) * (mouse.y - c.y)) / denom;
        const float wb = ((c.y - a.y) * (mouse.x - c.x) + (a.x - c.x) * (mouse.y - c.y)) / denom;
        const float wc = 1.f - wa - wb;
        if (wa >= -1e-4f && wb >= -1e-4f && wc >= -1e-4f) {
          const float depth = wa * t.vertexDepth[0] + wb * t.vertexDepth[1] + wc * t.vertexDepth[2];
          if (depth > closestDepth) {
            closestDepth = depth;
            picked = t.block;
          }
        }
      }
    if (state.logicManipulation) {
      if (hitLogicEdge >= 0) {
        const auto &edge = logicEdges[hitLogicEdge];
        if (Parser::removeConnectionById(blueprint, blocks, edge.source, edge.targetId))
          notify("Logic connection removed");
        if (state.logicSource == edge.source)
          state.logicSource = -1;
      } else if (picked < 0) {
        state.logicSource = -1;
        state.selection = -1;
        state.selectionSet.clear();
        notify("Selection cleared");
      } else if (controllerIdForPart(picked) < 0)
        notify("That part cannot be connected");
      else if (state.logicSource < 0) {
        state.logicSource = picked;
        state.selection = picked;
        state.selectionSet.clear();
        state.selectionSet.insert(picked);
        notify("Logic source selected");
      } else if (ImGui::GetIO().KeyShift) {
        state.logicSource = picked;
        state.selection = picked;
        state.selectionSet.clear();
        state.selectionSet.insert(picked);
        notify("Logic source changed");
      } else if (picked == state.logicSource) {
        const std::string itemName = partDisplayName(picked);
        if (Parser::addDirectedConnection(blueprint, blocks, picked, picked))
          notify("Wired " + itemName + " into itself");
        else
          notify("Already wired " + itemName + " into itself");
      } else if (Parser::addDirectedConnection(blueprint, blocks, state.logicSource, picked))
        notify("Logic connection created");
      else
        notify("These parts are already connected");
    } else if (!hitAxis) {
      if (ImGui::GetIO().KeyShift) {
        if (picked >= 0) {
          state.selection = picked;
          addSelectionGroup(picked);
        }
      } else {
        state.selection = picked;
        state.selectionSet.clear();
        if (picked >= 0)
          addSelectionGroup(picked);
      }
    }
    if (!state.logicManipulation && state.stackSelection && state.selection >= 0 &&
        state.selection < (int)blocks.size() && !blocks[state.selection].isJoint) {
      const Parser::Position target = blocks[state.selection].pos;
      for (int index : state.selectionSet)
        if (index >= 0 && index < (int)blocks.size() && !blocks[index].isJoint)
          blocks[index].pos = target;
      synchronizeJointPositions();
    }
    state.dragSelection.clear();
    if (hitAxis) {
      state.dragSelection.assign(state.selectionSet.begin(), state.selectionSet.end());
      if (state.dragSelection.empty() && state.selection >= 0)
        state.dragSelection.push_back(state.selection);
    }
    state.dragAxis = hitAxis;
    state.lastDragMouse = mouse;
    state.dragRemainder = 0.f;
    state.dragWorldPerPixel = 0.f;
    state.dragScreenAxis = {0, 0};
    if (hitAxis) {
      const AxisHandle handle = getAxisHandle(hitAxis);
      state.dragScreenAxis = handle.screenAxis;
      state.dragWorldPerPixel = handle.worldPerPixel;
    }
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    state.dragSelection.clear();
    state.dragAxis = 0;
  }
  if (!state.dragSelection.empty() && state.dragAxis && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    const ImVec2 delta = ImVec2(mouse.x - state.lastDragMouse.x, mouse.y - state.lastDragMouse.y);
    state.lastDragMouse = mouse;
    const int signedAxis = state.dragAxis;
    if (state.dragWorldPerPixel > 0.f) {
      state.dragRemainder +=
          (delta.x * state.dragScreenAxis.x + delta.y * state.dragScreenAxis.y) * state.dragWorldPerPixel;
      const int steps = (int)std::trunc(state.dragRemainder);
      if (steps) {
        moveSelection(signedAxis == 1    ? steps
                      : signedAxis == -1 ? -steps
                                         : 0,
                      signedAxis == 2    ? steps
                      : signedAxis == -2 ? -steps
                                         : 0,
                      signedAxis == 3    ? steps
                      : signedAxis == -3 ? -steps
                                         : 0);
        state.dragRemainder -= steps;
      }
    }
  }
  ImGui::EndChild();
  ImGui::SameLine();
  renderInspector(InspectorContext{blocks, items, inspector, coarsePreview, moveSelection, synchronizeJointPositions,
                                   selectedWorldSize, setSelectionColor, removeSelection,
                                   logicGateModeForPart});
  json frameSnapshot = blueprint;
  Parser::applyBlockListToNode(blocks, frameSnapshot);
  if (state.blueprintHistory.empty()) {
    state.blueprintHistory.push_back(frameSnapshot);
    state.historyCursor = 0;
  } else if (frameSnapshot != state.blueprintHistory[state.historyCursor]) {
    const bool interactionActive = ImGui::GetIO().MouseDown[0] || ImGui::IsAnyItemActive();
    if (state.historyActionInProgress && interactionActive)
      state.blueprintHistory[state.historyCursor] = std::move(frameSnapshot);
    else {
      state.blueprintHistory.erase(state.blueprintHistory.begin() + (ptrdiff_t)state.historyCursor + 1,
                                   state.blueprintHistory.end());
      state.blueprintHistory.push_back(std::move(frameSnapshot));
      state.historyCursor = state.blueprintHistory.size() - 1;
      constexpr size_t maximumHistoryStates = 64;
      if (state.blueprintHistory.size() > maximumHistoryStates) {
        state.blueprintHistory.erase(state.blueprintHistory.begin());
        --state.historyCursor;
      }
    }
    state.historyActionInProgress = interactionActive;
  } else if (!ImGui::GetIO().MouseDown[0] && !ImGui::IsAnyItemActive())
    state.historyActionInProgress = false;
  if (!state.notification.empty() && ImGui::GetTime() < state.notificationUntil) {
    const ImVec2 textSize = ImGui::CalcTextSize(state.notification.c_str());
    const ImVec2 padding(12.f, 8.f);
    const ImVec2 toastSize(textSize.x + padding.x * 2.f, textSize.y + padding.y * 2.f);
    const ImVec2 toastPos(origin.x + size.x - toastSize.x - 12.f, origin.y + 12.f);
    auto *foreground = ImGui::GetForegroundDrawList();
    foreground->AddRectFilled(toastPos, ImVec2(toastPos.x + toastSize.x, toastPos.y + toastSize.y),
                              IM_COL32(37, 91, 66, 242), 5.f);
    foreground->AddText(ImVec2(toastPos.x + padding.x, toastPos.y + padding.y), IM_COL32(238, 250, 240, 255),
                        state.notification.c_str());
  } else if (ImGui::GetTime() >= state.notificationUntil)
    state.notification.clear();
  ImGui::End();
}
} // namespace BlueprintViewport
