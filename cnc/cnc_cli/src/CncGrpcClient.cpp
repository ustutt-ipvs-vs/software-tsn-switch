#include "../include/CncGrpcClient.h"

using namespace cnc::rpc;

CncGrpcClient::CncGrpcClient(const std::string& target_address) {
    auto channel = grpc::CreateChannel(target_address, grpc::InsecureChannelCredentials());
    stub_ = CncService::NewStub(channel);
}

// 1. topology currently only --all
std::string CncGrpcClient::getTopology(bool asJson) {
    EmptyRequest request;
    Topology reply;
    grpc::ClientContext context;
    
    grpc::Status status = stub_->GetNetworkState(&context, request, &reply);
    
    return status.ok() ? formatResponse(reply, asJson) : "gRPC Error: " + status.error_message();
}

std::string CncGrpcClient::getTopologyGraph(bool asJson) {
    EmptyRequest request;
    TopologyGraph reply;
    grpc::ClientContext context;

    grpc::Status status = stub_->GetTopologyGraph(&context, request, &reply);

    return status.ok() ? formatResponse(reply, asJson) : "gRPC Error: " + status.error_message();
}

// 2. lldp can do --all and --targets
std::string CncGrpcClient::getLldp(bool selectAll, const std::vector<std::string>& targets, bool asJson) {
    if (selectAll) {
        EmptyRequest request;
        AllLldpDataResponse reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->GetAllLldpData(&context, request, &reply);
        return status.ok() ? formatResponse(reply, asJson) : "gRPC Error: " + status.error_message();
    }

    std::string final_output = "";
    for (const auto& target : targets) {
        grpc::ClientContext context;
        auto dotPos = target.find('.');

        if (dotPos != std::string::npos) {
            // target has a dot (e.g., vstsn01.enp2s0f0) -> Interface Request
            InterfaceRequest request;
            request.set_host_name(target.substr(0, dotPos));
            request.set_interface_name(target.substr(dotPos + 1));
            LldpPort reply;
            
            grpc::Status status = stub_->GetInterfaceLldpData(&context, request, &reply);
            final_output += "--- Target: " + target + " ---\n";
            final_output += status.ok() ? formatResponse(reply, asJson) : "Error: " + status.error_message();
            final_output += "\n";
        } else {
            // target has no dot (e.g., vstsn01) -> Node Request
            NodeRequest request;
            request.set_host_name(target);
            LldpNode reply;
            
            grpc::Status status = stub_->GetNodeLldpData(&context, request, &reply);
            final_output += "--- Target: " + target + " ---\n";
            final_output += status.ok() ? formatResponse(reply, asJson) : "Error: " + status.error_message();
            final_output += "\n";
        }
    }
    return final_output;
}

// 3. ptp can do --all and --targets
std::string CncGrpcClient::getPtp(bool selectAll, const std::vector<std::string>& targets, bool asJson) {
    if (selectAll) {
        EmptyRequest request;
        AllPtpDataResponse reply;
        grpc::ClientContext context;
        grpc::Status status = stub_->GetAllPtpData(&context, request, &reply);
        return status.ok() ? formatResponse(reply, asJson) : "gRPC Error: " + status.error_message();
    }

    std::string final_output = "";
    for (const auto& target : targets) {
        grpc::ClientContext context;
        auto dotPos = target.find('.');

        if (dotPos != std::string::npos) {
            // target has a dot (e.g., vstsn01.enp2s0f0) -> Interface Request
            InterfaceRequest request;
            request.set_host_name(target.substr(0, dotPos));
            request.set_interface_name(target.substr(dotPos + 1));
            PtpPort reply;
            
            grpc::Status status = stub_->GetInterfacePtpData(&context, request, &reply);
            final_output += "--- Target: " + target + " ---\n";
            final_output += status.ok() ? formatResponse(reply, asJson) : "Error: " + status.error_message();
            final_output += "\n";
        } else {
            // target has no dot (e.g., vstsn01) -> Node Request
            NodeRequest request;
            request.set_host_name(target);
            PtpNode reply;
            
            grpc::Status status = stub_->GetNodePtpData(&context, request, &reply);
            final_output += "--- Target: " + target + " ---\n";
            final_output += status.ok() ? formatResponse(reply, asJson) : "Error: " + status.error_message();
            final_output += "\n";
        }
    }
    return final_output;
}

std::string CncGrpcClient::getSchedule(bool selectAll, const std::vector<std::string>& targets, bool asJson) {
    if (selectAll) {
        return "Error: Schedule data requires specific interface targets (e.g., vstsn01.enp2s0f0). --all is not supported here.";
    }

    std::string final_output = "";
    for (const auto& target : targets) {
        grpc::ClientContext context;
        auto dotPos = target.find('.');

        if (dotPos != std::string::npos) {
            // target has a dot (e.g., vstsn01.enp2s0f0) -> Interface Request
            InterfaceRequest request;
            request.set_host_name(target.substr(0, dotPos));
            request.set_interface_name(target.substr(dotPos + 1));
            GclConfig reply;
            
            grpc::Status status = stub_->GetInterfaceGcl(&context, request, &reply);
            
            if (status.ok()) {
                if (asJson) {
                    // package output into a wrapper message so that it can directly used as a perfect JSON input for setInterfaceSchedule
                    SetInterfaceScheduleRequest wrapper;
                    wrapper.set_host_name(request.host_name());
                    wrapper.set_interface_name(request.interface_name());
                    *wrapper.mutable_new_admin_gcl() = reply; // Zuweisung des gesamten GclConfig Objekts
                    
                    // format the wrapper message as JSON
                    std::string jsonStr;
                    google::protobuf::util::JsonPrintOptions options;
                    options.add_whitespace = true; // makes json more readable
                    options.always_print_primitive_fields = true; // ensures all fields are included in the JSON, even if they have default values (e.g., empty lists)
                    google::protobuf::util::MessageToJsonString(wrapper, &jsonStr, options);
                    
                    final_output += jsonStr + "\n";
                } else {
                    // standard view with target header and formatted GCL data (for user)
                    final_output += "--- Target: " + target + " ---\n";
                    final_output += formatResponse(reply, asJson) + "\n";
                }
            } else {
                final_output += "Error: " + status.error_message() + "\n";
            }
        } else {
            // target has no dot. For GCL we need interfaces!
            final_output += "--- Target: " + target + " ---\n"; // maybe remove or else you cannot reuse this json to put back into cnc
            final_output += "Error: Schedule requires an interface target with a dot (e.g., vstsn01.enp2s0f0).\n\n";
        }
    }
    return final_output;
}

bool CncGrpcClient::setInterfaceSchedule(const SetInterfaceScheduleRequest& request) {
    IetfInterface reply;
    grpc::ClientContext context;

    // call the gRPC method
    grpc::Status status = stub_->SetInterfaceSchedule(&context, request, &reply);

    if (status.ok()) {
        return true;
    } else {
        throw std::runtime_error("[gRPC Error] " + status.error_message());
    }
}

bool CncGrpcClient::setNodeSchedule(const SetNodeScheduleRequest& request) {
    SetNodeScheduleResponse reply;
    grpc::ClientContext context;

    grpc::Status status = stub_->SetNodeSchedule(&context, request, &reply);

    if (status.ok()) {
        return reply.overall_success();
    } else {
        throw std::runtime_error("[gRPC Error] " + status.error_message());
    }
}