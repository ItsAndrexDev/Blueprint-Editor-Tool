#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BlueprintViewport::Internal {
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

struct BinaryFbxParseResult {
    std::vector<FNode> roots;
    std::string error;
    std::size_t offset{};
    bool stoppedEarly{};
};

BinaryFbxParseResult parseBinaryFbx(const std::vector<std::uint8_t>& bytes);
} // namespace BlueprintViewport::Internal
