#include <spdlog/spdlog.h>

#include <filesystem>
#include <iostream>

#include "JsonImporter.h"
#include "Topology.h"

using namespace cnc;

int main() {
    spdlog::info("--- JSON Importer Test ---");

    spdlog::info("[DEBUG] Current Working Directory: {}", std::filesystem::current_path().string());

    // 1. Create Topology object
    Topology topology;

    // 2. Call importer (adjust filename if needed)
    std::string filename = "../../cnc/examples/simple_example_schedule_v2.json";

    // Note: Path must be relative to the execution directory (build/)
    // If the file is in the root, consider using "../test_config.json"
    // or copy the file to build/.

    if (JsonImporter::importFromFile(filename, topology)) {
        spdlog::info("Success! Loaded {} nodes.", topology.nodes.size());

        // 3. Check imported data
        for (const auto& node : topology.nodes) {
            spdlog::info("[Node] {}", node.hostName);
            spdlog::info("  - IP: '{}'", node.ipAddress);

            for (const auto& iface : node.interfaces) {
                spdlog::info("  - Interface: '{}'", iface.name);

                if (iface.bridgePort.gateParameterTable.gateEnabled) {
                    spdlog::info("    -> GCL Config found (Cycle: {}ns)",
                                 iface.bridgePort.gateParameterTable.adminCycleTime.numerator);
                }
            }
        }
    } else {
        spdlog::error("Failed to import JSON!");
        return 1;
    }

    return 0;
}