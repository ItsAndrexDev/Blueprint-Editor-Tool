#pragma once

#include "../blueprint/blueprint_viewport.hpp"
#include "imgui/imgui.h"
#include <glad/glad.h>

#include <string>
#include <vector>

struct BlueprintItem {
    std::string name;
    GLuint textureID{};
    Parser::BlueprintPaths paths;
};

enum ProgramStatus {
    BLUEPRINT_SELECT,
    BLUEPRINT_EDIT
};

void renderBlueprintLoader(std::vector<BlueprintItem>& loadedPrints,
                           ProgramStatus& status,
                           json& outBlueprint,
                           BlueprintItem& itemSelected,
                           std::vector<Parser::Block>& blocks,
                           bool& refreshRequested,
                           ImGuiID& helpDockId);

void renderBlueprintEditor(ProgramStatus& status,
                           json& blueprint,
                           BlueprintItem& itemSelected,
                           std::vector<Parser::Block>& blocks,
                           const json& items);
