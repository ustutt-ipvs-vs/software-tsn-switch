#include "../include/GclParser.h"

#include <cstring>
#include <iostream>
#include <pugixml.hpp>
#include <string>

namespace cnc {
// Helper function to check if node name matches target, considering namespaces
static bool isNodeNameMatch(const pugi::xml_node& node, const std::string& targetName) {
    std::string name = node.name();
    // 1. Exact match
    if (name == targetName) {
        return true;
    }

    // 2. Suffix Match (ignores namespace prefix)
    if (name.length() > targetName.length()) {
        if (name.compare(name.length() - targetName.length(), targetName.length(), targetName) == 0) {
            // Check if there is a colon before the suffix (e.g., "ietf:lldp")
            if (name[name.length() - targetName.length() - 1] == ':') {
                return true;
            }
        }
    }
    return false;
}

// Helper function to find node recursively by name, considering namespaces
static pugi::xml_node findNodeDeep(const pugi::xml_node& parent, const std::string& targetName) {
    if (isNodeNameMatch(parent, targetName)) return parent;

    for (pugi::xml_node child : parent.children()) {
        pugi::xml_node found = findNodeDeep(child, targetName);
        if (found) return found;
    }
    return pugi::xml_node();
}

// Helper function to read value
static std::string getVal(const pugi::xml_node& parent, const std::string& name, const char* def = "") {
    for (pugi::xml_node child : parent.children()) {
        if (isNodeNameMatch(child, name)) return child.child_value();
    }
    return def;
}

bool GclParser::parseOperationalGclData(const std::string& xmlData, CncNode_t& node) {
    if (xmlData.empty()) {
        return false;
    }

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xmlData.c_str());

    if (!result) {
        std::cerr << "XML parsing error: " << result.description() << " at offset " << result.offset << std::endl;
        return false;
    }

    // 1. Locate the root node containing interfaces
    pugi::xml_node root = findNodeDeep(doc, "interfaces");

    if (!root) {
        std::cerr << "No interfaces found in GCL data." << std::endl;
        return false;
    }

    // 2. Iterate over each interface
    for (pugi::xml_node ifaceNode : root.children()) {
        // Make sure it's an interface node
        if (!isNodeNameMatch(ifaceNode, "interface")) continue;

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

    // Navigation: Search recursively for gate-parameter-table
    pugi::xml_node gclNode = findNodeDeep(*ifnode, "gate-parameter-table");
    if (!gclNode) {
        return;
    }

    GclConfig_t& gclConfig = iface.bridgePort.gateParameterTable;

    // oper-gate-states
    std::string operGateStatesStr = getVal(gclNode, "oper-gate-states");
    if (!operGateStatesStr.empty()) {
        gclConfig.operGateStates = static_cast<uint8_t>(std::stoi(operGateStatesStr));
    }

    // oper-cycle-time
    // not in datastore --> create maybe fallback to admin time since admin is now running
    pugi::xml_node operCycleTimeNode = findNodeDeep(gclNode, "oper-cycle-time");
    if (operCycleTimeNode) {
        std::string numStr = getVal(operCycleTimeNode, "numerator");
        std::string denStr = getVal(operCycleTimeNode, "denominator");
        if (!numStr.empty() && !denStr.empty()) {
            gclConfig.operCycleTime.numerator = static_cast<uint32_t>(std::stoul(numStr));
            gclConfig.operCycleTime.denominator = static_cast<uint32_t>(std::stoul(denStr));
        }
    }

    // oper-base-time
    pugi::xml_node operBaseTimeNode = findNodeDeep(gclNode, "oper-base-time");
    if (operBaseTimeNode) {
        std::string secStr = getVal(operBaseTimeNode, "seconds");
        std::string nsecStr = getVal(operBaseTimeNode, "nanoseconds");
        if (!secStr.empty() && !nsecStr.empty()) {
            gclConfig.operBaseTime.seconds = static_cast<uint64_t>(std::stoull(secStr));
            gclConfig.operBaseTime.nanoseconds = static_cast<uint32_t>(std::stoul(nsecStr));
        }
    }

    // oper-control-list
    pugi::xml_node operControlListNode = findNodeDeep(gclNode, "oper-control-list");

    if (operControlListNode) {
        // 1. Collect all gate-control-entry nodes
        std::vector<pugi::xml_node> entries;
        for (pugi::xml_node entryNode : operControlListNode.children()) {
            // We check prefix-blind for "gate-control-entry"
            if (isNodeNameMatch(entryNode, "gate-control-entry")) {
                entries.push_back(entryNode);
            }
        }

        // 2. Clear old memory
        gclConfig.operControlList.clear();
        gclConfig.operControlList.resize(entries.size());

        // 3. Write new data
        if (!entries.empty()) {
            for (size_t i = 0; i < entries.size(); ++i) {
                pugi::xml_node entryNode = entries[i];
                GclEntry_t& entry = gclConfig.operControlList[i];

                // Read index
                std::string idxStr = getVal(entryNode, "index");
                // Fallback to loop index if empty
                entry.index = idxStr.empty() ? (uint32_t)i : (uint32_t)std::stoul(idxStr);

                // Gate States
                std::string gateStatesStr = getVal(entryNode, "gate-states-value");
                if (!gateStatesStr.empty()) {
                    entry.gateStatesValue = static_cast<uint8_t>(std::stoi(gateStatesStr));
                }

                // Time Interval
                std::string timeIntervalStr = getVal(entryNode, "time-interval-value");
                if (!timeIntervalStr.empty()) {
                    entry.timeIntervalValue = static_cast<uint32_t>(std::stoul(timeIntervalStr));
                }

                // Operation Name
                entry.operationName = getVal(entryNode, "operation-name", "sched:set-gate-states");
            }
        }
    }
}
}  // namespace cnc