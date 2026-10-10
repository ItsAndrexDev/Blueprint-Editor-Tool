#pragma once

#include "scrap_parser.hpp"
#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace BlueprintViewport {
struct Vec3 { float x{}, y{}, z{}; };

// Authored model transform recovered from Scrap Mechanic's placement rules.
// `translation` is applied after scaling mesh-local coordinates; `basis` maps
// those local coordinates to blueprint world axes.
struct PartTransform {
	Vec3 translation;
	std::array<Vec3, 3> basis;
};

inline PartTransform placementTransform(const Parser::Block& part, Vec3 footprint) noexcept {
	const auto axisVector = [](int code) -> Vec3 {
		switch (code) {
		case 1: return { 1, 0, 0 }; case -1: return { -1, 0, 0 };
		case 2: return { 0, 1, 0 }; case -2: return { 0, -1, 0 };
		case 3: return { 0, 0, 1 }; case -3: return { 0, 0, -1 };
		default: return { 1, 0, 0 };
		}
	};
	const Vec3 x = axisVector(part.xaxis), z = axisVector(part.zaxis);
	const Vec3 y{ z.y * x.z - z.z * x.y, z.z * x.x - z.x * x.z, z.x * x.y - z.y * x.x };
	Vec3 offset{};
	if (part.isJoint) {
		offset = { x.x + y.x + z.x < 0.f ? 1.f : 0.f,
			x.y + y.y + z.y < 0.f ? 1.f : 0.f,
			x.z + y.z + z.z < 0.f ? 1.f : 0.f };
	}
	const Vec3 half{ footprint.x * .5f, footprint.y * .5f, footprint.z * .5f };
	const Vec3 translation{
		part.pos.x + offset.x + x.x * half.x + y.x * half.y + z.x * half.z,
		part.pos.y + offset.y + x.y * half.x + y.y * half.y + z.y * half.z,
		part.pos.z + offset.z + x.z * half.x + y.z * half.y + z.z * half.z
	};
	return { translation, { x, y, z } };
}

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
