#pragma once

#include "libyang/libyang.h"
#include "CncTypes.h"

namespace cnc {
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
}