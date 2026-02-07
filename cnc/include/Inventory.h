#pragma once
#include <map>
#include <string>

namespace cnc {
/**
 * @brief Structure to hold device credentials for network devices.
 * Used for SSH network management access.
 */
struct DeviceCredentials_t {
    std::string ip;
    std::string username;
    std::string password;
};

// Map of device hostname to their credentials
using InventoryMap = std::map<std::string, DeviceCredentials_t>;
}  // namespace cnc