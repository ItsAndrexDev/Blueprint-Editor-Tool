#pragma once

#include "../imgui/imgui.h"
#include "blueprint_viewport.hpp"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <glad/glad.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace BlueprintViewport::Internal {
using V3 = BlueprintViewport::Vec3;

inline const char *logicGateModeName(int mode) {
  static constexpr const char *names[] = {"AND", "OR", "XOR", "NAND", "NOR", "XNOR"};
  return mode >= 0 && mode < (int)(sizeof(names) / sizeof(names[0]))
             ? names[mode]
             : "Unknown";
}

inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float b) { return {a.x * b, a.y * b, a.z * b}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline V3 norm(V3 a) {
  const float length = std::sqrt(dot(a, a));
  return length > 1e-7f ? a * (1.f / length) : V3{0, 1, 0};
}

struct Mesh {
  std::vector<V3> vertices;
  std::vector<V3> normals;
  std::vector<std::array<float, 2>> uvs;
  std::vector<std::uint32_t> indices;
  std::vector<std::string> triangleMaterials;
  V3 boundsMin{}, boundsMax{};
  bool hasBounds{};
  bool loaded{};
  std::string detail;
};

struct Entry {
  std::vector<std::filesystem::path> models;
  std::filesystem::path diffuseTexture;
  std::unordered_map<std::string, std::filesystem::path> materialTextures;
  std::string name;
  bool centerMeshOnFootprint{};
};

struct SceneVertex {
  float x, y, z, r, g, b, u, v;
};
struct SceneBatch {
  GLuint texture{};
  std::vector<SceneVertex> vertices;
};
struct GlStateSnapshot {
  GLint drawFramebuffer{}, readFramebuffer{}, viewport[4]{}, program{},
      vertexArray{}, arrayBuffer{}, renderbuffer{};
  GLint activeTexture{}, activeTextureBinding{}, texture0Binding{},
      depthFunction{}, scissorBox[4]{};
  GLboolean depthTest{}, depthWrite{}, scissorTest{}, blend{}, cullFace{},
      colorWrite[4]{};
  GLfloat clearColor[4]{};
};

// Persistent viewport data shared by the UI, asset pipeline, and GL renderer.
struct State {
  std::unordered_map<std::string, Entry> catalog;
  std::unordered_set<std::string> blockShapes;
  std::unordered_set<std::string> footprintAnchoredParts;
  std::unordered_map<std::string, V3> shapeSizes;
  std::unordered_map<std::string, std::string> shapeDefaultColors;
  std::unordered_map<std::string, std::filesystem::path> shapeDiffuseTextures;
  std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;
  std::unordered_map<std::wstring, std::shared_ptr<Mesh>> meshFiles;
  std::unordered_map<std::wstring, GLuint> textures;
  std::unordered_map<std::wstring, std::array<GLuint, 6>> logicGateModeTextures;

  std::vector<json> blueprintHistory;
  std::size_t historyCursor{};
  bool historyActionInProgress{};
  std::string notification;
  double notificationUntil{};
  std::size_t meshCacheBytes{};
  std::size_t textureCacheBytes{};
  std::filesystem::path game;
  std::filesystem::path currentBlueprint;

  int selection = -1;
  std::unordered_set<int> selectionSet;
  bool stackSelection{};
  bool logicManipulation{};
  int logicSource = -1;
  bool copyBlockState{false};
  bool showLogicGateLabels{false};
  Parser::SelectionClipboard clipboard;
  std::filesystem::path clipboardBlueprint;
  std::size_t clipboardPasteCount{};
  std::vector<int> dragSelection;

  // The default yaw follows the same side as the in-game blueprint preview.
  float yaw = 0.72f, pitch = 0.42f, zoom = 1.f;
  ImVec2 pan{0, 0};
  int dragAxis = 0;
  ImVec2 lastDragMouse{};
  ImVec2 dragScreenAxis{};
  float dragWorldPerPixel{};
  float dragRemainder{};

  bool catalogLoaded{};
  bool catalogAvailable{};
  std::string error;
  GLuint sceneProgram{};
  GLuint sceneVao{};
  GLuint sceneVbo{};
  GLuint sceneFramebuffer{};
  GLuint sceneColorTexture{};
  GLuint sceneDepthBuffer{};
  int sceneTargetWidth{};
  int sceneTargetHeight{};
  std::string renderError;
};

extern State state;

std::string lower(std::string value);
void loadCatalog();
V3 axis(int code);
V3 orient(V3 value, const Parser::Block &block);
V3 shapeSize(const Parser::Block &block);
std::size_t gridCellCount(const std::vector<Parser::Block> &blocks);
int rotateAxisAroundZ(int axisCode, int quarterTurns);
void setOrientation(Parser::Block &block, int xaxis, int zaxis);
std::string displayNameFromShapeSet(const std::string &source);
int displayColor(const Parser::Block &block);

std::shared_ptr<Mesh> meshFor(const std::string &shapeId);
GLuint diffuseTextureFor(const std::filesystem::path &diffuseTexture);
GLuint logicGateModeTextureFor(const std::filesystem::path &diffuseTexture, int mode);
bool loadFbx(const std::filesystem::path &path, Mesh &mesh);
bool loadDae(const std::filesystem::path &path, Mesh &mesh);
bool loadOgreMesh(const std::filesystem::path &path, Mesh &mesh);
void buildNormals(Mesh &mesh);
void releaseTextureCache();

GlStateSnapshot captureGlState();
void restoreGlState(const GlStateSnapshot &saved);
bool drawDepthBufferedScene(const std::vector<SceneBatch> &batches,
                            ImVec2 origin, ImVec2 size, ImDrawList *drawList);
void shutdownSceneRenderer();

class EditingActions {
public:
  EditingActions(json &blueprint, const std::filesystem::path &blueprintPath,
                 std::vector<Parser::Block> &blocks, const json &items);

  bool isSelected(int index) const;
  json *controllerForPart(int index);
  std::int64_t controllerIdForPart(int index);
  std::string partDisplayName(int index) const;
  int logicGateModeForPart(int index) const;
  void setSelectionLogicGateMode(int mode);
  struct ControllerBearingSettings {
    std::int64_t jointId = -1;
    int connectionIndex = -1;
    std::string name;
    bool reverse{};
    std::vector<int> frameAngles;
  };
  std::vector<ControllerBearingSettings> controllerBearingSettings(int index);
  bool setControllerBearingAngle(int index, std::int64_t jointId, int frameIndex, int angle);
  bool setControllerBearingReverse(int index, std::int64_t jointId, bool reverse);
  void selectPart(int index, bool additive);
  void selectAllOfType(const std::string &shapeId, bool additive);
  void addSelectionGroup(int index);
  void synchronizeJointPositions();
  void moveSelection(int x, int y, int z);
  V3 selectedWorldSize() const;
  void setSelectionColor(const std::string &color);
  void saveBlueprint();
  void undo();
  void redo();
  bool removeSelection();
  void copySelectedObjects();
  void pasteCopiedObjects();

private:
  json &blueprint_;
  const std::filesystem::path &blueprintPath_;
  std::vector<Parser::Block> &blocks_;
  const json &items_;

  void addTurretSeatSelection(int index);
  void restoreHistory(std::size_t cursor);
};

struct InspectorContext {
  std::vector<Parser::Block> &blocks;
  const json &items;
  float width{};
  bool coarsePreview{};
  std::function<void(int, int, int)> moveSelection;
  std::function<void()> synchronizeJointPositions;
  std::function<V3()> selectedWorldSize;
  std::function<void(const std::string &)> setSelectionColor;
  std::function<bool()> removeSelection;
  std::function<int(int)> logicGateMode;
  std::function<void(int)> setSelectionLogicGateMode;
  std::function<std::string(int)> partDisplayName;
  std::function<std::vector<EditingActions::ControllerBearingSettings>(int)> controllerBearingSettings;
  std::function<bool(int, std::int64_t, int, int)> setControllerBearingAngle;
  std::function<bool(int, std::int64_t, bool)> setControllerBearingReverse;
  std::function<void(int, bool)> selectPart;
  std::function<void(const std::string &, bool)> selectAllOfType;
};
void renderInspector(const InspectorContext &context);
} // namespace BlueprintViewport::Internal
