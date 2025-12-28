#pragma once
#include <vector>
#include <string>
#include "CncTypes.h"

class JsonImporter {
public:
    /**
     * @brief Imports CNC node configurations from a JSON file.
     * @param filePath The path to the JSON file.
     * @return A vector of CncNode_t structures representing the imported nodes.
     */
    static std::vector<CncNode_t> importFromFile(const std::string& filePath);
};