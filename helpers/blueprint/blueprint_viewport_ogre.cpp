#include "blueprint_viewport_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace BlueprintViewport::Internal {
bool loadOgreMesh(const std::filesystem::path& path, Mesh& mesh) {
    std::error_code sizeError;
    const auto fileSize = std::filesystem::file_size(path, sizeError);
    constexpr uintmax_t maximumModelFileBytes = 32ull * 1024ull * 1024ull;
    if (!sizeError && fileSize > maximumModelFileBytes) {
        mesh.detail = "Ogre mesh exceeds the 32 MiB viewer safety limit";
        return false;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        mesh.detail = "Mesh file could not be opened";
        return false;
    }
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
    const char sig[] = "[MeshSerializer_v1.8]";
    if (b.size() < 32 || std::memcmp(b.data() + 2, sig, sizeof(sig) - 1) != 0) {
        mesh.detail = "Unrecognized Ogre mesh header";
        return false;
    }
    auto u16 = [&](size_t p, uint16_t& v) {
        if (p + 2 > b.size())
            return false;
        v = (uint16_t)b[p] | ((uint16_t)b[p + 1] << 8);
        return true;
    };
    auto u32 = [&](size_t p, uint32_t& v) {
        if (p + 4 > b.size())
            return false;
        v = (uint32_t)b[p] | ((uint32_t)b[p + 1] << 8) | ((uint32_t)b[p + 2] << 16) |
            ((uint32_t)b[p + 3] << 24);
        return true;
    };
    size_t p = 25;
    uint32_t rootLen = 0;
    if (p >= b.size() || b[p++] != 0x30 || !u32(p, rootLen)) {
        mesh.detail = "Invalid Ogre mesh root";
        return false;
    }
    p += 4;
    const size_t rootEnd = std::min(b.size(), p + (size_t)rootLen);
    // Scrap Mechanic's v1.8 writer includes a 16-bit stream header before
    // its compact one-byte submesh chunk IDs.
    if (p + 2 <= rootEnd)
        p += 2;
    size_t guard = 0;
    while (p + 5 <= rootEnd && guard++ < 10000) {
        uint8_t id = b[p++];
        if (id != 0x40)
            break;
        uint32_t len;
        if (!u32(p, len)) {
            break;
        }
        p += 4;
        size_t end = std::min(b.size(), p + (size_t)len);
        if (end <= p)
            break;
        if (id == 0x40) { // Submesh: material C-string, index count, indices, then
                          // geometry.
            size_t q = p;
            while (q < end && b[q])
                q++;
            if (q < end)
                q++;
            const uint32_t vertexBase = static_cast<uint32_t>(mesh.vertices.size());
            uint32_t count = 0;
            if (q + 4 <= end && u32(q, count)) {
                q += 4;
                if (count <= 3000000 && q + (size_t)count * 2 <= end) {
                    for (uint32_t i = 0; i < count; i++) {
                        const size_t at = q + (size_t)i * 2;
                        mesh.indices.push_back(vertexBase + (((uint32_t)b[at] << 8) | b[at + 1]));
                    }
                    q += (size_t)count * 2;
                }
                // The game writer inserts a one-byte submesh flag before the geometry
                // chunk.
                if (q + 2 < end && b[q + 1] == 0x00 && b[q + 2] == 0x50)
                    q++;
                // Geometry records contain the vertex count, declaration and one or
                // more buffers.
                while (q + 6 <= b.size()) {
                    uint16_t cid;
                    uint32_t clen;
                    if (!u16(q, cid) || !u32(q + 2, clen))
                        break;
                    size_t ce = std::min(b.size(), q + 6 + (size_t)clen);
                    if (ce <= q + 6)
                        break;
                    if (cid == 0x5000 && q + 11 <= ce) {
                        uint32_t vc;
                        u32(q + 6, vc);
                        if (vc > 0 && vc < 500000) {
                            size_t g = q + 11;
                            std::vector<std::array<uint16_t, 5>> elems;
                            struct VB {
                                uint16_t bind{}, stride{};
                                size_t data{}, end{};
                            };
                            std::vector<VB> buffers;
                            while (g + 5 <= ce) {
                                uint8_t gid = b[g];
                                uint32_t gl;
                                if (!u32(g + 1, gl))
                                    break;
                                size_t ge = std::min(ce, g + 5 + (size_t)gl);
                                if (ge <= g + 5)
                                    break;
                                if (gid == 0x51) {
                                    for (size_t e = g + 5; e + 6 <= ge;) {
                                        uint16_t eid;
                                        uint32_t el;
                                        if (!u16(e, eid) || !u32(e + 2, el) ||
                                            e + 6 + (size_t)el > ge)
                                            break;
                                        if (eid == 0x5110 && el >= 10) {
                                            std::array<uint16_t, 5> a{};
                                            for (int k = 0; k < 5; k++)
                                                u16(e + 6 + k * 2, a[k]);
                                            elems.push_back(a);
                                        }
                                        // Scrap Mechanic's compact Ogre writer stores the chunk
                                        // size including the 6-byte element header (16 bytes for
                                        // the five-short declaration), unlike standard Ogre's
                                        // payload-only size. The old walk skipped into the next
                                        // declaration after reading only the position element.
                                        // Accept either convention while staying inside the
                                        // declaration chunk.
                                        const size_t compactEnd = e + (size_t)el;
                                        const bool sizeIncludesHeader =
                                            el == 16 && compactEnd <= ge;
                                        e = sizeIncludesHeader ? compactEnd : e + 6 + (size_t)el;
                                    }
                                } else if (gid == 0x52 && g + 15 <= ge) {
                                    uint16_t did;
                                    uint32_t dl;
                                    if (u16(g + 9, did) && u32(g + 11, dl) && did == 0x5210 &&
                                        g + 15 + (size_t)dl <= ge) {
                                        VB v;
                                        u16(g + 5, v.bind);
                                        u16(g + 7, v.stride);
                                        v.data = g + 15;
                                        v.end = g + 15 + (size_t)dl;
                                        buffers.push_back(v);
                                    }
                                }
                                g = ge;
                            }
                            // The game pads some vertex declarations; locate the following
                            // buffer record within this bounded geometry chunk as a fallback.
                            if (buffers.empty())
                                for (size_t t = q + 10; t + 15 <= ce; t++)
                                    if (b[t] == 0x52) {
                                        uint32_t bl, dl;
                                        uint16_t did;
                                        VB v;
                                        if (u32(t + 1, bl) && t + 5 + (size_t)bl <= ce &&
                                            u16(t + 9, did) && u32(t + 11, dl) && did == 0x5210) {
                                            u16(t + 5, v.bind);
                                            u16(t + 7, v.stride);
                                            v.data = t + 15;
                                            v.end = std::min(ce, v.data + (size_t)dl);
                                            if (v.stride >= 12 &&
                                                v.data + (size_t)vc * v.stride <= v.end) {
                                                buffers.push_back(v);
                                                break;
                                            }
                                        }
                                    }
                            auto posElem =
                                std::find_if(elems.begin(), elems.end(), [](const auto& e) {
                                    return e[2] == 1;
                                });
                            if (posElem != elems.end()) {
                                uint16_t source = (*posElem)[0], type = (*posElem)[1],
                                         off = (*posElem)[3];
                                auto vb =
                                    std::find_if(buffers.begin(), buffers.end(), [&](const VB& v) {
                                        return v.bind == source;
                                    });
                                if (vb != buffers.end() && type == 2 && vb->stride >= off + 12 &&
                                    vb->data + (size_t)vc * vb->stride <= vb->end) {
                                    // Ogre vertex declarations store texture coordinates as
                                    // VES_TEXCOORD (semantic 7), usually VET_FLOAT2 (type 1),
                                    // in a buffer that may differ from the position buffer.
                                    // The old loader discarded these values, leaving every
                                    // textured triangle to sample UV (0, 0), which made large
                                    // areas of pipes and other .mesh parts appear black.
                                    const auto uvElem =
                                        std::find_if(elems.begin(), elems.end(), [](const auto& e) {
                                            return e[2] == 7 && e[4] == 0;
                                        });
                                    const VB* uvBuffer = nullptr;
                                    uint16_t uvOffset = 0;
                                    if (uvElem != elems.end()) {
                                        uvOffset = (*uvElem)[3];
                                        const auto foundUvBuffer = std::find_if(
                                            buffers.begin(), buffers.end(), [&](const VB& v) {
                                                return v.bind == (*uvElem)[0];
                                            });
                                        if (foundUvBuffer != buffers.end() &&
                                            foundUvBuffer->stride >= uvOffset + 8 &&
                                            foundUvBuffer->data +
                                                    (size_t)vc * foundUvBuffer->stride <=
                                                foundUvBuffer->end)
                                            uvBuffer = &*foundUvBuffer;
                                    }
                                    for (uint32_t i = 0; i < vc; i++) {
                                        float xyz[3];
                                        std::memcpy(xyz,
                                                    b.data() + vb->data + (size_t)i * vb->stride +
                                                        off,
                                                    12);
                                        mesh.vertices.push_back({xyz[0], xyz[1], xyz[2]});
                                        std::array<float, 2> uv{};
                                        if (uvBuffer) {
                                            float coordinates[2];
                                            std::memcpy(coordinates,
                                                        b.data() + uvBuffer->data +
                                                            (size_t)i * uvBuffer->stride + uvOffset,
                                                        sizeof(coordinates));
                                            uv = {coordinates[0], 1.f - coordinates[1]};
                                        }
                                        mesh.uvs.push_back(uv);
                                    }
                                }
                            }
                        }
                    }
                    q = ce;
                }
            }
        }
        p = end;
    }
    // Some builds store 32-bit indices; reject out-of-range data instead of
    // drawing corrupt triangles.
    mesh.indices.erase(std::remove_if(mesh.indices.begin(),
                                      mesh.indices.end(),
                                      [&](uint32_t i) {
                                          return i >= mesh.vertices.size();
                                      }),
                       mesh.indices.end());
    mesh.indices.resize(mesh.indices.size() / 3 * 3);
    mesh.loaded = !mesh.vertices.empty() && !mesh.indices.empty();
    if (!mesh.loaded)
        mesh.detail = "Ogre mesh has no supported position vertex buffer and "
                      "triangle indices";
    return mesh.loaded;
}
} // namespace BlueprintViewport::Internal
