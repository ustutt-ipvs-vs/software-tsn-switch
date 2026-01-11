#include "../include/NetworkManager.h"
#include "../include/GclXmlBuilder.h"
#include <iostream>
#include <sstream> // Needed for XML construction

namespace cnc {
    NetworkManager::NetworkManager(Topology& topology) : topology_(topology) {
        // Constructor implementation (if needed)
    }

    NetworkManager::~NetworkManager() {
        // shared_ptr will automatically clean up the session
        sessions_.clear();
    }

    bool NetworkManager::connectAllNodes(const std::string& username, const std::string& password) {
        bool atLeastOneConnected = false;

        for (const auto& node : topology_.nodes) {
            // Check if ip and port exist
            if (node.ipAddress.empty()) {
                std::cerr << "[Warning] Node " << node.hostName << " is missing IP address." << std::endl;
                continue;
            }

            
            // Create new session (smart pointer for automatic memory management)
            auto session = std::make_shared<common::NetconfSession>();

            // Using same user and password for all nodes for simplicity (can be extended later)
            if (session->connect(node.ipAddress, 830, username, password)) {
                // Add new session to the map
                sessions_[node.hostName] = session;
                atLeastOneConnected = true;
            } else {
                std::cerr << "[Error] Failed to connect to node " << node.hostName << " at " << node.ipAddress << std::endl;
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
            if (!session->isConnected()) continue;

            std::string lldpData = session->getData("ietfs-lldp:lldp");

            if (lldpData.empty()) {
                // No LLDP data retrieved
                continue;
            } else {
                // TODO: Parsing LLDP data (assuming XML format) 
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
}