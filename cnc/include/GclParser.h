#pragma once

#include <CncTypes.h>
#include <Topology.h>

#include <string>

namespace cnc {
class GclParser {
   public:
    /**
     * @brief Parses GCL configuration data from the provided XML string and populates the given CncNode_t structure.
     * @param xmlData The GCL configuration data in XML format.
     * @param node Reference to the CncNode_t structure to populate.
     * @return true if parsing is successful, false otherwise.
     */
    static bool parseOperationalGclData(const std::string& xmlData, CncNode_t& node);

   private:
    /**
     * @brief Parses GCL configuration for a single interface from the given XML node pointer.
     * @param xmlNodePtr Pointer to the XML node containing the interface GCL data.
     * @param iface Reference to the ietfInterface_t structure to populate.
     */
    static void parseInterfaceGcl(const void* xmlNodePtr, ietfInterface_t& iface);
};
}  // namespace cnc