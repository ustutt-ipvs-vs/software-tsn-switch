#include "JsonImporter.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <ctime>

using json = nlohmann::json;

std::vector<CncNode_t> JsonImporter::importFromFile(const std::string& filePath) {
    // Open the JSON file
    std::ifstream file(filePath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filePath);
    }

    json root;
    // Parse the JSON content
    try {
        root = json::parse(file);
    } catch (const json::parse_error& e) {
        throw std::runtime_error("JSON parse error in file " + filePath + ": " + e.what());
    }   

    std::vector<CncNode_t> nodes;

    // Check for "nodes" array
    if(root.contains("nodes") && root["nodes"].is_array()) {
        // Iterate through each node (host)
        for (const auto& nodeItem : root["nodes"]) {
            CncNode_t node;

            node.hostName = nodeItem.value("hostName", ""); //TODO: handle missing hostName
            node.id = 0; //TODO: handle id automatically
            // Check if interfaces exist
            if(nodeItem.contains("interfaces") && nodeItem["interfaces"].is_array()) {
                for(const auto& ifaceItem : nodeItem["interfaces"]) {
                    ietfInterface_t iface;
                    iface.name = ""; // Name is not known in this moment

                    iface.lldpNeighbor.systemName = ifaceItem.value("neighbor", ""); // TODO: handle missing neighbor
                    iface.lldpNeighbor.hasNeighbor = !iface.lldpNeighbor.systemName.empty();

                    if(ifaceItem.contains("gcl")) {
                        auto gclItem = ifaceItem["gcl"];
                        GclConfig_t& gclConfig = iface.bridgePort.gateParameterTable;
                        
                        // Parse GCL entries
                        gclConfig.gateEnabled = true;
                        gclConfig.adminGateStates = gclItem.value("adminGateStates", 255); // TODO: handle missing adminGateStates
                        gclConfig.configChange = true;

                        // Convert cycle time from nanoseconds to RationalTime_t
                        uint32_t cycleNs = gclItem.value("cycleTime", 1'000'000'000); // TODO: handle missing cycleTime
                        gclConfig.adminCycleTime.numerator = cycleNs;
                        gclConfig.adminCycleTime.denominator = 1'000'000'000;

                        // Set cycle time extension
                        gclConfig.adminCycleTimeExtensionNs = gclItem.value("cycleTimeExtension", 0); // TODO: handle missing cycleTimeExtension

                        // Set default base time to zero
                        uint64_t baseSeconds = 0;
                        uint32_t baseNanoseconds = 0;

                        // Check if base time is provided and set it if so
                        if(gclItem.contains("baseTimeSeconds")) {
                            baseSeconds = gclItem["baseTimeSeconds"].get<uint64_t>();
                        }
                        if(gclItem.contains("baseTimeNanoseconds")) {
                            baseNanoseconds = gclItem["baseTimeNanoseconds"].get<uint32_t>();
                        }

                        // Set admin base time
                        gclConfig.adminBaseTime.seconds = baseSeconds;
                        gclConfig.adminBaseTime.nanoseconds = baseNanoseconds;

                        // Check if entries exist
                        if(gclItem.contains("entries") && gclItem["entries"].is_array()) {
                            auto entries = gclItem["entries"];
                            gclConfig.adminControlListSize = static_cast<uint32_t>(entries.size());

                            if(gclConfig.adminControlListSize > 0) {
                                gclConfig.adminControlList = new GclEntry_t[gclConfig.adminControlListSize];

                                int idx = 0;

                                // Iterate through each GCL entry
                                for(const auto& entryItem : entries) {
                                    GclEntry_t& gclEntry = gclConfig.adminControlList[idx];
                                    gclEntry.index = entryItem.value("index", idx); // TODO: check if idx is sufficient
                                    gclEntry.timeIntervalValue = entryItem.value("timeIntervalValue", 0); // TODO: handle missing timeIntervalNs
                                    gclEntry.gateStatesValue = static_cast<uint8_t>(entryItem.value("gateStatesValue", 255)); // TODO: handle missing gateStates, currentyl set to all open
                                    gclEntry.operationName = entryItem.value("operationName", "sched:set-gate-states"); // TODO: handle missing operationName, currently set to default
                                    
                                    idx++;
                                }
                            }
                        }
                    }
                    // Add interface to node
                    node.interfaces.push_back(iface);
                }
            }
            // Add node to nodes list
            nodes.push_back(node);
        }
    }
    return nodes;
}