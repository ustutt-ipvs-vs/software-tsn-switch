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
#include "spdlog/spdlog.h"

int main() {
    // Time measurement variables (in microseconds)
    long long t_init = 0;
    long long t_connect = 0;
    long long t_lldp = 0;
    long long t_deploy = 0;
    long long t_verify = 0;

    auto startTotal = std::chrono::high_resolution_clock::now();
    auto t_start_step = std::chrono::high_resolution_clock::now();

    spdlog::info("========================================");
    spdlog::info("      CNC NETWORK CONTROLLER v1.5       ");
    spdlog::info("========================================");

    // 1. Hardcoded path to file
    const std::string TOPOLOGY_FILE = "../../cnc/examples/simple_example_schedule_v4.json";
    const std::string INVENTORY_FILE = "../../cnc/config/inventory.json";

    spdlog::info("[INFO] Topology File:  {}", TOPOLOGY_FILE);
    spdlog::info("[INFO] Inventory File: {}", INVENTORY_FILE);

    // Check if files exist
    if (!std::filesystem::exists(TOPOLOGY_FILE)) {
        spdlog::error("[FATAL] File not found: {}", TOPOLOGY_FILE);
        spdlog::error("        CWD: {}", std::filesystem::current_path().string());
        return 1;
    }

    if (!std::filesystem::exists(INVENTORY_FILE)) {
        spdlog::error("[FATAL] File not found: {}", INVENTORY_FILE);
        return 1;
    }

    // 2. Create Topology and Import from JSON
    Topology topology;
    spdlog::info("[INFO] Importing topology... ");

    if (!cnc::JsonImporter::importFromFile(TOPOLOGY_FILE, topology)) {
        spdlog::error("[FATAL] Failed to import topology from JSON!");
        return 1;
    }

    // Build index for fast lookups
    topology.buildIndex();

    if (topology.nodes.empty()) {
        spdlog::error("[FATAL] No nodes found in imported topology!");
        return 1;
    }

    // 3. Import Inventory (Device Credentials)
    spdlog::info("[INFO] Importing inventory ...");
    auto inventoryMap = cnc::JsonImporter::importInventory(INVENTORY_FILE);

    if (inventoryMap.empty()) {
        spdlog::error("[FATAL] No inventory entries found!");
        return 1;
    }

    // [TIME] Init Phase stop
    auto t_end_step = std::chrono::high_resolution_clock::now();
    t_init = std::chrono::duration_cast<std::chrono::microseconds>(t_end_step - t_start_step).count();

    // 4. Initialize Network Manager
    cnc::NetworkManager manager(topology);

    // 5. Connect to all nodes
    spdlog::info("[INFO] --- Starting Connection Phase ---");

    t_start_step = std::chrono::high_resolution_clock::now();

    bool connected = manager.connectAllNodes(inventoryMap);

    if (!connected) {
        // TODO: Look more into which nodes failed
        spdlog::error("[FATAL] Could not connect to all nodes!");
        return 1;
    }

    t_end_step = std::chrono::high_resolution_clock::now();
    t_connect = std::chrono::duration_cast<std::chrono::microseconds>(t_end_step - t_start_step).count();

    // 6. Fetch LLDP Data
    spdlog::info("[INFO] --- Fetching LLDP Data ---");

    t_start_step = std::chrono::high_resolution_clock::now();

    manager.fetchLldpData();

    t_end_step = std::chrono::high_resolution_clock::now();
    t_lldp = std::chrono::duration_cast<std::chrono::microseconds>(t_end_step - t_start_step).count();

    // Print LLDP neighbor information for verification
    spdlog::info("[VERIFICATION] LLDP Neighbor Check:");
    bool anyNeighborFound = false;
    for (const auto& node : topology.nodes) {
        for (const auto& iface : node.interfaces) {
            if (iface.lldpNeighbor.hasNeighbor) {
                anyNeighborFound = true;
                spdlog::info("  [MATCH] Node: {} | Iface: {} <--> Remote: {} (PortID: {})", node.hostName, iface.name,
                             iface.lldpNeighbor.systemName, iface.lldpNeighbor.portId);
            }
        }
    }
    if (!anyNeighborFound) {
        spdlog::warn("  [WARN] No LLDP neighbors found in topology data.");
    }
    spdlog::info("----------------------------------------");

    // 7. Deployment (Create XML and send via Netconf)
    spdlog::info("[INFO] --- Starting Deployment Phase ---");

    t_start_step = std::chrono::high_resolution_clock::now();

    manager.deployConfigToAll();

    t_end_step = std::chrono::high_resolution_clock::now();
    t_deploy = std::chrono::duration_cast<std::chrono::microseconds>(t_end_step - t_start_step).count();

    // 8. Fetch Operational GCL Data for verification
    spdlog::info("[INFO] --- Fetching Operational GCL Data for Verification ---");

    t_start_step = std::chrono::high_resolution_clock::now();

    manager.fetchOperationGcl();

    spdlog::info("[VERIFICATION] Operational GCL Data Check:");
    bool anyGclFound = false;

    for (const auto& node : topology.nodes) {
        for (const auto& iface : node.interfaces) {
            // Fetch operational GCL data from struct
            const auto& gcl = iface.bridgePort.gateParameterTable;

            // We show it only if entries were found
            if (!gcl.operControlList.empty()) {
                anyGclFound = true;
                spdlog::info("  [MATCH] Node: {} | Iface: {}", node.hostName, iface.name);

                // display cycle time
                spdlog::info("    Cycle Time: {} / {} ns", gcl.operCycleTime.numerator, gcl.operCycleTime.denominator);

                // display base time (optional, for safety)
                spdlog::info("    Base Time:  {}s {}ns", gcl.operBaseTime.seconds, gcl.operBaseTime.nanoseconds);

                // iterate over the list of GCL entries
                spdlog::info("    Gate Control List ({} entries):", gcl.operControlList.size());
                spdlog::info("      Index | Interval (ns) | Gate Mask (Hex)");
                spdlog::info("      ------+---------------+----------------");

                for (uint32_t i = 0; i < gcl.operControlList.size(); ++i) {
                    const auto& entry = gcl.operControlList[i];

                    // HIER IST DER MAGISCHE TEIL:
                    spdlog::info("      {:5} | {:13} | 0x{:X}", i, entry.timeIntervalValue,
                                 static_cast<int>(entry.gateStatesValue));
                }

                spdlog::info("----------------------------------------");
            }
        }
    }

    if (!anyGclFound) {
        spdlog::warn("  [WARN] No Operational GCL data found in structs.");
        spdlog::warn("         (Check if XML names match Interface names!)");
    }
    spdlog::info("----------------------------------------");

    t_end_step = std::chrono::high_resolution_clock::now();
    t_verify = std::chrono::duration_cast<std::chrono::microseconds>(t_end_step - t_start_step).count();

    // 9. Finish
    spdlog::info("========================================");
    spdlog::info("      CNC OPERATION FINISHED            ");
    spdlog::info("========================================");

    // --- FINAL TIMING SUMMARY ---
    auto endTotal = std::chrono::high_resolution_clock::now();
    auto durTotal = std::chrono::duration_cast<std::chrono::microseconds>(endTotal - startTotal).count();

    // Kleiner Trick: Leere Info für Abstand, falls gewünscht
    spdlog::info("");
    spdlog::info("========================================");
    spdlog::info("{:^40}", "PERFORMANCE METRICS");  // Automatisch zentriert!
    spdlog::info("========================================");

    // Header: Phase links (25), Time rechts (12)
    spdlog::info("{:<25}{:>12}", "PHASE", "TIME (us)");
    spdlog::info("----------------------------------------");

    // Werte
    spdlog::info("{:<25}{:>12}", "1. Initialization", t_init);
    spdlog::info("{:<25}{:>12}", "2. Connection (SSH)", t_connect);
    spdlog::info("{:<25}{:>12}", "3. Fetch LLDP", t_lldp);
    spdlog::info("{:<25}{:>12}", "4. Deploy Config", t_deploy);
    spdlog::info("{:<25}{:>12}", "5. Verify (GCL Fetch)", t_verify);

    spdlog::info("----------------------------------------");
    spdlog::info("{:<25}{:>12}", "TOTAL EXECUTION", durTotal);
    spdlog::info("========================================");

    return 0;
}