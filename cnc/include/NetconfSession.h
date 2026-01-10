#pragma once

#include <string>

// Forward declarations
// Netconf session and RPC structures
struct nc_session;

namespace common {
    class NetconfSession {
    public:
        // Constructor and Destructor
        NetconfSession();
        ~NetconfSession();

        /**
         * @brief Establishes a NETCONF session with the specified device via SSH.
         * @param ip The IP address of the device e.g., "192.168.1.1".
         * @param port The Netconf port of the device (standard is 830).
         * @param user The username for authentication.
         * @param password The password for authentication.
         * @return true if the connection is successful, false otherwise.
         */
        bool connect(const std::string& ip, int port, const std::string& user, const std::string& password);

        /**
         * @brief Disconnects the NETCONF session.
         */
        void disconnect();

        /**
         * @brief Checks if the NETCONF session is currently connected.
         * @return true if connected, false otherwise.
         */
        bool isConnected() const;

        /**
         * @brief Retrieves data from the NETCONF server using the specified XPath filter.
         * @param xpath The XPath filter to apply (default is empty, which retrieves all data).
         * @return The retrieved data as a string.
         */
        std::string getData(const std::string& xpath="");

    private:
        // Pointer to the underlying NETCONF session
        struct nc_session* session_;
    };
}