#include "blueprint_viewport_internal.hpp"

#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace BlueprintViewport::Internal {
std::shared_ptr<Mesh> meshFor(const std::string& uuid) {
    loadCatalog();
    auto id = lower(uuid);
    auto found = state.meshes.find(id);
    if (found != state.meshes.end())
        return found->second;
    auto mesh = std::make_shared<Mesh>();
    auto entry = state.catalog.find(id);
    if (entry != state.catalog.end()) {
        std::string lastFailure;
        for (const auto& modelPath : entry->second.models) {
            auto asset = state.meshFiles.find(modelPath.lexically_normal().native());
            if (asset != state.meshFiles.end()) {
                mesh = asset->second;
                if (mesh->loaded)
                    break;
                lastFailure = mesh->detail;
                continue;
            }
            auto candidate = std::make_shared<Mesh>();
            const auto extension = lower(modelPath.extension().string());
            if (extension == ".fbx")
                loadFbx(modelPath, *candidate);
            else if (extension == ".dae")
                loadDae(modelPath, *candidate);
            else
                loadOgreMesh(modelPath, *candidate);
            if (candidate->vertices.size() > 500000 || candidate->indices.size() > 1500000) {
                candidate->vertices.clear();
                candidate->indices.clear();
                candidate->loaded = false;
                candidate->detail =
                    "Model exceeds the 500,000 vertex / 1,500,000 index viewer limit";
            }
            if (candidate->loaded)
                buildNormals(*candidate);
            constexpr size_t maximumMeshCacheBytes = 128ull * 1024ull * 1024ull;
            const size_t meshBytes = candidate->vertices.capacity() * sizeof(V3) +
                                     candidate->normals.capacity() * sizeof(V3) +
                                     candidate->indices.capacity() * sizeof(uint32_t);
            if (candidate->loaded && meshBytes > maximumMeshCacheBytes - state.meshCacheBytes) {
                candidate->vertices = std::vector<V3>();
                candidate->normals = std::vector<V3>();
                candidate->indices = std::vector<uint32_t>();
                candidate->loaded = false;
                candidate->hasBounds = false;
                candidate->detail =
                    "Per-blueprint mesh cache limit reached; using a cuboid preview";
            } else if (candidate->loaded)
                state.meshCacheBytes += meshBytes;
            if (!candidate->loaded) {
                candidate->vertices = std::vector<V3>();
                candidate->normals = std::vector<V3>();
                candidate->indices = std::vector<uint32_t>();
                lastFailure = candidate->detail;
            }
            state.meshFiles.emplace(modelPath.lexically_normal().native(), candidate);
            mesh = std::move(candidate);
            if (mesh->loaded)
                break;
        }
        if (!mesh->loaded && !lastFailure.empty())
            mesh->detail = lastFailure;
    } else if (state.blockShapes.contains(id))
        mesh->detail = "Built-in Scrap Mechanic block; shown as a voxel cuboid";
    else
        mesh->detail = "Shape UUID was not found in the installed shape sets";
    state.meshes.emplace(id, mesh);
    return mesh;
}
GLuint diffuseTextureFor(const std::filesystem::path& diffuseTexture) {
    if (diffuseTexture.empty())
        return 0;
    const auto key = diffuseTexture.lexically_normal().native();
    auto found = state.textures.find(key);
    if (found != state.textures.end())
        return found->second;
    int width = 0, height = 0, channels = 0;
    GLuint texture = 0;
    if (stbi_info(diffuseTexture.string().c_str(), &width, &height, &channels) && width > 0 &&
        height > 0 && width <= 8192 && height <= 8192 &&
        (uint64_t)width * (uint64_t)height <= 32ull * 1024ull * 1024ull &&
        (size_t)width * (size_t)height * 4 <=
            256ull * 1024ull * 1024ull - state.textureCacheBytes) {
        unsigned char* pixels =
            stbi_load(diffuseTexture.string().c_str(), &width, &height, &channels, 4);
        if (pixels) {
            const GlStateSnapshot saved = captureGlState();
            GLint unpackAlignment = 4;
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
            while (glGetError() != GL_NO_ERROR) {
            }
            glGenTextures(1, &texture);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(
                GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glGenerateMipmap(GL_TEXTURE_2D);
            stbi_image_free(pixels);
            const GLenum uploadError = glGetError();
            glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
            restoreGlState(saved);
            if (uploadError != GL_NO_ERROR) {
                if (texture)
                    glDeleteTextures(1, &texture);
                texture = 0;
            } else
                state.textureCacheBytes += (size_t)width * (size_t)height * 4;
        }
    }
    state.textures.emplace(key, texture);
    return texture;
}

GLuint logicGateModeTextureFor(const std::filesystem::path& diffuseTexture, int mode) {
    if (diffuseTexture.empty() || mode < 0 || mode > 5)
        return 0;
    const auto key = diffuseTexture.lexically_normal().native();
    auto found = state.logicGateModeTextures.find(key);
    if (found != state.logicGateModeTextures.end() && found->second[mode])
        return found->second[mode];

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels =
        stbi_load(diffuseTexture.string().c_str(), &width, &height, &channels, 4);
    if (!pixels || width < 6 || height < 6) {
        if (pixels)
            stbi_image_free(pixels);
        return 0;
    }

    // The game texture has six gate symbols across each atlas row. Its second
    // row is the lit cyan version used to make the selected setting visible.
    // Mode indices follow LogicGateIcons.xml: AND, OR, XOR, NAND, NOR, XNOR.
    const int x0 = mode * width / 6;
    const int x1 = (mode + 1) * width / 6;
    const int cropWidth = x1 - x0;
    const int y0 = height / 6;
    const int y1 = height / 3;
    const int cropHeight = y1 - y0;
    if (cropWidth <= 0 || cropHeight <= 0 ||
        (size_t)cropWidth * (size_t)cropHeight * 4 >
            256ull * 1024ull * 1024ull - state.textureCacheBytes) {
        stbi_image_free(pixels);
        return 0;
    }
    std::vector<unsigned char> crop((size_t)cropWidth * cropHeight * 4);
    for (int y = 0; y < cropHeight; ++y)
        std::memcpy(crop.data() + (size_t)y * cropWidth * 4,
                    pixels + ((size_t)(y0 + y) * width + x0) * 4,
                    (size_t)cropWidth * 4);
    stbi_image_free(pixels);

    const GlStateSnapshot saved = captureGlState();
    GLint unpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
    while (glGetError() != GL_NO_ERROR) {
    }
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 cropWidth,
                 cropHeight,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 crop.data());
    const GLenum uploadError = glGetError();
    glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
    restoreGlState(saved);
    if (uploadError != GL_NO_ERROR) {
        if (texture)
            glDeleteTextures(1, &texture);
        return 0;
    }
    state.textureCacheBytes += crop.size();
    state.logicGateModeTextures[key][mode] = texture;
    return texture;
}

void buildNormals(Mesh& mesh) {
    struct PositionKey {
        int64_t x{}, y{}, z{};
        bool operator==(const PositionKey& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };
    struct PositionHash {
        size_t operator()(const PositionKey& p) const {
            size_t h = std::hash<int64_t>{}(p.x);
            h ^= std::hash<int64_t>{}(p.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int64_t>{}(p.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    auto key = [](V3 v) {
        return PositionKey{(int64_t)std::llround(v.x * 100000.f),
                           (int64_t)std::llround(v.y * 100000.f),
                           (int64_t)std::llround(v.z * 100000.f)};
    };
    mesh.normals.assign(mesh.vertices.size(), V3{});
    if (!mesh.vertices.empty()) {
        mesh.boundsMin = mesh.boundsMax = mesh.vertices.front();
        for (const auto& v : mesh.vertices) {
            mesh.boundsMin.x = std::min(mesh.boundsMin.x, v.x);
            mesh.boundsMin.y = std::min(mesh.boundsMin.y, v.y);
            mesh.boundsMin.z = std::min(mesh.boundsMin.z, v.z);
            mesh.boundsMax.x = std::max(mesh.boundsMax.x, v.x);
            mesh.boundsMax.y = std::max(mesh.boundsMax.y, v.y);
            mesh.boundsMax.z = std::max(mesh.boundsMax.z, v.z);
        }
        mesh.hasBounds = true;
    }
    std::unordered_map<PositionKey, std::vector<V3>, PositionHash> coincidentFaceNormals;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size())
            continue;
        V3 n = cross(mesh.vertices[b] - mesh.vertices[a], mesh.vertices[c] - mesh.vertices[a]);
        if (dot(n, n) < 1e-16f)
            continue;
        mesh.normals[a] = mesh.normals[a] + n;
        mesh.normals[b] = mesh.normals[b] + n;
        mesh.normals[c] = mesh.normals[c] + n;
        coincidentFaceNormals[key(mesh.vertices[a])].push_back(n);
        coincidentFaceNormals[key(mesh.vertices[b])].push_back(n);
        coincidentFaceNormals[key(mesh.vertices[c])].push_back(n);
    }
    // Ogre and FBX exports often duplicate vertices at submesh and UV seams.
    // Blend normals at coincident positions across gentle edges so triangulated
    // faces do not show as a patchwork of flat triangles. Keep true hard edges.
    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        V3 reference = norm(mesh.normals[i]);
        auto found = coincidentFaceNormals.find(key(mesh.vertices[i]));
        if (found != coincidentFaceNormals.end()) {
            V3 smooth{};
            for (V3 candidate : found->second)
                if (dot(reference, norm(candidate)) >= .5f)
                    smooth = smooth + candidate;
            if (dot(smooth, smooth) > 1e-16f)
                reference = norm(smooth);
        }
        mesh.normals[i] = reference;
    }
}

void releaseTextureCache() {
    for (const auto& [path, textures] : state.logicGateModeTextures) {
        (void)path;
        for (GLuint texture : textures)
            if (texture)
                glDeleteTextures(1, &texture);
    }
    state.logicGateModeTextures.clear();
    for (const auto& [path, texture] : state.textures)
        if (texture)
            glDeleteTextures(1, &texture);
    state.textures.clear();
    state.textureCacheBytes = 0;
}
} // namespace BlueprintViewport::Internal

namespace BlueprintViewport {
using namespace Internal;

ModelLoadReport inspectModelFile(const std::filesystem::path& path) {
    Internal::Mesh mesh;
    const auto extension = Internal::lower(path.extension().string());
    const bool loaded = extension == ".mesh"  ? Internal::loadOgreMesh(path, mesh)
                        : extension == ".dae" ? Internal::loadDae(path, mesh)
                                              : Internal::loadFbx(path, mesh);
    return {loaded, mesh.vertices.size(), mesh.indices.size() / 3, mesh.detail};
}

ModelLoadReport inspectShape(const std::string& shapeId) {
    Internal::loadCatalog();
    const auto found = Internal::state.catalog.find(Internal::lower(shapeId));
    if (found == Internal::state.catalog.end()) {
        const auto id = Internal::lower(shapeId);
        if (Internal::state.blockShapes.contains(id))
            return {false,
                    8,
                    12,
                    "Built-in Scrap Mechanic block; shown as a voxel cuboid because "
                    "it has no imported mesh"};
        if (Internal::state.shapeSizes.contains(id))
            return {false, 8, 12, "Known part without a supported mesh; shown as a cuboid preview"};
        return {false, 0, 0, "Shape UUID was not found in the installed shape sets"};
    }
    const auto mesh = Internal::meshFor(shapeId);
    if (!mesh)
        return {false, 0, 0, "Model loader did not create a mesh record"};
    ModelLoadReport report{
        mesh->loaded, mesh->vertices.size(), mesh->indices.size() / 3, mesh->detail};
    if (report.detail.empty() && !found->second.models.empty())
        report.detail = found->second.models.front().string();
    return report;
}
} // namespace BlueprintViewport
