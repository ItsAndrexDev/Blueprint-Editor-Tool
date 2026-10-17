#include "blueprint_viewport_internal.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BlueprintViewport::Internal {
V3 axis(int v) {
    switch (v) {
    case 1:
        return {1, 0, 0};
    case -1:
        return {-1, 0, 0};
    case 2:
        return {0, 1, 0};
    case -2:
        return {0, -1, 0};
    case 3:
        return {0, 0, 1};
    case -3:
        return {0, 0, -1};
    default:
        return {1, 0, 0};
    }
}
V3 orient(V3 v, const Parser::Block& b) {
    V3 x = axis(b.xaxis), z = axis(b.zaxis), y = norm(cross(z, x));
    return x * v.x + y * v.y + z * v.z;
}
V3 shapeSize(const Parser::Block& b) {
    const std::string id = lower(b.shapeID);
    if (b.hasBounds || state.blockShapes.contains(id))
        return {(float)std::max(1, b.bounds.x),
                (float)std::max(1, b.bounds.y),
                (float)std::max(1, b.bounds.z)};
    const auto found = state.shapeSizes.find(id);
    if (found != state.shapeSizes.end())
        return found->second;
    return {(float)std::max(1, b.bounds.x),
            (float)std::max(1, b.bounds.y),
            (float)std::max(1, b.bounds.z)};
}
size_t gridCellCount(const std::vector<Parser::Block>& blocks) {
    size_t count = 0;
    for (const auto& block : blocks) {
        const V3 size = shapeSize(block);
        count += (size_t)std::max(1, (int)std::lround(size.x)) *
                 (size_t)std::max(1, (int)std::lround(size.y)) *
                 (size_t)std::max(1, (int)std::lround(size.z));
    }
    return count;
}
int rotateAxisAroundZ(int axisCode, int quarterTurns) {
    if (quarterTurns < 0) {
        switch (axisCode) {
        case 1:
            return -2;
        case -2:
            return -1;
        case -1:
            return 2;
        case 2:
            return 1;
        case 3:
            return 3;
        case -3:
            return -3;
        default:
            return axisCode;
        }
    }
    switch (axisCode) {
    case 1:
        return 2;
    case 2:
        return -1;
    case -1:
        return -2;
    case -2:
        return 1;
    case 3:
        return 3;
    case -3:
        return -3;
    default:
        return axisCode;
    }
}
void setOrientation(Parser::Block& block, int xaxis, int zaxis) {
    // Blueprint positions are the part's local grid anchor. Rotate only its
    // orientation so the saved transform matches Scrap Mechanic's placement.
    block.xaxis = xaxis;
    block.zaxis = zaxis;
}
bool colorValue(std::string s, int& out) {
    if (!s.empty() && s[0] == '#')
        s.erase(0, 1);
    if (s.size() != 6)
        return false;
    try {
        size_t n;
        unsigned long x = std::stoul(s, &n, 16);
        if (n != 6)
            return false;
        out = (int)x;
        return true;
    } catch (...) {
        return false;
    }
}
std::string displayNameFromShapeSet(const std::string& source) {
    std::string name = lower(source);
    auto stripPrefix = [&](std::string_view prefix) {
        if (name.rfind(prefix, 0) == 0)
            name.erase(0, prefix.size());
    };
    stripPrefix("obj_");
    stripPrefix("jnt_");
    for (const std::string_view prefix : {"interactive_", "survivalobject_", "scrap_"})
        stripPrefix(prefix);
    for (const std::string_view suffix : {"_creative", "_challenge", "_warehouse", "_scrap"})
        if (name.size() > suffix.size() && name.ends_with(suffix))
            name.resize(name.size() - suffix.size());
    if (name.size() > 3 && name[name.size() - 3] == '_' &&
        std::isdigit((unsigned char)name[name.size() - 2]) &&
        std::isdigit((unsigned char)name[name.size() - 1]))
        name.resize(name.size() - 3);
    static const std::pair<std::string_view, std::string_view> compounds[] = {
        {"driverseat", "Driver's Seat"},
        {"driversaddle", "Driver's Saddle"},
        {"gasengine", "Gas Engine"},
        {"electricengine", "Electric Engine"}};
    for (const auto& [key, title] : compounds)
        if (name == key)
            return std::string(title);
    std::string title;
    bool capitalize = true;
    for (unsigned char character : name) {
        if (character == '_' || character == '-') {
            title.push_back(' ');
            capitalize = true;
        } else {
            title.push_back(capitalize ? (char)std::toupper(character) : (char)character);
            capitalize = false;
        }
    }
    return title;
}
int displayColor(const Parser::Block& block) {
    int color = 0x8c9bad;
    if (block.hasColor && colorValue(block.color, color))
        return color;
    const auto defaultColor = state.shapeDefaultColors.find(lower(block.shapeID));
    if (defaultColor != state.shapeDefaultColors.end() && colorValue(defaultColor->second, color))
        return color;
    return 0x8c9bad;
}

} // namespace BlueprintViewport::Internal
