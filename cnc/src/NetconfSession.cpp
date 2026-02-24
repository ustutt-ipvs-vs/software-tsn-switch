#include "NetconfSession.h"

// Not needed anymore since defined in CMakeLists.txt
/*#ifndef NC_ENABLED_SSH_TlS
#define NC_ENABLED_SSH_TLS
#endif*/
#include <libnetconf2/log.h>  // For logging functions
#include <libnetconf2/session.h>
#include <libnetconf2/session_client.h>
#include <libyang/libyang.h>  // For libyang functions
#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace common {
// Global variable to hold the current password for the callback
static std::string g_current_password;

// Callback function for password authentication
char *password_interactive_clb(const char *username, const char *hostname, void *priv_data) {
    return strdup(g_current_password.c_str());
}

// Callback function for keyboard-interactive authentication (bad solution --> maybe change in future)
char *kb_interactive_clb(const char *auth_name, const char *instruction, const char *prompt, int echo,
                         void *priv_data) {
    // Only one prompt is expected for password
    return strdup(g_current_password.c_str());
}

NetconfSession::NetconfSession() {
    static bool initialized = false;

    if (!initialized) {
        nc_client_init();
        nc_verbosity(NC_VERB_WARNING);

        nc_client_ssh_set_auth_pref(NC_SSH_AUTH_INTERACTIVE, 10);
        nc_client_ssh_set_auth_pref(NC_SSH_AUTH_PASSWORD, 20);
        nc_client_ssh_set_auth_pref(NC_SSH_AUTH_PUBLICKEY, 30);

        initialized = true;
    }
}

NetconfSession::~NetconfSession() {
    disconnect();         // Ensure the session is disconnected
    nc_client_destroy();  // Cleanup libnetconf2 client - maybe not needed
}

bool NetconfSession::connect(const std::string &ip, int port, const std::string &user, const std::string &password) {
    if (session_ != nullptr) {
        disconnect();
    }

    spdlog::info("Connecting to {} on port {} as user {}", ip, port, user);

    if (nc_client_ssh_set_username(user.c_str()) != 0) {
        spdlog::error("Failed to set SSH username.");
        return false;
    }

    g_current_password = password;
    // Using first callback for password auth
    nc_client_ssh_set_auth_password_clb(password_interactive_clb, nullptr);
    // Using second callback for keyboard-interactive auth
    nc_client_ssh_set_auth_interactive_clb(kb_interactive_clb, nullptr);

    session_ = nc_connect_ssh(ip.c_str(), port, nullptr);

    g_current_password.clear();

    if (session_ == nullptr) {
        spdlog::error("[NetconfSession] Connection failed.");
        return false;
    }

    spdlog::info("[NetconfSession] Connected successfully.");
    return true;
}

void NetconfSession::disconnect() {
    if (session_ != nullptr) {
        nc_session_free(session_, nullptr);
        session_ = nullptr;
        spdlog::info("Disconnected from NETCONF session.");
    }
}

bool NetconfSession::isConnected() const {
    return session_ != nullptr;
}

std::string NetconfSession::getData(const std::string &xpath) {
    if (session_ == nullptr) {
        spdlog::error("[NetconfSession] Not connected.");
        return "";
    }

    // 1. Create RPC object
    std::string result_xml;
    struct nc_rpc *rpc = nullptr;

    if (xpath.empty()) {
        rpc = nc_rpc_get(nullptr, NC_WD_ALL, NC_PARAMTYPE_CONST);
    } else {
        rpc = nc_rpc_get(xpath.c_str(), NC_WD_ALL, NC_PARAMTYPE_CONST);
    }

    if (rpc == nullptr) {
        spdlog::error("[NetconfSession] Error: Failed to create RPC.");
        return "";
    }

    // 2. Send RPC to server
    uint64_t msgid;
    NC_MSG_TYPE status = nc_send_rpc(session_, rpc, 1000, &msgid);
    if (status == NC_MSG_ERROR || status == NC_MSG_WOULDBLOCK) {
        spdlog::error("[NetconfSession] Error: Failed to send RPC.");
        nc_rpc_free(rpc);
        return "";
    }

    struct lyd_node *envp = nullptr;  // Envelope (RPC wrapper)
    struct lyd_node *op = nullptr;    // Operation data

    NC_MSG_TYPE msgtype = nc_recv_reply(session_, rpc, msgid, 5000, &envp, &op);

    if (msgtype == NC_MSG_REPLY) {
        // Data received successfully
        if (op != nullptr) {
            char *str_out = nullptr;

            // libyang v3 Printing
            // LYD_XML: Format
            // LYD_PRINT_SIBLINGS: Recursively print all siblings
            lyd_print_mem(&str_out, op, LYD_XML, LYD_PRINT_SIBLINGS);

            if (str_out != nullptr) {
                result_xml = std::string(str_out);
                free(str_out);
            }
        } else {
            spdlog::info("[NetconfSession] Reply OK but empty data.");
        }
    } else if (msgtype == NC_MSG_ERROR) {
        spdlog::error("[NetconfSession] Server replied with ERROR.");
    }

    if (op != nullptr) {
        lyd_free_all(op);
    }
    if (envp != nullptr) {
        lyd_free_all(envp);
    }

    return result_xml;
}

bool NetconfSession::editData(const std::string &configXml) {
    if (session_ == nullptr) {
        spdlog::error("[NetconfSession] Not connected.");
        return false;
    }

    if (configXml.empty()) {
        spdlog::error("[NetconfSession] Configuration XML is empty.");
        return false;
    }

    // 1. Create RPC: <edit-config>
    struct nc_rpc *rpc = nc_rpc_edit(NC_DATASTORE_CANDIDATE, NC_RPC_EDIT_DFLTOP_MERGE, NC_RPC_EDIT_TESTOPT_TESTSET,
                                     NC_RPC_EDIT_ERROPT_STOP, configXml.c_str(), NC_PARAMTYPE_CONST);

    if (rpc == nullptr) {
        spdlog::error("[NetconfSession] Error: Failed to create edit-config RPC.");
        return false;
    }

    // 2. Send RPC to server
    uint64_t msgid;
    NC_MSG_TYPE status = nc_send_rpc(session_, rpc, 1000, &msgid);
    if (status == NC_MSG_ERROR || status == NC_MSG_WOULDBLOCK) {
        spdlog::error("[NetconfSession] Error: Failed to send edit-config RPC.");
        nc_rpc_free(rpc);
        return false;
    }

    // 3. Receive reply
    struct lyd_node *envp = nullptr;  // Envelope (RPC wrapper)
    struct lyd_node *op = nullptr;    // Operation data

    NC_MSG_TYPE msgtype = nc_recv_reply(session_, rpc, msgid, 5000, &envp, &op);

    bool success = false;
    if (msgtype == NC_MSG_REPLY) {
        // Edit-config successful
        success = true;
        spdlog::info("[NetconfSession] edit-config successful.");
    } else if (msgtype == NC_MSG_ERROR) {
        spdlog::error("[NetconfSession] Server replied with ERROR to edit-config.");
    }

    // Cleanup
    nc_rpc_free(rpc);
    if (op != nullptr) {
        lyd_free_all(op);
    }
    if (envp != nullptr) {
        lyd_free_all(envp);
    }

    return success;
}

bool NetconfSession::commit() {
    if (session_ == nullptr) {
        spdlog::error("[NetconfSession] Not connected.");
        return false;
    }

    // 1. Create RPC: <commit>
    struct nc_rpc *rpc = nc_rpc_commit(0, 0, nullptr, nullptr, NC_PARAMTYPE_CONST);

    if (rpc == nullptr) {
        spdlog::error("[NetconfSession] Error: Failed to create commit RPC.");
        return false;
    }

    // 2. Send RPC to server
    uint64_t msgid;
    NC_MSG_TYPE status = nc_send_rpc(session_, rpc, 1000, &msgid);
    if (status == NC_MSG_ERROR || status == NC_MSG_WOULDBLOCK) {
        spdlog::error("[NetconfSession] Error: Failed to send commit RPC.");
        nc_rpc_free(rpc);
        return false;
    }

    // 3. Receive reply
    struct lyd_node *envp = nullptr;  // Envelope (RPC wrapper)
    struct lyd_node *op = nullptr;    // Operation data

    NC_MSG_TYPE msgtype = nc_recv_reply(session_, rpc, msgid, 5000, &envp, &op);

    bool success = false;
    if (msgtype == NC_MSG_REPLY) {
        // Commit successful
        success = true;
        spdlog::info("[NetconfSession] commit successful.");
    } else if (msgtype == NC_MSG_ERROR) {
        spdlog::error("[NetconfSession] Server replied with ERROR to commit.");
    }

    // Cleanup
    nc_rpc_free(rpc);
    if (op != nullptr) {
        lyd_free_all(op);
    }
    if (envp != nullptr) {
        lyd_free_all(envp);
    }

    return success;
}
}  // namespace common