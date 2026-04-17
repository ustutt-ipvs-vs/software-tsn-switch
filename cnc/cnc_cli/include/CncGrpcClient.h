#pragma once

#include <google/protobuf/util/json_util.h>
#include <grpcpp/grpcpp.h>

#include <memory>
#include <string>
#include <vector>

#include "cnc.grpc.pb.h"

/**
 * @brief CncGrpcClient is a gRPC client for communicating with the CNC gRPC server (cncd).
 * It provides methods to fetch topology, LLDP, PTP, and schedule data from the server, as well as to set interface and node schedules. 
 * The client is used by the CNC CLI application to interact with the server and retrieve or update network information in various formats (JSON or human-readable).
 */
class CncGrpcClient {
   public:
    CncGrpcClient(const std::string& target_address);

    /**
     * @brief Fetches the full network topology from the CNC gRPC server
     * @param asJson If true, returns the topology in JSON format; otherwise, returns a human-readable indented format
     * @return The topology as a string in the requested format
     */
    std::string getTopology(bool asJson);

    std::string getTopologyGraph(bool asJson);

    /**
     * @brief Fetches LLDP data for all nodes/interfaces from the CNC gRPC server
     * @param selectAll If true, retrieves LLDP data for all nodes/interfaces; otherwise, retrieves data only for
     * specified targets
     * @param targets A list of node hostnames or interface identifiers to retrieve LLDP data for (ignored if selectAll
     * is true)
     * @param asJson If true, returns the LLDP data in JSON format; otherwise, returns a human-readable indented format
     * @return The LLDP data as a string in the requested format
     */
    std::string getLldp(bool selectAll, const std::vector<std::string>& targets, bool asJson);

    /**
     * @brief Fetches PTP data for all nodes/interfaces from the CNC gRPC server
     * @param selectAll If true, retrieves PTP data for all nodes/interfaces; otherwise, retrieves data only for
     * specified targets
     * @param targets A list of node hostnames or interface identifiers to retrieve PTP data for (ignored if selectAll
     * is true)
     * @param asJson If true, returns the PTP data in JSON format; otherwise, returns a human-readable indented format
     * @return The PTP data as a string in the requested format
     */
    std::string getPtp(bool selectAll, const std::vector<std::string>& targets, bool asJson);

    /**
     * @brief Fetches schedule data for all nodes/interfaces from the CNC gRPC server
     * @param selectAll If true, retrieves schedule data for all nodes/interfaces; otherwise, retrieves data only for
     * specified targets
     * @param targets A list of node hostnames or interface identifiers to retrieve schedule data for (ignored if
     * selectAll is true)
     * @param asJson If true, returns the schedule data in JSON format; otherwise, returns a human-readable indented
     * format
     * @return The schedule data as a string in the requested format
     */
    std::string getSchedule(bool selectAll, const std::vector<std::string>& targets, bool asJson);

    /**
     * @brief Sets the schedule for a specific interface on the CNC gRPC server
     * @param request A SetInterfaceScheduleRequest containing the hostname, interface name, and new schedule
     * configuration
     * @return True if the schedule was successfully set; false otherwise
     */
    bool setInterfaceSchedule(const cnc::rpc::SetInterfaceScheduleRequest& request);

    bool setNodeSchedule(const cnc::rpc::SetNodeScheduleRequest& request);

   private:
    // gRPC stub for communicating with the cncd
    std::unique_ptr<cnc::rpc::CncService::Stub> stub_;

    // Helper methods to call specific gRPC endpoints and format responses
    template <typename ResponseType>
    std::string formatResponse(const ResponseType& msg, bool asJson) {
        if (asJson) {
            std::string json_string;
            google::protobuf::util::JsonPrintOptions options;
            options.add_whitespace = true;
            options.always_print_primitive_fields = true;
            google::protobuf::util::MessageToJsonString(msg, &json_string, options);
            return json_string;
        }
        return msg.DebugString();
    }
};