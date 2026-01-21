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


    }
}