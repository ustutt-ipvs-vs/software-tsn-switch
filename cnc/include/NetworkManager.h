#pragma once

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "CncTypes.h"
#include "Inventory.h"
#include "NetconfSession.h"
#include "Topology.h"

namespace cnc {

/**
 * @brief Worker structure for managing NETCONF sessions and job queues for individual nodes.
 */
struct NodeWorker {
    std::shared_ptr<common::NetconfSession> opsSession;
    std::shared_ptr<common::NetconfSession> notifSession;

    std::thread workerThread;
    std::queue<std::function<void()>> jobQueue;
    std::mutex queueMutex;
    std::condition_variable cv;
    bool stop = false;

    const size_t MAX_QUEUE_SIZE = 10;  // Load shredder: Max 10 pending jobs per node
};

/**
 * @brief Coordinates NETCONF-based communication and TSN configuration workflows across topology nodes.
 *
 * This class manages per-node NETCONF sessions, retrieves operational LLDP and GCL data, and applies
 * generated TSN/GCL configuration back to devices. It serves as the orchestration layer between
 * topology state, inventory credentials, XML builders/parsers, and device-facing NETCONF operations.
 */
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
     * @param inventory The inventory map containing device credentials (hostname to DeviceCredentials_t).
     * @return true if all nodes were connected successfully, false otherwise.
     */
    bool connectAllNodes(const InventoryMap& inventory);

    /**
     * @brief Fetches LLDP data from all connected nodes and updates the topology.
     * Reads LLDP data via /ietf-lldp:lldp-entries via Netconf <get>.
     */
    void fetchLldpData();

    /**
     * @brief Fetches operational GCL data from all connected nodes and updates their configurations.
     * Reads GCL data via /interfaces/interface/bridge-port/gate-parameter-table via Netconf <get>.
     */
    void fetchOperationGcl();

    /**
     * @brief Fetches interface data from all connected nodes and updates the topology.
     * Reads interface data via /ietf-interfaces:interfaces/interface via Netconf <get>.
     */
    void fetchInterfaces();

    /**
     * @brief Fetches PTP data from all connected nodes and updates the topology.
     * Reads PTP data via /ieee1588-ptp:ptp-data via Netconf <get>.
     */
    void fetchPtpData();

    /**
     * @brief Discovers the network topology and populating the Topology structure.
     */
    void discoverNetwork();

    /**
     * @brief Fetches LLDP data for a specific node and updates the topology.
     * @param nodeName The hostname of the node to fetch LLDP data from.
     * @return true if the LLDP data was successfully fetched and parsed, false otherwise.
     */
    bool fetchLldpDataForNode(const std::string& nodeName);

    /**
     * @brief Fetches operational GCL data for a specific node and updates the topology.
     * @param nodeName The hostname of the node to fetch GCL data from.
     * @return true if the GCL data was successfully fetched and parsed, false otherwise.
     */
    bool fetchOperationGclForNode(const std::string& nodeName);

    /**
     * @brief Fetches PTP data for a specific node and updates the topology.
     * @param nodeName The hostname of the node to fetch PTP data from.
     * @return true if the PTP data was successfully fetched and parsed, false otherwise.
     */
    bool fetchPtpDataForNode(const std::string& nodeName);

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
     * @brief Configures a specific interface on a node with its TSN parameters.
     * 1. Transforms the interface's TSN parameters into appropriate XML configuration.
     * 2. Sends configuration via Netconf <edit-config> to the target node (candidate datastore).
     * 3. Commits the configuration to the running datastore.
     * @param nodeName The hostname of the node containing the interface to configure.
     * @param ifaceName The name of the interface to configure.
     * @return true if the configuration was successfully deployed, false otherwise.
     */
    bool deployInterfaceConfig(const std::string& nodeName, const std::string& ifaceName);

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

    // Active NodeWorkers keyed by node hostname.
    // Use shared_ptr to manage session lifetimes automatically. (no dangling pointers)
    // std::map<std::string, std::shared_ptr<common::NetconfSession>> sessions_;
    std::map<std::string, std::shared_ptr<NodeWorker>> nodeWorkers_;

    bool executeOnNodeWorker(const std::string& nodeName,
                             std::function<void(std::shared_ptr<common::NetconfSession>)> task);

    /**
     * @brief Builds the XML configuration for GCL based on the node's TSN parameters.
     * @param node The CncNode_t containing TSN parameters.
     * @return The XML string representing the GCL configuration.
     */
    static std::string buildGclXml(const CncNode_t& node);

    std::string buildInterfaceGclXml(const ietfInterface_t& iface);
};
}  // namespace cnc