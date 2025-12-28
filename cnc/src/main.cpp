#include <iostream>
#include <vector>
#include "JsonImporter.h"

int main() {
    std::string filePath = "cnc/examples/simple_example_schedule.json"; // Path to test json file
    try {
        std::vector<CncNode_t> nodes = JsonImporter::importFromFile(filePath);

        size_t nodeCount = nodes.size();

        std::cout << "Imported " << nodeCount << " nodes from " << filePath << ":\n";
    } catch (const std::exception& e) {
        std::cerr << "Error importing nodes: " << e.what() << std::endl;
        return -1;
    }

    return 0;
}

