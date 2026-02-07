#pragma once
#include <string>
#include <vector>

#include "Topology.h"

namespace cnc {
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