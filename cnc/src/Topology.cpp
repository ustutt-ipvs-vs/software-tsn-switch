#include "../include/Topology.h"

void Topology::buildIndex() {
    nodeLookup.clear();
    // Create the lookup map by looping through all nodes
    for (auto& node : nodes) {
        // node.hostName is the key, pointer to node is the value
        nodeLookup[node.hostName] = &node;
    }
}

CncNode_t* Topology::getNode(const std::string& nodeName) {
    // Check if the node exists in the lookup map
    if (nodeLookup.count(nodeName)) {
        return nodeLookup[nodeName];
    }
    return nullptr;
}

ietfInterface_t* Topology::getInterface(const LldpNeighbor_t& neighborInfo) {
    CncNode_t* node = getNode(neighborInfo.systemName);
    // Check if the node exists
    if (!node) return nullptr;
    // Loop through the interfaces to find a matching LLDP neighbor
    for (auto& iface : node->interfaces) {
        if (iface.name == neighborInfo.portId) {
            return &iface;
        }
    }

    return nullptr;
}