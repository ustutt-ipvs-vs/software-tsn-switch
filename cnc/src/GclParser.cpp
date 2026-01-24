#include "../include/GclParser.h"
#include <iostream>
#include <pugixml.hpp>
#include <cstring>
#include <string>

namespace cnc {
    static pugi::xml_node findChild(const pugi::xml_node& parent, const char* childName) {
        pugi::xml_node child = parent.child(childName);
        if (child) return child;

        for (pugi::xml_node c : parent.children()) {
            std::string cName = c.name();
            // Check if name ends with the desired child name (to ignore namespaces)
            if (cName.length() >= strlen(childName)) {
                if (cName.compare(cName.length() - strlen(childName), strlen(childName), childName) == 0) {
                    return c;
                }
            }
        }
        return pugi::xml_node(); // Return empty node if not found
    }

    static std::string getVal(const pugi::xml_node& parent, const char* name, const char* def = "") {
        pugi::xml_node child = findChild(parent, name);
        return child ? child.child_value() : def;
    }

    bool GclParser::parseOperationalGclData(const std::string& xmlData, CncNode_t& node) {
        if (xmlData.empty()) return false;

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_string(xmlData.c_str());

        if (!result) {
            std::cerr << "XML parsing error: " << result.description() << " at offset " << result.offset << std::endl;
            return false;
        }

        // 1. Locate the root node containing interfaces
        pugi::xml_node root = findChild(doc, "interfaces");
        if (!root) root = findChild(doc.child("data"), "interface");

        if (!root) {
            std::cerr << "No interfaces found in GCL data." << std::endl;
            return false;
        }

        // 2. Iterate over each interface
        for (pugi::xml_node ifaceNode : root.children()) {
            // Name of the interface
            std::string ifaceName = getVal(ifaceNode, "name");
            if (ifaceName.empty()) continue;

            // 3. Search for interface in struct 
            ietfInterface_t* targetIface = nullptr;
            for (auto& nodeIface : node.interfaces) {
                if (nodeIface.name == ifaceName) {
                    targetIface = &nodeIface;
                    break;
                }
            }

            // If interface found in XML, parse its GCL
            if (targetIface) {
                parseInterfaceGcl(&ifaceNode, *targetIface);
            }
        }
        return true;
    }

    void GclParser::parseInterfaceGcl(const void* xmlNodePtr, ietfInterface_t& iface) {
        const pugi::xml_node* ifnode = static_cast<const pugi::xml_node*>(xmlNodePtr);

        // Navigation: interface -> bridge-port -> gate-parameter-table
        pugi::xml_node bridgePortNode = findChild(*ifnode, "bridge-port");
        if (!bridgePortNode) return;

        pugi::xml_node gclNode = findChild(bridgePortNode, "gate-parameter-table");
        if (!gclNode) return;

        GclConfig_t& gclConfig = iface.bridgePort.gateParameterTable;

        // oper-gate-states
        std::string operGateStatesStr = getVal(gclNode, "oper-gate-states");
        if (!operGateStatesStr.empty()) {
            gclConfig.operGateStates = static_cast<uint8_t>(std::stoi(operGateStatesStr));
        }

        // oper-cycle-time
        pugi::xml_node operCycleTimeNode = findChild(gclNode, "oper-cycle-time");
        if (operCycleTimeNode) {
            std::string numStr = getVal(operCycleTimeNode, "numerator");
            std::string denStr = getVal(operCycleTimeNode, "denominator");
            if (!numStr.empty() && !denStr.empty()) {
                gclConfig.operCycleTime.numerator = static_cast<uint32_t>(std::stoul(numStr));
                gclConfig.operCycleTime.denominator = static_cast<uint32_t>(std::stoul(denStr));
            }
        }

        // oper-base-time
        pugi::xml_node operBaseTimeNode = findChild(gclNode, "oper-base-time");
        if (operBaseTimeNode) {
            std::string secStr = getVal(operBaseTimeNode, "seconds");
            std::string nsecStr = getVal(operBaseTimeNode, "nanoseconds");
            if (!secStr.empty() && !nsecStr.empty()) {
                gclConfig.operBaseTime.seconds = static_cast<uint64_t>(std::stoull(secStr));
                gclConfig.operBaseTime.nanoseconds = static_cast<uint32_t>(std::stoul(nsecStr));
            }
        }

        // oper-control-list
        pugi::xml_node operControlListNode = findChild(gclNode, "oper-control-list");
        if (operControlListNode) {
            // First, count entries
            std::vector<pugi::xml_node> entries;
            for (pugi::xml_node entryNode : operControlListNode.children()) {
                std::string name = entryNode.name();
                if (name.find("gate-control-entry") != std::string::npos) {
                    entries.push_back(entryNode);
                }
            }

            // Remove old array if exists
            if (gclConfig.operControlList != nullptr) {
                delete[] gclConfig.operControlList;
                gclConfig.operControlList = nullptr;
                gclConfig.operControlListSize = 0;
            }

            // Allocate new array
            if (!entries.empty()) {
                gclConfig.operControlListSize = static_cast<uint32_t>(entries.size());
                gclConfig.operControlList = new GclEntry_t[gclConfig.operControlListSize];

                // Parse each entry
                for (size_t i = 0; i < entries.size(); ++i) {
                    pugi::xml_node entryNode = entries[i];
                    GclEntry_t& entry = gclConfig.operControlList[i];

                    entry.index = i; // or parse from XML if available?

                    std::string gateStatesStr = getVal(entryNode, "gate-states-value");
                    if (!gateStatesStr.empty()) entry.gateStatesValue = static_cast<uint8_t>(std::stoi(gateStatesStr));

                    std::string timeIntervalStr = getVal(entryNode, "time-interval-value");
                    if (!timeIntervalStr.empty()) entry.timeIntervalValue = static_cast<uint32_t>(std::stoul(timeIntervalStr));

                    entry.operationName = getVal(entryNode, "operation-name", "sched:set-gate-states");
                }
            }
        }
    }
}