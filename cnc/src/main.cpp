#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "CncTypes.h"
#include "Inventory.h"
#include "JsonImporter.h"
#include "NetworkManager.h"
#include "Topology.h"

int main() {
    // Time measurement variables (in microseconds)
    long long t_init = 0;
    long long t_connect = 0;
    long long t_lldp = 0;
    long long t_deploy = 0;
    long long t_verify = 0;

    auto startTotal = std::chrono::high_resolution_clock::now();
    auto t_start_step = std::chrono::high_resolution_clock::now();

    std::cout << "========================================" << std::endl;
    std::cout << "      CNC NETWORK CONTROLLER v1.5       " << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. Hardcoded path to file
    const std::string TOPOLOGY_FILE = "cnc/examples/simple_example_schedule_v3.json";
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

    // Build index for fast lookups
    topology.buildIndex();

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

    // [TIME] Init Phase stop
    auto t_end_step = std::chrono::high_resolution_clock::now();
    t_init = std::chrono::duration_cast<std::chrono::microseconds>(t_end_step - t_start_step).count();

    // 4. Initialize Network Manager
    cnc::NetworkManager manager(topology);

    // 5. Connect to all nodes
    std::cout << "\n[INFO] --- Starting Connection Phase ---" << std::endl;

    t_start_step = std::chrono::high_resolution_clock::now();

    bool connected = manager.connectAllNodes(inventoryMap);

    if (!connected) {
        // TODO: Look more into which nodes failed
        std::cerr << "[FATAL] Could not connect to all nodes!" << std::endl;
        return 1;
    }

    t_end_step = std::chrono::high_resolution_clock::now();
    t_connect = std::chrono::duration_cast<std::chrono::microseconds>(t_end_step - t_start_step).count();

    // 6. Fetch LLDP Data
    std::cout << "\n[INFO] --- Fetching LLDP Data ---" << std::endl;

    t_start_step = std::chrono::high_resolution_clock::now();

    manager.fetchLldpData();

    t_end_step = std::chrono::high_resolution_clock::now();
    t_lldp = std::chrono::duration_cast<std::chrono::microseconds>(t_end_step - t_start_step).count();

    // Print LLDP neighbor information for verification
    std::cout << "\n[VERIFICATION] LLDP Neighbor Check:" << std::endl;
    bool anyNeighborFound = false;
    for (const auto& node : topology.nodes) {
        for (const auto& iface : node.interfaces) {
            if (iface.lldpNeighbor.hasNeighbor) {
                anyNeighborFound = true;
                std::cout << "  [MATCH] Node: " << node.hostName << " | Iface: " << iface.name
                          << " <--> Remote: " << iface.lldpNeighbor.systemName
                          << " (PortID: " << iface.lldpNeighbor.portId << ")" << std::endl;
            }
        }
    }
    if (!anyNeighborFound) {
        std::cout << "  [WARN] No LLDP neighbors found in topology data." << std::endl;
    }
    std::cout << "----------------------------------------" << std::endl;

    // 7. Deployment (Create XML and send via Netconf)
    std::cout << "\n[INFO] --- Starting Deployment Phase ---" << std::endl;

    t_start_step = std::chrono::high_resolution_clock::now();

    manager.deployConfigToAll();

    t_end_step = std::chrono::high_resolution_clock::now();
    t_deploy = std::chrono::duration_cast<std::chrono::microseconds>(t_end_step - t_start_step).count();

    // 8. Fetch Operational GCL Data for verification
    std::cout << "\n[INFO] --- Fetching Operational GCL Data for Verification ---" << std::endl;

    t_start_step = std::chrono::high_resolution_clock::now();

    manager.fetchOperationGcl();

    std::cout << "\n[VERIFICATION] Operational GCL Data Check:" << std::endl;
    bool anyGclFound = false;

    for (const auto& node : topology.nodes) {
        for (const auto& iface : node.interfaces) {
            // Fetch operational GCL data from struct
            const auto& gcl = iface.bridgePort.gateParameterTable;

            // We show it only if entries were found
            if (!gcl.operControlList.empty()) {
                anyGclFound = true;
                std::cout << "  [MATCH] Node: " << node.hostName << " | Iface: " << iface.name << std::endl;

                // display cycle time
                std::cout << "    Cycle Time: " << gcl.operCycleTime.numerator << " / " << gcl.operCycleTime.denominator
                          << " ns" << std::endl;

                // display base time (optional, for safety)
                std::cout << "    Base Time:  " << gcl.operBaseTime.seconds << "s " << gcl.operBaseTime.nanoseconds
                          << "ns" << std::endl;

                // iterate over the list of GCL entries
                std::cout << "    Gate Control List (" << gcl.operControlList.size() << " entries):" << std::endl;
                std::cout << "      Index | Interval (ns) | Gate Mask (Hex)" << std::endl;
                std::cout << "      ------+---------------+----------------" << std::endl;

                for (uint32_t i = 0; i < gcl.operControlList.size(); ++i) {
                    const auto& entry = gcl.operControlList[i];
                    std::cout << "      " << std::setw(5) << i << " | " << std::setw(13) << entry.timeIntervalValue
                              << " | 0x" << std::hex << std::uppercase << (int)entry.gateStatesValue
                              << std::dec  // Hex-Format für Maske
                              << std::endl;
                }
                std::cout << "----------------------------------------" << std::endl;
            }
        }
    }

    if (!anyGclFound) {
        std::cout << "  [WARN] No Operational GCL data found in structs." << std::endl;
        std::cout << "         (Check if XML names match Interface names!)" << std::endl;
    }
    std::cout << "----------------------------------------" << std::endl;

    t_end_step = std::chrono::high_resolution_clock::now();
    t_verify = std::chrono::duration_cast<std::chrono::microseconds>(t_end_step - t_start_step).count();

    // 9. Finish
    std::cout << "\n========================================" << std::endl;
    std::cout << "      CNC OPERATION FINISHED            " << std::endl;
    std::cout << "========================================" << std::endl;

    // --- FINAL TIMING SUMMARY ---
    auto endTotal = std::chrono::high_resolution_clock::now();
    auto durTotal = std::chrono::duration_cast<std::chrono::microseconds>(endTotal - startTotal).count();

    std::cout << "\n========================================" << std::endl;
    std::cout << "         PERFORMANCE METRICS            " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::left << std::setw(25) << "PHASE" << std::right << std::setw(12) << "TIME (µs)" << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << std::left << std::setw(25) << "1. Initialization" << std::right << std::setw(12) << t_init
              << std::endl;
    std::cout << std::left << std::setw(25) << "2. Connection (SSH)" << std::right << std::setw(12) << t_connect
              << std::endl;
    std::cout << std::left << std::setw(25) << "3. Fetch LLDP" << std::right << std::setw(12) << t_lldp << std::endl;
    std::cout << std::left << std::setw(25) << "4. Deploy Config" << std::right << std::setw(12) << t_deploy
              << std::endl;
    std::cout << std::left << std::setw(25) << "5. Verify (GCL Fetch)" << std::right << std::setw(12) << t_verify
              << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << std::left << std::setw(25) << "TOTAL EXECUTION" << std::right << std::setw(12) << durTotal
              << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}