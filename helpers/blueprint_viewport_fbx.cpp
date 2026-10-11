#include "blueprint_viewport_internal.hpp"

#include "fbx_inflate.hpp"

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
struct FProp {
  char type{};
  std::string text;
  std::vector<double> arr;
};
struct FNode {
  std::string name;
  std::vector<FProp> props;
  std::vector<FNode> children;
};
struct BinFbx {
  const std::vector<uint8_t> &b;
  size_t pos = 27;
  bool wide{};
  size_t nodes = 0;
  size_t properties = 0;
  size_t arrayElements = 0;
  std::string error;
  template <class T> bool take(T &v) {
    if (pos + sizeof(T) > b.size())
      return false;
    std::memcpy(&v, b.data() + pos, sizeof(T));
    pos += sizeof(T);
    return true;
  }
  bool bytes(size_t n, const uint8_t *&p) {
    if (n > b.size() - pos)
      return false;
    p = b.data() + pos;
    pos += n;
    return true;
  }
  bool str(std::string &s, size_t n) {
    const uint8_t *p;
    if (!bytes(n, p))
      return false;
    s.assign((const char *)p, n);
    return true;
  }
  bool prop(FProp &p) {
    if (pos >= b.size()) {
      error = "Unexpected end of file before property";
      return false;
    }
    p.type = (char)b[pos++];
    if (p.type == 'S' || p.type == 'R') {
      uint32_t n;
      if (!take(n) || n > b.size() - pos)
        return false;
      if (p.type == 'S')
        return str(p.text, n);
      pos += n;
      return true;
    }
    if (p.type == 'Y') {
      int16_t v;
      if (!take(v))
        return false;
      p.arr.push_back(v);
      return true;
    }
    if (p.type == 'C') {
      uint8_t v;
      if (!take(v))
        return false;
      p.arr.push_back(v);
      return true;
    }
    if (p.type == 'I') {
      int32_t v;
      if (!take(v))
        return false;
      p.arr.push_back(v);
      return true;
    }
    if (p.type == 'F') {
      float v;
      if (!take(v))
        return false;
      p.arr.push_back(v);
      return true;
    }
    if (p.type == 'D') {
      double v;
      if (!take(v))
        return false;
      p.arr.push_back(v);
      return true;
    }
    if (p.type == 'L') {
      int64_t v;
      if (!take(v))
        return false;
      p.arr.push_back((double)v);
      return true;
    }
    size_t el = 0;
    switch (p.type) {
    case 'f':
    case 'i':
      el = 4;
      break;
    case 'd':
    case 'l':
      el = 8;
      break;
    case 'b':
    case 'c':
      el = 1;
      break;
    default:
      error = std::string("Unsupported FBX property type '") + p.type + "'";
      return false;
    }
    constexpr size_t maximumArrayElements = 2000000;
    constexpr size_t maximumTotalArrayElements = 8000000;
    uint32_t count;
    if (!take(count) || count > maximumArrayElements || count > maximumTotalArrayElements - arrayElements) {
      error = "FBX array exceeds parser element limits";
      return false;
    }
    uint32_t encoding, length;
    if (!take(encoding) || !take(length) || length > b.size() - pos) {
      error = "Invalid FBX array header";
      return false;
    }
    const size_t decodedBytes = (size_t)count * el;
    if (decodedBytes > 16ull * 1024ull * 1024ull) {
      error = "FBX array exceeds the 16 MiB decoded-array limit";
      return false;
    }
    const uint8_t *data;
    if (!bytes(length, data)) {
      error = "Truncated FBX array";
      return false;
    }
    std::vector<uint8_t> inflated;
    if (encoding == 1) {
      inflated.resize(decodedBytes);
      if (!FbxInflate::decode(data, length, inflated.data(), decodedBytes)) {
        error = "FBX array decompression or checksum failed";
        return false;
      }
      data = inflated.data();
    } else if (encoding != 0 || decodedBytes != length) {
      error = "Invalid uncompressed FBX array";
      return false;
    }
    arrayElements += count;
    p.arr.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
      if (el == 8) {
        if (p.type == 'd') {
          double x;
          std::memcpy(&x, data + i * el, el);
          p.arr.push_back(x);
        } else {
          int64_t x;
          std::memcpy(&x, data + i * el, el);
          p.arr.push_back((double)x);
        }
      } else if (el == 4) {
        if (p.type == 'f') {
          float x;
          std::memcpy(&x, data + i * el, el);
          p.arr.push_back(x);
        } else {
          int32_t x;
          std::memcpy(&x, data + i * el, el);
          p.arr.push_back(x);
        }
      } else
        p.arr.push_back(data[i]);
    }
    return true;
  }
  bool node(FNode &out, int depth = 0) {
    auto fail = [&](const std::string &why) {
      if (error.empty())
        error = why + " at byte " + std::to_string(pos);
      return false;
    };
    if (depth > 64)
      return fail("FBX node nesting limit exceeded");
    if (++nodes > 250000)
      return fail("FBX node count limit exceeded");
    uint64_t end = 0, nprop = 0, plen = 0;
    uint8_t nameLen = 0;
    if (wide) {
      if (!take(end) || !take(nprop) || !take(plen) || !take(nameLen))
        return fail("Truncated FBX node header");
    } else {
      uint32_t a, c, d;
      if (!take(a) || !take(c) || !take(d) || !take(nameLen))
        return fail("Truncated FBX node header");
      end = a;
      nprop = c;
      plen = d;
    }
    if (end == 0)
      return true;
    if (end > b.size() || end < pos || nameLen > end - pos || nprop > 10000 || nprop > 250000 - properties)
      return fail("Invalid FBX node bounds or property count limit");
    properties += (size_t)nprop;
    if (!str(out.name, nameLen))
      return fail("Truncated FBX node name");
    const size_t propertyStart = pos;
    out.props.reserve((size_t)nprop);
    for (uint64_t i = 0; i < nprop; i++) {
      FProp p;
      if (!prop(p))
        return fail("Invalid FBX node property");
      out.props.push_back(std::move(p));
    }
    if (pos - propertyStart != plen || pos > end)
      return fail("FBX property list length does not match its node");
    while (pos < end) {
      if (wide && end - pos < 25)
        break;
      if (!wide && end - pos < 13)
        break;
      FNode c;
      size_t before = pos;
      bool parsed = node(c, depth + 1);
      if (!c.name.empty())
        out.children.push_back(std::move(c));
      if (!parsed)
        return fail("Invalid nested FBX node");
      if (pos <= before)
        return fail("FBX parser made no progress");
    }
    pos = (size_t)end;
    return true;
  }
};
struct FbxTransform {
  V3 translation{}, rotation{}, scale{1.f, 1.f, 1.f};
  V3 rotationOffset{}, rotationPivot{}, preRotation{}, postRotation{};
  V3 scalingOffset{}, scalingPivot{};
  int rotationOrder{};
};
bool numericProperty(const FNode &node, size_t index, int64_t &value) {
  if (index >= node.props.size())
    return false;
  const auto &p = node.props[index];
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
V3 propertyVector(const FNode &node, const char *name, V3 fallback) {
  for (const auto &p : node.children) {
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
int propertyInt(const FNode &node, const char *name, int fallback) {
  for (const auto &p : node.children)
    if (p.name == "P" && !p.props.empty() && p.props[0].text == name) {
      int64_t value;
      if (numericProperty(p, 4, value))
        return (int)std::clamp<int64_t>(value, 0, 5);
    }
  return fallback;
}
FbxTransform transformFromProperties(const FNode &node, const char *translation, const char *rotation,
                                     const char *scale) {
  const FNode *properties = nullptr;
  for (const auto &child : node.children)
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
  static constexpr int sequences[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 2, 0}, {1, 0, 2}, {2, 0, 1}, {2, 1, 0}};
  const float angles[3] = {degrees.x * pi / 180.f, degrees.y * pi / 180.f, degrees.z * pi / 180.f};
  const int *sequence = sequences[std::clamp(order, 0, 5)];
  for (int i = 0; i < 3; i++) {
    const int axisIndex = sequence[inverse ? 2 - i : i];
    p = rotateAxis(p, axisIndex, angles[axisIndex] * (inverse ? -1.f : 1.f));
  }
  return p;
}
V3 applyFbxTransform(V3 p, const FbxTransform &t) {
  p = p - t.scalingPivot;
  p = {p.x * t.scale.x, p.y * t.scale.y, p.z * t.scale.z};
  p = p + t.scalingPivot + t.scalingOffset;
  p = p - t.rotationPivot;
  p = rotateEuler(p, t.postRotation, t.rotationOrder, true);
  p = rotateEuler(p, t.rotation, t.rotationOrder);
  p = rotateEuler(p, t.preRotation, t.rotationOrder);
  return p + t.rotationPivot + t.rotationOffset + t.translation;
}
const FNode *childNamed(const FNode &node, const char *name) {
  for (const auto &child : node.children)
    if (child.name == name)
      return &child;
  return nullptr;
}
bool geometryData(const FNode &node, Mesh &mesh, const std::vector<FbxTransform> &modelChain,
                  const std::vector<std::string> &materialNames) {
  const FProp *verts = nullptr;
  const FProp *faces = nullptr;
  for (const auto &c : node.children) {
    if (c.name == "Vertices" && !c.props.empty())
      verts = &c.props[0];
    if (c.name == "PolygonVertexIndex" && !c.props.empty())
      faces = &c.props[0];
  }
  if (!verts || !faces || verts->arr.size() < 9 || faces->arr.size() < 3 || verts->arr.size() % 3)
    return false;
  const FProp *uvValues = nullptr;
  const FProp *uvIndices = nullptr;
  std::string uvMapping, uvReference;
  const FProp *materialIndices = nullptr;
  std::string materialMapping;
  if (const FNode *layer = childNamed(node, "LayerElementMaterial"))
    for (const auto &c : layer->children) {
      if (c.name == "Materials" && !c.props.empty())
        materialIndices = &c.props[0];
      else if (c.name == "MappingInformationType" && !c.props.empty())
        materialMapping = c.props[0].text;
    }
  if (const FNode *layer = childNamed(node, "LayerElementUV"))
    for (const auto &c : layer->children) {
      if (c.name == "UV" && !c.props.empty())
        uvValues = &c.props[0];
      else if (c.name == "UVIndex" && !c.props.empty())
        uvIndices = &c.props[0];
      else if (c.name == "MappingInformationType" && !c.props.empty())
        uvMapping = c.props[0].text;
      else if (c.name == "ReferenceInformationType" && !c.props.empty())
        uvReference = c.props[0].text;
    }
  const FbxTransform geometric =
      transformFromProperties(node, "GeometricTranslation", "GeometricRotation", "GeometricScaling");
  auto transformedPoint = [&](uint32_t index) {
    V3 point{(float)verts->arr[index * 3], (float)verts->arr[index * 3 + 1], (float)verts->arr[index * 3 + 2]};
    point = applyFbxTransform(point, geometric);
    for (const auto &transform : modelChain)
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
      direct = uvMapping == "ByVertice" || uvMapping == "ByVertex" ? vertexIndex : (int64_t)polygonVertex;
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
      const std::string materialName = materialIndex >= 0 && (size_t)materialIndex < materialNames.size()
                                           ? lower(materialNames[(size_t)materialIndex])
                                           : std::string{};
      std::vector<uint32_t> polygonIndices;
      polygonIndices.reserve(poly.size());
      for (const auto &[vertexIndex, uv] : poly) {
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
void loadFbxScene(const std::vector<FNode> &roots, Mesh &mesh) {
  struct Link {
    int64_t child{}, parent{};
  };
  std::unordered_map<int64_t, const FNode *> geometryById, modelById, materialById;
  std::unordered_map<int64_t, int64_t> geometryParent, modelParent;
  std::unordered_map<int64_t, std::vector<std::string>> modelMaterials;
  std::vector<Link> links;
  auto visit = [&](auto &&self, const FNode &node) -> void {
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
    for (const auto &child : node.children)
      self(self, child);
  };
  for (const auto &root : roots)
    visit(visit, root);
  for (const auto &link : links) {
    if (geometryById.contains(link.child) && modelById.contains(link.parent))
      geometryParent[link.child] = link.parent;
    if (modelById.contains(link.child) && modelById.contains(link.parent))
      modelParent[link.child] = link.parent;
    if (materialById.contains(link.child) && modelById.contains(link.parent)) {
      const auto *material = materialById[link.child];
      std::string name = material->props.size() > 1 ? material->props[1].text : std::string{};
      const auto separator = name.rfind("::");
      if (separator != std::string::npos)
        name.erase(0, separator + 2);
      modelMaterials[link.parent].push_back(lower(name));
    }
  }
  for (const auto &[id, geometry] : geometryById) {
    std::vector<FbxTransform> chain;
    std::unordered_map<int64_t, bool> seen;
    auto parent = geometryParent.find(id);
    const int64_t geometryModelId = parent != geometryParent.end() ? parent->second : 0;
    while (parent != geometryParent.end() && modelById.contains(parent->second) && !seen.contains(parent->second)) {
      const int64_t modelId = parent->second;
      seen[modelId] = true;
      chain.push_back(transformFromProperties(*modelById[modelId], "Lcl Translation", "Lcl Rotation", "Lcl Scaling"));
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
bool loadFbx(const std::filesystem::path &path, Mesh &mesh) {
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
    uint32_t version = 0;
    std::memcpy(&version, b.data() + 23, 4);
    BinFbx p{b};
    p.pos = 27;
    p.wide = version >= 7500;
    bool parseFailed = false;
    std::vector<FNode> roots;
    while (p.pos + (p.wide ? 25 : 13) < b.size()) {
      FNode n;
      size_t before = p.pos;
      bool parsed = p.node(n);
      if (!n.name.empty())
        roots.push_back(std::move(n));
      if (!parsed || p.pos <= before) {
        parseFailed = true;
        break;
      }
    }
    loadFbxScene(roots, mesh);
    mesh.loaded = !mesh.indices.empty();
    if (!mesh.loaded)
      mesh.detail = p.error.empty() ? (parseFailed ? "FBX node parse stopped at byte " + std::to_string(p.pos)
                                                   : "No triangulatable Geometry node found")
                                    : p.error;
    return mesh.loaded;
  }
  // ASCII FBX exports are useful with modded assets. The stock game's binary
  // FBX meshes take the binary path above.
  std::string s((const char *)b.data(), b.size());
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
