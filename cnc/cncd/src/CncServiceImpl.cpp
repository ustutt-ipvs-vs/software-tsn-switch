#include "CncServiceImpl.h"

#include <filesystem>
#include <stdexcept>

#include "JsonImporter.h"
#include "PerformanceLogger.h"
#include "spdlog/spdlog.h"

CncServiceImpl::CncServiceImpl(const std::string& inventoryFilePath) {
    spdlog::info("Initializing CncServiceImpl with inventory file: {}", inventoryFilePath);

    if (!std::filesystem::exists(inventoryFilePath)) {
        spdlog::error("Inventory file not found: {}", inventoryFilePath);
        throw std::runtime_error("Inventory file not found");
    }

    spdlog::info("Importing inventory from {}...", inventoryFilePath);
    auto inventoryMap = cnc::JsonImporter::importInventory(inventoryFilePath);
    if (inventoryMap.empty()) {
        spdlog::error("Failed to import inventory from {}", inventoryFilePath);
        throw std::runtime_error("Failed to import inventory");
    }

    spdlog::info("Inventory imported successfully from {}", inventoryFilePath);

    networkManager = std::make_unique<cnc::NetworkManager>(topology);

    spdlog::info("Connecting to all nodes in the inventory...");
    bool connected = networkManager->connectAllNodes(inventoryMap);

    if (!connected) {
        spdlog::error("Failed to connect to all nodes in the inventory.");
        throw std::runtime_error("Failed to connect to all nodes");
    }

    networkManager->discoverNetwork();

    ptpTimerThread = std::thread(&CncServiceImpl::ptpBackgroundTask, this);

    spdlog::info("Successfully connected and discovered all nodes in the inventory. gRPC ready to receive...");
}

CncServiceImpl::~CncServiceImpl() {
    {
        std::lock_guard<std::mutex> lock(timerMutex);
        stopTimer = true;
    }

    timerCV.notify_all();

    if (ptpTimerThread.joinable()) {
        ptpTimerThread.join();
    }
}

void CncServiceImpl::ptpBackgroundTask() {
    while (!stopTimer) {
        std::unique_lock<std::mutex> cvLock(timerMutex);

        // sleep for 15 minutes or until notified to stop
        if (timerCV.wait_for(cvLock, std::chrono::minutes(15), [this] { return stopTimer.load(); })) {
            break;  // Der Server fährt runter, brich die Schleife ab!
        }
        cvLock.unlock();  // unlock before doing the work

        spdlog::info("[Background] 15 minutes passed. Fetching fresh PTP data...");

        // IMPORTANT: create write lock
        std::unique_lock<std::shared_mutex> writeLock(topologyMutex);
        networkManager->fetchPtpData();
    }
}

// Helper function to find a node by hostname in the topology
CncNode_t* CncServiceImpl::FindNode(const std::string& hostName) {
    if (hostName.empty()) return nullptr;
    return topology.getNode(hostName);  // Nutzt deine schnelle Map-Suche
}

// Helper function to map internal Ptp port data to protobuf message
void CncServiceImpl::MapPtpPort(const PtpPort_t& port, cnc::rpc::PtpPort* protoPort) {
    protoPort->set_port_index(port.portIndex);
    protoPort->set_underlying_interface(port.underlyingInterface);
    protoPort->mutable_port_ds()->set_port_state(static_cast<cnc::rpc::PtpPortState>(port.portDs.portState));
    protoPort->mutable_port_ds()->set_mean_link_delay(port.portDs.meanLinkDelay);
}

// Helper function to map internal Lldp port data to protobuf message
void CncServiceImpl::MapLldpPort(const LldpPort_t& port, cnc::rpc::LldpPort* protoPort) {
    protoPort->set_name(port.name);
    protoPort->set_dest_mac_address(port.destMacAddress);
    protoPort->set_admin_status(port.adminStatus);

    for (const auto& neighbor : port.neighbors) {
        auto* protoNeighbor = protoPort->add_neighbors();
        protoNeighbor->set_chassis_id(neighbor.chassisId);
        protoNeighbor->set_port_id(neighbor.portId);
        protoNeighbor->set_system_name(neighbor.systemName);
    }
}

// Helper function to map internal CncNode_t to cnc::rpc::Node protobuf message
void CncServiceImpl::MapInterface(const ietfInterface_t& iface, cnc::rpc::IetfInterface* protoIface) {
    protoIface->set_ifindex(iface.ifindex);
    protoIface->set_name(iface.name);
    protoIface->set_last_link_update_id(iface.lastLinkUpdateId);
    protoIface->set_last_qdisc_update_id(iface.lastQdiscUpdateId);
    protoIface->set_admin_enabled(iface.adminEnabled);
    protoIface->set_mtu(iface.mtu);
    protoIface->set_num_tx_queues(iface.numTxQueues);
    protoIface->set_num_active_tx_queues(iface.numActiveTxQueues);
    protoIface->set_speed(iface.speed);

    protoIface->set_has_phys_addr(iface.hasPhysAddr);
    if (iface.hasPhysAddr) {
        protoIface->set_phys_address(std::string(iface.physAddress.begin(), iface.physAddress.end()));
    }

    switch (iface.type) {
        case IfType::ETHERNET:
            protoIface->set_type(cnc::rpc::IF_TYPE_ETHERNET);
            break;
        case IfType::BRIDGE:
            protoIface->set_type(cnc::rpc::IF_TYPE_BRIDGE);
            break;
        case IfType::LAG:
            protoIface->set_type(cnc::rpc::IF_TYPE_LAG);
            break;
        case IfType::LOOPBACK:
            protoIface->set_type(cnc::rpc::IF_TYPE_LOOPBACK);
            break;
    }

    switch (iface.operStatus) {
        case OperStatus::UP:
            protoIface->set_oper_status(cnc::rpc::OPER_STATUS_UP);
            break;
        case OperStatus::DOWN:
            protoIface->set_oper_status(cnc::rpc::OPER_STATUS_DOWN);
            break;
        case OperStatus::TESTING:
            protoIface->set_oper_status(cnc::rpc::OPER_STATUS_TESTING);
            break;
        case OperStatus::DORMANT:
            protoIface->set_oper_status(cnc::rpc::OPER_STATUS_DORMANT);
            break;
        case OperStatus::NOT_PRESENT:
            protoIface->set_oper_status(cnc::rpc::OPER_STATUS_NOT_PRESENT);
            break;
        case OperStatus::LOWER_LAYER_DOWN:
            protoIface->set_oper_status(cnc::rpc::OPER_STATUS_LOWER_LAYER_DOWN);
            break;
        default:
            protoIface->set_oper_status(cnc::rpc::OPER_STATUS_UNKNOWN);
            break;
    }

    // Bridge Port Mapping
    auto* protoBridge = protoIface->mutable_bridge_port();
    protoBridge->set_bridge_name(iface.bridgePort.bridgeName);
    protoBridge->set_master_index(iface.bridgePort.masterIndex);

    // GCL Mapping
    const auto& internalGcl = iface.bridgePort.gateParameterTable;
    auto* protoGcl = protoBridge->mutable_gate_parameter_table();
    protoGcl->set_oper_data_set(internalGcl.operDataSet);
    protoGcl->set_admin_data_set(internalGcl.adminDataSet);

    if (internalGcl.operDataSet) {
        protoGcl->set_oper_gate_states(internalGcl.operGateStates);

        protoGcl->mutable_oper_cycle_time()->set_numerator(internalGcl.operCycleTime.numerator);
        protoGcl->mutable_oper_cycle_time()->set_denominator(internalGcl.operCycleTime.denominator);

        protoGcl->mutable_oper_base_time()->set_seconds(internalGcl.operBaseTime.seconds);
        protoGcl->mutable_oper_base_time()->set_nanoseconds(internalGcl.operBaseTime.nanoseconds);

        for (const auto& entry : internalGcl.operControlList) {
            auto* protoEntry = protoGcl->add_oper_control_list();
            protoEntry->set_index(entry.index);
            protoEntry->set_gate_states_value(entry.gateStatesValue);
            protoEntry->set_time_interval_value(entry.timeIntervalValue);
            protoEntry->set_operation_name(entry.operationName);
        }
    }

    if (internalGcl.adminDataSet) {
        protoGcl->set_admin_gate_states(internalGcl.adminGateStates);

        protoGcl->mutable_admin_cycle_time()->set_numerator(internalGcl.adminCycleTime.numerator);
        protoGcl->mutable_admin_cycle_time()->set_denominator(internalGcl.adminCycleTime.denominator);

        protoGcl->mutable_admin_base_time()->set_seconds(internalGcl.adminBaseTime.seconds);
        protoGcl->mutable_admin_base_time()->set_nanoseconds(internalGcl.adminBaseTime.nanoseconds);

        for (const auto& entry : internalGcl.adminControlList) {
            auto* protoEntry = protoGcl->add_admin_control_list();
            protoEntry->set_index(entry.index);
            protoEntry->set_gate_states_value(entry.gateStatesValue);
            protoEntry->set_time_interval_value(entry.timeIntervalValue);
            protoEntry->set_operation_name(entry.operationName);
        }
    }
}

void CncServiceImpl::MapPtpNode(const PtpNode_t& ptp, cnc::rpc::PtpNode* protoPtp) {
    auto* defaultDs = protoPtp->mutable_default_ds();
    defaultDs->set_num_ports(ptp.defaultDs.numPorts);
    defaultDs->set_priority1(ptp.defaultDs.priority1);
    defaultDs->set_clock_identity(ptp.defaultDs.clockIdentity);
    defaultDs->set_instance_type(static_cast<cnc::rpc::PtpInstanceType>(ptp.defaultDs.instanceType));

    auto* currentDs = protoPtp->mutable_current_ds();
    currentDs->set_steps_removed(ptp.currentDs.stepsRemoved);
    currentDs->set_mean_delay(ptp.currentDs.meanDelay);
    currentDs->set_offset_from_time_transmitter(ptp.currentDs.offsetFromTimeTransmitter);

    auto* parentDs = protoPtp->mutable_parent_ds();
    parentDs->set_parent_clock_identity(ptp.parentDs.parentClockIdentity);
    parentDs->set_parent_port_number(ptp.parentDs.parentPortNumber);
    parentDs->set_grand_parent_clock_identity(ptp.parentDs.grandParentClockIdentity);

    // Usage of port helper function for ptp
    for (const auto& port : ptp.ports) {
        MapPtpPort(port, protoPtp->add_ports());
    }
}

void CncServiceImpl::MapLldpNode(const LldpNode_t& lldp, cnc::rpc::LldpNode* protoLldp) {
    protoLldp->set_message_tx_interval(lldp.messageTxInterval);

    auto* localSys = protoLldp->mutable_local_system_data();
    localSys->set_system_name(lldp.localSystemData.systemName);
    localSys->set_chassis_id(lldp.localSystemData.chassisId);

    // Usage of port helper function for lldp
    for (const auto& port : lldp.ports) {
        MapLldpPort(port, protoLldp->add_ports());
    }
}

// 1. return the entire network topology (nodes + interfaces + LLDP/PTP data) in one call (with refreshed data from the
// network)
grpc::Status CncServiceImpl::GetNetworkState(grpc::ServerContext* context, const cnc::rpc::EmptyRequest* request,
                                             cnc::rpc::Topology* response) {
    PERFORMANCE_LOGGING("[CncServiceImpl::GetNetworkState]", "Start full topology fetch");

    // create write lock
    std::unique_lock<std::shared_mutex> writeLock(topologyMutex);

    // Getting fresh data from the network before responding
    networkManager->discoverNetwork();
    for (const auto& internalNode : topology.nodes) {
        auto* protoNode = response->add_nodes();
        protoNode->set_id(internalNode.id);
        protoNode->set_host_name(internalNode.hostName);
        protoNode->set_ip_address(internalNode.ipAddress);

        for (const auto& iface : internalNode.interfaces) {
            auto* protoIface = protoNode->add_interfaces();
            MapInterface(iface, protoIface);
        }

        MapPtpNode(internalNode.ptpAllData, protoNode->mutable_ptp_all_data());
        MapLldpNode(internalNode.lldpAllData, protoNode->mutable_lldp_all_data());
    }
    spdlog::info("[gRPC] GetNetworkState called. Returned full topology.");
    PERFORMANCE_LOGGING("[CncServiceImpl::GetNetworkState]", "Stop full topology fetch");
    return grpc::Status::OK;
}

// 2. global feature data (LLDP/PTP) across all nodes/interfaces
grpc::Status CncServiceImpl::GetAllPtpData(grpc::ServerContext* context, const cnc::rpc::EmptyRequest* request,
                                           cnc::rpc::AllPtpDataResponse* response) {
    // create read lock
    std::shared_lock<std::shared_mutex> readLock(topologyMutex);

    for (const auto& internalNode : topology.nodes) {
        auto* protoNodePtp = response->add_nodes();
        protoNodePtp->set_host_name(internalNode.hostName);

        MapPtpNode(internalNode.ptpAllData, protoNodePtp->mutable_ptp_data());
    }
    return grpc::Status::OK;
}

grpc::Status CncServiceImpl::GetAllLldpData(grpc::ServerContext* context, const cnc::rpc::EmptyRequest* request,
                                            cnc::rpc::AllLldpDataResponse* response) {
    // create read lock
    std::shared_lock<std::shared_mutex> readLock(topologyMutex);

    for (const auto& internalNode : topology.nodes) {
        auto* protoNodeLldp = response->add_nodes();
        protoNodeLldp->set_host_name(internalNode.hostName);

        MapLldpNode(internalNode.lldpAllData, protoNodeLldp->mutable_lldp_data());
    }
    return grpc::Status::OK;
}

// 3. node-specific data (LLDP/PTP) for a given node identified by hostname
grpc::Status CncServiceImpl::GetNodePtpData(grpc::ServerContext* context, const cnc::rpc::NodeRequest* request,
                                            cnc::rpc::PtpNode* response) {
    // create read lock
    std::shared_lock<std::shared_mutex> readLock(topologyMutex);

    CncNode_t* node = FindNode(request->host_name());
    if (!node) return grpc::Status(grpc::StatusCode::NOT_FOUND, "Node not found");

    MapPtpNode(node->ptpAllData, response);
    return grpc::Status::OK;
}

grpc::Status CncServiceImpl::GetNodeLldpData(grpc::ServerContext* context, const cnc::rpc::NodeRequest* request,
                                             cnc::rpc::LldpNode* response) {
    // create read lock
    std::shared_lock<std::shared_mutex> readLock(topologyMutex);

    CncNode_t* node = FindNode(request->host_name());
    if (!node) return grpc::Status(grpc::StatusCode::NOT_FOUND, "Node not found");

    MapLldpNode(node->lldpAllData, response);
    return grpc::Status::OK;
}

// 4. interface-specific data (LLDP/PTP) for a given interface identified by node hostname + interface name
grpc::Status CncServiceImpl::GetInterfacePtpData(grpc::ServerContext* context,
                                                 const cnc::rpc::InterfaceRequest* request,
                                                 cnc::rpc::PtpPort* response) {
    // create read lock
    std::shared_lock<std::shared_mutex> readLock(topologyMutex);
    CncNode_t* node = FindNode(request->host_name());
    if (!node) return grpc::Status(grpc::StatusCode::NOT_FOUND, "Node not found");

    for (const auto& port : node->ptpAllData.ports) {
        if (port.underlyingInterface == request->interface_name()) {
            MapPtpPort(port, response);
            return grpc::Status::OK;
        }
    }
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "PTP Interface not found");
}

grpc::Status CncServiceImpl::GetInterfaceLldpData(grpc::ServerContext* context,
                                                  const cnc::rpc::InterfaceRequest* request,
                                                  cnc::rpc::LldpPort* response) {
    // create read lock
    std::shared_lock<std::shared_mutex> readLock(topologyMutex);

    CncNode_t* node = FindNode(request->host_name());
    if (!node) return grpc::Status(grpc::StatusCode::NOT_FOUND, "Node not found");

    for (const auto& port : node->lldpAllData.ports) {
        if (port.name == request->interface_name()) {
            MapLldpPort(port, response);
            return grpc::Status::OK;
        }
    }
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "LLDP Interface not found");
}

// 5. gcl-data for a given interface identified by node hostname + interface name
grpc::Status CncServiceImpl::GetInterfaceGcl(grpc::ServerContext* context, const cnc::rpc::InterfaceRequest* request,
                                             cnc::rpc::GclConfig* response) {
    // create read lock
    std::shared_lock<std::shared_mutex> readLock(topologyMutex);

    CncNode_t* node = FindNode(request->host_name());
    if (!node) return grpc::Status(grpc::StatusCode::NOT_FOUND, "Node not found");

    for (const auto& iface : node->interfaces) {
        if (iface.name == request->interface_name()) {
            const auto& gcl = iface.bridgePort.gateParameterTable;
            response->set_oper_data_set(gcl.operDataSet);
            response->set_admin_data_set(gcl.adminDataSet);

            // Map Oper List
            if (gcl.operDataSet) {
                response->set_oper_gate_states(gcl.operGateStates);
                for (const auto& entry : gcl.operControlList) {
                    auto* protoEntry = response->add_oper_control_list();
                    protoEntry->set_index(entry.index);
                    protoEntry->set_gate_states_value(entry.gateStatesValue);
                    protoEntry->set_time_interval_value(entry.timeIntervalValue);
                }
            }

            // Map Admin List
            if (gcl.adminDataSet) {
                response->set_admin_gate_states(gcl.adminGateStates);
                for (const auto& entry : gcl.adminControlList) {
                    auto* protoEntry = response->add_admin_control_list();
                    protoEntry->set_index(entry.index);
                    protoEntry->set_gate_states_value(entry.gateStatesValue);
                    protoEntry->set_time_interval_value(entry.timeIntervalValue);
                }
            }
            return grpc::Status::OK;
        }
    }
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "Interface not found for GCL data");
}

grpc::Status CncServiceImpl::GetInterfaceAdminGcl(grpc::ServerContext* context,
                                                  const cnc::rpc::InterfaceRequest* request,
                                                  cnc::rpc::AdminGclResponse* response) {
    // create read lock
    std::shared_lock<std::shared_mutex> readLock(topologyMutex);

    CncNode_t* node = FindNode(request->host_name());
    if (!node) return grpc::Status(grpc::StatusCode::NOT_FOUND, "Node not found");

    for (const auto& iface : node->interfaces) {
        if (iface.name == request->interface_name()) {
            const auto& gcl = iface.bridgePort.gateParameterTable;

            if (!gcl.adminDataSet)
                return grpc::Status(grpc::StatusCode::NOT_FOUND, "No Admin GCL data set for this interface");

            response->set_gate_enabled(gcl.gateEnabled);
            response->set_admin_gate_states(gcl.adminGateStates);
            response->set_admin_cycle_time_extension_ns(gcl.adminCycleTimeExtensionNs);

            response->mutable_admin_cycle_time()->set_numerator(gcl.adminCycleTime.numerator);
            response->mutable_admin_cycle_time()->set_denominator(gcl.adminCycleTime.denominator);

            response->mutable_admin_base_time()->set_seconds(gcl.adminBaseTime.seconds);
            response->mutable_admin_base_time()->set_nanoseconds(gcl.adminBaseTime.nanoseconds);

            for (const auto& entry : gcl.adminControlList) {
                auto* protoEntry = response->add_admin_control_list();
                protoEntry->set_index(entry.index);
                protoEntry->set_gate_states_value(entry.gateStatesValue);
                protoEntry->set_time_interval_value(entry.timeIntervalValue);
            }
            return grpc::Status::OK;
        }
    }
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "Interface not found");
}

grpc::Status CncServiceImpl::GetInterfaceOperGcl(grpc::ServerContext* context,
                                                 const cnc::rpc::InterfaceRequest* request,
                                                 cnc::rpc::OperGclResponse* response) {
    // create read lock
    std::shared_lock<std::shared_mutex> readLock(topologyMutex);

    CncNode_t* node = FindNode(request->host_name());
    if (!node) return grpc::Status(grpc::StatusCode::NOT_FOUND, "Node not found");

    for (const auto& iface : node->interfaces) {
        if (iface.name == request->interface_name()) {
            const auto& gcl = iface.bridgePort.gateParameterTable;

            if (!gcl.operDataSet)
                return grpc::Status(grpc::StatusCode::NOT_FOUND, "No Oper GCL data set for this interface");

            response->set_oper_gate_states(gcl.operGateStates);
            response->set_oper_cycle_time_extension_ns(gcl.operCycleTimeExtensionNs);

            response->mutable_oper_cycle_time()->set_numerator(gcl.operCycleTime.numerator);
            response->mutable_oper_cycle_time()->set_denominator(gcl.operCycleTime.denominator);

            response->mutable_oper_base_time()->set_seconds(gcl.operBaseTime.seconds);
            response->mutable_oper_base_time()->set_nanoseconds(gcl.operBaseTime.nanoseconds);

            for (const auto& entry : gcl.operControlList) {
                auto* protoEntry = response->add_oper_control_list();
                protoEntry->set_index(entry.index);
                protoEntry->set_gate_states_value(entry.gateStatesValue);
                protoEntry->set_time_interval_value(entry.timeIntervalValue);
            }
            return grpc::Status::OK;
        }
    }
    return grpc::Status(grpc::StatusCode::NOT_FOUND, "Interface not found");
}

grpc::Status CncServiceImpl::SetNodeSchedule(grpc::ServerContext* context,
                                             const cnc::rpc::SetNodeScheduleRequest* request,
                                             cnc::rpc::SetNodeScheduleResponse* response) {
    PERFORMANCE_LOGGING("[CncServiceImpl::SetNodeSchedule]",
                        "Start setting node schedule for node: " + request->host_name());

    const std::string& hostName = request->host_name();
    spdlog::info("[gRPC] SetNodeSchedule called for node '{}'.", hostName);

    // 1. write lock
    // std::unique_lock<std::shared_mutex> writeLock(topologyMutex);
    std::shared_lock<std::shared_mutex> topoReadLock(
        topologyMutex);  // allows multithreading else we would block all other requests while deploying the config,
                         // which can take a while

    CncNode_t* internalNode = FindNode(hostName);
    if (!internalNode) {
        spdlog::error("[gRPC] SetNodeSchedule: Node '{}' not found.", hostName);
        return grpc::Status(grpc::StatusCode::NOT_FOUND, "Node not found: " + hostName);
    }

    // get node-specific mutex from topology
    std::lock_guard<std::mutex> nodeWriteLock(topology.getNodeMutex(hostName));

    const auto& mode = request->mode();
    std::map<std::string, const cnc::rpc::IetfInterface*> requestMap;
    for (const auto& iface : request->interfaces()) {
        requestMap[iface.name()] = &iface;
    };

    for (auto& iface : internalNode->interfaces) {
        // Initially set all to false so that no false interfaces land in the candidate store
        iface.bridgePort.gateParameterTable.configChange = false;

        // Filter out loopback interfaces
        if (iface.type == IfType::LOOPBACK) {
            spdlog::debug("[gRPC] Skipping loopback interface '{}'", iface.name);
            continue;
        }

        auto it = requestMap.find(iface.name);
        bool inRequest = (it != requestMap.end());

        // If interface in request, set the gateParameterTable
        if (inRequest) {
            auto& gcl = iface.bridgePort.gateParameterTable;
            const auto& protoGcl = it->second->bridge_port().gate_parameter_table();

            gcl.adminDataSet = protoGcl.admin_data_set();
            gcl.gateEnabled = protoGcl.gate_enabled();
            gcl.adminGateStates = protoGcl.admin_gate_states();
            gcl.adminCycleTime.numerator = protoGcl.admin_cycle_time().numerator();
            gcl.adminCycleTime.denominator = protoGcl.admin_cycle_time().denominator();
            gcl.adminBaseTime.seconds = protoGcl.admin_base_time().seconds();
            gcl.adminBaseTime.nanoseconds = protoGcl.admin_base_time().nanoseconds();
            gcl.adminControlList.clear();
            for (const auto& protoEntry : protoGcl.admin_control_list()) {
                GclEntry_t entry;
                entry.index = protoEntry.index();
                entry.gateStatesValue = protoEntry.gate_states_value();
                entry.timeIntervalValue = protoEntry.time_interval_value();
                entry.operationName = protoEntry.operation_name();
                gcl.adminControlList.push_back(entry);
            }
            gcl.configChange = true;
        }
        // If not in request and ZERO, reset all interfaces that are not set
        else if (mode == cnc::rpc::ScheduleUpdateMode::ZERO) {
            auto& gcl = iface.bridgePort.gateParameterTable;

            if (gcl.gateEnabled == true) {
                gcl.gateEnabled = false;
                gcl.configChange = true;
                spdlog::info("Mode ZERO: Resetting interface {}", iface.name);
            }
        }
        // If not in request and DENY, deny the request because not all interfaces are set
        else if (mode == cnc::rpc::ScheduleUpdateMode::DENY) {
            return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Interface was not defined: " + iface.name);
        }
    };

    auto* result = response->add_node_results();
    result->set_host_name(hostName);

    // 3. deploy config to node
    bool deployed = networkManager->deployConfigToNode(hostName);
    if (!deployed) {
        spdlog::error("[gRPC] SetNodeSchedule: Deployment failed for node '{}'.", hostName);
        response->set_overall_success(false);
        PERFORMANCE_LOGGING("[CncServiceImpl::SetNodeSchedule]", "ERROR_DEPLOYMENT_FAILED for node: " + hostName);
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to deploy configuration to node hardware");
    }

    PERFORMANCE_LOGGING("[CncServiceImpl::SetNodeSchedule]", "Finished deploying config to node: " + hostName);

    // 4. Zur Verifikation frische Daten holen (ist safe, weil wir das writeLock haben!) --> TODO: eben nicht muss
    // gefixt werden weil wir nur nen mutex auf einen node haben
    spdlog::info("[gRPC] Fetching operational GCL for verification...");
    networkManager->fetchOperationGcl();

    for (const auto& iface : internalNode->interfaces) {
        if (iface.bridgePort.gateParameterTable.operControlList.empty()) continue;
        auto* protoIface = result->add_interfaces();
        MapInterface(iface, protoIface);
    }

    response->set_overall_success(true);
    spdlog::info("[gRPC] SetNodeSchedule: Node '{}' deployed successfully.", hostName);
    PERFORMANCE_LOGGING("[CncServiceImpl::SetNodeSchedule]",
                        "Finished setting node schedule (with verification) for node: " + hostName);
    return grpc::Status::OK;
}

grpc::Status CncServiceImpl::SetInterfaceSchedule(grpc::ServerContext* context,
                                                  const cnc::rpc::SetInterfaceScheduleRequest* request,
                                                  cnc::rpc::IetfInterface* response) {
    PERFORMANCE_LOGGING("[CncServiceImpl::SetInterfaceSchedule]", "Start setting interface schedule for interface " +
                                                                      request->interface_name() + " on node " +
                                                                      request->host_name());
    const std::string& hostName = request->host_name();
    const std::string& ifName = request->interface_name();
    spdlog::info("[gRPC] SetInterfaceSchedule called for interface {} on node {}", ifName, hostName);

    // 1. SCHREIB-SCHLOSS SETZEN
    std::unique_lock<std::shared_mutex> writeLock(topologyMutex);

    CncNode_t* node = FindNode(hostName);
    if (!node) return grpc::Status(grpc::StatusCode::NOT_FOUND, "Node not found: " + hostName);

    std::lock_guard<std::mutex> nodeWriteLock(topology.getNodeMutex(hostName));

    auto it = std::find_if(node->interfaces.begin(), node->interfaces.end(),
                           [&](const ietfInterface_t& iface) { return iface.name == ifName; });
    if (it == node->interfaces.end()) return grpc::Status(grpc::StatusCode::NOT_FOUND, "Interface not found");

    if (it->type == IfType::LOOPBACK) {
        return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "Cant set schedule of loopback interface");
    }

    auto& internalGcl = it->bridgePort.gateParameterTable;
    const auto& protoGcl = request->new_admin_gcl();

    // 2. Daten mappen
    internalGcl.gateEnabled = protoGcl.gate_enabled();
    internalGcl.adminDataSet = true;
    internalGcl.adminCycleTimeExtensionNs = protoGcl.admin_cycle_time_extension_ns();
    internalGcl.adminGateStates = protoGcl.admin_gate_states();
    internalGcl.adminCycleTime.numerator = protoGcl.admin_cycle_time().numerator();
    internalGcl.adminCycleTime.denominator = protoGcl.admin_cycle_time().denominator();
    internalGcl.adminBaseTime.seconds = protoGcl.admin_base_time().seconds();
    internalGcl.adminBaseTime.nanoseconds = protoGcl.admin_base_time().nanoseconds();

    internalGcl.adminControlList.clear();
    for (const auto& protoEntry : protoGcl.admin_control_list()) {
        GclEntry_t entry;
        entry.index = protoEntry.index();
        entry.gateStatesValue = protoEntry.gate_states_value();
        entry.timeIntervalValue = protoEntry.time_interval_value();
        entry.operationName = protoEntry.operation_name();
        internalGcl.adminControlList.push_back(entry);
    }

    // DER BUGFIX
    internalGcl.configChange = true;

    // 3. Konfiguration pushen
    if (!networkManager->deployInterfaceConfig(hostName, ifName)) {
        spdlog::error("[gRPC] Deployment failed for interface {} on node '{}'.", ifName, hostName);
        return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to deploy configuration to node hardware");
    }

    PERFORMANCE_LOGGING("[CncServiceImpl::SetNodeSchedule]", "ERROR_COMMIT_FAILED for node: " + hostName);

    // 4. Verifizieren
    networkManager->fetchOperationGcl();
    MapInterface(*it, response);

    PERFORMANCE_LOGGING(
        "[CncServiceImpl::SetInterfaceSchedule]",
        "Finished setting interface schedule (with verification) for interface " + ifName + " on node " + hostName);
    return grpc::Status::OK;
}

grpc::Status CncServiceImpl::GetTopologyGraph(grpc::ServerContext* context, const cnc::rpc::EmptyRequest* request,
                                              cnc::rpc::TopologyGraph* response) {
    std::shared_lock<std::shared_mutex> readLock(topologyMutex);

    std::set<std::string> knownNodes;

    for (const auto& internalNode : topology.nodes) {
        auto* protoNode = response->add_nodes();
        protoNode->set_host_name(internalNode.hostName);
        protoNode->set_ip_address(internalNode.ipAddress);
        protoNode->set_is_foreign(false);
        knownNodes.insert(internalNode.hostName);
    }

    std::set<std::pair<std::string, std::string>> seenEdges;

    for (const auto& internalNode : topology.nodes) {
        const std::string& localHost = internalNode.hostName;

        for (const auto& lldpPort : internalNode.lldpAllData.ports) {
            for (const auto& neighbor : lldpPort.neighbors) {
                // system_name as identifier
                if (neighbor.systemName.empty()) continue;
                const std::string& remoteHost = neighbor.systemName;

                // If this neighbor is a foreign CNC node, add it as foreign
                if (!knownNodes.count(remoteHost)) {
                    auto* protoNode = response->add_nodes();
                    protoNode->set_host_name(remoteHost);
                    protoNode->set_chassis_id(neighbor.chassisId);
                    protoNode->set_is_foreign(true);
                    knownNodes.insert(remoteHost);
                }

                auto edgeKey = (localHost < remoteHost) ? std::make_pair(localHost, remoteHost)
                                                        : std::make_pair(remoteHost, localHost);

                if (seenEdges.count(edgeKey)) continue;
                seenEdges.insert(edgeKey);

                auto* edge = response->add_edges();
                edge->set_local_host_name(localHost);
                edge->set_local_port(lldpPort.name);
                edge->set_remote_host_name(remoteHost);
                edge->set_remote_port(neighbor.portId);
                edge->set_remote_chassis_id(neighbor.chassisId);
            }
        }
    }

    spdlog::info("[gRPC] GetTopologyGraph: {} nodes ({} non-foreign), {} edges.", response->nodes_size(),
                 topology.nodes.size(), response->edges_size());
    return grpc::Status::OK;
}