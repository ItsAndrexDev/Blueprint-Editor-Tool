#include "blueprint_viewport_fbx_binary.hpp"
#include "fbx_inflate.hpp"

#include <cstring>
#include <string>
#include <utility>

namespace BlueprintViewport::Internal {
namespace {
struct BinFbx {
    const std::vector<uint8_t>& b;
    size_t pos = 27;
    bool wide{};
    size_t nodes = 0;
    size_t properties = 0;
    size_t arrayElements = 0;
    std::string error;
    template <class T> bool take(T& v) {
        if (pos + sizeof(T) > b.size())
            return false;
        std::memcpy(&v, b.data() + pos, sizeof(T));
        pos += sizeof(T);
        return true;
    }
    bool bytes(size_t n, const uint8_t*& p) {
        if (n > b.size() - pos)
            return false;
        p = b.data() + pos;
        pos += n;
        return true;
    }
    bool str(std::string& s, size_t n) {
        const uint8_t* p;
        if (!bytes(n, p))
            return false;
        s.assign((const char*)p, n);
        return true;
    }
    bool prop(FProp& p) {
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
        if (!take(count) || count > maximumArrayElements ||
            count > maximumTotalArrayElements - arrayElements) {
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
        const uint8_t* data;
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
    bool node(FNode& out, int depth = 0) {
        auto fail = [&](const std::string& why) {
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
        if (end > b.size() || end < pos || nameLen > end - pos || nprop > 10000 ||
            nprop > 250000 - properties)
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

} // namespace

BinaryFbxParseResult parseBinaryFbx(const std::vector<std::uint8_t>& bytes) {
    BinFbx parser{bytes};
    std::uint32_t version = 0;
    std::memcpy(&version, bytes.data() + 23, sizeof(version));
    parser.wide = version >= 7500;

    BinaryFbxParseResult result;
    while (parser.pos + (parser.wide ? 25 : 13) < bytes.size()) {
        FNode node;
        const std::size_t previousOffset = parser.pos;
        const bool parsed = parser.node(node);
        if (!node.name.empty())
            result.roots.push_back(std::move(node));
        if (!parsed || parser.pos <= previousOffset) {
            result.stoppedEarly = true;
            break;
        }
    }

    result.error = std::move(parser.error);
    result.offset = parser.pos;
    return result;
}
} // namespace BlueprintViewport::Internal
