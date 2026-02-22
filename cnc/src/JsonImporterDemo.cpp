#include <filesystem>
#include <iostream>

#include "JsonImporter.h"
#include "Topology.h"

using namespace cnc;

int main() {
    std::cout << "--- JSON Importer Test ---" << '\n';

    std::cout << "[DEBUG] Current Working Directory: " << std::filesystem::current_path() << '\n';

    // 1. Create Topology object
    Topology topology;

    // 2. Call importer (adjust filename if needed)
    std::string filename = "../../cnc/examples/simple_example_schedule_v2.json";

    // Note: Path must be relative to the execution directory (build/)
    // If the file is in the root, consider using "../test_config.json"
    // or copy the file to build/.

    if (JsonImporter::importFromFile(filename, topology)) {
        std::cout << "Success! Loaded " << topology.nodes.size() << " nodes." << '\n';

        // 3. Check imported data
        for (const auto& node : topology.nodes) {
            std::cout << "\n[Node] " << node.hostName << '\n';
            std::cout << "  - IP: '" << node.ipAddress << "'" << '\n';

            for (const auto& iface : node.interfaces) {
                std::cout << "  - Interface: '" << iface.name << "'" << '\n';

                if (iface.bridgePort.gateParameterTable.gateEnabled) {
                    std::cout << "    -> GCL Config found (Cycle: "
                              << iface.bridgePort.gateParameterTable.adminCycleTime.numerator << "ns)" << '\n';
                }
            }
        }
    } else {
        std::cerr << "Failed to import JSON!" << '\n';
        return 1;
    }

    return 0;
}