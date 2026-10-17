#include "blueprint_viewport_internal.hpp"

#include "blueprint_viewport_fbx_binary.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace BlueprintViewport::Internal {
constexpr float pi = 3.14159265358979323846f;
struct FbxTransform {
    V3 translation{}, rotation{}, scale{1.f, 1.f, 1.f};
    V3 rotationOffset{}, rotationPivot{}, preRotation{}, postRotation{};
    V3 scalingOffset{}, scalingPivot{};
    int rotationOrder{};
};
bool numericProperty(const FNode& node, size_t index, int64_t& value) {
    if (index >= node.props.size())
        return false;
    const auto& p = node.props[index];
    if (!p.arr.empty()) {
        value = (int64_t)p.arr[0];
        return true;
    }
    if (!p.text.empty()) {
        try {
            size_t used = 0;
            value = std::stoll(p.text, &used);
            return used == p.text.size();
        } catch (...) {
        }
    }
    return false;
}
V3 propertyVector(const FNode& node, const char* name, V3 fallback) {
    for (const auto& p : node.children) {
        if (p.name != "P" || p.props.empty() || p.props[0].text != name)
            continue;
        float values[3]{};
        int count = 0;
        for (size_t i = 4; i < p.props.size() && count < 3; i++)
            if (!p.props[i].arr.empty())
                values[count++] = (float)p.props[i].arr[0];
        if (count == 3)
            return {values[0], values[1], values[2]};
    }
    return fallback;
}
int propertyInt(const FNode& node, const char* name, int fallback) {
    for (const auto& p : node.children)
        if (p.name == "P" && !p.props.empty() && p.props[0].text == name) {
            int64_t value;
            if (numericProperty(p, 4, value))
                return (int)std::clamp<int64_t>(value, 0, 5);
        }
    return fallback;
}
FbxTransform transformFromProperties(const FNode& node,
                                     const char* translation,
                                     const char* rotation,
                                     const char* scale) {
    const FNode* properties = nullptr;
    for (const auto& child : node.children)
        if (child.name == "Properties70") {
            properties = &child;
            break;
        }
    if (!properties)
        return {};
    FbxTransform result;
    result.translation = propertyVector(*properties, translation, {});
    result.rotation = propertyVector(*properties, rotation, {});
    result.scale = propertyVector(*properties, scale, {1.f, 1.f, 1.f});
    result.rotationOffset = propertyVector(*properties, "RotationOffset", {});
    result.rotationPivot = propertyVector(*properties, "RotationPivot", {});
    result.preRotation = propertyVector(*properties, "PreRotation", {});
    result.postRotation = propertyVector(*properties, "PostRotation", {});
    result.scalingOffset = propertyVector(*properties, "ScalingOffset", {});
    result.scalingPivot = propertyVector(*properties, "ScalingPivot", {});
    result.rotationOrder = propertyInt(*properties, "RotationOrder", 0);
    return result;
}
V3 rotateAxis(V3 p, int a, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    if (a == 0)
        return {p.x, c * p.y - s * p.z, s * p.y + c * p.z};
    if (a == 1)
        return {c * p.x + s * p.z, p.y, -s * p.x + c * p.z};
    return {c * p.x - s * p.y, s * p.x + c * p.y, p.z};
}
V3 rotateEuler(V3 p, V3 degrees, int order, bool inverse = false) {
    static constexpr int sequences[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 2, 0}, {1, 0, 2}, {2, 0, 1}, {2, 1, 0}};
    const float angles[3] = {
        degrees.x * pi / 180.f, degrees.y * pi / 180.f, degrees.z * pi / 180.f};
    const int* sequence = sequences[std::clamp(order, 0, 5)];
    for (int i = 0; i < 3; i++) {
        const int axisIndex = sequence[inverse ? 2 - i : i];
        p = rotateAxis(p, axisIndex, angles[axisIndex] * (inverse ? -1.f : 1.f));
    }
    return p;
}
V3 applyFbxTransform(V3 p, const FbxTransform& t) {
    p = p - t.scalingPivot;
    p = {p.x * t.scale.x, p.y * t.scale.y, p.z * t.scale.z};
    p = p + t.scalingPivot + t.scalingOffset;
    p = p - t.rotationPivot;
    p = rotateEuler(p, t.postRotation, t.rotationOrder, true);
    p = rotateEuler(p, t.rotation, t.rotationOrder);
    p = rotateEuler(p, t.preRotation, t.rotationOrder);
    return p + t.rotationPivot + t.rotationOffset + t.translation;
}
const FNode* childNamed(const FNode& node, const char* name) {
    for (const auto& child : node.children)
        if (child.name == name)
            return &child;
    return nullptr;
}
bool geometryData(const FNode& node,
                  Mesh& mesh,
                  const std::vector<FbxTransform>& modelChain,
                  const std::vector<std::string>& materialNames) {
    const FProp* verts = nullptr;
    const FProp* faces = nullptr;
    for (const auto& c : node.children) {
        if (c.name == "Vertices" && !c.props.empty())
            verts = &c.props[0];
        if (c.name == "PolygonVertexIndex" && !c.props.empty())
            faces = &c.props[0];
    }
    if (!verts || !faces || verts->arr.size() < 9 || faces->arr.size() < 3 || verts->arr.size() % 3)
        return false;
    const FProp* uvValues = nullptr;
    const FProp* uvIndices = nullptr;
    std::string uvMapping, uvReference;
    const FProp* materialIndices = nullptr;
    std::string materialMapping;
    if (const FNode* layer = childNamed(node, "LayerElementMaterial"))
        for (const auto& c : layer->children) {
            if (c.name == "Materials" && !c.props.empty())
                materialIndices = &c.props[0];
            else if (c.name == "MappingInformationType" && !c.props.empty())
                materialMapping = c.props[0].text;
        }
    if (const FNode* layer = childNamed(node, "LayerElementUV"))
        for (const auto& c : layer->children) {
            if (c.name == "UV" && !c.props.empty())
                uvValues = &c.props[0];
            else if (c.name == "UVIndex" && !c.props.empty())
                uvIndices = &c.props[0];
            else if (c.name == "MappingInformationType" && !c.props.empty())
                uvMapping = c.props[0].text;
            else if (c.name == "ReferenceInformationType" && !c.props.empty())
                uvReference = c.props[0].text;
        }
    const FbxTransform geometric = transformFromProperties(
        node, "GeometricTranslation", "GeometricRotation", "GeometricScaling");
    auto transformedPoint = [&](uint32_t index) {
        V3 point{(float)verts->arr[index * 3],
                 (float)verts->arr[index * 3 + 1],
                 (float)verts->arr[index * 3 + 2]};
        point = applyFbxTransform(point, geometric);
        for (const auto& transform : modelChain)
            point = applyFbxTransform(point, transform);
        return point;
    };
    auto uvFor = [&](size_t polygonVertex, uint32_t vertexIndex) {
        std::array<float, 2> uv{};
        if (!uvValues || uvValues->arr.size() < 2)
            return uv;
        int64_t direct = -1;
        if (uvReference == "IndexToDirect" && uvIndices && polygonVertex < uvIndices->arr.size())
            direct = (int64_t)uvIndices->arr[polygonVertex];
        else
            direct = uvMapping == "ByVertice" || uvMapping == "ByVertex" ? vertexIndex
                                                                         : (int64_t)polygonVertex;
        if (direct >= 0 && (size_t)direct * 2 + 1 < uvValues->arr.size()) {
            uv[0] = (float)uvValues->arr[(size_t)direct * 2];
            uv[1] = 1.f - (float)uvValues->arr[(size_t)direct * 2 + 1];
        }
        return uv;
    };
    std::vector<std::pair<uint32_t, std::array<float, 2>>> poly;
    size_t polygonVertex = 0, polygon = 0;
    for (double d : faces->arr) {
        const int64_t raw = (int64_t)d;
        const bool end = raw < 0;
        const uint64_t ix = (uint64_t)(end ? -raw - 1 : raw);
        if (ix >= verts->arr.size() / 3) {
            poly.clear();
            ++polygonVertex;
            continue;
        }
        poly.emplace_back((uint32_t)ix, uvFor(polygonVertex++, (uint32_t)ix));
        if (end) {
            int materialIndex = -1;
            if (materialIndices && (materialMapping == "ByPolygon" || materialMapping.empty()) &&
                polygon < materialIndices->arr.size())
                materialIndex = (int)materialIndices->arr[polygon];
            const std::string materialName =
                materialIndex >= 0 && (size_t)materialIndex < materialNames.size()
                    ? lower(materialNames[(size_t)materialIndex])
                    : std::string{};
            std::vector<uint32_t> polygonIndices;
            polygonIndices.reserve(poly.size());
            for (const auto& [vertexIndex, uv] : poly) {
                polygonIndices.push_back((uint32_t)mesh.vertices.size());
                mesh.vertices.push_back(transformedPoint(vertexIndex));
                mesh.uvs.push_back(uv);
            }
            for (size_t k = 1; k + 1 < polygonIndices.size(); k++) {
                mesh.indices.push_back(polygonIndices[0]);
                mesh.indices.push_back(polygonIndices[k]);
                mesh.indices.push_back(polygonIndices[k + 1]);
                mesh.triangleMaterials.push_back(materialName);
            }
            ++polygon;
            poly.clear();
        }
    }
    return true;
}
void loadFbxScene(const std::vector<FNode>& roots, Mesh& mesh) {
    struct Link {
        int64_t child{}, parent{};
    };
    std::unordered_map<int64_t, const FNode*> geometryById, modelById, materialById;
    std::unordered_map<int64_t, int64_t> geometryParent, modelParent;
    std::unordered_map<int64_t, std::vector<std::string>> modelMaterials;
    std::vector<Link> links;
    auto visit = [&](auto&& self, const FNode& node) -> void {
        if (node.name == "Geometry" || node.name == "Model" || node.name == "Material") {
            int64_t id;
            if (numericProperty(node, 0, id)) {
                if (node.name == "Geometry")
                    geometryById[id] = &node;
                else if (node.name == "Model")
                    modelById[id] = &node;
                else
                    materialById[id] = &node;
            }
        }
        if (node.name == "C" && node.props.size() >= 3) {
            int64_t child, parent;
            if (numericProperty(node, 1, child) && numericProperty(node, 2, parent))
                links.push_back({child, parent});
        }
        for (const auto& child : node.children)
            self(self, child);
    };
    for (const auto& root : roots)
        visit(visit, root);
    for (const auto& link : links) {
        if (geometryById.contains(link.child) && modelById.contains(link.parent))
            geometryParent[link.child] = link.parent;
        if (modelById.contains(link.child) && modelById.contains(link.parent))
            modelParent[link.child] = link.parent;
        if (materialById.contains(link.child) && modelById.contains(link.parent)) {
            const auto* material = materialById[link.child];
            std::string name = material->props.size() > 1 ? material->props[1].text : std::string{};
            const auto separator = name.rfind("::");
            if (separator != std::string::npos)
                name.erase(0, separator + 2);
            modelMaterials[link.parent].push_back(lower(name));
        }
    }
    for (const auto& [id, geometry] : geometryById) {
        std::vector<FbxTransform> chain;
        std::unordered_map<int64_t, bool> seen;
        auto parent = geometryParent.find(id);
        const int64_t geometryModelId = parent != geometryParent.end() ? parent->second : 0;
        while (parent != geometryParent.end() && modelById.contains(parent->second) &&
               !seen.contains(parent->second)) {
            const int64_t modelId = parent->second;
            seen[modelId] = true;
            chain.push_back(transformFromProperties(
                *modelById[modelId], "Lcl Translation", "Lcl Rotation", "Lcl Scaling"));
            parent = modelParent.find(modelId);
        }
        std::vector<std::string> materialNames;
        if (geometryModelId != 0) {
            auto materials = modelMaterials.find(geometryModelId);
            if (materials != modelMaterials.end())
                materialNames = materials->second;
        }
        geometryData(*geometry, mesh, chain, materialNames);
    }
}
bool loadFbx(const std::filesystem::path& path, Mesh& mesh) {
    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(path, sizeError);
    constexpr uintmax_t maximumModelFileBytes = 32ull * 1024ull * 1024ull;
    if (!sizeError && fileSize > maximumModelFileBytes) {
        mesh.detail = "FBX exceeds the 32 MiB viewer safety limit";
        return false;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        mesh.detail = "FBX file could not be opened";
        return false;
    }
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    if (b.size() < 32) {
        mesh.detail = "FBX file is too short";
        return false;
    }
    const char magic[] = "Kaydara FBX Binary  ";
    if (b.size() >= 27 && std::memcmp(b.data(), magic, sizeof(magic) - 1) == 0) {
        const BinaryFbxParseResult parsed = parseBinaryFbx(b);
        loadFbxScene(parsed.roots, mesh);
        mesh.loaded = !mesh.indices.empty();
        if (!mesh.loaded) {
            mesh.detail = parsed.error.empty()
                              ? (parsed.stoppedEarly ? "FBX node parse stopped at byte " +
                                                           std::to_string(parsed.offset)
                                                     : "No triangulatable Geometry node found")
                              : parsed.error;
        }
        return mesh.loaded;
    }
    // ASCII FBX exports are useful with modded assets. The stock game's binary
    // FBX meshes take the binary path above.
    std::string s((const char*)b.data(), b.size());
    std::regex vr(R"(Vertices\s*:\s*\*\s*\d+\s*\{\s*a\s*:\s*([^}]*)\})");
    std::regex fr(R"(PolygonVertexIndex\s*:\s*\*\s*\d+\s*\{\s*a\s*:\s*([^}]*)\})");
    std::smatch vm, fm;
    if (!std::regex_search(s, vm, vr) || !std::regex_search(s, fm, fr)) {
        mesh.detail = "ASCII FBX has no Vertices/PolygonVertexIndex arrays";
        return false;
    }
    auto nums = [](std::string t) {
        std::vector<double> v;
        const std::regex r(R"(-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)");
        for (std::sregex_iterator i(t.begin(), t.end(), r), e; i != e; ++i) {
            try {
                v.push_back(std::stod(i->str()));
            } catch (...) {
            }
        }
        return v;
    };
    auto v = nums(vm[1].str()), idx = nums(fm[1].str());
    if (v.size() < 9 || v.size() % 3)
        return false;
    for (size_t i = 0; i < v.size(); i += 3)
        mesh.vertices.push_back({(float)v[i], (float)v[i + 1], (float)v[i + 2]});
    std::vector<uint32_t> poly;
    for (double d : idx) {
        auto r = (int64_t)d;
        bool end = r < 0;
        uint64_t ix = (uint64_t)(end ? -r - 1 : r);
        if (ix >= mesh.vertices.size()) {
            poly.clear();
            continue;
        }
        poly.push_back((uint32_t)ix);
        if (end) {
            for (size_t k = 1; k + 1 < poly.size(); k++) {
                mesh.indices.insert(mesh.indices.end(), {poly[0], poly[k], poly[k + 1]});
            }
            poly.clear();
        }
    }
    mesh.loaded = !mesh.indices.empty();
    if (!mesh.loaded)
        mesh.detail = "ASCII FBX contains no triangulatable polygons";
    return mesh.loaded;
}
} // namespace BlueprintViewport::Internal
