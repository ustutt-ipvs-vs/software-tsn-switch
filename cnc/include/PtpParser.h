#pragma once

#include "CncTypes.h"
#include "libyang/libyang.h"

namespace cnc {
/**
 * @brief Parses PTP data from libyang XML nodes and populates CncNode_t structures.
 * This parser is responsible for extracting PTP-related data from the XML data provided
 * by the NETCONF/YANG models. It maps the relevant XML nodes to the corresponding fields
 * in the CncNode_t structure defined in CncTypes.h.
 */
class PtpParser {
   public:
    /**
     * @brief Parses PTP data from the given XML node and populates the ptpAllData field of the provided CncNode_t.
     * @param root The root XML node containing PTP data (expected to be the result of a Netconf <get> operation).
     * @param node The CncNode_t instance to populate with the parsed PTP data.
     * @return true if parsing was successful and ptpAllData was populated, false otherwise.
     */
    static bool parseOperationalPtpData(const struct lyd_node* root, CncNode_t& node);
};
}  // namespace cnc