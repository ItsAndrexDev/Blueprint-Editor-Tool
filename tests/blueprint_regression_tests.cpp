#include "../helpers/blueprint_viewport.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
void near(float actual, float expected) { assert(std::abs(actual - expected) < 1e-5f); }

json sampleBlueprint() {
	return json::parse(R"JSON({
		"version":3,
		"bodies":[
			{"childs":[{"color":"4c6fe3","joints":[{"id":466},{"id":468}],"pos":{"x":0,"y":9,"z":4},"shapeId":"8024db09-9147-4fe3-a747-00bc30ab724b","xaxis":-2,"zaxis":-1}]},
			{"childs":[{"color":"4c6fe3","joints":[{"id":466},{"id":467}],"pos":{"x":-1,"y":5,"z":4},"shapeId":"8024db09-9147-4fe3-a747-00bc30ab724b","xaxis":2,"zaxis":1}]},
			{"childs":[{"bounds":{"x":1,"y":4,"z":1},"color":"820a0a","joints":[{"id":467},{"id":468}],"pos":{"x":-1,"y":5,"z":3},"shapeId":"027bd4ec-b16d-47d2-8756-e18dc2af3eb6","xaxis":1,"zaxis":3}]}
		],
		"joints":[
			{"childA":0,"childB":1,"color":"4c6fe3","controller":{"controllers":null,"id":3761,"joints":null,"stiffnessLevel":2},"id":466,"posA":{"x":-1,"y":7,"z":4},"posB":{"x":-1,"y":5,"z":4},"shapeId":"aa8d89eb-919b-42f4-8b58-af6f0d5856bc","xaxisA":-1,"xaxisB":-1,"zaxisA":-2,"zaxisB":-2},
			{"childA":2,"childB":1,"color":"df7f01","id":467,"posA":{"x":-1,"y":5,"z":4},"posB":{"x":-1,"y":5,"z":4},"shapeId":"4a1b886b-913e-4aad-b5b6-6e41b0db23a6","xaxisA":1,"xaxisB":1,"zaxisA":3,"zaxisB":3},
			{"childA":2,"childB":0,"color":"df7f01","id":468,"posA":{"x":-1,"y":8,"z":4},"posB":{"x":-1,"y":8,"z":4},"shapeId":"4a1b886b-913e-4aad-b5b6-6e41b0db23a6","xaxisA":1,"xaxisB":1,"zaxisA":3,"zaxisB":3}
		]
	})JSON");
}

void testPlacementRules() {
	Parser::Block child;
	child.pos = { 3, 4, 5 }; child.xaxis = 1; child.zaxis = 3;
	auto placement = BlueprintViewport::placementTransform(child, { 2, 4, 6 });
	near(placement.translation.x, 4); near(placement.translation.y, 6); near(placement.translation.z, 8);

	Parser::Block joint;
	joint.isJoint = true; joint.pos = { 10, 20, 30 }; joint.jointPosB = { -90, -80, -70 };
	joint.xaxis = 1; joint.zaxis = 3;
	placement = BlueprintViewport::placementTransform(joint, { 1, 1, 1 });
	near(placement.translation.x, 10.5f); near(placement.translation.y, 20.5f); near(placement.translation.z, 30.5f);

	joint.xaxis = -1; joint.zaxis = -2;
	placement = BlueprintViewport::placementTransform(joint, { 1, 1, 1 });
	near(placement.translation.x, 10.5f); near(placement.translation.y, 20.5f); near(placement.translation.z, 30.5f);
	near(placement.basis[0].x, -1); near(placement.basis[1].z, -1); near(placement.basis[2].y, -1);

	for (int x : { -3, -2, -1, 1, 2, 3 }) for (int z : { -3, -2, -1, 1, 2, 3 }) {
		if (std::abs(x) == std::abs(z)) continue;
		joint.xaxis = x; joint.zaxis = z;
		placement = BlueprintViewport::placementTransform(joint, { 2, 4, 6 });
		const auto& a = placement.basis[0]; const auto& b = placement.basis[1]; const auto& c = placement.basis[2];
		near(a.x * a.x + a.y * a.y + a.z * a.z, 1); near(b.x * b.x + b.y * b.y + b.z * b.z, 1); near(c.x * c.x + c.y * c.y + c.z * c.z, 1);
		near(a.x * b.x + a.y * b.y + a.z * b.z, 0); near(a.x * c.x + a.y * c.y + a.z * c.z, 0); near(b.x * c.x + b.y * c.y + b.z * c.z, 0);
	}
}

void testParserAndRoundTrip() {
	auto blueprint = sampleBlueprint();
	auto blocks = Parser::parseBlueprint(blueprint);
	assert(blocks.size() == 6);
	assert(!blocks[0].isJoint && blocks[2].hasBounds && blocks[2].bounds.y == 4);
	assert(blocks[3].isJoint && blocks[3].jointPosA.z == 4 && blocks[3].jointPosB.z == 4);
	auto unchanged = blueprint;
	Parser::applyBlockListToNode(blocks, blueprint);
	assert(blueprint == unchanged);

	blocks[0].pos.x += 2;
	Parser::applyBlockListToNode(blocks, blueprint);
	assert(blueprint["bodies"][0]["childs"][0]["pos"]["x"] == 2);
	assert(blueprint["joints"][0]["posA"]["x"] == 1);
	assert(blueprint["joints"][2]["posB"]["x"] == 1);
	assert(blueprint["joints"][0]["controller"]["id"] == 3761);

	auto rotated = sampleBlueprint();
	auto rotatedBlocks = Parser::parseBlueprint(rotated);
	rotatedBlocks[0].xaxis = 1; rotatedBlocks[0].zaxis = 3;
	Parser::applyBlockListToNode(rotatedBlocks, rotated);
	assert(rotated["joints"][0]["posA"] == json({ {"x", 2}, {"y", 9}, {"z", 5} }));
	assert(rotated["joints"][2]["posB"] == json({ {"x", 1}, {"y", 9}, {"z", 5} }));
	assert(rotated["joints"][0]["controller"]["id"] == 3761);

	const auto savePath = std::filesystem::temp_directory_path() /
		("bpeditor-parser-regression-" + std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + ".json");
	assert(Parser::saveBlueprint(savePath.string(), blueprint));
	json saved;
	{ std::ifstream input(savePath, std::ios::binary); input >> saved; }
	assert(saved == blueprint);
	std::filesystem::remove(savePath);

	auto removable = sampleBlueprint();
	assert(Parser::removeBlockFromNode(removable, 0, 0));
	assert(removable["bodies"][0]["childs"].empty());
	assert(removable["joints"].size() == 1);
	assert(removable["joints"][0]["childA"] == 1 && removable["joints"][0]["childB"] == 0);

	auto jointRemovable = sampleBlueprint();
	assert(Parser::removeJointFromNode(jointRemovable, 1));
	assert(jointRemovable["joints"].size() == 2);
	for (const auto& body : jointRemovable["bodies"]) for (const auto& child : body["childs"]) {
		if (!child.contains("joints")) continue;
		for (const auto& ref : child["joints"]) assert(ref["id"] != 467);
	}
}
}

int main(int argc, char** argv) {
	testPlacementRules();
	testParserAndRoundTrip();
	if (argc > 1) {
		std::size_t checked = 0;
		std::error_code error;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[1], error)) {
			if (error) { std::cerr << "Could not enumerate blueprint folder: " << error.message() << '\n'; return 2; }
			if (!entry.is_regular_file() || entry.path().filename() != "blueprint.json") continue;
			try {
				std::ifstream input(entry.path(), std::ios::binary);
				json source; input >> source;
				auto view = source;
				const auto blocks = Parser::parseBlueprint(source);
				Parser::applyBlockListToNode(blocks, view);
				if (source != view) { std::cerr << "No-op parse/save changed " << entry.path() << '\n' << json::diff(source, view).dump(2) << '\n'; return 3; }
				++checked;
			} catch (const std::exception& exception) {
				std::cerr << "Failed to parse " << entry.path() << ": " << exception.what() << '\n'; return 4;
			}
		}
		std::cout << "Validated no-op parse/save for " << checked << " installed blueprints.\n";
	}
	std::cout << "All viewport transform and parser regression tests passed.\n";
	return 0;
}
