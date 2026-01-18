#include "LldpParser.h"
#include <iostream>
#include <pugixml.hpp>
#include <cstring>

namespace cnc {
    // Helper function to extract tag value considering possible namespaces
    static std::string getChildValue(const pugi::xml_node& parent, const char* childName) {
        // Direct child lookup
        pugi::xml_node child = parent.child(childName);
        
        // If not found, search through all children for matching suffix
        if (!child) {
            for (pugi::xml_node c : parent.children()) {
                std::string cName = c.name();
                // Check if cName ends with childName
                if(cName.find(childName) >= strlen(childName)) {
                    // Check if it matches at the end
                    if (cName.compare(cName.length() - strlen(childName), strlen(childName), childName) == 0) {
                        child = c;
                        break; // Found matching child
                    }
                }
            }
        }
        if (child) {
            return child.child_value();
        }
        return ""; // Return empty string if not found
    }

    bool LldpParser::parseLldpData(const std::string& xmlData, CncNode_t& node) {
        // Check for empty input
        if (xmlData.empty()) {
            std::cerr << "[Error] Empty XML data provided for LLDP parsing." << std::endl;
            return false;
        }

        // Load XML data using pugixml
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_string(xmlData.c_str());

        if (!result) {
            std::cerr << "[Error] Failed to parse LLDP XML data: " << result.description() << std::endl;
            return false;
        }

        // 1. Locate the LLDP root element
        // Directly look for <lldp> or <data><lldp> in root
        pugi::xml_node lldpRoot = doc.child("lldp");
        if (!lldpRoot) lldpRoot = doc.child("data").child("lldp"); // Handle possible wrapping

        // If no namespaces are used, try to find any element with "lldp" in its name
        if (!lldpRoot) {
            for (pugi::xml_node child : doc.children()) {
                std::string childName = child.name();
                if(childName.find("lldp") != std::string::npos) {
                    lldpRoot = child;
                    break;
                }
            }
        }

        // No LLDP root found
        if (!lldpRoot) {
            std::cerr << "[Error] No <lldp> root element found in XML data." << std::endl;
            return false;
        }

        // 2. Iterate over all <port> elements
        for (pugi::xml_node port : lldpRoot.children()) {
            std::string nodeName = port.name();
            // Only process <port> elements
            if (nodeName.find("port") == std::string::npos) {
                continue; // Skip non-port nodes
            }

            // Get Name wih helper function
            std::string interfaceName = getChildValue(port, "name");
            if (interfaceName.empty()) continue;

            // 3. Check if interface already exists in node
            ietfInterface_t* targetInterface = nullptr;
            for (auto& iface : node.interfaces) {
                if (iface.name == interfaceName) {
                    targetInterface = &iface;
                    break;
                }
            }

            // If port does not exist in config, ignore it
            if (!targetInterface) continue;

            // 4. Parse LLDP neighbor information
            pugi::xml_node remoteData = port.child("remote-system-data");

            // Fallback for namespaced remote-system-data
            if (!remoteData) {
                for (pugi::xml_node child : port.children()) {
                    std::string childName = child.name();
                    if(childName.find("remote-system-data") != std::string::npos) {
                        remoteData = child;
                        break;
                    }
                }
            }

            // 5. Write data into struct
            if (remoteData) {
                targetInterface->lldpNeighbor.hasNeighbor = true;

                // User helper function to get child values
                targetInterface->lldpNeighbor.chassisId = getChildValue(remoteData, "chassis-id");
                targetInterface->lldpNeighbor.portId = getChildValue(remoteData, "port-id");
                targetInterface->lldpNeighbor.systemName = getChildValue(remoteData, "system-name");
                
                // Look for ip address
                pugi::xml_node mgmtIpNode = remoteData.child("management-address");
                if (!mgmtIpNode) {
                    for (pugi::xml_node child : remoteData.children()) {
                        std::string childName = child.name();
                        if(childName.find("management-address") != std::string::npos) {
                            mgmtIpNode = child;
                            break;
                        }
                    }
                }

                if (mgmtIpNode) {
                    targetInterface->lldpNeighbor.managementIp = getChildValue(mgmtIpNode, "address");
                }
            } else {
                targetInterface->lldpNeighbor.hasNeighbor = false;
            }
        }
        return true;
    }
}