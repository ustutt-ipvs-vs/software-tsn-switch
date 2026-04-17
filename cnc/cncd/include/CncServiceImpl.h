#pragma once

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>

#include "NetworkManager.h"
#include "Topology.h"
#include "cnc.grpc.pb.h"

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
    /**
     * @brief Maps an internal CncNode_t structure to a cnc::rpc::Node protobuf message.
     * @param internalIface the internal ietfInterface_t structure containing the interface data to be mapped
     * @param protoIface the protobuf message to be filled with the mapped data from internalIface
     */
    void MapInterface(const ietfInterface_t& internalIface, cnc::rpc::IetfInterface* protoIface);

    /**
     * @brief Maps an internal PtpNode_t structure to a cnc::rpc::PtpNode protobuf message.
     * @param internalPtp the internal PtpNode_t structure containing the PTP data to be mapped
     * @param protoPtp the protobuf message to be filled with the mapped data from internalPtp
     */
    void MapPtpNode(const PtpNode_t& internalPtp, cnc::rpc::PtpNode* protoPtp);

    /**
     * @brief Maps an internal LldpNode_t structure to a cnc::rpc::LldpNode protobuf message.
     * @param internalLldp the internal LldpNode_t structure containing the LLDP data to be mapped
     * @param protoLldp the protobuf message to be filled with the mapped data from internalLldp
     */
    void MapLldpNode(const LldpNode_t& internalLldp, cnc::rpc::LldpNode* protoLldp);

    /**
     * @brief Maps an internal PtpPort_t structure to a cnc::rpc::PtpPort protobuf message.
     * @param internalPort the internal PtpPort_t structure containing the PTP port data to be mapped
     * @param protoPort the protobuf message to be filled with the mapped data from internalPort
     */
    void MapPtpPort(const PtpPort_t& internalPort, cnc::rpc::PtpPort* protoPort);
    
    /**
     * @brief Maps an internal LldpPort_t structure to a cnc::rpc::LldpPort protobuf message.
     * @param internalPort the internal LldpPort_t structure containing the LLDP port data to be mapped
     * @param protoPort the protobuf message to be filled with the mapped data from internalPort
     */
    void MapLldpPort(const LldpPort_t& internalPort, cnc::rpc::LldpPort* protoPort);

    /**
     * @brief Background task that periodically fetches PTP data for all nodes.
     * This function runs in a separate thread and updates the topology with the latest PTP data at regular intervals
     * (every 15 minutes). It uses a condition variable to wait for the next fetch interval or to be notified when the
     * service is shutting down.
     */
    void ptpBackgroundTask();

   public:
    CncServiceImpl(const std::string& inventoryFilePath);

    ~CncServiceImpl();

    /**
     * @brief gRPC method implementation to retrieve the entire network topology, including nodes, interfaces, and their associated LLDP/PTP data.
     * @param context the gRPC server context for the request
     * @param request an empty request message (no parameters needed)
     * @param response the protobuf message to be filled with the complete network topology data to be returned to the client
     * @return a gRPC status indicating the success or failure of the operation
     */
    grpc::Status GetNetworkState(grpc::ServerContext* context, const cnc::rpc::EmptyRequest* request,
                                 cnc::rpc::Topology* response) override;

    /**
     * @brief gRPC method implementation to retrieve all PTP data across all nodes and interfaces in the network.
     * @param context the gRPC server context for the request
     * @param request an empty request message (no parameters needed)
     * @param response the protobuf message to be filled with all PTP data from the network to be returned to the client
     * @return a gRPC status indicating the success or failure of the operation
     */
    grpc::Status GetAllPtpData(grpc::ServerContext* context, const cnc::rpc::EmptyRequest* request,
                               cnc::rpc::AllPtpDataResponse* response) override;

    /**
     * @brief gRPC method implementation to retrieve all LLDP data across all nodes and interfaces in the network.
     * @param context the gRPC server context for the request
     * @param request an empty request message (no parameters needed)
     * @param response the protobuf message to be filled with all LLDP data from the network to be returned to the client
     * @return a gRPC status indicating the success or failure of the operation
     */
    grpc::Status GetAllLldpData(grpc::ServerContext* context, const cnc::rpc::EmptyRequest* request,
                                cnc::rpc::AllLldpDataResponse* response) override;

    /**
     * @brief gRPC method implementation to retrieve node-specific PTP data for a given node identified by its hostname.
     * @param context the gRPC server context for the request
     * @param request the protobuf message NodeRequest containing the hostname of the node for which PTP data is requested
     * @param response the protobuf message to be filled with the PTP data of the specified node to be returned to the client
     * @return a gRPC status indicating the success or failure of the operation
     */
    grpc::Status GetNodePtpData(grpc::ServerContext* context, const cnc::rpc::NodeRequest* request,
                                cnc::rpc::PtpNode* response) override;
               
   /**
    * @brief gRPC method implementation to retrieve node-specific LLDP data for a given node identified by its hostname.
    * @param context the gRPC server context for the request
    * @param request the protobuf message NodeRequest containing the hostname of the node for which LLDP data is requested
    * @param response the protobuf message to be filled with the LLDP data of the specified node to be returned to the client
    * @return a gRPC status indicating the success or failure of the operation
    */
    grpc::Status GetNodeLldpData(grpc::ServerContext* context, const cnc::rpc::NodeRequest* request,
                                 cnc::rpc::LldpNode* response) override;

    /**
     * @brief gRPC method implementation to retrieve interface-specific PTP data for a given interface identified by node hostname and interface name.
     * @param context the gRPC server context for the request
     * @param request the protobuf message InterfaceRequest containing the hostname of the node and the name of the interface for which PTP data is requested
     * @param response the protobuf message to be filled with the PTP data of the specified interface to be returned to the client
     * @return a gRPC status indicating the success or failure of the operation
     */
    grpc::Status GetInterfacePtpData(grpc::ServerContext* context, const cnc::rpc::InterfaceRequest* request,
                                     cnc::rpc::PtpPort* response) override;

   /**
    * @brief gRPC method implementation to retrieve interface-specific LLDP data for a given interface identified by node hostname and interface name.
    * @param context the gRPC server context for the request
    * @param request the protobuf message InterfaceRequest containing the hostname of the node and the name of the interface for which LLDP data is requested
    * @param response the protobuf message to be filled with the LLDP data of the specified interface to be returned to the client
    * @return a gRPC status indicating the success or failure of the operation
    */
    grpc::Status GetInterfaceLldpData(grpc::ServerContext* context, const cnc::rpc::InterfaceRequest* request,
                                      cnc::rpc::LldpPort* response) override;

    /**
     * @brief gRPC method implementation to retrieve GCL (Gate Control List) data for a specific interface identified by node hostname and interface name.
     * @param context the gRPC server context for the request
     * @param request the protobuf message InterfaceRequest containing the hostname of the node and the name of the interface for which GCL data is requested
     * @param response the protobuf message to be filled with the GCL data of the specified interface to be returned to the client
     * @return a gRPC status indicating the success or failure of the operation
     */
    grpc::Status GetInterfaceGcl(grpc::ServerContext* context, const cnc::rpc::InterfaceRequest* request,
                                 cnc::rpc::GclConfig* response) override;

   /**
    * @brief gRPC method implementation to retrieve administrative GCL (Gate Control List) data for a specific interface identified by node hostname and interface name.
    * @param context the gRPC server context for the request
    * @param request the protobuf message InterfaceRequest containing the hostname of the node and the name of the interface for which administrative GCL data is requested
    * @param response the protobuf message to be filled with the administrative GCL data of the specified interface to be returned to the client
    * @return a gRPC status indicating the success or failure of the operation
    */
    grpc::Status GetInterfaceAdminGcl(grpc::ServerContext* context, const cnc::rpc::InterfaceRequest* request,
                                      cnc::rpc::AdminGclResponse* response) override;

   /**
    * @brief gRPC method implementation to retrieve operational GCL (Gate Control List) data for a specific interface identified by node hostname and interface name.
    * @param context the gRPC server context for the request
    * @param request the protobuf message InterfaceRequest containing the hostname of the node and the name of the interface for which operational GCL data is requested
    * @param response the protobuf message to be filled with the operational GCL data of the specified interface to be returned to the client
    * @return a gRPC status indicating the success or failure of the operation
    */
    grpc::Status GetInterfaceOperGcl(grpc::ServerContext* context, const cnc::rpc::InterfaceRequest* request,
                                     cnc::rpc::OperGclResponse* response) override;

    /**
     * @brief gRPC method implementation to set schedules for all interfaces on a specific node identified by its hostname.
     * @param context the gRPC server context for the request
     * @param request the protobuf message SetNodeScheduleRequest containing the hostname of the node and the schedule data to be set for all interfaces on that node
     * @param response the protobuf message to be filled with the result of the schedule setting operation (e.g., success status, error messages) to be returned to the client
     * @return a gRPC status indicating the success or failure of the operation
     */
    grpc::Status SetNodeSchedule(grpc::ServerContext* context, const cnc::rpc::SetNodeScheduleRequest* request,
                                 cnc::rpc::SetNodeScheduleResponse* response);

    /**
     * @brief gRPC method implementation to set the schedule for a specific interface identified by node hostname and interface name.
     * @param context the gRPC server context for the request
     * @param request the protobuf message SetInterfaceScheduleRequest containing the hostname of the node, the name of the interface, and the schedule data to be set for that specific interface
     * @param response the protobuf message to be filled with the result of the schedule setting operation (e.g., success status, error messages) to be returned to the client
     * @return a gRPC status indicating the success or failure of the operation
     */
    grpc::Status SetInterfaceSchedule(grpc::ServerContext* context,
                                      const cnc::rpc::SetInterfaceScheduleRequest* request,
                                      cnc::rpc::IetfInterface* response);


   /**
    * @brief gRPC method implementation to retrieve the topology graph of the network, which includes the nodes and their interconnections based on LLDP data.
    * @param context the gRPC server context for the request
    * @param request an empty request message (no parameters needed)
    * @param response the protobuf message to be filled with the topology graph data (nodes and their connections) to be returned to the client
    * @return a gRPC status indicating the success or failure of the operation
    */
    grpc::Status GetTopologyGraph(grpc::ServerContext* context, const cnc::rpc::EmptyRequest* request,
                                  cnc::rpc::TopologyGraph* response);
};
