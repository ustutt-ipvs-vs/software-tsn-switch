#include <iostream>
#include "JsonImporter.h"
#include "Topology.h"

using namespace cnc;

int main() {
    std::cout << "--- JSON Importer Test ---" << std::endl;

    // 1. Create Topology object
    Topology topology;
    
    // 2. Call importer (adjust filename if needed)
    std::string filename = "test_config.json"; 
    
    // Note: Path must be relative to the execution directory (build/) 
    // If the file is in the root, consider using "../test_config.json" 
    // or copy the file to build/.
    
    if (JsonImporter::importFromFile(filename, topology)) {
        std::cout << "Success! Loaded " << topology.nodes.size() << " nodes." << std::endl;

        // 3. Check imported data
        for (const auto& node : topology.nodes) {
            std::cout << "\n[Node] " << node.hostName << std::endl;
            std::cout << "  - IP: '" << node.ipAddress << "'" << std::endl; 
            
            for (const auto& iface : node.interfaces) {
                std::cout << "  - Interface: '" << iface.name << "'" << std::endl;
                
                if (iface.bridgePort.gateParameterTable.gateEnabled) {
                    std::cout << "    -> GCL Config found (Cycle: " 
                              << iface.bridgePort.gateParameterTable.adminCycleTime.numerator << "ns)" << std::endl;
                }
            }
        }
    } else {
        std::cerr << "Failed to import JSON!" << std::endl;
        return 1;
    }

    return 0;
}