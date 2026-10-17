#include "scrap_parser.hpp"

#include <string>

std::string Parser::findBlockNameByShapeID(const std::string& shapeID, const json& items) {
    if (items.is_object() && items.contains(shapeID) && items[shapeID].is_object() &&
        items[shapeID].contains("title") && items[shapeID]["title"].is_string())
        return items[shapeID]["title"].get<std::string>();
    return {};
}

std::string Parser::findShapeIDByBlockName(const std::string& name, const json& items) {
    if (!items.is_object())
        return {};
    for (const auto& [uuid, data] : items.items())
        if (data.is_object() && data.contains("title") && data["title"].is_string() &&
            data["title"].get<std::string>() == name)
            return uuid;
    return {};
}
