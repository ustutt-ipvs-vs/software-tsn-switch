#include "NetconfSession.h"

#include <libnetconf2/session_client.h>
#include <libnetconf2/log.h> // For logging functions

#include <iostream>
#include <cstring>

namespace common {
    // Global variable to hold the current password for the callback
    static std::string g_current_password;

    // Calls libnetconf2 if server requests a password
    char* password_interactive_clb(const char *username, const char *hostname, void *priv_data) {
        return strdup(g_current_password.c_str());
    }

    NetconfSession::NetconfSession() : session_(nullptr) {
        static bool initialized = false;

        if (!initialized) {
            nc_client_init();
            nc_verbosity(NC_VERB_WARNING);
            initialized = true;
        }
    }

    NetconfSession::~NetconfSession() {
        disconnect(); // Ensure the session is disconnected
        // nc_client_destroy(); // Cleanup libnetconf2 client - maybe not needed
    }

    bool NetconfSession::connect(const std::string& ip, int port, const std::string& user, const std::string& password) {
        if (session_) {
            std::cerr << "Session already connected." << std::endl;
            return false;
        }

        /*g_current_password = password; // Set the global password for the callback
        nc_client_ssh_set_auth_password_clb(password_interactive_clb, nullptr);

        nc_client_ssh_set_auth_pref(NC_SSH_AUTH_PUBLICKEY, 100);
        nc_client_ssh_set_auth_pref(NC_SSH_AUTH_PASSWORD, 50);
        nc_client_ssh_set_auth_pref(NC_SSH_AUTH_INTERACTIVE, 50);

        session_ = nc_connect_ssh(ip.c_str(), port, nullptr);

        g_current_password = ""; // Clear the global password after use

        if (session_ == nullptr) {
            std::cerr << "Failed to connect to " << ip << ":" << port << std::endl;
            return false;
        }   */

        std::cout << "Connected to " << ip << ":" << port << std::endl;
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
}