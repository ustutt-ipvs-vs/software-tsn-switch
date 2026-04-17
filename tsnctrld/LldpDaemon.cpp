#include "include/LldpDaemon.h"

#include <arpa/inet.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <chrono>
#include <iostream>
#include <stdexcept>

#include "PerformanceLogger.h"

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
    PERFORMANCE_LOGGING("[LLDP] [CHANGE_EVENT]", "Start");
    auto* self = static_cast<LldpDaemon*>(data);
    self->processEvent(type, iface, neigh);
    PERFORMANCE_LOGGING("[LLDP] [CHANGE_EVENT]", "End");
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
 * @brief Map lldpctl chassis ID subtype integer to YANG enum string.
 *
 * The YANG model ieee802-dot1ab-lldp uses ieee:chassis-id-subtype-type which
 * is an enumeration. lldpctl returns an integer matching LLDP_CHASSISID_SUBTYPE_*
 * constants. This function maps between the two.
 *
 * @param lldpVal string value out of the chassis object defining the chassis subtype
 */
static std::string mapChassisIdSubtype(const std::string& lldpVal) {
    if (lldpVal == "mac") {
        return "mac-address";
    }
    if (lldpVal == "ifname") {
        return "interface-name";
    }
    if (lldpVal == "local") {
        return "locally-assigned";
    }
    if (lldpVal == "ip") {
        return "network-address";
    }
    if (lldpVal == "chassis") {
        return "chassis-component";
    }
    if (lldpVal == "ifalias") {
        return "interface-alias";
    }
    if (lldpVal == "port") {
        return "port-component";
    }
    return "";
}

/**
 * @brief Map lldpctl port ID subtype integer to YANG enum string.
 *
 * The YANG model uses ieee:port-id-subtype-type enumeration.
 * lldpctl returns an integer matching LLDP_PORTID_SUBTYPE_* constants.
 *
 * @param lldpVal string value out of the neighbor atom defining the port subtype
 */
static std::string mapPortIdSubtype(const std::string& lldpVal) {
    if (lldpVal == "ifname") {
        return "interface-name";
    }
    if (lldpVal == "mac") {
        return "mac-address";
    }
    if (lldpVal == "local") {
        return "local";
    }
    if (lldpVal == "ip") {
        return "network-address";
    }
    if (lldpVal == "ifalias") {
        return "interface-alias";
    }
    if (lldpVal == "port") {
        return "port-component";
    }
    if (lldpVal == "agent") {
        return "agent-circuit-id";
    }
    return "";
}

/**
 * @brief Map lldpctl system capabilities bitmask to a space-separated YANG bits string.
 *
 * The YANG model uses lldp-types:system-capabilities-map which is a YANG 'bits' type.
 * sysrepo expects a space-separated string of bit names that are set.
 * lldpctl returns an integer bitmask using LLDP_CAP_* constants.
 *
 * @param caps integer out of the chassis capabilities atom defining the capabilities type
 */
static std::string mapCapabilities(int caps) {
    std::string result;

    auto append = [&](const char* bit) {
        if (!result.empty()) {
            result += ' ';
        }
        result += bit;
    };

    if ((caps & (1 << 0)) != 0) {
        append("other");
    }
    if ((caps & (1 << 1)) != 0) {
        append("repeater");
    }
    if ((caps & (1 << 2)) != 0) {
        append("bridge");
    }
    if ((caps & (1 << 3)) != 0) {
        append("wlan-access-point");
    }
    if ((caps & (1 << 4)) != 0) {
        append("router");
    }
    if ((caps & (1 << 5)) != 0) {
        append("telephone");
    }
    if ((caps & (1 << 6)) != 0) {
        append("docsis-cable-device");
    }
    if ((caps & (1 << 7)) != 0) {
        append("station-only");
    }
    if ((caps & (1 << 8)) != 0) {
        append("cvlan-component");
    }
    if ((caps & (1 << 9)) != 0) {
        append("svlan-component");
    }
    if ((caps & (1 << 10)) != 0) {
        append("two-port-mac-relay");
    }

    return result;
}

/**
 * @brief Convert a dotted-decimal IPv4 address string to uppercase hex string.
 *
 * Converts an IPv4 address (e.g. "192.168.1.1") to the hex encoded format
 * required by the YANG type man-addr-type (e.g. "C0A80101").
 *
 * @param ip Dotted-decimal IPv4 address string (e.g. "192.168.1.1").
 * @return Uppercase hex string (e.g. "C0A80101"), or empty string if conversion fails.
 */
static std::string ipv4ToHex(const std::string& ip) {
    in_addr addr{};
    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
        return "";
    }

    std::array<uint8_t, 4> bytes{};
    std::memcpy(bytes.data(), &addr.s_addr, sizeof(bytes));

    std::ostringstream oss;
    for (const uint8_t byte : bytes) {
        oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

/**
 * @brief Convert an IPv6 address string to uppercase hex string.
 *
 * Converts an IPv6 address (e.g. "fe80::1") to the hex encoded format
 * required by the YANG type man-addr-type (e.g. "FE800000000000000000000000000001").
 *
 * @param ip IPv6 address string in any valid notation (e.g. "fe80::1").
 * @return Uppercase hex string of 32 characters, or empty string if conversion fails.
 */
static std::string ipv6ToHex(const std::string& ip) {
    in6_addr addr{};
    if (inet_pton(AF_INET6, ip.c_str(), &addr) != 1) {
        return "";
    }

    std::ostringstream oss;
    for (const uint8_t byte : addr.s6_addr) {
        oss << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

/**
 * @brief Construct LLDP daemon and starts two lldcptl connections. One for the query connection that handles the
 * initial snapshot reads on startup. One watch connection for event notification.
 *
 * The watch connection must be created manually with custom callbacks because otherwise there is no way to stop the
 * monitoring loop without an event happening.
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
    try {
        m_watchConn_Fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_watchConn_Fd < 0) {
            throw std::runtime_error("Failed to open the lldp socket. Is lldpd running?");
        }

        struct sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, lldpctl_get_default_transport(), sizeof(addr.sun_path) - 1);
        auto res = connect(m_watchConn_Fd, (struct sockaddr*)&addr, sizeof(addr));
        if (res != 0) {
            throw std::runtime_error("Failed to connect to the lldp socket directly. Is lldpd running?");
        }

        m_watchConn = lldpctl_new(watch_send_cb, watch_recv_cb, this);
        if (m_watchConn == nullptr) {
            throw std::runtime_error(
                "Failed to connect to lldpd (watch) (lldpctl_new returned nullptr). Is lldpd running?");
        }

        lldpctl_watch_callback2(m_watchConn, lldp_change_callback, this);
    } catch (...) {
        if (m_queryConn != nullptr) {
            lldpctl_release(m_queryConn);
            m_queryConn = nullptr;
        }
        if (m_watchConn_Fd >= 0) {
            close(m_watchConn_Fd);
            m_watchConn_Fd = -1;
        }
        throw;
    }
}

/**
 * @brief Write all supported remote-systems-data leaves for a single neighbor into sysrepo.
 *
 * Extracted as a helper to avoid duplicating the logic between syncInitialNeighbors()
 * and refreshPortNeighbors().
 *
 * @param base      The XPath base for this remote-systems-data instance.
 * @param neigh     lldpctl neighbor atom.
 * @param chassis   lldpctl chassis atom (may be nullptr).
 */
void LldpDaemon::writeNeighborData(const std::string& base, lldpctl_atom_t* neigh, lldpctl_atom_t* chassis) {
    const std::string portId = getStr(neigh, lldpctl_k_port_id);
    const std::string portDescr = getStr(neigh, lldpctl_k_port_descr);
    const std::string portIdSubtype = mapPortIdSubtype(getStr(neigh, lldpctl_k_port_id_subtype));

    m_operSess.setItem(base + "/port-id", portId);
    if (!portIdSubtype.empty()) {
        m_operSess.setItem(base + "/port-id-subtype", portIdSubtype);
    } else {
        SPDLOG_WARN("[LLDP] Unknown port-id-subtype integer");
    }

    if (!portDescr.empty()) {
        m_operSess.setItem(base + "/port-desc", portDescr);
    }

    // --- Chassis fields ---
    if (chassis == nullptr) {
        return;
    }

    const std::string chassisId = getStr(chassis, lldpctl_k_chassis_id);
    const std::string systemName = getStr(chassis, lldpctl_k_chassis_name);
    const std::string systemDescr = getStr(chassis, lldpctl_k_chassis_descr);
    const std::string chassisIdSubtype = mapChassisIdSubtype(getStr(chassis, lldpctl_k_chassis_id_subtype));

    m_operSess.setItem(base + "/chassis-id", chassisId);

    if (!chassisIdSubtype.empty()) {
        m_operSess.setItem(base + "/chassis-id-subtype", chassisIdSubtype);
    } else {
        SPDLOG_WARN("[LLDP] Unknown chassis-id-subtype integer: {}");
    }

    if (!systemName.empty()) {
        m_operSess.setItem(base + "/system-name", systemName);
    }
    if (!systemDescr.empty()) {
        m_operSess.setItem(base + "/system-description", systemDescr);
    }

    // --- Capabilties ---
    const int capsSupported = static_cast<int>(lldpctl_atom_get_int(chassis, lldpctl_k_chassis_cap_available));
    const int capsEnabled = static_cast<int>(lldpctl_atom_get_int(chassis, lldpctl_k_chassis_cap_enabled));
    const std::string capsSupportedStr = mapCapabilities(capsSupported);
    const std::string capsEnabledStr = mapCapabilities(capsEnabled);

    if (!capsSupportedStr.empty()) {
        m_operSess.setItem(base + "/system-capabilities-supported", capsSupportedStr);
    }
    if (!capsEnabledStr.empty()) {
        m_operSess.setItem(base + "/system-capabilities-enabled", capsEnabledStr);
    }

    lldpctl_atom_t* mgmtAddrs = lldpctl_atom_get(chassis, lldpctl_k_chassis_mgmt);
    if (mgmtAddrs != nullptr) {
        lldpctl_atom_t* mgmt = nullptr;
        lldpctl_atom_foreach(mgmtAddrs, mgmt) {
            const std::string addr = getStr(mgmt, lldpctl_k_mgmt_ip);

            if (addr.empty()) {
                continue;
            }

            const bool isIpv6 = addr.find(':') != std::string::npos;
            const std::string addrSubtype = isIpv6 ? "ietf-routing:ipv6" : "ietf-routing:ipv4";
            const std::string addrHex = isIpv6 ? ipv6ToHex(addr) : ipv4ToHex(addr);

            uint32_t ifId = lldpctl_atom_get_int(mgmt, lldpctl_k_mgmt_iface_index);
            std::string ifSubtype = "port-ref";  // One of unknown or port-ref or system-port-number

            std::string mgmtBase;
            mgmtBase.reserve(base.size() + 64);
            mgmtBase.append(base);
            mgmtBase.append("/management-address");
            mgmtBase.append("[address-subtype='");
            mgmtBase.append(addrSubtype);
            mgmtBase.append("']");
            mgmtBase.append("[address='");
            mgmtBase.append(addrHex);
            mgmtBase.append("']");

            try {
                m_operSess.setItem(mgmtBase + "/if-subtype", ifSubtype);
                m_operSess.setItem(mgmtBase + "/if-id", std::to_string(ifId));
            } catch (const std::exception& e) {
                spdlog::error("[LLDP] management-address setItem failed. addr={} subtype={} hex={} error: {}", addr,
                              addrSubtype, addrHex, e.what());
                continue;
            }
        }
        lldpctl_atom_dec_ref(mgmtAddrs);
    }
}

/**
 * @brief Destructor: stops watcher thread and releases lldpctl connections.
 */
LldpDaemon::~LldpDaemon() {
    m_stop = true;
    if (m_watchThread.joinable()) {
        m_watchThread.join();
    }
    if (m_watchConn != nullptr) {
        lldpctl_release(m_watchConn);
        m_watchConn = nullptr;
    }
    if (m_queryConn != nullptr) {
        lldpctl_release(m_queryConn);
        m_queryConn = nullptr;
    }
    spdlog::info("Stopped LLDP monitoring thread...");
}

void LldpDaemon::getConfigData(LldpNode_t& lldp_node) {
    lldpctl_atom_t* lldp_config = nullptr;
    {
        std::lock_guard guard(m_queryMutex);

        lldp_config = lldpctl_get_configuration(m_queryConn);

        SPDLOG_DEBUG("[LLDP] Reading local config from lldpd...");

        if (lldp_config == nullptr) {
            spdlog::error("[LLDP] Failed to get config: {}", lldpctl_last_strerror(m_queryConn));
            return;
        }
    }
    lldp_node.messageTxInterval = lldpctl_atom_get_int(lldp_config, lldpctl_k_config_tx_interval);
    lldp_node.messageTxHoldMultiplier = lldpctl_atom_get_int(lldp_config, lldpctl_k_config_tx_hold);
    lldp_node.messageFastTx = lldpctl_atom_get_int(lldp_config, lldpctl_k_config_fast_start_interval);

    const int receive_only = static_cast<int>(lldpctl_atom_get_int(lldp_config, lldpctl_k_config_receiveonly));

    lldpctl_atom_t* iface = nullptr;
    lldpctl_atom_t* ifaces = nullptr;

    {
        std::lock_guard guard(m_queryMutex);
        ifaces = lldpctl_get_interfaces(m_queryConn);

        SPDLOG_DEBUG("[LLDP] Reading interfaces from lldpd...");

        if (ifaces == nullptr) {
            spdlog::error("[LLDP] Failed to get interfaces: {}", lldpctl_last_strerror(m_queryConn));
            return;
        }
    }

    lldpctl_atom_foreach(ifaces, iface) {
        const std::string ifName = getStr(iface, lldpctl_k_interface_name);

        auto& currentPort = lldp_node.ports.emplace_back();
        currentPort.name = ifName;
        currentPort.destMacAddress = "01-80-c2-00-00-0e";
        if (receive_only != 0) {
            currentPort.adminStatus = "rx-only";
        } else {
            lldpctl_atom_t* port = lldpctl_get_port(iface);
            const int status = static_cast<int>(lldpctl_atom_get_int(port, lldpctl_k_port_status));
            switch (status) {
                case 1:  // LLDPD_RXTX_TXONLY
                    currentPort.adminStatus = "tx-only";
                    break;
                case 2:  // LLDPD_RXTX_RXONLY
                    currentPort.adminStatus = "rx-only";
                    break;
                case 4:  // LLDPD_RXTX_BOTH
                    currentPort.adminStatus = "tx-and-rx";
                    break;
                case 3:  // LLDPD_RXTX_DISABLED
                default:
                    currentPort.adminStatus = "disabled";
                    break;
            }
        }
    }

    lldpctl_atom_dec_ref(lldp_config);
    lldpctl_atom_dec_ref(ifaces);
}

void LldpDaemon::getLocalInfo(LldpNode_t& lldp_node) {
    lldpctl_atom_t* chassis = nullptr;
    {
        std::lock_guard guard(m_queryMutex);

        chassis = lldpctl_get_local_chassis(m_queryConn);

        if (chassis == nullptr) {
            spdlog::error("[LLDP] Failed to get chassis: {}", lldpctl_last_strerror(m_queryConn));
            return;
        }
    }

    const std::string chassisId = getStr(chassis, lldpctl_k_chassis_id);
    const std::string systemName = getStr(chassis, lldpctl_k_chassis_name);
    const std::string systemDescr = getStr(chassis, lldpctl_k_chassis_descr);
    const std::string chassisIdSubtype = mapChassisIdSubtype(getStr(chassis, lldpctl_k_chassis_id_subtype));

    const int capsSupported = static_cast<int>(lldpctl_atom_get_int(chassis, lldpctl_k_chassis_cap_available));
    const int capsEnabled = static_cast<int>(lldpctl_atom_get_int(chassis, lldpctl_k_chassis_cap_enabled));
    const std::string capsSupportedStr = mapCapabilities(capsSupported);
    const std::string capsEnabledStr = mapCapabilities(capsEnabled);

    lldp_node.localSystemData.chassisId = chassisId;
    lldp_node.localSystemData.systemName = systemName;
    lldp_node.localSystemData.systemDescription = systemDescr;
    lldp_node.localSystemData.chassisIdSubtype = chassisIdSubtype;

    lldp_node.localSystemData.systemCapabilitiesSupported = capsSupportedStr;
    lldp_node.localSystemData.systemCapabilitiesEnabled = capsEnabledStr;
}

/**
 * @brief Initial snapshot sync of all currently known neighbors from lldpd. Writes into sysrepo operational datastore.
 */
void LldpDaemon::syncInitialNeighbors() {
    lldpctl_atom_t* iface = nullptr;
    lldpctl_atom_t* ifaces = nullptr;
    {
        std::lock_guard guard(m_queryMutex);

        ifaces = lldpctl_get_interfaces(m_queryConn);

        SPDLOG_DEBUG("[LLDP] Reading current neighbors from lldpd...");

        if (ifaces == nullptr) {
            spdlog::error("[LLDP] Failed to get interfaces: {}", lldpctl_last_strerror(m_queryConn));
            return;
        }
    }
    {
        std::lock_guard guard(m_sessMutex);

        m_operSess.switchDatastore(sysrepo::Datastore::Operational);
        m_operSess.setOriginatorName("tsn-daemon-lldp");

        lldpctl_atom_foreach(ifaces, iface) {
            const std::string ifName = getStr(iface, lldpctl_k_interface_name);
            lldpctl_atom_t* port = lldpctl_get_port(iface);
            uint32_t remoteIndex = 1;

            if (port == nullptr) {
                continue;
            }

            lldpctl_atom_t* neighbors = lldpctl_atom_get(port, lldpctl_k_port_neighbors);
            if (neighbors == nullptr) {
                lldpctl_atom_dec_ref(port);
                continue;
            }

            lldpctl_atom_t* neigh = nullptr;
            lldpctl_atom_foreach(neighbors, neigh) {
                lldpctl_atom_t* chassis = lldpctl_atom_get(neigh, lldpctl_k_port_chassis);
                const uint32_t timeMark = currentTimeMark();

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

                writeNeighborData(base, neigh, chassis);

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
    SPDLOG_DEBUG("[LLDP] Operational datastore updated");
}

/**
 * @brief Start background watcher thread which listens for LLDP changes. Continuously calls lldctl_watch() and exits
 * once LldpDaemon is destructed.
 *
 * The function `lldpctl_watch` blocks the entire thread until an event happens, even if the program is supposed to
 * terminate. We use custom send- and receive-callbacks to be able to add a timeout. This workaround is needed in
 * version 1.0.18 and apparently fixed by the function `lldpctl_watch_sync_unblock` starting from version 1.0.19.
 */
void LldpDaemon::startWatching() {
    if (m_watchThread.joinable()) {
        return;
    }
    m_stop = false;
    m_watchThread = std::thread([this]() {
        SPDLOG_DEBUG("[LLDP] Watcher thread started. Waiting for events...");
        while (!m_stop) {
            if (lldpctl_watch(m_watchConn) < 0) {
                if (lldpctl_last_error(m_watchConn) == -501) {
                    // The `LLDPCTL_ERR_WOULDBLOCK` error with description "A IO related operation would block if
                    // performed". We (mis)use this to recognize a timeout of our custom @ref watch_recv_cb.
                    continue;
                }
                spdlog::error("[LLDP] Watcher error: {}", lldpctl_last_strerror(m_watchConn));
                break;
            }
            if (m_stop) {
                break;
            }
        }
        SPDLOG_DEBUG("[LLDP] Watcher thread exiting.");
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

    switch (type) {
        case lldpctl_c_deleted:
            SPDLOG_DEBUG("[LLDP] [EVENT] Neighbor lost on {}. Cleaning datastore.", ifName);
            m_operSess.deleteItem(basePort + "/remote-systems-data");
            m_operSess.applyChanges();
            break;
        case lldpctl_c_updated:
        case lldpctl_c_added:
            SPDLOG_DEBUG("[LLDP] [EVENT] Neighbor update on {}", ifName);
            refreshPortNeighbors(ifName);
            m_operSess.applyChanges();
            break;
    }

    auto notif_node =
        m_operSess.getContext().newPath2("/ieee802-dot1ab-lldp:remote-table-change", std::nullopt).createdNode;
    if (!notif_node.has_value()) {
        spdlog::warn("[LLDP] [EVENT] notif_node of type std::optional has no value, makes no sense, returning...");
        return;
    }

    m_operSess.sendNotification(notif_node.value(), sysrepo::Wait::No);
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
    lldpctl_atom_t* ifaces = nullptr;
    {
        std::lock_guard guard(m_queryMutex);
        ifaces = lldpctl_get_interfaces(m_queryConn);
        if (ifaces == nullptr) {
            spdlog::error("[LLDP] refreshPortNeighbors: get_interfaces failed: {}", lldpctl_last_strerror(m_queryConn));
        }
    }
    const std::string basePort =
        "/ieee802-dot1ab-lldp:lldp/port[name='" + ifName + "'][dest-mac-address='01-80-c2-00-00-0e']";
    m_operSess.deleteItem(basePort + "/remote-systems-data");

    lldpctl_atom_t* iface = nullptr;
    lldpctl_atom_foreach(ifaces, iface) {
        const std::string name = getStr(iface, lldpctl_k_interface_name);
        if (name != ifName) {
            continue;
        }

        lldpctl_atom_t* port = lldpctl_get_port(iface);
        if (port == nullptr) {
            continue;
        }

        lldpctl_atom_t* neighbors = lldpctl_atom_get(port, lldpctl_k_port_neighbors);
        if (neighbors == nullptr) {
            lldpctl_atom_dec_ref(port);
            break;
        }

        uint32_t remoteIndex = 1;
        lldpctl_atom_t* neigh = nullptr;
        lldpctl_atom_foreach(neighbors, neigh) {
            lldpctl_atom_t* chassis = lldpctl_atom_get(neigh, lldpctl_k_port_chassis);
            const uint32_t timeMark = currentTimeMark();

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

            writeNeighborData(base, neigh, chassis);

            remoteIndex++;

            if (chassis != nullptr) {
                lldpctl_atom_dec_ref(chassis);
            }
        }

        lldpctl_atom_dec_ref(neighbors);
        lldpctl_atom_dec_ref(port);
        break;
    }

    lldpctl_atom_dec_ref(ifaces);
}

/*
 * The following two callback functions (watch_send_cb and watch_recv_cb) are adapted from liblldpctl.
 *
 * Copyright (c) 2012 Vincent Bernat <bernat@luffy.cx>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * @brief Reimplementation of the internal `sync_send` callback in the lldpctl library. Copyright (c) 2012 Vincent
 * Bernat <bernat@luffy.cx>.
 *
 * @param conn
 * @param data
 * @param length
 * @param user_data
 * @return
 */
ssize_t LldpDaemon::watch_send_cb(lldpctl_conn_t* conn, const uint8_t* data, size_t length, void* user_data) {
    auto* self = static_cast<LldpDaemon*>(user_data);
    int fd = self->m_watchConn_Fd;
    ssize_t nb;

    while ((nb = write(fd, data, length)) == -1) {
        if (errno == EAGAIN || errno == EINTR) {
            continue;
        }
        return -902;  // LLDPCTL_ERR_CALLBACK_FAILURE
    }
    return nb;
}

/**
 * @brief Reimplementation of the internal `sync_recv` callback in the lldpctl library, with added timeout logic.
 * Copyright (c) 2012 Vincent Bernat <bernat@luffy.cx>.
 *
 * @param conn The lldpctl connection. Unused because we pass the sockets file descriptor via @ref user_data.
 * @param data
 * @param length
 * @param user_data A pointer to this instance of the @ref LldpDaemon. Used to access the sockets file descriptor.
 * @return
 */
ssize_t LldpDaemon::watch_recv_cb(lldpctl_conn_t* conn, const uint8_t* data, size_t length, void* user_data) {
    auto* self = static_cast<LldpDaemon*>(user_data);
    int fd = self->m_watchConn_Fd;
    ssize_t nb = 0;
    size_t remain = length;
    size_t offset = 0;

    do {
        // --- INJECTED TIMEOUT LOGIC ---
        struct pollfd pfd = {.fd = fd, .events = POLLIN, .revents = 0};
        int p_res = poll(&pfd, 1, 500);  // 500ms timeout

        if (p_res == 0) {  // Timeout occurred
            if (offset > 0) {
                return static_cast<ssize_t>(offset);
            }             // Return what we have so far
            return -501;  // LLDPCTL_ERR_WOULDBLOCK
        }
        if (p_res < 0) {  // Error in poll
            if (errno == EINTR) {
                continue;
            }
            return -902;  // LLDPCTL_ERR_CALLBACK_FAILURE
        }
        // ------------------------------

        // Original read logic
        nb = read(fd, (unsigned char*)data + offset, remain);
        if (nb == -1) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            return -902;  // LLDPCTL_ERR_CALLBACK_FAILURE
        }

        remain -= nb;
        offset += nb;
    } while (remain > 0 && nb != 0);

    return static_cast<ssize_t>(offset);
}
