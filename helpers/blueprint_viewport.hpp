#pragma once

#include "scrap_parser.hpp"
#include <cstddef>
#include <string>

namespace BlueprintViewport {
struct ModelLoadReport {
    bool loaded{};
    std::size_t vertices{};
    std::size_t triangles{};
    std::string detail;
};
ModelLoadReport inspectModelFile(const std::filesystem::path& path);
ModelLoadReport inspectShape(const std::string& shapeId);
void resetSelection();
void shutdownRenderer();
void render(json& blueprint, const std::filesystem::path& blueprintPath,
            std::vector<Parser::Block>& blocks, const json& items,
            const std::string& blueprintName, bool& backToSelectionRequested);
}
