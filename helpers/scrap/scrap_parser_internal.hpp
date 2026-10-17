#pragma once

#include "scrap_parser.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace Parser::Internal {
inline int integer(const json& object, const char* key, int fallback = 0) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_number_integer())
        return fallback;

    try {
        const auto value = object[key].get<std::int64_t>();
        return static_cast<int>(std::clamp<std::int64_t>(value, -1000000, 1000000));
    } catch (...) {
        return fallback;
    }
}

inline std::int64_t integer64(const json& object, const char* key, std::int64_t fallback = -1) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_number_integer())
        return fallback;

    try {
        return object[key].get<std::int64_t>();
    } catch (...) {
        return fallback;
    }
}

inline Position position(const json& value, Position fallback = {}) {
    return {integer(value, "x", fallback.x),
            integer(value, "y", fallback.y),
            integer(value, "z", fallback.z)};
}

inline int flatChildIndex(const json& blueprint, int bodyIndex, int childIndex) {
    if (!blueprint.contains("bodies") || !blueprint["bodies"].is_array() || bodyIndex < 0 ||
        bodyIndex >= static_cast<int>(blueprint["bodies"].size()))
        return -1;

    int flatIndex = childIndex;
    for (int index = 0; index < bodyIndex; ++index) {
        const auto& body = blueprint["bodies"][index];
        if (body.is_object() && body.contains("childs") && body["childs"].is_array())
            flatIndex += static_cast<int>(body["childs"].size());
    }

    return flatIndex;
}

inline void writePosition(json& object, const std::string& key, const Position& value) {
    if (!object.contains(key) || !object[key].is_object())
        object[key] = json::object();

    object[key]["x"] = value.x;
    object[key]["y"] = value.y;
    object[key]["z"] = value.z;
}
} // namespace Parser::Internal
