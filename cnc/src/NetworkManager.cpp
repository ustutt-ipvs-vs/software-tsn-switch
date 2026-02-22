#include "../include/NetworkManager.h"

#include <iostream>
#include <sstream>  // Needed for XML construction

#include "../include/GclParser.h"
#include "../include/GclXmlBuilder.h"
#include "../include/Inventory.h"
#include "../include/LldpParser.h"

namespace cnc {
NetworkManager::NetworkManager(Topology& topology) : topology_(topology) {
    // Constructor implementation (if needed)
}

NetworkManager::~NetworkManager() {
    // shared_ptr will automatically clean up the session
    sessions_.clear();
}

bool NetworkManager::connectAllNodes(const InventoryMap& inventory) {
    bool atLeastOneConnected = false;

    for (const auto& node : topology_.nodes) {
        // Lookup credentials in inventory
        auto it = inventory.find(node.hostName);

        if (it == inventory.end()) {
            std::cerr << "[Warning] No credentials found for node " << node.hostName << std::endl;
            continue;
        }

        const DeviceCredentials_t& creds = it->second;

        // Validate credentials
        if (creds.username.empty() || creds.password.empty()) {
            std::cerr << "[Warning] Incomplete credentials for node " << node.hostName << std::endl;
            continue;
        }

        // Create new session (smart pointer for automatic memory management)
        auto session = std::make_shared<common::NetconfSession>();

        // Using same user and password for all nodes for simplicity (can be extended later)
        if (session->connect(creds.ip, 830, creds.username, creds.password)) {
            // Add new session to the map
            sessions_[node.hostName] = session;
            atLeastOneConnected = true;
        } else {
            std::cerr << "[Error] Failed to connect to node " << node.hostName << " at " << creds.ip << std::endl;
        }
    }

    return atLeastOneConnected;
}

std::shared_ptr<common::NetconfSession> NetworkManager::getSession(const std::string& nodeName) {
    auto it = sessions_.find(nodeName);
    if (it != sessions_.end()) {
        return it->second;
    } else {
        std::cerr << "[Error] No session found for node " << nodeName << std::endl;
        return nullptr;
    }
}

void NetworkManager::fetchLldpData() {
    for (auto const& [name, session] : sessions_) {
        if (!session->isConnected()) {
            continue;
        }

        std::string lldpData = session->getData("/ieee802-dot1ab-lldp:lldp");

        if (lldpData.empty()) {
            // No LLDP data retrieved
            continue;
        } else {
            CncNode_t* currentNode = topology_.getNode(name);
            if (currentNode) {
                if (!cnc::LldpParser::parseLldpData(lldpData, *currentNode)) {
                    std::cerr << "[Error] Failed to parse LLDP data for node " << name << std::endl;
                }
            } else {
                std::cerr << "[Error] Node " << name << " not found in topology." << std::endl;
            }
        }
    }
}

void NetworkManager::fetchOperationGcl() {
    const std::string gclXPath =
        "/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port/"
        "ieee802-dot1q-sched-bridge:gate-parameter-table";

    for (auto const& [name, session] : sessions_) {
        if (!session->isConnected()) {
            continue;
        }

        // Fetch GCL data via NETCONF <get>
        std::string gclData = session->getData(gclXPath);

        if (gclData.empty()) {
            // No GCL data retrieved
            continue;
        }

        CncNode_t* node = topology_.getNode(name);
        if (!node) {
            continue;
        }

        if (cnc::GclParser::parseOperationalGclData(gclData, *node)) {
            // Successfully parsed and updated node's GCL data
        } else {
            std::cerr << "[Error] Failed to parse GCL data for node " << name << std::endl;
        }
    }
}

bool NetworkManager::deployConfigToNode(const std::string& nodeName) {
    auto session = getSession(nodeName);
    if (!session) {
        std::cerr << "[Error] Cannot deploy config. No session for node " << nodeName << std::endl;
        return false;
    }

    // 1. Search for node in topology to get its GCL
    const CncNode_t* targetNode = nullptr;
    for (const auto& node : topology_.nodes) {
        if (node.hostName == nodeName) {
            targetNode = &node;
            break;
        }
    }

    if (!targetNode) {
        std::cerr << "[Error] Node " << nodeName << " not found in topology." << std::endl;
        return false;
    }

    // 2. Construct XML configuration from GCL
    std::string configXml = buildGclXml(*targetNode);

    // Output for debugging
    // std::cout << "Generated XML:\n" << configXml << std::endl;

    // 3. Deploy configuration via NETCONF to candidate datastore
    if (!session->editData(configXml)) {
        std::cerr << "[Error] Failed to deploy configuration to node " << nodeName << std::endl;
        return false;
    }

    // 4. Commit the changes (from candidate to running)
    if (!session->commit()) {
        std::cerr << "[Error] Failed to commit configuration on node " << nodeName << std::endl;
        return false;
    }

    return true;
}

void NetworkManager::deployConfigToAll() {
    int successCount = 0;
    int failCount = 0;

    for (const auto& node : topology_.nodes) {
        // Check if session exists
        if (sessions_.find(node.hostName) != sessions_.end()) {
            if (deployConfigToNode(node.hostName)) {
                successCount++;
            } else {
                failCount++;
            }
        } else {
            std::cerr << "[Warning] No session for node " << node.hostName << ". Skipping deployment." << std::endl;
            failCount++;
        }
    }
}

std::string NetworkManager::buildGclXml(const CncNode_t& node) {
    return GclXmlBuilder::buildXmlForNode(node);
}
}  // namespace cnc