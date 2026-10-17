#include "blueprint_viewport_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace BlueprintViewport::Internal {
std::string xmlAttribute(const std::string& attributes, const char* name) {
    const std::regex pattern(std::string(R"(\b)") + name + R"(\s*=\s*["']([^"']*)["'])");
    std::smatch match;
    return std::regex_search(attributes, match, pattern) ? match[1].str() : std::string{};
}
std::string xmlElementText(const std::string& source, const char* tag) {
    const std::regex pattern(std::string("<") + tag + R"(\b[^>]*>([\s\S]*?)</)" + tag + R"(\s*>)");
    std::smatch match;
    return std::regex_search(source, match, pattern) ? match[1].str() : std::string{};
}
bool parseXmlNumbers(const std::string& text, std::vector<double>& values, size_t maximumValues) {
    const char* p = text.c_str();
    const char* end = p + text.size();
    while (p < end) {
        while (p < end && (std::isspace(static_cast<unsigned char>(*p)) || *p == ','))
            ++p;
        if (p == end)
            break;
        if (values.size() >= maximumValues)
            return false;
        char* next = nullptr;
        const double value = std::strtod(p, &next);
        if (next == p || next > end || !std::isfinite(value))
            return false;
        values.push_back(value);
        p = next;
    }
    return true;
}
bool loadDae(const std::filesystem::path& path, Mesh& mesh) {
    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(path, sizeError);
    constexpr uintmax_t maximumModelFileBytes = 32ull * 1024ull * 1024ull;
    if (!sizeError && fileSize > maximumModelFileBytes) {
        mesh.detail = "COLLADA mesh exceeds the 32 MiB viewer safety limit";
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        mesh.detail = "COLLADA mesh file could not be opened";
        return false;
    }
    const std::string xml((std::istreambuf_iterator<char>(file)), {});
    if (xml.find("<COLLADA") == std::string::npos) {
        mesh.detail = "Unrecognized COLLADA document";
        return false;
    }

    constexpr size_t maximumVertices = 500000;
    constexpr size_t maximumIndices = 1500000;
    const std::regex geometryPattern(R"(<geometry\b([^>]*)>([\s\S]*?)</geometry\s*>)");
    const std::regex sourcePattern(R"(<source\b([^>]*)>([\s\S]*?)</source\s*>)");
    const std::regex verticesPattern(R"(<vertices\b([^>]*)>([\s\S]*?)</vertices\s*>)");
    const std::regex inputPattern(R"(<input\b([^>]*)/?>)");
    const std::regex primitivePattern(R"(<(triangles|polylist)\b([^>]*)>([\s\S]*?)</\1\s*>)");
    const std::regex pPattern(R"(<p\b[^>]*>([\s\S]*?)</p\s*>)");
    const std::regex vcountPattern(R"(<vcount\b[^>]*>([\s\S]*?)</vcount\s*>)");
    std::string diagnostic;
    for (std::sregex_iterator geometryIt(xml.begin(), xml.end(), geometryPattern), end;
         geometryIt != end;
         ++geometryIt) {
        const std::string geometry = (*geometryIt)[2].str();
        std::unordered_map<std::string, std::vector<double>> sources;
        for (std::sregex_iterator sourceIt(geometry.begin(), geometry.end(), sourcePattern);
             sourceIt != end;
             ++sourceIt) {
            const std::string id = xmlAttribute((*sourceIt)[1].str(), "id");
            if (id.empty())
                continue;
            const std::string floats = xmlElementText((*sourceIt)[2].str(), "float_array");
            if (floats.empty())
                continue;
            std::vector<double> values;
            if (parseXmlNumbers(floats, values, maximumVertices * 3) && values.size() >= 2)
                sources.emplace(id, std::move(values));
        }

        std::unordered_map<std::string, std::string> vertexPositions;
        for (std::sregex_iterator verticesIt(geometry.begin(), geometry.end(), verticesPattern);
             verticesIt != end;
             ++verticesIt) {
            const std::string verticesId = xmlAttribute((*verticesIt)[1].str(), "id");
            if (verticesId.empty())
                continue;
            const std::string inputs = (*verticesIt)[2].str();
            for (std::sregex_iterator inputIt(inputs.begin(), inputs.end(), inputPattern);
                 inputIt != end;
                 ++inputIt) {
                if (xmlAttribute((*inputIt)[1].str(), "semantic") != "POSITION")
                    continue;
                std::string sourceId = xmlAttribute((*inputIt)[1].str(), "source");
                if (!sourceId.empty() && sourceId.front() == '#')
                    sourceId.erase(sourceId.begin());
                vertexPositions[verticesId] = std::move(sourceId);
                break;
            }
        }

        for (std::sregex_iterator primitiveIt(geometry.begin(), geometry.end(), primitivePattern);
             primitiveIt != end;
             ++primitiveIt) {
            const std::string kind = (*primitiveIt)[1].str();
            const std::string primitive = (*primitiveIt)[3].str();
            const std::string inputs = primitive;
            int tupleStride = 0, positionOffset = -1, uvOffset = -1;
            std::string positionSource, uvSource;
            for (std::sregex_iterator inputIt(inputs.begin(), inputs.end(), inputPattern);
                 inputIt != end;
                 ++inputIt) {
                const std::string inputAttributes = (*inputIt)[1].str();
                const std::string semantic = xmlAttribute(inputAttributes, "semantic");
                const std::string offsetText = xmlAttribute(inputAttributes, "offset");
                int offset = 0;
                if (!offsetText.empty()) {
                    try {
                        offset = std::stoi(offsetText);
                    } catch (...) {
                        offset = -1;
                    }
                }
                if (offset < 0 || offset >= 32)
                    continue;
                tupleStride = std::max(tupleStride, offset + 1);
                if (semantic == "TEXCOORD" &&
                    (uvOffset < 0 || xmlAttribute(inputAttributes, "set") == "0")) {
                    std::string sourceId = xmlAttribute(inputAttributes, "source");
                    if (!sourceId.empty() && sourceId.front() == '#')
                        sourceId.erase(sourceId.begin());
                    if (sources.contains(sourceId)) {
                        uvOffset = offset;
                        uvSource = std::move(sourceId);
                    }
                    continue;
                }
                if (semantic != "VERTEX" && semantic != "POSITION")
                    continue;
                std::string sourceId = xmlAttribute(inputAttributes, "source");
                if (!sourceId.empty() && sourceId.front() == '#')
                    sourceId.erase(sourceId.begin());
                if (semantic == "VERTEX") {
                    const auto vertexSource = vertexPositions.find(sourceId);
                    if (vertexSource == vertexPositions.end())
                        continue;
                    sourceId = vertexSource->second;
                }
                if (positionOffset < 0 && sources.contains(sourceId) &&
                    sources[sourceId].size() % 3 == 0) {
                    positionOffset = offset;
                    positionSource = std::move(sourceId);
                }
            }
            if (tupleStride <= 0 || positionOffset < 0) {
                diagnostic = "COLLADA primitive has no supported position input";
                continue;
            }
            const auto positionIt = sources.find(positionSource);
            if (positionIt == sources.end())
                continue;
            const auto uvIt = uvSource.empty() ? sources.end() : sources.find(uvSource);
            std::unordered_map<uint64_t, uint32_t> primitiveVertices;
            for (std::sregex_iterator pIt(primitive.begin(), primitive.end(), pPattern); pIt != end;
                 ++pIt) {
                std::vector<double> rawIndices;
                if (!parseXmlNumbers((*pIt)[1].str(), rawIndices, maximumIndices * 4ull)) {
                    diagnostic = "COLLADA primitive has invalid index data";
                    continue;
                }
                if (rawIndices.size() % (size_t)tupleStride != 0) {
                    diagnostic = "COLLADA primitive index tuple is truncated";
                    continue;
                }
                std::vector<uint32_t> primitiveIndices;
                primitiveIndices.reserve(rawIndices.size() / (size_t)tupleStride);
                bool valid = true;
                for (size_t tuple = 0; tuple < rawIndices.size() / (size_t)tupleStride; ++tuple) {
                    const size_t at = tuple * (size_t)tupleStride;
                    const double positionValue = rawIndices[at + (size_t)positionOffset];
                    if (positionValue < 0 || positionValue >= positionIt->second.size() / 3 ||
                        std::floor(positionValue) != positionValue) {
                        valid = false;
                        break;
                    }
                    const uint32_t positionIndex = (uint32_t)positionValue;
                    uint32_t uvIndex = 0;
                    if (uvOffset >= 0 && uvIt != sources.end()) {
                        const double uvValue = rawIndices[at + (size_t)uvOffset];
                        if (uvValue < 0 || uvValue >= uvIt->second.size() / 2 ||
                            std::floor(uvValue) != uvValue) {
                            valid = false;
                            break;
                        }
                        uvIndex = (uint32_t)uvValue;
                    }
                    const uint64_t key = ((uint64_t)positionIndex << 32) | uvIndex;
                    auto vertex = primitiveVertices.find(key);
                    if (vertex == primitiveVertices.end()) {
                        if (mesh.vertices.size() >= maximumVertices) {
                            mesh.detail = "COLLADA mesh exceeds the 500,000 vertex viewer limit";
                            return false;
                        }
                        const size_t p = (size_t)positionIndex * 3;
                        const uint32_t newIndex = (uint32_t)mesh.vertices.size();
                        mesh.vertices.push_back({(float)positionIt->second[p],
                                                 (float)positionIt->second[p + 1],
                                                 (float)positionIt->second[p + 2]});
                        std::array<float, 2> uv{};
                        if (uvOffset >= 0 && uvIt != sources.end()) {
                            const size_t u = (size_t)uvIndex * 2;
                            uv = {(float)uvIt->second[u], 1.f - (float)uvIt->second[u + 1]};
                        }
                        mesh.uvs.push_back(uv);
                        vertex = primitiveVertices.emplace(key, newIndex).first;
                    }
                    primitiveIndices.push_back(vertex->second);
                }
                if (!valid) {
                    diagnostic = "COLLADA primitive references an invalid position";
                    continue;
                }
                if (kind == "triangles") {
                    if (primitiveIndices.size() % 3 != 0) {
                        diagnostic = "COLLADA triangle list is incomplete";
                        continue;
                    }
                    if (primitiveIndices.size() > maximumIndices - mesh.indices.size()) {
                        mesh.detail = "COLLADA mesh exceeds the 1,500,000 index viewer limit";
                        return false;
                    }
                    mesh.indices.insert(
                        mesh.indices.end(), primitiveIndices.begin(), primitiveIndices.end());
                } else {
                    std::smatch vcountMatch;
                    if (!std::regex_search(primitive, vcountMatch, vcountPattern)) {
                        diagnostic = "COLLADA polygon list has no vertex counts";
                        continue;
                    }
                    std::vector<double> counts;
                    if (!parseXmlNumbers(vcountMatch[1].str(), counts, maximumIndices)) {
                        diagnostic = "COLLADA polygon list has invalid vertex counts";
                        continue;
                    }
                    size_t at = 0;
                    for (double countValue : counts) {
                        if (countValue < 3 || countValue > primitiveIndices.size() - at ||
                            std::floor(countValue) != countValue) {
                            valid = false;
                            break;
                        }
                        const size_t count = (size_t)countValue;
                        if ((count - 2) * 3 > maximumIndices - mesh.indices.size()) {
                            mesh.detail = "COLLADA mesh exceeds the 1,500,000 index viewer limit";
                            return false;
                        }
                        for (size_t k = 1; k + 1 < count; k++)
                            mesh.indices.insert(mesh.indices.end(),
                                                {primitiveIndices[at],
                                                 primitiveIndices[at + k],
                                                 primitiveIndices[at + k + 1]});
                        at += count;
                    }
                    if (!valid || at != primitiveIndices.size())
                        diagnostic = "COLLADA polygon counts do not match its indices";
                }
            }
        }
    }
    mesh.loaded = !mesh.vertices.empty() && !mesh.indices.empty();
    if (!mesh.loaded)
        mesh.detail =
            diagnostic.empty() ? "COLLADA document has no supported triangle geometry" : diagnostic;
    return mesh.loaded;
}
// Scrap Mechanic still ships many parts in Ogre's binary v1.8 mesh format.
// Its mesh/submesh headers use one-byte chunk IDs; vertex geometry retains
// Ogre's standard two-byte IDs.
} // namespace BlueprintViewport::Internal
