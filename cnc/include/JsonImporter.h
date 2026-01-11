#pragma once
#include <vector>
#include <string>
#include "CncTypes.h"
#include "Topology.h"

namespace cnc {
    class JsonImporter {
    public:
        /**
         * @brief Imports CNC node configurations from a JSON file.
         * @param filePath The path to the JSON file.
         * @return True if the import was successful, false otherwise.
         */
        static bool importFromFile(const std::string& filePath, Topology& topology);
    };
}