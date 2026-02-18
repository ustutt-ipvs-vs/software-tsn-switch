#pragma once
#include <string>
#include <vector>

#include "Topology.h"

namespace cnc {
/**
 * @brief Parses LLDP operational XML data from NETCONF and maps it to internal CNC structures.
 *
 * This parser consumes LLDP information retrieved from the NETCONF datastore in XML form and
 * populates the corresponding fields in `CncNode_t`. It is used to translate neighbor and
 * interface discovery data into the controller's in-memory topology representation.
 */
class LldpParser {
   public:
    /**
     * @brief Parses LLDP data from the provided XML string and populates the given CncNode_t structure.
     * @param xmlData The LLDP data in XML format.
     * @param node Reference to the CncNode_t structure to populate.
     * @return true if parsing is successful, false otherwise.
     */
    static bool parseLldpData(const std::string& xmlData, CncNode_t& node);
};
};  // namespace cnc