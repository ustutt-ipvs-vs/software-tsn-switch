#include "LldpParser.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <iostream>
#include <pugixml.hpp>

namespace cnc {
static bool isNodeNameMatch(const pugi::xml_node& node, const std::string& targetName) {
    std::string name = node.name();
    // 1. Exact Match
    if (name == targetName) {
        return true;
    }

    // 2. Suffix Match (e.g., "ieee802...:lldp")
    if (name.length() > targetName.length()) {
        if (name.compare(name.length() - targetName.length(), targetName.length(), targetName) == 0) {
            if (name[name.length() - targetName.length() - 1] == ':') {
                return true;
            }
        }
    }
    return false;
}

// Recursive function to find a node by name, considering namespaces
static pugi::xml_node findNodeDeep(const pugi::xml_node& parent, const std::string& targetName) {
    if (isNodeNameMatch(parent, targetName)) {
        return parent;
    }

    for (pugi::xml_node child : parent.children()) {
        pugi::xml_node found = findNodeDeep(child, targetName);
        if (found != nullptr) {
            return found;
        }
    }
    return {};
}

// Helper function to extract tag value considering possible namespaces
static std::string getChildValue(const pugi::xml_node& parent, const std::string& name) {
    // Direct child lookup
    for (pugi::xml_node child : parent.children()) {
        if (isNodeNameMatch(child, name)) {
            return child.child_value();
        }
    }
    return "";
}

bool LldpParser::parseLldpData(const std::string& xmlData, CncNode_t& node) {
    // Check for empty input
    if (xmlData.empty()) {
        spdlog::error("[Error] Empty XML data provided for LLDP parsing.");
        return false;
    }

    // Load XML data using pugixml
    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_string(xmlData.c_str());

    if (!result) {
        spdlog::error("[Error] Failed to parse LLDP XML data: {}", result.description());
        return false;
    }

    // 1. Locate the LLDP root element
    pugi::xml_node lldpRoot = findNodeDeep(doc, "lldp");

    // If no namespaces are used, try to find any element with "lldp" in its name
    if (!lldpRoot) {
        for (pugi::xml_node child : doc.children()) {
            std::string childName = child.name();
            if (childName.find("lldp") != std::string::npos) {
                lldpRoot = child;
                break;
            }
        }
    }

    // No LLDP root found
    if (!lldpRoot) {
        spdlog::error("[Error] No <lldp> root element found in XML data.");
        return false;
    }

    // 2. Iterate over all <port> elements
    for (pugi::xml_node port : lldpRoot.children()) {
        // Use helper function to check for "port" node
        if (!isNodeNameMatch(port, "port")) {
            continue;
        }

        // Get Name wih helper function
        std::string interfaceName = getChildValue(port, "name");
        if (interfaceName.empty()) {
            continue;
        }

        // Security check
        if (node.interfaces.empty()) {
            spdlog::error("[FATAL] Node interface list is empty! Check CncNode initialization.");
            return false;
        }

        // 3. Check if interface already exists in node
        ietfInterface_t* targetInterface = nullptr;
        for (auto& iface : node.interfaces) {
            if (iface.name == interfaceName) {
                targetInterface = &iface;
                break;
            }
        }

        // If port does not exist in config, ignore it
        if (targetInterface == nullptr) {
            continue;
        }

        // 4. Parse LLDP neighbor information
        /*
        pugi::xml_node remoteData = findNodeDeep(port, "remote-systems-data");
        */

        // 5. Write data into struct
        /*
        if (remoteData != nullptr) {
            targetInterface->lldpNeighbor.hasNeighbor = true;

            // Use helper function to get child values
            targetInterface->lldpNeighbor.chassisId = getChildValue(remoteData, "chassis-id");
            targetInterface->lldpNeighbor.portId = getChildValue(remoteData, "port-id");
            targetInterface->lldpNeighbor.systemName = getChildValue(remoteData, "system-name");

            // Look for ip address
            pugi::xml_node mgmtIpNode = findNodeDeep(remoteData, "management-address");

            if (mgmtIpNode != nullptr) {
                targetInterface->lldpNeighbor.managementIp = getChildValue(mgmtIpNode, "address");
            }
        } else {
            targetInterface->lldpNeighbor.hasNeighbor = false;
        }
        */
    }
    return true;
}
}  // namespace cnc