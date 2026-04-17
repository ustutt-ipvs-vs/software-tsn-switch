#pragma once

#include <grpcpp/grpcpp.h>
#include "cnc.grpc.pb.h"
#include "NetworkManager.h"
#include "Topology.h"
#include <memory>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <string>
#include <condition_variable>

class CncServiceImpl final : public cnc::rpc::CncService::Service {
private:
    Topology topology;
    std::unique_ptr<cnc::NetworkManager> networkManager;

    // mutex to protect access to data structure for timer based ptp fetching
    std::shared_mutex topologyMutex;
    std::thread ptpTimerThread;
    std::atomic<bool> stopTimer{false};
    std::condition_variable timerCV;
    std::mutex timerMutex;

    // helper function to find a node by hostname in the topology
    CncNode_t* FindNode(const std::string& hostName);
    
    // helper function to map internal CncNode_t to cnc::rpc::Node protobuf message
    void MapInterface(const ietfInterface_t& internalIface, cnc::rpc::IetfInterface* protoIface);
    void MapPtpNode(const PtpNode_t& internalPtp, cnc::rpc::PtpNode* protoPtp);
    void MapLldpNode(const LldpNode_t& internalLldp, cnc::rpc::LldpNode* protoLldp);

    void MapPtpPort(const PtpPort_t& internalPort, cnc::rpc::PtpPort* protoPort);
    void MapLldpPort(const LldpPort_t& internalPort, cnc::rpc::LldpPort* protoPort);

    /**
     * @brief Background task that periodically fetches PTP data for all nodes.
     * This function runs in a separate thread and updates the topology with the latest PTP data at regular intervals (every 15 minutes). It uses a condition variable to wait for the next fetch interval or to be notified when the service is shutting down.
     */
    void ptpBackgroundTask();

public:
    CncServiceImpl(const std::string& inventoryFilePath);

    ~CncServiceImpl();

    // 1. return the entire network topology (nodes + interfaces + LLDP/PTP data) in one call
    grpc::Status GetNetworkState(grpc::ServerContext* context, const cnc::rpc::EmptyRequest* request, cnc::rpc::Topology* response) override;

    // 2. global feature data (LLDP/PTP) across all nodes/interfaces
    grpc::Status GetAllPtpData(grpc::ServerContext* context, const cnc::rpc::EmptyRequest* request, cnc::rpc::AllPtpDataResponse* response) override;
    grpc::Status GetAllLldpData(grpc::ServerContext* context, const cnc::rpc::EmptyRequest* request, cnc::rpc::AllLldpDataResponse* response) override;

    // 3. node-specific data (LLDP/PTP) for a given node identified by hostname
    grpc::Status GetNodePtpData(grpc::ServerContext* context, const cnc::rpc::NodeRequest* request, cnc::rpc::PtpNode* response) override;
    grpc::Status GetNodeLldpData(grpc::ServerContext* context, const cnc::rpc::NodeRequest* request, cnc::rpc::LldpNode* response) override;

    // 4. interface-specific data (LLDP/PTP) for a given interface identified by node hostname + interface name
    grpc::Status GetInterfacePtpData(grpc::ServerContext* context, const cnc::rpc::InterfaceRequest* request, cnc::rpc::PtpPort* response) override;
    grpc::Status GetInterfaceLldpData(grpc::ServerContext* context, const cnc::rpc::InterfaceRequest* request, cnc::rpc::LldpPort* response) override;

    // 5. gcl-data for a given interface identified by node hostname + interface name
    grpc::Status GetInterfaceGcl(grpc::ServerContext* context, const cnc::rpc::InterfaceRequest* request, cnc::rpc::GclConfig* response) override;
    grpc::Status GetInterfaceAdminGcl(grpc::ServerContext* context, const cnc::rpc::InterfaceRequest* request, cnc::rpc::AdminGclResponse* response) override;
    grpc::Status GetInterfaceOperGcl(grpc::ServerContext* context, const cnc::rpc::InterfaceRequest* request, cnc::rpc::OperGclResponse* response) override;

    // 6. set schedules of all interfaces on a specific node
    grpc::Status SetNodeSchedule(grpc::ServerContext* context, const cnc::rpc::SetNodeScheduleRequest* request, cnc::rpc::SetNodeScheduleResponse* response);

    // 7. set schedules of a specific interface on a spe
    grpc::Status SetInterfaceSchedule(grpc::ServerContext* context, const cnc::rpc::SetInterfaceScheduleRequest* request, cnc::rpc::IetfInterface* response);

    grpc::Status GetTopologyGraph(grpc::ServerContext* context, const cnc::rpc::EmptyRequest* request, cnc::rpc::TopologyGraph* response);
};
