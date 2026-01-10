#pragma once

#include <string>
#include <map>
#include <memory>
#include <vector>

#include "NetconfSession.h"
#include "Topology.h"
#include "CncTypes.h"

namespace cnc {
    class NetworkManager {
        public:
            /**
             * @brief Constructs a NetworkManager with the given topology reference.
             * @param topology Reference to the Topology object managing network nodes.
             */
            NetworkManager(Topology& topology);

            /**
             * @brief Destructor for NetworkManager. Disconnects all active sessions.
             */
            ~NetworkManager();

            /**
             * @brief Connects to all nodes in the topology using the provided credentials.
             * @param username The username for authentication.
             * @param password The password for authentication.
             * @return true if all nodes were connected successfully, false otherwise.
             */
            bool connectAllNodes(const std::string& username, const std::string& password);

            /**
             * @brief Fetches LLDP data from all connected nodes and updates the topology.
             * Reads LLDP data via /ietf-lldp:lldp-entries via Netconf <get>.
             */
            void fetchLldpData();

            /**
             * @brief Configures specific nodes with TSN parameters.
             * 1. Transforms CncNode_t data into appropriate XML configuration.
             * 2. Sends configuration via Netconf <edit-config> to the target nodes (candidate datastore).
             * 3. Commits the configuration to the running datastore.
             * @param nodeName The hostname of the node to configure.
             * @return true if the configuration was successfully deployed, false otherwise.
             */
            bool deployConfigToNode(const std::string& nodeName);

            /**
             * @brief Configures all nodes in the topology with their respective TSN parameters.
             */
            void deployConfigToAll();

            /**
             * @brief Retrieves the Netconf session for a given node by its hostname.
             * @param nodeName The hostname of the node.
             * @return Shared pointer to the NetconfSession if found, nullptr otherwise.
             */
            std::shared_ptr<common::NetconfSession> getSession(const std::string& nodeName);

        private:
            Topology& topology_;  // Reference to the topology managing network nodes.

            // Active Netconf sessions keyed by node hostname.
            // Use shared_ptr to manage session lifetimes automatically. (no dangling pointers)
            std::map<std::string, std::shared_ptr<common::NetconfSession>> sessions_; 

            /**
             * @brief Builds the XML configuration for GCL based on the node's TSN parameters.
             * @param node The CncNode_t containing TSN parameters.
             * @return The XML string representing the GCL configuration.
             */
            std::string buildGclXml(const CncNode_t& node);
    };
}