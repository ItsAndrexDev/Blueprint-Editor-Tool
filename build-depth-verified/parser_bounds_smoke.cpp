#include "../helpers/scrap_parser.hpp"
#include <iostream>

int main(int argc, char** argv) {
	if (argc != 2) return 10;
	std::ifstream input(argv[1]);
	json blueprint;
	if (!(input >> blueprint)) return 11;
	auto blocks = Parser::parseBlueprint(blueprint);
	if (blocks.size() != 4 || !blocks[2].hasBounds || blocks[2].bounds.x != 1 ||
		blocks[2].bounds.y != 1 || blocks[2].bounds.z != 1) return 12;
	Parser::applyBlockListToNode(blocks, blueprint);
	const auto& savedBounds = blueprint["bodies"][0]["childs"][2]["bounds"];
	if (!savedBounds.is_object() || savedBounds.value("x", 0) != 1 ||
		savedBounds.value("y", 0) != 1 || savedBounds.value("z", 0) != 1) return 13;
	if (blueprint.dump().find("a092359d-5cea-484d-a274-470d9a567632") != std::string::npos) return 14;
	std::cout << "parser round-trip retained the explicit 1x1x1 bounds; four edited shapes remain\n";
	return 0;
}
