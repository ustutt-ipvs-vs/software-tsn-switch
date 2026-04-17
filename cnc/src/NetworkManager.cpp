#include "../include/NetworkManager.h"

#include <InterfaceParser.h>
#include <libyang/libyang.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <iostream>
#include <sstream>  // Needed for XML construction

#include "../include/GclParser.h"
#include "../include/GclXmlBuilder.h"
#include "../include/Inventory.h"
#include "../include/LldpParser.h"
#include "../include/PtpParser.h"
#include "PerformanceLogger.h"
#include "spdlog/spdlog.h"

namespace cnc {
NetworkManager::NetworkManager(Topology& topology) : topology_(topology) {
    // Constructor implementation (if needed)
}

NetworkManager::~NetworkManager() {
    for (auto& [name, worker] : nodeWorkers_) {
        {
            std::lock_guard<std::mutex> lock(worker->queueMutex);
            worker->stop = true;
        }
        worker->cv.notify_one();  // wake up the worker thread to exit
        if (worker->workerThread.joinable()) {
            worker->workerThread.join();
        }
    }
    nodeWorkers_.clear();
}

bool NetworkManager::executeOnNodeWorker(const std::string& nodeName,
                                         std::function<void(std::shared_ptr<common::NetconfSession>)> task) {
    auto it = nodeWorkers_.find(nodeName);
    if (it == nodeWorkers_.end()) {
        spdlog::error("[Error] No worker found for node {}", nodeName);
        return false;
    }

    auto worker = it->second;

    // Promise and Future, to make the calling gRPC-thread wait for the task to complete and get the result
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    {
        std::lock_guard<std::mutex> lock(worker->queueMutex);

        if (worker->jobQueue.size() >= worker->MAX_QUEUE_SIZE) {
            spdlog::warn("[Worker {}] Job queue is full. Rejecting new task.", nodeName);
            return false;
        }

        // Push task into the worker's job queue
        worker->jobQueue.push([task, promise, worker]() {
            try {
                task(worker->opsSession);
                promise->set_value();  // Signal that the task is done
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        });
    }

    worker->cv.notify_one();  // Wake up the worker thread to process the task

    try {
        future.get();  // Wait for the task to complete and surface exceptions
    } catch (const std::exception& e) {
        spdlog::error("[Worker {}] Task threw exception: {}", nodeName, e.what());
        return false;
    } catch (...) {
        spdlog::error("[Worker {}] Task threw unknown exception.", nodeName);
        return false;
    }
    return true;
}

bool NetworkManager::connectAllNodes(const InventoryMap& inventory) {
    bool atLeastOneConnected = false;

    for (const auto& [hostName, creds] : inventory) {
        // Validate credentials
        if (creds.username.empty() || creds.password.empty()) {
            spdlog::warn("Incomplete credentials for node {}", hostName);
            continue;
        }

        // Create two sessions (smart pointer for automatic memory management)
        auto opsSession = std::make_shared<common::NetconfSession>();
        auto notifSession = std::make_shared<common::NetconfSession>();

        // Using same user and password for all nodes for simplicity (can be extended later)
        if (opsSession->connect(creds.ip, 830, creds.username, creds.password) &&
            notifSession->connect(creds.ip, 830, creds.username, creds.password)) {
            auto worker = std::make_shared<NodeWorker>();
            worker->opsSession = opsSession;
            worker->notifSession = notifSession;

            worker->workerThread = std::thread([worker]() {
                while (true) {
                    std::function<void()> job;
                    {
                        std::unique_lock<std::mutex> lock(worker->queueMutex);
                        // Sleep until there is a job or we need to stop
                        worker->cv.wait(lock, [worker]() { return worker->stop || !worker->jobQueue.empty(); });

                        if (worker->stop && worker->jobQueue.empty()) {
                            return;  // Exit thread if stop is signaled and no jobs are left
                        }

                        job = std::move(worker->jobQueue.front());
                        worker->jobQueue.pop();
                    }
                    job();  // Execute the job
                }
            });

            // Setup lldp subscription on the notifSession (maybe move to a different place later)
            worker->notifSession->subscribe(
                "/ieee802-dot1ab-lldp:remote-table-change",
                [this, worker, hostName](
                    struct lyd_node* rawData) {  // for testing empty string (for production: /ieee802-dot1ab-lldp:lldp)
                    PERFORMANCE_LOGGING("[NetworkManager::subscribe]", "LLDP_PIPELINE");
                    if (rawData == nullptr) {
                        spdlog::error("[Notification] Received null data for node {}", hostName);
                        return;
                    }
                    // here lldp performance metric
                    spdlog::info("[LLDP Event] Neighbor change detected on node {}. Fetching fresh data...", hostName);

                    // Debug log for raw notification data (TODO: remove in production)
                    // char *str_out = nullptr;
                    // lyd_print_mem(&str_out, rawData, LYD_XML, LYD_PRINT_SIBLINGS);
                    // if (str_out) {
                    //    spdlog::info("!!! NOTIFICATION EMPFANGEN !!!\n{}", str_out);
                    //    free(str_out);
                    //}

                    PERFORMANCE_LOGGING("[NetworkManager::fetchLldpDataForNode]", "LLDP_PIPELINE");
                    this->fetchLldpDataForNode(hostName);
                    PERFORMANCE_LOGGING("[NetworkManager::finished]", "LLDP_PIPELINE");
                });

            worker->notifSession->startNotificationListener();

            nodeWorkers_[hostName] = worker;
            atLeastOneConnected = true;

            bool nodeExists = false;
            for (auto& existingNode : topology_.nodes) {
                if (existingNode.hostName == hostName) {
                    existingNode.ipAddress = creds.ip;
                    nodeExists = true;
                    break;
                }
            }

            if (!nodeExists) {
                CncNode_t newNode;
                newNode.hostName = hostName;
                newNode.ipAddress = creds.ip;
                topology_.nodes.push_back(newNode);
                spdlog::info("Connected to node {} at {}", hostName, creds.ip);
            }

        } else {
            spdlog::error("Failed to connect to node {} at {}", hostName, creds.ip);
        }
    }
    topology_.buildIndex();  // Rebuild index after adding nodes

    return atLeastOneConnected;
}

std::shared_ptr<common::NetconfSession> NetworkManager::getSession(const std::string& nodeName) {
    auto it = nodeWorkers_.find(nodeName);
    if (it != nodeWorkers_.end()) {
        return it->second->opsSession;
    }
    spdlog::error("[Error] No session found for node {}", nodeName);
    return nullptr;
}

bool NetworkManager::fetchLldpDataForNode(const std::string& nodeName) {
    PERFORMANCE_LOGGING("[NetworkManager::fetchLldpDataForNode]", "Start LLDP data fetch for node: " + nodeName);
    return executeOnNodeWorker(nodeName, [this, nodeName](std::shared_ptr<common::NetconfSession> session) {
        struct lyd_node* lldpNode = session->getData("/ieee802-dot1ab-lldp:lldp");
        if (lldpNode != nullptr) {
            CncNode_t* currentNode = topology_.getNode(nodeName);
            if (currentNode != nullptr) {
                if (!cnc::LldpParser::parseLldpData(lldpNode, *currentNode)) {
                    spdlog::error("[Error] Failed to parse LLDP data for node {}", nodeName);
                }
            } else {
                spdlog::error("[Error] Node {} not found in topology.", nodeName);
            }
            lyd_free_all(lldpNode);
        }
    });
    PERFORMANCE_LOGGING("[NetworkManager::fetchLldpDataForNode]", "Stop LLDP data fetch for node: " + nodeName);
}

bool NetworkManager::fetchPtpDataForNode(const std::string& nodeName) {
    PERFORMANCE_LOGGING("[NetworkManager::fetchPtpDataForNode]", "Start PTP data fetch for node: " + nodeName);
    return executeOnNodeWorker(nodeName, [this, nodeName](std::shared_ptr<common::NetconfSession> session) {
        struct lyd_node* ptpNodeTree = session->getData("/ieee1588-ptp-tt:ptp");
        if (ptpNodeTree != nullptr) {
            CncNode_t* node = topology_.getNode(nodeName);
            if (node != nullptr) {
                if (!cnc::PtpParser::parseOperationalPtpData(ptpNodeTree, *node)) {
                    spdlog::error("[Error] Failed to parse PTP data for node {}", nodeName);
                }
            } else {
                spdlog::error("[Error] Node {} not found in topology.", nodeName);
            }
            lyd_free_all(ptpNodeTree);
        }
    });
    PERFORMANCE_LOGGING("[NetworkManager::fetchPtpDataForNode]", "Stop PTP data fetch for node: " + nodeName);
}

bool NetworkManager::fetchOperationGclForNode(const std::string& nodeName) {
    PERFORMANCE_LOGGING("[NetworkManager::fetchOperationGclForNode]",
                        "Start operational GCL fetch for node: " + nodeName);
    return executeOnNodeWorker(nodeName, [this, nodeName](std::shared_ptr<common::NetconfSession> session) {
        struct lyd_node* gclNodeTree = session->getData(
            "/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port/"
            "ieee802-dot1q-sched-bridge:gate-parameter-table");

        if (gclNodeTree != nullptr) {
            CncNode_t* node = topology_.getNode(nodeName);
            if (node != nullptr) {
                if (!cnc::GclParser::parseOperationalGclData(gclNodeTree, *node)) {
                    spdlog::error("[Error] Failed to parse GCL data for node {}", nodeName);
                }
            } else {
                spdlog::error("[Error] Node {} not found in topology.", nodeName);
            }
            lyd_free_all(gclNodeTree);
        }
    });
    PERFORMANCE_LOGGING("[NetworkManager::fetchOperationGclForNode]",
                        "Stop operational GCL fetch for node: " + nodeName);
}

void NetworkManager::fetchLldpData() {
    PERFORMANCE_LOGGING("[NetworkManager::fetchLldpData]", "Start LLDP data fetch");
    for (auto const& [name, worker] : nodeWorkers_) {
        if (worker->opsSession->isConnected()) {
            fetchLldpDataForNode(name);
        }
    }
    PERFORMANCE_LOGGING("[NetworkManager::fetchLldpData]", "Stop LLDP data fetch");
}

void NetworkManager::fetchOperationGcl() {
    PERFORMANCE_LOGGING("[NetworkManager::fetchOperationGcl]", "Start operational GCL fetch");
    for (auto const& [name, worker] : nodeWorkers_) {
        if (worker->opsSession->isConnected()) {
            fetchOperationGclForNode(name);
        }
    }
    PERFORMANCE_LOGGING("[NetworkManager::fetchOperationGcl]", "Stop operational GCL fetch");
}

void NetworkManager::fetchPtpData() {
    PERFORMANCE_LOGGING("[NetworkManager::fetchPtpData]", "Start PTP data fetch");
    for (auto const& [name, worker] : nodeWorkers_) {
        if (worker->opsSession->isConnected()) {
            fetchPtpDataForNode(name);
        }
    }
    PERFORMANCE_LOGGING("[NetworkManager::fetchPtpData]", "Stop PTP data fetch");
}

void NetworkManager::fetchInterfaces() {
    PERFORMANCE_LOGGING("[NetworkManager::fetchInterfaces]", "Start interface discovery");
    for (auto const& [name, worker] : nodeWorkers_) {
        if (!worker->opsSession->isConnected()) {
            continue;
        }

        executeOnNodeWorker(name, [this, name](std::shared_ptr<common::NetconfSession> session) {
            struct lyd_node* ifaceNodeTree = session->getData("/ietf-interfaces:interfaces/interface");
            if (ifaceNodeTree != nullptr) {
                CncNode_t* node = topology_.getNode(name);
                if (node != nullptr) {
                    if (!cnc::InterfaceParser::parseInterface(ifaceNodeTree, *node)) {
                        spdlog::error("[Error] Failed to parse interface data for node {}", name);
                    }
                } else {
                    spdlog::error("[Error] Node {} not found in topology.", name);
                }
                lyd_free_all(ifaceNodeTree);
            }
        });
    }
    PERFORMANCE_LOGGING("[NetworkManager::fetchInterfaces]", "Stop interface discovery");
}

void NetworkManager::discoverNetwork() {
    PERFORMANCE_LOGGING("[NetworkManager::discoverNetwork]", "Start network discovery");
    spdlog::info("Starting network discovery...");

    fetchInterfaces();
    fetchLldpData();
    // fetchPtpData(); // removed only for testing, because tsnctld crashes on fetchPtp
    fetchOperationGcl();

    spdlog::info("Network discovery completed.");
    PERFORMANCE_LOGGING("[NetworkManager::discoverNetwork]", "Stop network discovery");
}

bool NetworkManager::deployInterfaceConfig(const std::string& nodeName, const std::string& ifaceName) {
    PERFORMANCE_LOGGING("[NetworkManager::deployInterfaceConfig]",
                        "Start interface config deployment for interface " + ifaceName + " on node " + nodeName);
    const CncNode_t* targetNode = topology_.getNode(nodeName);
    if (!targetNode) return false;

    auto it = std::find_if(targetNode->interfaces.begin(), targetNode->interfaces.end(),
                           [&](const ietfInterface_t& iface) { return iface.name == ifaceName; });
    if (it == targetNode->interfaces.end()) return false;

    std::string configXml = GclXmlBuilder::buildXmlForInterface(*it);
    bool success = false;

    spdlog::critical("!!! GENERATED CONFIG XML FOR INTERFACE {} !!!\n{}", ifaceName, configXml);

    // WICHTIG: Hier workerSession statt opsSession, wie in deiner Execute-Logik
    bool executed = executeOnNodeWorker(
        nodeName, [&success, configXml, nodeName](std::shared_ptr<common::NetconfSession> workerSession) {
            if (!workerSession->editData(configXml)) return;
            if (!workerSession->commit()) return;
            success = true;
        });
    PERFORMANCE_LOGGING("[NetworkManager::deployInterfaceConfig]",
                        "Finished interface config deployment for interface " + ifaceName + " on node " + nodeName);
    return executed && success;
}

bool NetworkManager::deployConfigToNode(const std::string& nodeName) {
    PERFORMANCE_LOGGING("[NetworkManager::deployConfigToNode]", "Start config deployment for node: " + nodeName);
    auto session = getSession(nodeName);
    if (!session || !session->isConnected()) {
        spdlog::error("[Error] Cannot deploy config. No session for node {}", nodeName);
        return false;
    }

    // 1. Search for node in topology to get its GCL
    const CncNode_t* targetNode = topology_.getNode(nodeName);

    if (targetNode == nullptr) {
        spdlog::error("[Error] Node {} not found in topology.", nodeName);
        return false;
    }

    // 2. Filter out all interfaces
    CncNode_t filteredNode = *targetNode;
    filteredNode.interfaces.erase(
        std::remove_if(filteredNode.interfaces.begin(), filteredNode.interfaces.end(),
                       [](const ietfInterface_t& iface) { return !iface.bridgePort.gateParameterTable.configChange; }),
        filteredNode.interfaces.end());

    // 3. Construct XML configuration from GCL
    std::string configXml = buildGclXml(filteredNode);

    // Output for debugging
    spdlog::critical("!!! GENERATED CONFIG XML FOR NODE {} !!!\n{}", nodeName, configXml);

    bool success = false;

    // 4. Deploy configuration via NETCONF to candidate datastore
    bool executed = executeOnNodeWorker(
        nodeName, [&success, configXml, nodeName](std::shared_ptr<common::NetconfSession> workerSession) {
            // 4. Edit data in candidate datastore
            if (!workerSession->editData(configXml)) {
                spdlog::error("[Error] Failed to edit config for node {}", nodeName);
                return;
            }

            // 6. Commit the configuration to running datastore
            if (!workerSession->commit()) {
                spdlog::error("[Error] Failed to commit config for node {}", nodeName);
                return;
            }

            PERFORMANCE_LOGGING("[NetworkManager::deployConfigToNode]",
                                "Successfully deployed config for node: " + nodeName);
            success = true;
        });

    // happens if queue was full or commit failed to execute
    if (!executed) {
        spdlog::error("[Error] Failed to execute deployment task for node {}", nodeName);
        return false;
    }

    return executed && success;
}

void NetworkManager::deployConfigToAll() {
    int successCount = 0;
    int failCount = 0;

    for (const auto& node : topology_.nodes) {
        // Check if session exists
        if (nodeWorkers_.find(node.hostName) != nodeWorkers_.end()) {
            if (deployConfigToNode(node.hostName)) {
                successCount++;
            } else {
                failCount++;
            }
        } else {
            spdlog::error("[Warning] No session for node {}. Skipping deployment.", node.hostName);
            failCount++;
        }
    }
}

std::string NetworkManager::buildGclXml(const CncNode_t& node) {
    return GclXmlBuilder::buildXmlForNode(node);
}
}  // namespace cnc