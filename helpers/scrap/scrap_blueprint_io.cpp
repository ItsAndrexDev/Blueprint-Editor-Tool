#define NOMINMAX
#include "scrap_parser_internal.hpp"
#include <Windows.h>

#include <fstream>
#include <string>

bool Parser::saveBlueprint(const std::string& path, const json& blueprint) {
    const fs::path target(path);
    fs::path temporary = target;
    temporary += L".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output << blueprint.dump(4);
        output.flush();
        if (!output.good()) {
            output.close();
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return false;
        }
    }
    if (!MoveFileExW(temporary.c_str(),
                     target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    return true;
}
