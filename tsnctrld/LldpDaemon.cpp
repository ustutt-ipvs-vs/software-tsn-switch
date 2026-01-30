#include "include/LldpDaemon.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

/**
 * @brief Convert a C string to std::string safely.
 *
 * @param cstr Pointer to a null-terminated C string (may be nullptr).
 * @return std::string containing the same content, or empty string if cstr is nullptr.
 */
static std::string convertCString(const char* cstr) {
    return (cstr != nullptr) ? std::string(cstr) : std::string();
}

/**
 * @brief LLDP callback function that calls the processEvent() method to handle the event
 *
 * @param type Change type (added/updated/deleted).
 * @param iface Interface atom.
 * @param neigh Neighbor atom (may be nullptr depending on event).
 * @param data Pointer to the LldpDaemon object.
 */
static void lldp_change_callback(lldpctl_change_t type, lldpctl_atom_t* iface, lldpctl_atom_t* neigh, void* data) {
    auto* self = static_cast<LldpDaemon*>(data);
    self->processEvent(type, iface, neigh);
}

/**
 * @brief Read a string attribute from an lldpctl atom.
 *
 * @param atom lldpctl atom.
 * @param key Attribute key.
 * @return String value or empty string if missing.
 */
std::string LldpDaemon::getStr(lldpctl_atom_t* atom, lldpctl_key_t key) {
    return convertCString(lldpctl_atom_get_str(atom, key));
}

/**
 * @brief Generate a time-mark value for remote-systems-data list instances.
 *
 * @return time-mark as uint32_t.
 */
uint32_t LldpDaemon::currentTimeMark() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

/**
 * @brief Construct LLDP daemon and starts two lldcptl connections. One for the query connection that handles the
 * initial snapshot reads on startup. One watch connection for event notification.
 *
 * @param operSession Reference to an operational sysrepo session used to write operational state.
 * @throws std::runtime_error if lldpd cannot be reached.
 */
LldpDaemon::LldpDaemon(sysrepo::Session& operSession) : m_operSess(operSession) {
    m_queryConn = lldpctl_new(nullptr, nullptr, nullptr);

    if (m_queryConn == nullptr) {
        throw std::runtime_error(
            "Failed to connect to lldpd (query) (lldpctl_new returned nullptr). Is lldpd running?");
    }

    m_watchConn = lldpctl_new(nullptr, nullptr, nullptr);
    if (m_watchConn == nullptr) {
        lldpctl_release(m_queryConn);
        m_queryConn = nullptr;
        throw std::runtime_error(
            "Failed to connect to lldpd (watch) (lldpctl_new returned nullptr). Is lldpd running?");
    }

    lldpctl_watch_callback2(m_watchConn, lldp_change_callback, this);
}

/**
 * @brief Destructor: stops watcher thread and releases lldpctl connections.
 */
LldpDaemon::~LldpDaemon() {
    m_stop = true;

    if (m_watchConn != nullptr) {
        lldpctl_release(m_watchConn);
        m_watchConn = nullptr;
    }
    if (m_watchThread.joinable()) {
        m_watchThread.join();
    }
    if (m_queryConn != nullptr) {
        lldpctl_release(m_queryConn);
        m_queryConn = nullptr;
    }
}

/**
 * @brief Initial snapshot sync of all currently known neighbors from lldpd. Writes into sysrepo operational datastore.
 */
void LldpDaemon::syncInitialNeighbors() {
    lldpctl_atom_t* iface = nullptr;
    lldpctl_atom_t* ifaces = lldpctl_get_interfaces(m_queryConn);

    std::cout << "[LLDP] Reading current neighbors from lldpd...\n";

    if (ifaces == nullptr) {
        std::cerr << "[LLDP] Failed to get interfaces: " << lldpctl_last_error(m_queryConn) << "\n";
        return;
    }

    {
        std::lock_guard guard(m_sessMutex);

        m_operSess.switchDatastore(sysrepo::Datastore::Operational);
        m_operSess.setOriginatorName("tsn-daemon-lldp");

        lldpctl_atom_foreach(ifaces, iface) {
            const std::string ifName = getStr(iface, lldpctl_k_interface_name);
            lldpctl_atom_t* port = lldpctl_get_port(iface);
            uint32_t remoteIndex = 1;
            lldpctl_atom_t* neigh = nullptr;

            if (port == nullptr) {
                continue;
            }

            lldpctl_atom_t* neighbors = lldpctl_atom_get(port, lldpctl_k_port_neighbors);
            if (neighbors == nullptr) {
                lldpctl_atom_dec_ref(port);
                continue;
            }

            lldpctl_atom_foreach(neighbors, neigh) {
                lldpctl_atom_t* chassis = lldpctl_atom_get(neigh, lldpctl_k_port_chassis);

                const uint32_t timeMark = currentTimeMark();
                const std::string chassisId = (chassis != nullptr) ? getStr(chassis, lldpctl_k_chassis_id) : "";
                const std::string systemName = (chassis != nullptr) ? getStr(chassis, lldpctl_k_chassis_name) : "";
                const std::string portId = getStr(neigh, lldpctl_k_port_id);

                const std::string base =
                    "/ieee802-dot1ab-lldp:lldp"
                    "/port[name='" +
                    ifName +
                    "']"
                    "[dest-mac-address='01-80-c2-00-00-0e']"
                    "/remote-systems-data"
                    "[time-mark='" +
                    std::to_string(timeMark) +
                    "']"
                    "[remote-index='" +
                    std::to_string(remoteIndex) + "']";

                m_operSess.setItem(base + "/chassis-id", chassisId);
                m_operSess.setItem(base + "/port-id", portId);
                m_operSess.setItem(base + "/system-name", systemName);

                remoteIndex++;

                if (chassis != nullptr) {
                    lldpctl_atom_dec_ref(chassis);
                }
            }

            lldpctl_atom_dec_ref(neighbors);
            lldpctl_atom_dec_ref(port);
        }

        m_operSess.applyChanges();
    }
    lldpctl_atom_dec_ref(ifaces);

    std::cout << "[LLDP] Operational datastore updated\n";
}

/**
 * @brief Start background watcher thread which listens for LLDP changes. Continuously calls lldctl_watch() and exits
 * once LldpDaemon is destructed.
 */
void LldpDaemon::startWatching() {
    if (m_watchThread.joinable()) return;

    m_stop = false;
    m_watchThread = std::thread([this]() {
        std::cout << "[LLDP] Watcher thread started. Waiting for events...\n";
        while (!m_stop) {
            if (lldpctl_watch(m_watchConn) < 0) {
                std::cerr << "[LLDP] Watcher error: " << lldpctl_last_error(m_watchConn) << "\n";
                break;
            }
            if (m_stop) break;
        }
        std::cout << "[LLDP] Watcher thread exiting.\n";
    });
}

/**
 * @brief Start background watcher thread which listens for LLDP changes. Once a change event (deleted, added, updated)
 * occurs, update the operational data store accordingly.
 */
void LldpDaemon::processEvent(lldpctl_change_t type, lldpctl_atom_t* iface, lldpctl_atom_t* neigh) {
    std::lock_guard guard(m_sessMutex);

    m_operSess.switchDatastore(sysrepo::Datastore::Operational);
    m_operSess.setOriginatorName("lldp-daemon-watcher");

    const std::string ifName = getStr(iface, lldpctl_k_interface_name);
    const std::string basePort =
        "/ieee802-dot1ab-lldp:lldp/port[name='" + ifName + "'][dest-mac-address='01-80-c2-00-00-0e']";

    if (type == lldpctl_c_deleted) {
        std::cout << "[LLDP] [EVENT] Neighbor lost on " << ifName << ". Cleaning datastore.\n";
        m_operSess.deleteItem(basePort + "/remote-systems-data");
        m_operSess.applyChanges();
        return;
    }

    if (type == lldpctl_c_added || type == lldpctl_c_updated) {
        std::cout << "[LLDP] [EVENT] Neighbor update on " << ifName << "\n";
        refreshPortNeighbors(ifName);
        m_operSess.applyChanges();
    }
}

/**
 * @brief Handle LLDP change event from liblldpctl watcher. Deletes nodes in lldp yang tree on delete events or
 * updates/builds new nodes on update/added event.
 *
 * @param type Change type.
 * @param iface Interface atom (must not be nullptr).
 * @param neigh Neighbor atom (may be nullptr depending on event).
 */
void LldpDaemon::refreshPortNeighbors(const std::string& ifName) {
    lldpctl_atom_t* ifaces = lldpctl_get_interfaces(m_queryConn);
    if (!ifaces) {
        std::cerr << "[LLDP] refreshPortNeighbors: get_interfaces failed: " << lldpctl_last_error(m_queryConn) << "\n";
    }

    const std::string basePort =
        "/ieee802-dot1ab-lldp:lldp/port[name='" + ifName + "'][dest-mac-address='01-80-c2-00-00-0e']";
    m_operSess.deleteItem(basePort + "/remote-systems-data");

    lldpctl_atom_t* iface = nullptr;
    lldpctl_atom_foreach(ifaces, iface) {
        const std::string name = getStr(iface, lldpctl_k_interface_name);
        if (name != ifName) continue;

        lldpctl_atom_t* port = lldpctl_get_port(iface);
        if (!port) break;

        lldpctl_atom_t* neighbors = lldpctl_atom_get(port, lldpctl_k_port_neighbors);
        if (!neighbors) {
            lldpctl_atom_dec_ref(port);
            break;
        }

        uint32_t remoteIndex = 1;
        lldpctl_atom_t* neigh = nullptr;
        lldpctl_atom_foreach(neighbors, neigh) {
            lldpctl_atom_t* chassis = lldpctl_atom_get(neigh, lldpctl_k_port_chassis);

            const uint32_t timeMark = currentTimeMark();
            const std::string chassisId = chassis ? getStr(chassis, lldpctl_k_chassis_id) : "";
            const std::string systemName = chassis ? getStr(chassis, lldpctl_k_chassis_name) : "";
            const std::string portId = getStr(neigh, lldpctl_k_port_id);

            const std::string base =
                "/ieee802-dot1ab-lldp:lldp"
                "/port[name='" +
                ifName +
                "']"
                "[dest-mac-address='01-80-c2-00-00-0e']"
                "/remote-systems-data"
                "[time-mark='" +
                std::to_string(timeMark) +
                "']"
                "[remote-index='" +
                std::to_string(remoteIndex) + "']";

            m_operSess.setItem(base + "/chassis-id", chassisId);
            m_operSess.setItem(base + "/port-id", portId);
            m_operSess.setItem(base + "/system-name", systemName);

            remoteIndex++;

            if (chassis) lldpctl_atom_dec_ref(chassis);
        }

        lldpctl_atom_dec_ref(neighbors);
        lldpctl_atom_dec_ref(port);
        break;
    }

    lldpctl_atom_dec_ref(ifaces);
}