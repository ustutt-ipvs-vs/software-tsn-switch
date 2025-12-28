#include <iostream>
#include <vector>
#include <filesystem>
#include "JsonImporter.h"

int main() {
    std::filesystem::path projectRoot = CNC_SOURCE_DIR;
    std::filesystem::path jsonPath = projectRoot / "examples" / "simple_example_schedule.json";
    //std::string filePath = "cnc/examples/simple_example_schedule.json"; // Path to test json file
    try {
        std::vector<CncNode_t> nodes = JsonImporter::importFromFile(jsonPath);

        size_t nodeCount = nodes.size();

        std::cout << "Imported " << nodeCount << " nodes from " << jsonPath << ":\n";
    } catch (const std::exception& e) {
        std::cerr << "Error importing nodes: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}

