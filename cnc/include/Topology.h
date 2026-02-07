#pragma once

#include <map>
#include <string>
#include <vector>

#include "CncTypes.h"

class Topology {
   public:
    /**
     * @brief The main database storing all network nodes (Switches/End-Devices).
     * This vector owns the memory of the objects.
     */
    std::vector<CncNode_t> nodes;

    /**
     * @brief Creates an index for fast node lookup by hostname
     * Must be called after populating the 'nodes' vector.
     */
    void buildIndex();

    /**
     * @brief Retrieves a node by its hostname
     * @param nodeName The hostname of the node to retrieve
     * @return Pointer to the CncNode_t if found, nullptr otherwise
     */
    CncNode_t* getNode(const std::string& nodeName);

    /**
     * @brief Retrieves an interface based on LLDP neighbor information
     * @param neighborInfo The LLDP neighbor information to match
     * @return Pointer to the ietfInterface_t if found, nullptr otherwise
     */
    ietfInterface_t* getInterface(const LldpNeighbor_t& neighborInfo);

   private:
    /**
     * @brief Internal helper for fast lookups.
     * Avoids looping through the vector every time if searching for a node.
     */
    std::map<std::string, CncNode_t*> nodeLookup;
};