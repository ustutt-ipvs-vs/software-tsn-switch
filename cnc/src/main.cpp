#include <iostream>
#include <string>
#include <fstream>
#include <map>
#include <filesystem>

#include "CncTypes.h"
#include "Topology.h"
#include "JsonImporter.h"
#include "NetworkManager.h"
#include "Inventory.h"

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "      CNC NETWORK CONTROLLER v1.2       " << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. Hardcoded path to file 
    const std::string TOPOLOGY_FILE = "cnc/examples/simple_example_schedule_v2.json";
    const std::string INVENTORY_FILE = "cnc/config/inventory.json";

    std::cout << "[INFO] Topology File:  " << TOPOLOGY_FILE << std::endl;
    std::cout << "[INFO] Inventory File: " << INVENTORY_FILE << std::endl;

    // Check if files exist
    if (!std::filesystem::exists(TOPOLOGY_FILE)) {
        std::cerr << "[FATAL] File not found: " << TOPOLOGY_FILE << std::endl;
        std::cerr << "        CWD: " << std::filesystem::current_path() << std::endl;
        return 1;
    }

    if (!std::filesystem::exists(INVENTORY_FILE)) {
        std::cerr << "[FATAL] File not found: " << INVENTORY_FILE << std::endl;
        return 1;
    }

    // 2. Create Topology and Import from JSON
    Topology topology;
    std::cout << "[INFO] Importing topology... " << std::endl;

    if (!cnc::JsonImporter::importFromFile(TOPOLOGY_FILE, topology)) {
        std::cerr << "[FATAL] Failed to import topology from JSON!" << std::endl;
        return 1;
    }

    if (topology.nodes.empty()) {
        std::cerr << "[FATAL] No nodes found in imported topology!" << std::endl;
        return 1;
    }

    // 3. Import Inventory (Device Credentials)
    std::cout << "[INFO] Importing inventory ..." << std::endl;
    auto inventoryMap = cnc::JsonImporter::importInventory(INVENTORY_FILE);

    if (inventoryMap.empty()) {
        std::cerr << "[FATAL] No inventory entries found!" << std::endl;
        return 1;
    }

    // 4. Initialize Network Manager
    cnc::NetworkManager manager(topology);

    // 5. Connect to all nodes
    std::cout << "\n[INFO] --- Starting Connection Phase ---" << std::endl;

    bool connected = manager.connectAllNodes(inventoryMap);
    
    if (!connected) {
        // TODO: Look more into which nodes failed
        std::cerr << "[FATAL] Could not connect to all nodes!" << std::endl;
        return 1;
    }

    // 6. Fetch LLDP Data
    //std::cout << "\n[INFO] --- Fetching LLDP Data ---" << std::endl;
    //manager.fetchLldpData();

    // 6. Deployment (Create XML and send via Netconf)
    std::cout << "\n[INFO] --- Starting Deployment Phase ---" << std::endl;
    manager.deployConfigToAll();

    // 7. Finish
    std::cout << "\n========================================" << std::endl;
    std::cout << "      CNC OPERATION FINISHED            " << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}