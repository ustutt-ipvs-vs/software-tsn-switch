#include "NetconfSession.h"

#ifndef NC_ENABLED_SSH_TlS
#define NC_ENABLED_SSH_TLS
#endif

#include <libnetconf2/session_client.h>
#include <libnetconf2/session.h>
#include <libnetconf2/log.h> // For logging functions
#include <libyang/libyang.h> // For libyang functions

#include <iostream>
#include <cstring>
#include <vector>

namespace common {
    // Global variable to hold the current password for the callback
    static std::string g_current_password;

    // Callback function for password authentication
    char* password_interactive_clb(const char *username, const char *hostname, void *priv_data) {
        return strdup(g_current_password.c_str());
    }

    // Callback function for keyboard-interactive authentication (bad solution --> maybe change in future)
    char* kb_interactive_clb(const char *auth_name, const char *instruction, const char *prompt, int echo, void *priv_data) {
        // Only one prompt is expected for password
        return strdup(g_current_password.c_str());
    }

    NetconfSession::NetconfSession() : session_(nullptr) {
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
        disconnect(); // Ensure the session is disconnected
        nc_client_destroy(); // Cleanup libnetconf2 client - maybe not needed
    }

    bool NetconfSession::connect(const std::string& ip, int port, const std::string& user, const std::string& password) {
        if(session_) {
            disconnect();
        }

        std::cout << "Connecting to " << ip << " on port " << port << " as user " << user << std::endl;

        if (nc_client_ssh_set_username(user.c_str()) != 0) {
            std::cerr << "Failed to set SSH username." << std::endl;
            return false;
        }

        g_current_password = password;
        // Using first callback for password auth
        nc_client_ssh_set_auth_password_clb(password_interactive_clb, nullptr);
        // Using second callback for keyboard-interactive auth
        nc_client_ssh_set_auth_interactive_clb(kb_interactive_clb, nullptr);

        session_ = nc_connect_ssh(ip.c_str(), port, nullptr);

        g_current_password.clear();

        if (!session_) {
            std::cerr << "[NetconfSession] Connection failed." << std::endl;
            return false;
        }

        std::cout << "[NetconfSession] Connected successfully." << std::endl;
        return true;
    }

    void NetconfSession::disconnect() {
        if (session_) {
            nc_session_free(session_, nullptr);
            session_ = nullptr;
            std::cout << "Disconnected from NETCONF session." << std::endl;
        }
    }

    bool NetconfSession::isConnected() const {
        return session_ != nullptr;
    }

    std::string NetconfSession::getData(const std::string& xpath) {
        if (!session_) {
            std::cerr << "[NetconfSession] Not connected." << std::endl;
            return "";
        }

        // 1. Create RPC object
        std::string result_xml = "";
        struct nc_rpc *rpc = nullptr;

        if (xpath.empty()) {
            rpc = nc_rpc_get(nullptr, NC_WD_ALL, NC_PARAMTYPE_CONST);
        } else {
            rpc = nc_rpc_get(xpath.c_str(), NC_WD_ALL, NC_PARAMTYPE_CONST);
        }

        if (!rpc) {
            std::cerr << "[NetconfSession] Error: Failed to create RPC." << std::endl;
            return "";
        }

        // 2. Send RPC to server
        uint64_t msgid;
        NC_MSG_TYPE status = nc_send_rpc(session_, rpc, 1000, &msgid);
        if (status == NC_MSG_ERROR || status == NC_MSG_WOULDBLOCK) {
            std::cerr << "[NetconfSession] Error: Failed to send RPC." << std::endl;
            nc_rpc_free(rpc);
            return "";
        }
        nc_rpc_free(rpc); // RPC object can be freed after sending

        struct lyd_node *envp = nullptr; // Envelope (RPC wrapper)
        struct lyd_node *op = nullptr;   // Operation data
        
        NC_MSG_TYPE msgtype = nc_recv_reply(session_, rpc, msgid, 5000, &envp, &op);

        if (msgtype == NC_MSG_REPLY) {
            // Data received successfully
            if (op) {
                char *str_out = nullptr;
                
                // libyang v3 Printing
                // LYD_XML: Format
                // LYD_PRINT_SIBLINGS: Recursively print all siblings
                lyd_print_mem(&str_out, op, LYD_XML, LYD_PRINT_SIBLINGS);
                
                if (str_out) {
                    result_xml = std::string(str_out);
                    free(str_out);
                }
            } else {
                std::cout << "[NetconfSession] Reply OK but empty data." << std::endl;
            }
        } else if (msgtype == NC_MSG_ERROR) {
            std::cerr << "[NetconfSession] Server replied with ERROR." << std::endl;
        }

        if (op) lyd_free_all(op);
        if (envp) lyd_free_all(envp);
        
        return result_xml;
    }
}