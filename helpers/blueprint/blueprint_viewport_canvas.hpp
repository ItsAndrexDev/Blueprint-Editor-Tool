#pragma once

#include "blueprint_viewport_internal.hpp"

#include <array>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BlueprintViewport::Internal {
class ViewportCanvas {
  public:
    ViewportCanvas(json& blueprintData,
                   std::vector<Parser::Block>& blockList,
                   EditingActions& editingActions);

    bool render(float viewportWidth, float inspectorWidth);
    ImVec2 viewportOrigin() const;
    ImVec2 viewportSize() const;

  private:
    struct Camera {
        ImVec2 center{};
        float scale{};
        float cy{};
        float sy{};
        float cp{};
        float sp{};
    };

    struct WorldTriangle {
        std::array<ImVec2, 3> p;
        std::array<float, 3> vertexDepth;
        std::array<ImU32, 3> color;
        std::array<std::array<float, 2>, 3> uv;
        GLuint texture{};
        int block{};
    };

    struct LogicEdge {
        int source{};
        int target{};
        std::int64_t targetId{};
        std::vector<ImVec2> path;
    };

    struct AxisHandle {
        ImVec2 center{};
        ImVec2 end{};
        ImVec2 screenAxis{};
        float worldPerPixel{};
    };

    json& blueprint;
    std::vector<Parser::Block>& blocks;
    EditingActions& actions;

    ImVec2 origin{};
    ImVec2 size{};
    ImVec2 mouse{};
    ImDrawList* dl{};
    bool hovered{};

    Camera camera{};
    V3 focus{};
    V3 selectedCenter{};
    bool hasSelectedOutline{};
    bool coarsePreview{};
    std::vector<WorldTriangle> triangles;
    std::vector<std::pair<int, std::array<ImVec2, 8>>> selectedOutlines;
    std::unordered_set<int> coarsePreviewParts;
    std::vector<LogicEdge> logicEdges;
    ImVec2 logicHintMin{};
    ImVec2 logicHintMax{};

    ImVec2 screen(V3 world, float& depth) const;
    void setupCameraAndGrid();
    void buildSceneGeometry();
    void renderSceneGeometry();
    void drawLogicGateLabels();
    void buildLogicEdges();
    void drawLogicConnections();
    AxisHandle axisHandle(int signedAxis) const;
    int axisAtMouse(ImVec2 point) const;
    void drawSelectionGizmo();
    void handleInput();
};

V3 blockCenter(const Parser::Block& block);
} // namespace BlueprintViewport::Internal
