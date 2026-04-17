#pragma once

#include <map>
#include <string>
#include <vector>
#include <mutex>
#include <memory>

#include "CncTypes.h"

/**
 * @brief Stores and provides lookup access to the controller's in-memory network topology.
 *
 * This class owns the collection of `CncNode_t` objects representing switches and end devices,
 * and builds auxiliary indices for efficient hostname-based access. It also offers helper
 * queries to resolve interfaces from LLDP neighbor information during topology correlation.
 */
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

    /**
     * @brief Retrieves a mutex for synchronizing access to a specific node
     * @param nodeName The hostname of the node for which to retrieve the mutex
     * @return Reference to the mutex associated with the node
     */
     std::mutex& getNodeMutex(const std::string& nodeName);

   private:
    /**
     * @brief Internal helper for fast lookups.
     * Avoids looping through the vector every time if searching for a node.
     */
    std::map<std::string, CncNode_t*> nodeLookup;

    /**
     * @brief Mutexes for synchronizing access to individual nodes. The map is protected by 'mapMutex' to ensure thread safety when adding new nodes.
     */
    std::map<std::string, std::unique_ptr<std::mutex>> nodeMutexes;

    /**
     * @brief Mutex to protect access to the 'nodeMutexes' map when adding new nodes or retrieving mutexes.
     */
    std::mutex mapMutex;
};