#pragma once

#include <libyang/libyang.h>

#include "CncTypes.h"

namespace cnc {
/**
 * @brief Parses interface information from libyang XML nodes and populates CncNode_t structures.
 * This parser is responsible for extracting interface-related data, including GCL configurations, from
 * the XML data provided by the NETCONF/YANG models. It maps the relevant XML nodes to the corresponding 
 * fields in the CncNode_t and ietfInterface_t structures defined in CncTypes.h.
 */
class InterfaceParser {
   public:
    /**
     * @brief Parses the interface information from the given libyang node and populates the provided CncNode_t
     * structure.
     * @param rootNode The libyang node containing the interface information to be parsed.
     * @param node The CncNode_t structure to be populated with the parsed interface information
     * @return true if the parsing was successful, false otherwise.
     */
    static bool parseInterface(const struct lyd_node* rootNode, CncNode_t& node);
};
}  // namespace cnc