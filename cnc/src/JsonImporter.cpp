#include "JsonImporter.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "Inventory.h"

using json = nlohmann::json;

namespace cnc {

// Imports CNC node configurations from a JSON file.
bool JsonImporter::importFromFile(const std::string& filename, Topology& topology) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[Error] Could not open config file: " << filename << '\n';
        return false;
    }

    try {
        json root;
        file >> root;

        if (!root.contains("nodes") || !root["nodes"].is_array()) {
            std::cerr << "[Error] JSON root has no 'nodes' array." << '\n';
            return false;
        }

        for (const auto& nodeItem : root["nodes"]) {
            CncNode_t node;

            // 1. Hostname & IP
            node.hostName = nodeItem.value("hostName", "unknown");

            // 2. Interfaces
            if (nodeItem.contains("interfaces") && nodeItem["interfaces"].is_array()) {
                for (const auto& ifaceItem : nodeItem["interfaces"]) {
                    ietfInterface_t iface;

                    iface.name = ifaceItem.value("name", "");

                    // 3. GCL Parsing TODO: look into standard values --> maybe error instead of defaults?
                    if (ifaceItem.contains("gcl")) {
                        auto gclItem = ifaceItem["gcl"];
                        GclConfig_t& gclConfig = iface.bridgePort.gateParameterTable;

                        gclConfig.gateEnabled = true;
                        gclConfig.configChange = true;
                        gclConfig.adminGateStates = gclItem.value("adminGateStates", 255);

                        // Cycle Time (JSON ns -> Struct Rational)
                        uint32_t cycleNs = gclItem.value("cycleTime", 1000000);
                        gclConfig.adminCycleTime.numerator = cycleNs;
                        gclConfig.adminCycleTime.denominator = 1000000000;

                        gclConfig.adminCycleTimeExtensionNs = gclItem.value("cycleTimeExtension", 0);

                        // Base Time
                        gclConfig.adminBaseTime.seconds = gclItem.value("baseTimeSeconds", 0);
                        gclConfig.adminBaseTime.nanoseconds = gclItem.value("baseTimeNanoseconds", 0);

                        // Entries
                        if (gclItem.contains("entries") && gclItem["entries"].is_array()) {
                            auto entries = gclItem["entries"];

                            if (!entries.empty()) {
                                gclConfig.adminControlList.resize(entries.size());

                                int idx = 0;
                                for (const auto& entryItem : entries) {
                                    GclEntry_t& entry = gclConfig.adminControlList[idx];

                                    entry.index = entryItem.value("index", idx);
                                    entry.timeIntervalValue = entryItem.value("timeIntervalValue", 100000);
                                    entry.gateStatesValue =
                                        static_cast<uint8_t>(entryItem.value("gateStatesValue", 255));
                                    entry.operationName = entryItem.value("operationName", "sched:set-gate-states");

                                    idx++;
                                }
                            }
                        } else {
                            gclConfig.adminControlList.clear();  // No entries, empty list
                        }
                    }
                    node.interfaces.push_back(iface);
                }
            }
            topology.nodes.push_back(node);
        }

        return true;

    } catch (const json::exception& e) {
        std::cerr << "[Error] JSON Parse Error: " << e.what() << '\n';
        return false;
    }
}

InventoryMap JsonImporter::importInventory(const std::string& filename) {
    InventoryMap inventory;  // Das ist unsere Map<string, DeviceCredentials>

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "[Error] Could not open inventory file: " << filename << '\n';
        return inventory;
    }

    try {
        json root;
        file >> root;

        if (!root.contains("inventory") || !root["inventory"].is_array()) {
            std::cerr << "[Error] Inventory JSON missing 'inventory' array." << '\n';
            return inventory;
        }

        for (const auto& item : root["inventory"]) {
            std::string host = item.value("hostName", "");
            if (host.empty()) {
                continue;  // Ohne Hostname bringt der Eintrag nichts
            }

            DeviceCredentials_t creds;
            creds.ip = item.value("management_ip", "");
            creds.username = item.value("username", "");
            creds.password = item.value("password", "");

            // Ab in die Map damit!
            // Key = "vstsn01", Value = {IP, User, Pass}
            inventory[host] = creds;
        }

    } catch (const json::exception& e) {
        std::cerr << "[Error] Inventory JSON Parse Error: " << e.what() << '\n';
    }

    return inventory;
}
}  // namespace cnc