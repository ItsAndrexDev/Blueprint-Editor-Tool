#include "blueprint_viewport_canvas.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BlueprintViewport::Internal {
V3 blockCenter(const Parser::Block& block) {
    const V3 dimensions = shapeSize(block);
    const auto placement = placementTransform(block, {dimensions.x, dimensions.y, dimensions.z});
    return placement.translation;
}

void ViewportCanvas::setupCameraAndGrid() {
    float minY = std::numeric_limits<float>::max(), maxY = -minY, minX = minY, maxX = -minY,
          minZ = minY, maxZ = -minY;
    for (const auto& b : blocks) {
        const V3 center = blockCenter(b), dimensions = shapeSize(b), half = dimensions * .5f;
        for (int mask = 0; mask < 8; mask++) {
            const V3 c = center + orient({mask & 1 ? half.x : -half.x,
                                          mask & 2 ? half.y : -half.y,
                                          mask & 4 ? half.z : -half.z},
                                         b);
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
    focus = {(minX + maxX) * .5f, (minY + maxY) * .5f, (minZ + maxZ) * .5f};
    const float longest = std::max({maxX - minX, maxY - minY, maxZ - minZ, 1.f});
    const float fitScale = std::min(size.x, size.y) * .74f / std::max(8.f, longest * 1.65f);
    camera = {};
    camera.center = {origin.x + size.x * .5f + state.pan.x, origin.y + size.y * .52f + state.pan.y};
    camera.scale = std::clamp(fitScale * state.zoom, .25f, 140.f);
    camera.cy = std::cos(state.yaw);
    camera.sy = std::sin(state.yaw);
    camera.cp = std::cos(state.pitch);
    camera.sp = std::sin(state.pitch);
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
}

void ViewportCanvas::buildSceneGeometry() {
    triangles.clear();
    selectedOutlines.clear();
    hasSelectedOutline = false;
    selectedCenter = {};
    coarsePreviewParts.clear();
    size_t total = 0;
    for (const auto& b : blocks) {
        auto m = meshFor(b.shapeID);
        total += m->loaded ? m->indices.size() / 3 : 12;
    }
    // Keep per-frame projection and streamed GPU memory bounded, but let normal
    // creations use the actual part meshes. The former 15k triangle cutoff was
    // low enough that a handful of detailed parts switched the entire scene to
    // boxes, which made ordinary creations appear to have no models at all.
    constexpr size_t maximumSceneTriangles = 120000;
    constexpr size_t maximumPreviewBlocks = 10000;
    coarsePreview = total > maximumSceneTriangles;
    const size_t previewStride =
        coarsePreview
            ? std::max<size_t>(1, (blocks.size() + maximumPreviewBlocks - 1) / maximumPreviewBlocks)
            : 1;
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
                if (!actions.isSelected(*it) &&
                    (replace == coarsePreviewParts.end() || *it > *replace))
                    replace = it;
            if (replace != coarsePreviewParts.end()) {
                coarsePreviewParts.erase(replace);
                coarsePreviewParts.insert(index);
            }
        }
    }
    const size_t perPartTriangleBudget =
        coarsePreview
            ? std::max<size_t>(
                  12, maximumSceneTriangles / std::max<size_t>(1, coarsePreviewParts.size()))
            : total;
    triangles.reserve(coarsePreview ? maximumSceneTriangles : total);
    for (size_t bi = 0; bi < blocks.size(); bi++) {
        if (coarsePreview && !coarsePreviewParts.contains((int)bi))
            continue;
        const auto& b = blocks[bi];
        auto m = meshFor(b.shapeID);
        bool imported = m && m->loaded && !m->indices.empty();
        const int col = displayColor(b);
        const auto diffusePath = state.shapeDiffuseTextures.find(lower(b.shapeID));
        const GLuint diffuseTexture = imported && diffusePath != state.shapeDiffuseTextures.end()
                                          ? diffuseTextureFor(diffusePath->second)
                                          : 0;
        float red = (float)((col >> 16) & 255) / 255.f, green = (float)((col >> 8) & 255) / 255.f,
              blue = (float)(col & 255) / 255.f;
        const V3 dimensions = shapeSize(b);
        const auto catalogEntry = state.catalog.find(lower(b.shapeID));
        const bool isLogicGate = lower(b.shapeID) == "9f0f56e8-2c31-4d83-996c-d00a9b296c3f";
        const int logicGateMode = isLogicGate ? actions.logicGateModeForPart((int)bi) : 0;
        GLuint gateModeTexture = 0;
        if (isLogicGate && catalogEntry != state.catalog.end()) {
            const auto screenMaterial = catalogEntry->second.materialTextures.find("screen");
            if (screenMaterial != catalogEntry->second.materialTextures.end())
                gateModeTexture = logicGateModeTextureFor(screenMaterial->second, logicGateMode);
        }
        const bool centerMeshOnFootprint =
            catalogEntry != state.catalog.end() && catalogEntry->second.centerMeshOnFootprint;
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
        const V3 meshCenter = imported && m->hasBounds ? (m->boundsMin + m->boundsMax) * .5f : V3{};
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
        if (actions.isSelected((int)bi)) {
            const V3 localMin =
                imported ? lo - (centerMeshOnFootprint ? meshCenter : V3{}) : V3{-.5f, -.5f, -.5f};
            const V3 localMax =
                imported ? hi - (centerMeshOnFootprint ? meshCenter : V3{}) : V3{.5f, .5f, .5f};
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
        auto append = [&](V3 aa,
                          V3 bb,
                          V3 cc,
                          V3 na,
                          V3 nb,
                          V3 nc,
                          std::array<float, 2> ta = {},
                          std::array<float, 2> tb = {},
                          std::array<float, 2> tc = {},
                          GLuint triangleTexture = UINT32_MAX,
                          bool preserveTextureColors = false) {
            V3 a = world(aa), q = world(bb), r = world(cc);
            const V3 transformed[3] = {norm(orient({na.x / sc[0], na.y / sc[1], na.z / sc[2]}, b)),
                                       norm(orient({nb.x / sc[0], nb.y / sc[1], nb.z / sc[2]}, b)),
                                       norm(orient({nc.x / sc[0], nc.y / sc[1], nc.z / sc[2]}, b))};
            const V3 lightDir = norm(V3{-.4f, .5f, .82f});
            ImU32 colors[3];
            for (int i = 0; i < 3; i++) {
                float light = .88f + .12f * std::max(0.f, dot(transformed[i], lightDir));
                float shade = actions.isSelected((int)bi) ? std::min(1.f, light * 1.04f) : light;
                const float tintRed = preserveTextureColors ? 1.f : red;
                const float tintGreen = preserveTextureColors ? 1.f : green;
                const float tintBlue = preserveTextureColors ? 1.f : blue;
                colors[i] = IM_COL32((int)(tintRed * shade * 255),
                                     (int)(tintGreen * shade * 255),
                                     (int)(tintBlue * shade * 255),
                                     255);
            }
            float da, db, dc;
            auto pa = screen(a, da), pb = screen(q, db), pc = screen(r, dc);
            triangles.push_back({{pa, pb, pc},
                                 {da, db, dc},
                                 {colors[0], colors[1], colors[2]},
                                 {ta, tb, tc},
                                 triangleTexture == UINT32_MAX ? diffuseTexture : triangleTexture,
                                 (int)bi});
        };
        if (imported) {
            const size_t sourceTriangles = m->indices.size() / 3;
            const size_t triangleStride =
                coarsePreview
                    ? std::max<size_t>(
                          1, (sourceTriangles + perPartTriangleBudget - 1) / perPartTriangleBudget)
                    : 1;
            for (size_t tri = 0; tri < sourceTriangles; tri += triangleStride) {
                size_t ti = tri * 3;
                const auto ia = m->indices[ti], ib = m->indices[ti + 1], ic = m->indices[ti + 2];
                if (ia >= m->vertices.size() || ib >= m->vertices.size() ||
                    ic >= m->vertices.size())
                    continue;
                const auto uvAt = [&](uint32_t i) {
                    return i < m->uvs.size() ? m->uvs[i] : std::array<float, 2>{};
                };
                const bool logicGateScreen = isLogicGate && tri < m->triangleMaterials.size() &&
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
                if (catalogEntry != state.catalog.end() && tri < m->triangleMaterials.size()) {
                    auto material =
                        catalogEntry->second.materialTextures.find(m->triangleMaterials[tri]);
                    if (material != catalogEntry->second.materialTextures.end())
                        triangleTexture = diffuseTextureFor(material->second);
                }
                if (logicGateScreen && gateModeTexture)
                    triangleTexture = gateModeTexture;
                append(m->vertices[ia],
                       m->vertices[ib],
                       m->vertices[ic],
                       m->normals[ia],
                       m->normals[ib],
                       m->normals[ic],
                       screenUvAt(ia),
                       screenUvAt(ib),
                       screenUvAt(ic),
                       triangleTexture,
                       logicGateScreen);
            }
        } else {
            V3 v[8] = {{-.5f, -.5f, -.5f},
                       {.5f, -.5f, -.5f},
                       {.5f, .5f, -.5f},
                       {-.5f, .5f, -.5f},
                       {-.5f, -.5f, .5f},
                       {.5f, -.5f, .5f},
                       {.5f, .5f, .5f},
                       {-.5f, .5f, .5f}};
            const int ix[] = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                              3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
            for (int t = 0; t < 36; t += 3) {
                const V3 a = v[ix[t]], q = v[ix[t + 1]], r = v[ix[t + 2]],
                         n = norm(cross(q - a, r - a));
                append(a, q, r, n, n, n);
            }
        }
    }
}

void ViewportCanvas::renderSceneGeometry() {
    // Upload projected triangles to a small offscreen target with a real depth
    // buffer. Average-depth triangle sorting cannot correctly resolve large,
    // intersecting surfaces, which made floor blocks paint across other parts.
    float nearestDepth = -std::numeric_limits<float>::infinity();
    float farthestDepth = std::numeric_limits<float>::infinity();
    for (const auto& triangle : triangles)
        for (float depth : triangle.vertexDepth) {
            nearestDepth = std::max(nearestDepth, depth);
            farthestDepth = std::min(farthestDepth, depth);
        }
    const float depthRange = std::max(1e-5f, nearestDepth - farthestDepth);
    std::vector<SceneBatch> gpuBatches;
    std::unordered_map<GLuint, size_t> batchByTexture;
    for (const auto& triangle : triangles)
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
            auto& vertices = gpuBatches[batch->second].vertices;
            vertices.push_back({x,
                                y,
                                z,
                                (float)((packed >> IM_COL32_R_SHIFT) & 0xff) / 255.f,
                                (float)((packed >> IM_COL32_G_SHIFT) & 0xff) / 255.f,
                                (float)((packed >> IM_COL32_B_SHIFT) & 0xff) / 255.f,
                                triangle.uv[corner][0],
                                triangle.uv[corner][1]});
        }
    if (triangles.empty())
        state.renderError.clear();
    const bool sceneRendered =
        triangles.empty() || drawDepthBufferedScene(gpuBatches, origin, size, dl);
    if (!sceneRendered) {
        const ImVec2 message{origin.x + 12.f, origin.y + 12.f};
        dl->AddRectFilled(message,
                          ImVec2(message.x + std::min(size.x - 24.f, 500.f), message.y + 50.f),
                          IM_COL32(56, 31, 24, 240),
                          5.f);
        dl->AddText(ImVec2(message.x + 9.f, message.y + 9.f),
                    IM_COL32(255, 205, 170, 255),
                    "3D depth renderer could not start; see viewer diagnostics in "
                    "the inspector.");
    }
    if (coarsePreview) {
        const ImVec2 label{origin.x + std::max(12.f, size.x - 390.f), origin.y + 12.f};
        dl->AddRectFilled(
            label, ImVec2(label.x + 378.f, label.y + 28.f), IM_COL32(16, 20, 26, 235), 5.f);
        dl->AddText(ImVec2(label.x + 8.f, label.y + 7.f),
                    IM_COL32(255, 210, 135, 255),
                    "Large creation: real meshes shown at reduced detail.");
    }
}

void ViewportCanvas::drawLogicGateLabels() {
    if (state.showLogicGateLabels) {
        dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
        for (size_t index = 0; index < blocks.size(); ++index) {
            if (coarsePreview && !coarsePreviewParts.contains((int)index))
                continue;
            if (lower(blocks[index].shapeID) != "9f0f56e8-2c31-4d83-996c-d00a9b296c3f")
                continue;
            const char* mode = logicGateModeName(actions.logicGateModeForPart((int)index));
            const ImVec2 textSize = ImGui::CalcTextSize(mode);
            float gateDepth;
            const ImVec2 center = screen(blockCenter(blocks[index]), gateDepth);
            (void)gateDepth;
            const ImVec2 textPosition(center.x - textSize.x * .5f, center.y - textSize.y * .5f);
            constexpr float paddingX = 5.f, paddingY = 3.f;
            const ImVec2 badgeMin(textPosition.x - paddingX, textPosition.y - paddingY);
            const ImVec2 badgeMax(textPosition.x + textSize.x + paddingX,
                                  textPosition.y + textSize.y + paddingY);
            dl->AddRectFilled(badgeMin, badgeMax, IM_COL32(10, 20, 27, 238), 4.f);
            dl->AddRect(badgeMin, badgeMax, IM_COL32(70, 210, 226, 245), 4.f, 0, 1.f);
            dl->AddText(textPosition, IM_COL32(218, 251, 255, 255), mode);
        }
        dl->PopClipRect();
    }
}

} // namespace BlueprintViewport::Internal
