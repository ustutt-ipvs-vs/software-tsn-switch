#pragma once

#include <cstdint>
#include <string>

// Forward declarations
// Netconf session and RPC structures
struct nc_session;

namespace common {
/**
 * @brief Manages a NETCONF client session and basic datastore operations for a network device.
 *
 * This class wraps the underlying NETCONF session lifecycle, including SSH-based connection setup,
 * session teardown, and connectivity checks. It also provides helper methods for reading operational
 * data and applying configuration changes via candidate datastore edit/commit workflows.
 */
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
    [[nodiscard]] [[nodiscard]] bool isConnected() const;

    /**
     * @brief Retrieves data from the NETCONF server using the specified XPath filter.
     * @param xpath The XPath filter to apply (default is empty, which retrieves all data).
     * @return The retrieved data as a string.
     */
    std::string getData(const std::string& xpath = "");

    /**
     * @brief Edits the configuration data on the NETCONF server into the candidate datastore.
     * @param configXml The configuration data in XML format.
     * @return true if the edit operation is successful, false otherwise.
     */
    bool editData(const std::string& configXml);

    /**
     * @brief Commits the current configuration from the candidate datastore into the running datastore.
     * @return true if the commit operation is successful, false otherwise.
     */
    bool commit();

   private:
    // Pointer to the underlying NETCONF session
    struct nc_session* session_ = nullptr;
};
}  // namespace common