#include <libyang/libyang.h>
#include <nc_client.h>
#include <signal.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "PerformanceLogger.h"

const std::vector<std::string> xml_configs = {
    R"XML(<interfaces xmlns="urn:ietf:params:xml:ns:yang:ietf-interfaces" xmlns:sched="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched-bridge"><interface><name>enp2s0f2</name><bridge-port xmlns="urn:ieee:std:802.1Q:yang:ieee802-dot1q-bridge"><gate-parameter-table xmlns="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched-bridge"><admin-gate-states>255</admin-gate-states><admin-base-time><seconds>0</seconds><nanoseconds>0</nanoseconds></admin-base-time><admin-cycle-time><numerator>1500000</numerator><denominator>1000000000</denominator></admin-cycle-time><admin-control-list xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0" nc:operation="replace"><gate-control-entry><index>0</index><operation-name xmlns:sched="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched">sched:set-gate-states</operation-name><gate-states-value>255</gate-states-value><time-interval-value>800000</time-interval-value></gate-control-entry><gate-control-entry><index>1</index><operation-name xmlns:sched="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched">sched:set-gate-states</operation-name><gate-states-value>0</gate-states-value><time-interval-value>200000</time-interval-value></gate-control-entry><gate-control-entry><index>2</index><operation-name xmlns:sched="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched">sched:set-gate-states</operation-name><gate-states-value>129</gate-states-value><time-interval-value>250000</time-interval-value></gate-control-entry><gate-control-entry><index>3</index><operation-name xmlns:sched="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched">sched:set-gate-states</operation-name><gate-states-value>52</gate-states-value><time-interval-value>250000</time-interval-value></gate-control-entry></admin-control-list><config-change>true</config-change><gate-enabled>true</gate-enabled></gate-parameter-table></bridge-port></interface></interfaces>)XML", R"XML(<interfaces xmlns="urn:ietf:params:xml:ns:yang:ietf-interfaces" xmlns:sched="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched-bridge"><interface><name>enp2s0f2</name><bridge-port xmlns="urn:ieee:std:802.1Q:yang:ieee802-dot1q-bridge"><gate-parameter-table xmlns="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched-bridge"><admin-gate-states>255</admin-gate-states><admin-base-time><seconds>0</seconds><nanoseconds>0</nanoseconds></admin-base-time><admin-cycle-time><numerator>1500000</numerator><denominator>1000000000</denominator></admin-cycle-time><admin-control-list xmlns:nc="urn:ietf:params:xml:ns:netconf:base:1.0" nc:operation="replace"><gate-control-entry><index>0</index><operation-name xmlns:sched="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched">sched:set-gate-states</operation-name><gate-states-value>200</gate-states-value><time-interval-value>100000</time-interval-value></gate-control-entry><gate-control-entry><index>1</index><operation-name xmlns:sched="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched">sched:set-gate-states</operation-name><gate-states-value>3</gate-states-value><time-interval-value>200000</time-interval-value></gate-control-entry><gate-control-entry><index>2</index><operation-name xmlns:sched="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched">sched:set-gate-states</operation-name><gate-states-value>171</gate-states-value><time-interval-value>300000</time-interval-value></gate-control-entry><gate-control-entry><index>3</index><operation-name xmlns:sched="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched">sched:set-gate-states</operation-name><gate-states-value>42</gate-states-value><time-interval-value>400000</time-interval-value></gate-control-entry><gate-control-entry><index>4</index><operation-name xmlns:sched="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched">sched:set-gate-states</operation-name><gate-states-value>231</gate-states-value><time-interval-value>500000</time-interval-value></gate-control-entry></admin-control-list><config-change>true</config-change><gate-enabled>true</gate-enabled></gate-parameter-table></bridge-port></interface></interfaces>)XML", R"XML(<interfaces xmlns="urn:ietf:params:xml:ns:yang:ietf-interfaces" xmlns:sched="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched-bridge"><interface><name>enp2s0f2</name><bridge-port xmlns="urn:ieee:std:802.1Q:yang:ieee802-dot1q-bridge"><gate-parameter-table xmlns="urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched-bridge"><config-change>true</config-change><gate-enabled>false</gate-enabled></gate-parameter-table></bridge-port></interface></interfaces>)XML"};

bool keep_running = true;
void signal_handler(int) {
    keep_running = false;
}

void send_rpc_safe(struct nc_session* session, struct nc_rpc* rpc, const char* label, uint64_t* msgid) {
    // 1. Send RPC
    NC_MSG_TYPE msgtype = nc_send_rpc(session, rpc, 1000, msgid);
    if (msgtype != NC_MSG_RPC) {
        std::cerr << "Error: Failed to send RPC (" << label << ")" << std::endl;
        nc_rpc_free(rpc);
        return;
    }

    // 2. Prepare pointers for the reply
    struct lyd_node* envp = nullptr;  // For the <rpc-reply> envelope
    struct lyd_node* op = nullptr;    // For the actual data/errors

    // 3. Receive Reply
    // Signature: session, rpc, msgid, timeout, envp_out, op_out
    msgtype = nc_recv_reply(session, rpc, *msgid, 2000, &envp, &op);

    // 4. Check results
    if (msgtype == NC_MSG_REPLY) {
        // Success (usually an <ok/> for edit-config/commit)
        // If 'op' is null but msgtype is NC_MSG_REPLY, it is an <ok/>
    } else if (msgtype == NC_MSG_ERROR) {
        std::cerr << "Error: Critical failure receiving reply for " << label << std::endl;
    } else {
        std::cerr << "Error: Received unexpected message type for " << label << std::endl;
    }

    // 5. Cleanup
    // Important: libnetconf2 allocates these nodes; we must free them
    if (envp) lyd_free_all(envp);
    if (op) lyd_free_all(op);
    nc_rpc_free(rpc);
    return;
}

int main(int argc, char** argv) {
    signal(SIGINT, signal_handler);

    nc_client_init();
    nc_client_ssh_set_username("netconf-api");

    ly_ctx* ctx = nullptr;
    struct nc_session* session = nc_connect_ssh("vstsn01.infra.informatik.uni-stuttgart.de", 830, ctx);
    if (!session) return 1;

    // std::vector<struct lyd_node*> nodes;
    // for (const auto& xml : xml_configs) {
    //     struct lyd_node* node = nullptr;
    //     // Parsing with your specific version of libyang
    //     if (lyd_parse_data_mem(ctx, xml.c_str(), LYD_XML, LYD_PARSE_ONLY, 0, &node) != LY_SUCCESS) {
    //         std::cerr << "YANG Parse Error." << std::endl;
    //         return 1;
    //     }
    //     nodes.push_back(node);
    // }

    int iterations = 100;
    uint64_t msgid = 0;
    for (int i = 0; i < iterations && keep_running; ++i) {
        for (size_t j = 0; j < xml_configs.size(); ++j) {
            std::string label = fmt::format("Iteration={} Msg={}", i, j);

            // --- EDIT-CONFIG ---
            PERFORMANCE_LOGGING(label.c_str(), "EDIT_CANDIDATE_START");
            struct nc_rpc* rpc_edit =
                nc_rpc_edit(NC_DATASTORE_CANDIDATE, NC_RPC_EDIT_DFLTOP_MERGE, NC_RPC_EDIT_TESTOPT_SET,
                            NC_RPC_EDIT_ERROPT_STOP, xml_configs[j].c_str(), NC_PARAMTYPE_CONST);
            send_rpc_safe(session, rpc_edit, "edit-config", &msgid);
            PERFORMANCE_LOGGING(label.c_str(), "EDIT_CANDIDATE_DONE req={}", msgid);

            // --- COMMIT ---
            PERFORMANCE_LOGGING(label.c_str(), "COMMIT_START");
            struct nc_rpc* rpc_commit = nc_rpc_commit(0, 0, nullptr, nullptr, NC_PARAMTYPE_CONST);
            send_rpc_safe(session, rpc_commit, "commit", &msgid);
            PERFORMANCE_LOGGING(label.c_str(), "COMMIT_DONE req={}", msgid);
        }
    }

    // Cleanup
    // for (auto n : nodes) lyd_free_all(n);
    nc_session_free(session, nullptr);
    nc_client_destroy();
    return 0;
}