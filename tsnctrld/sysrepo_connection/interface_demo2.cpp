#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Linux & Sysrepo
#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <sys/types.h>
#include <unistd.h>

#include <libyang-cpp/Context.hpp>
#include <sysrepo-cpp/Changes.hpp>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/Session.hpp>
#include <sysrepo-cpp/Subscription.hpp>

// Structs provided in CncTypes.h
#include "../../common/include/CncTypes.h"

/* ---------------------------------------------------------
 * HARDWARE ABSTRACTION LAYER (MOCKS)
 * --------------------------------------------------------- */

// Requirement 2 & 8: Fetch data from Kernel/Netlink
// FIX: Added explicit initialization for adminCycleTime to prevent "Value 0" validation crash
GclConfig_t fetch_hw_state(const std::string& ifname) {
    std::cout << "[HW-FETCH] [DEBUG] Fetching state for " << ifname << "..." << std::endl;
    GclConfig_t cfg;

    // Capabilities
    cfg.supportedListMax = 1024;
    cfg.supportedCycleMaxNumerator = 1000000000;
    cfg.supportedCycleMaxDenominator = 1000000000;
    cfg.supportedIntervalMax = 1000000000;

    // Admin Defaults (Mandatory: Must be non-zero for YANG validation)
    cfg.adminCycleTime = {1000000, 1000000000};
    cfg.adminBaseTime = {0, 0};

    // Operational State
    cfg.operGateStates = 0x55;
    cfg.operCycleTime = {1000000, 1000000000};
    cfg.operBaseTime = {1700000000, 0};

    return cfg;
}

// Requirement 5 & 9: Apply GCL to Netlink
sysrepo::ErrorCode apply_gcl_to_hw(const std::string& ifname, const GclConfig_t& cfg) {
    std::cout << "[HW-ACTOR] [ACTION] Applying TAS to " << ifname << std::endl;
    if (ifname == "eno1") return sysrepo::ErrorCode::OperationFailed;
    return sysrepo::ErrorCode::Ok;
}

/* ---------------------------------------------------------
 * TSNSERVICE MANAGER
 * --------------------------------------------------------- */

class TsnManager {
   public:
    TsnManager(sysrepo::Session sess) : m_sess(sess) {
    }

    void initialize() {
        std::cout << "[INIT] Starting TSN Management Backend..." << std::endl;
        sync_hardware_to_running();
        setup_subscriptions();
        start_notification_loop();
    }

   private:
    sysrepo::Session m_sess;
    std::optional<sysrepo::Subscription> m_sub;

    /* --- CONFIGURATION CALLBACKS --- */

    static sysrepo::ErrorCode interface_config_cb(sysrepo::Session sess, uint32_t, const std::string&,
                                                  const std::optional<std::string>&, sysrepo::Event event, uint32_t) {
        std::cout << "[CB] [IF-CONFIG] [" << event << "] triggered." << std::endl;
        if (event != sysrepo::Event::Change) return sysrepo::ErrorCode::Ok;

        for (const auto& change : sess.getChanges("//.")) {
            std::string moduleName = change.node.schema().module().name();
            std::string nodeName = change.node.schema().name();
            std::string path = change.node.path();

            // 2. Logic: If the node doesn't belong to 'ietf-interfaces',
            // ignore it here. It's an augmentation handled by another callback.
            if (moduleName != "ietf-interfaces") {
                std::cout << "[CB] [IF-CONFIG] [DEBUG] Skipping node from external module: " << moduleName << std::endl;
                continue;
            }

            // 3. Now we are only dealing with native ietf-interfaces nodes.
            // Allow the 'enabled' leaf to be changed.
            if (nodeName == "enabled") {
                std::cout << "[CB] [IF-CONFIG] [ALLOW] Interface admin-state toggled: " << path << std::endl;
                continue;
            }

            if (change.operation == sysrepo::ChangeOperation::Created ||
                change.operation == sysrepo::ChangeOperation::Deleted) {
                std::cerr << "[CB] [IF-CONFIG] [REJECT] Structural change: \"" << path
                          << "\" with operation: " << change.operation << std::endl;
                return sysrepo::ErrorCode::Unsupported;
            }
        }
        return sysrepo::ErrorCode::Ok;
    }
    static sysrepo::ErrorCode interface_config_cb2(sysrepo::Session sess, uint32_t, const std::string&,
                                                   const std::optional<std::string>&, sysrepo::Event event, uint32_t) {
        std::cout << "[CB] [IF-CONFIG] [" << event << "] triggered." << std::endl;
        if (event != sysrepo::Event::Change) return sysrepo::ErrorCode::Ok;

        for (const auto& change : sess.getChanges("//.")) {
            std::string moduleName = change.node.schema().module().name();
            std::string nodeName = change.node.schema().name();
            std::string path = change.node.path();

            // 2. Logic: If the node doesn't belong to 'ietf-interfaces',
            // ignore it here. It's an augmentation handled by another callback.
            if (moduleName != "ietf-interfaces") {
                std::cout << "[CB] [IF-CONFIG] [DEBUG] Skipping node from external module: " << moduleName << std::endl;
                continue;
            }

            // 3. Now we are only dealing with native ietf-interfaces nodes.
            // Allow the 'enabled' leaf to be changed.
            if (nodeName == "enabled") {
                std::cout << "[CB] [IF-CONFIG] [ALLOW] Interface admin-state toggled: " << path << std::endl;
                continue;
            }

            if (change.operation == sysrepo::ChangeOperation::Created ||
                change.operation == sysrepo::ChangeOperation::Deleted) {
                std::cerr << "[CB] [IF-CONFIG] [REJECT] Structural change: \"" << path
                          << "\" with operation: " << change.operation << std::endl;
                return sysrepo::ErrorCode::Unsupported;
            }
        }
        return sysrepo::ErrorCode::Ok;
    }

    static sysrepo::ErrorCode gate_table_cb(sysrepo::Session sess, uint32_t, const std::string&,
                                            const std::optional<std::string>&, sysrepo::Event event, uint32_t) {
        std::cout << "[CB] [GATE-CONFIG] [" << event << "] triggered." << std::endl;
        if (event != sysrepo::Event::Change) return sysrepo::ErrorCode::Ok;

        for (const auto& change : sess.getChanges("//.")) {
            std::string path(change.node.path());
            if (path.find("config-change") != std::string::npos && change.node.asTerm().valueStr() == "true") {
                // Extract interface name
                size_t start = path.find("'") + 1;
                size_t end = path.find("'", start);
                std::string ifname = path.substr(start, end - start);

                std::cout << "[CB] [GATE-CONFIG] [ACTION] Applying qdisc for " << ifname << std::endl;
                GclConfig_t cfg = fetch_hw_state(ifname);  // Real app: pull admin values from sess.getData()
                auto res = apply_gcl_to_hw(ifname, cfg);
                if (res != sysrepo::ErrorCode::Ok) return res;
            }
        }
        return sysrepo::ErrorCode::Ok;
    }

    /* --- OPERATIONAL CALLBACKS (PULL MODEL) --- */

    static sysrepo::ErrorCode oper_interface_cb(sysrepo::Session sess, uint32_t, const std::string&,
                                                const std::optional<std::string>&, const std::optional<std::string>&,
                                                uint32_t, std::optional<libyang::DataNode>& parent) {
        std::cout << "[CB] [OPER-IF] Refreshing interface status..." << std::endl;
        struct ifaddrs *ifaddr, *ifa;
        if (getifaddrs(&ifaddr) == -1) return sysrepo::ErrorCode::Internal;
        auto ctx = sess.getContext();

        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) continue;
            std::string name = ifa->ifa_name;
            std::string path = "/ietf-interfaces:interfaces/interface[name='" + name + "']";
            bool is_loop = (ifa->ifa_flags & IFF_LOOPBACK);

            if (!parent)
                parent = ctx.newPath(path + "/oper-status", (ifa->ifa_flags & IFF_RUNNING) ? "up" : "down");
            else
                parent->newPath(path + "/oper-status", (ifa->ifa_flags & IFF_RUNNING) ? "up" : "down");
            struct sockaddr_ll* s = (struct sockaddr_ll*)ifa->ifa_addr;
            if (!is_loop) parent->newPath(path + "/phys-address", mac_to_string(s->sll_addr, 6, ':'));

            if (!get_bridge_master(name).empty() || is_bridge(name)) {
                GclConfig_t hw = fetch_hw_state(name);
                std::string gpt =
                    path + "/ieee802-dot1q-bridge:bridge-port/ieee802-dot1q-sched-bridge:gate-parameter-table";
                parent->newPath(gpt + "/oper-cycle-time/numerator", std::to_string(hw.operCycleTime.numerator));
                parent->newPath(gpt + "/oper-cycle-time/denominator", std::to_string(hw.operCycleTime.denominator));
            }
        }
        freeifaddrs(ifaddr);
        return sysrepo::ErrorCode::Ok;
    }

    // Callback 4: Bridge-Port List Mapping (Efficient pass on top-level /bridges)
    static sysrepo::ErrorCode oper_bridge_cb(sysrepo::Session sess, uint32_t, const std::string&,
                                             const std::optional<std::string>&, const std::optional<std::string>&,
                                             uint32_t, std::optional<libyang::DataNode>& parent) {
        std::cout << "[CB] [OPER-BR] Scanning interfaces for bridge members..." << std::endl;
        struct ifaddrs *ifaddr, *ifa;
        if (getifaddrs(&ifaddr) == -1) return sysrepo::ErrorCode::Internal;
        auto ctx = sess.getContext();

        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) continue;
            std::string ifname = ifa->ifa_name;
            std::string master = get_bridge_master(ifname);

            if (!master.empty()) {
                // Populate config-false leaf-list in bridge model
                std::string path = "/ieee802-dot1q-bridge:bridges/bridge[name='" + master + "']/component[name='" +
                                   master + "_comp']/bridge-port";
                if (!parent)
                    parent = ctx.newPath(path, ifname);
                else
                    parent->newPath(path, ifname);
            }
        }
        freeifaddrs(ifaddr);
        return sysrepo::ErrorCode::Ok;
    }

    /* --- GRANULAR REJECT CALLBACKS --- */

    static sysrepo::ErrorCode bridge_reject_cb(sysrepo::Session sess, uint32_t, const std::string& mod,
                                               const std::optional<std::string>&, sysrepo::Event event, uint32_t) {
        if (event == sysrepo::Event::Change) {
            std::cerr << "[CB] [REJECT] Edits to 'ieee802-dot1q-bridge' (Bridges) are currently not implemented."
                      << std::endl;
            return sysrepo::ErrorCode::Unsupported;
        }
        return sysrepo::ErrorCode::Ok;
    }

    static sysrepo::ErrorCode lldp_reject_cb(sysrepo::Session sess, uint32_t, const std::string& mod,
                                             const std::optional<std::string>&, sysrepo::Event event, uint32_t) {
        if (event == sysrepo::Event::Change) {
            std::cerr << "[CB] [REJECT] Edits to 'ieee802-dot1ab-lldp' (LLDP) are currently not implemented."
                      << std::endl;
            return sysrepo::ErrorCode::Unsupported;
        }
        return sysrepo::ErrorCode::Ok;
    }

    /* --- LLDP CALLBACKS --- */

    // Configuration Callback: Rejects any attempt to change LLDP settings
    static sysrepo::ErrorCode lldp_config_cb(sysrepo::Session sess, uint32_t, const std::string&,
                                             const std::optional<std::string>&, sysrepo::Event event, uint32_t) {
        if (event != sysrepo::Event::Change) return sysrepo::ErrorCode::Ok;

        for (const auto& change : sess.getChanges("//.")) {
            // Responsibility Filter
            if (change.node.schema().module().name() != "ieee802-dot1ab-lldp") continue;

            std::cerr << "[CB] [LLDP-CONFIG] [REJECT] Unauthorized modification on LLDP config: " << change.node.path()
                      << std::endl;
            return sysrepo::ErrorCode::Unsupported;
        }
        return sysrepo::ErrorCode::Ok;
    }

    // Operational Callback: Provides mock local data and neighbor information
    static sysrepo::ErrorCode oper_lldp_cb(sysrepo::Session sess, uint32_t, const std::string&,
                                           const std::optional<std::string>&, const std::optional<std::string>&,
                                           uint32_t, std::optional<libyang::DataNode>& parent) {
        std::cout << "[CB] [OPER-LLDP] [INFO] Providing mock LLDP operational data..." << std::endl;

        auto ctx = sess.getContext();
        // Use a helper to initialize 'parent' if it's the first time
        auto add_path = [&](const std::string& path, const std::string& val = "") {
            if (!parent) {
                parent = ctx.newPath(path, val.empty() ? std::nullopt : std::optional<std::string>(val),
                                     libyang::CreationOptions::Output);
            } else {
                parent->newPath(path, val.empty() ? std::nullopt : std::optional<std::string>(val),
                                libyang::CreationOptions::Output);
            }
        };

        try {
            // 1. Mock Local System Data
            std::string local = "/ieee802-dot1ab-lldp:lldp/local-system-data";
            add_path(local + "/chassis-id-subtype", "mac-address");
            add_path(local + "/chassis-id", "00-25-90-7c-8b-7c");
            add_path(local + "/system-name", "TSN-Switch-v01");

            // 2. Mock Neighbor Data (Remote Systems)
            // Note: The structure depends on the exact YANG version.
            // In 802.1ABcu, neighbors are often under lldp/port/remote-systems-data
            std::string remote_base =
                "/ieee802-dot1ab-lldp:lldp/port[name='enp2s0f2'][dest-mac-address='01-80-c2-00-00-0e']/"
                "remote-systems-data";

            // Neighbors are identified by a time-mark and a remote-index
            std::string neighbor = remote_base + "[time-mark=1][remote-index=1]";
            add_path(neighbor + "/chassis-id-subtype", "mac-address");
            add_path(neighbor + "/chassis-id", "aa-bb-cc-dd-ee-ff");
            add_path(neighbor + "/port-id-subtype", "interface-name");
            add_path(neighbor + "/port-id", "eth0");
            add_path(neighbor + "/system-name", "Neighbor-PLC-01");

        } catch (const std::exception& e) {
            std::cerr << "[CB] [OPER-LLDP] [ERR] " << e.what() << std::endl;
        }

        return sysrepo::ErrorCode::Ok;
    }

    void setup_subscriptions() {
        std::cout << "[INIT] [SUBS] Registering granular callbacks..." << std::endl;

        // m_sub = m_sess.onModuleChange("ietf-interfaces", interface_config_cb, "/ietf-interfaces:interfaces");
        m_sub = m_sess.onModuleChange("ietf-interfaces", interface_config_cb, std::nullopt);
        m_sub->onModuleChange("ietf-interfaces", gate_table_cb,
                              "/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port/"
                              "ieee802-dot1q-sched-bridge:gate-parameter-table");

        m_sub->onOperGet("ietf-interfaces", oper_interface_cb, "/ietf-interfaces:interfaces/interface");
        // FIX: Subscribe to top-level bridges for efficient population of all bridge-ports at once
        m_sub->onOperGet("ieee802-dot1q-bridge", oper_bridge_cb, "/ieee802-dot1q-bridge:bridges");

        m_sub->onModuleChange("ieee802-dot1q-bridge", bridge_reject_cb);
        m_sub->onModuleChange("ieee802-dot1ab-lldp", lldp_config_cb, std::nullopt);
        m_sub->onOperGet("ieee802-dot1ab-lldp", oper_lldp_cb, "/ieee802-dot1ab-lldp:lldp");
    }

    /* ---------------------------------------------------------
     * STARTUP SYNC (Pass 1, 2, 3 integrated)
     * --------------------------------------------------------- */

    void sync_hardware_to_running() {
        std::cout << "[INIT] [SYNC] Processing Interface Pass (1, 2, 3)..." << std::endl;
        m_sess.switchDatastore(sysrepo::Datastore::Running);
        auto ctx = m_sess.getContext();
        std::optional<libyang::DataNode> forest;

        struct ifaddrs *ifaddr, *ifa;
        if (getifaddrs(&ifaddr) == -1) return;

        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) continue;

            std::string name = ifa->ifa_name;
            if (name == "ovs-system") continue;  // Keep OVS skip as requested earlier

            // Determine properties via flags
            bool is_loop = (ifa->ifa_flags & IFF_LOOPBACK);
            bool is_br = is_bridge(name);
            bool is_up = (ifa->ifa_flags & IFF_UP);

            // --- PASS 1: Bridges (Only if it's a bridge and NOT loopback) ---
            if (is_br && !is_loop) {
                struct sockaddr_ll* s = (struct sockaddr_ll*)ifa->ifa_addr;
                std::string br_path = "/ieee802-dot1q-bridge:bridges/bridge[name='" + name + "']";
                std::string mac = mac_to_string(s->sll_addr, 6, '-');

                if (!forest)
                    forest = ctx.newPath(br_path + "/address", mac);
                else
                    forest->newPath(br_path + "/address", mac);

                forest->newPath(br_path + "/bridge-type", "ieee802-dot1q-bridge:customer-vlan-bridge");
                forest->newPath(br_path + "/component[name='" + name + "_comp']/id", "1");
                forest->newPath(br_path + "/component[name='" + name + "_comp']/type",
                                "ieee802-dot1q-bridge:c-vlan-component");
            }

            // --- PASS 2: Interface Core ---
            std::string if_path = "/ietf-interfaces:interfaces/interface[name='" + name + "']";
            std::string type = is_loop ? "iana-if-type:softwareLoopback"
                                       : (is_br ? "iana-if-type:bridge" : "iana-if-type:ethernetCsmacd");

            if (!forest)
                forest = ctx.newPath(if_path + "/type", type);
            else
                forest->newPath(if_path + "/type", type);

            forest->newPath(if_path + "/enabled", is_up ? "true" : "false");

            // --- PASS 3: Bridge-Port & TAS (Skip for Loopback) ---
            if (!is_loop) {
                std::string master = get_bridge_master(name);
                std::string bp_path = if_path + "/ieee802-dot1q-bridge:bridge-port";

                if (!master.empty()) {
                    forest->newPath(bp_path + "/bridge-name", master);
                    forest->newPath(bp_path + "/component-name", master + "_comp");
                }

                // Physical ports and Bridges get TAS capabilities to satisfy validation
                GclConfig_t hw_cfg = fetch_hw_state(name);
                std::string gpt = bp_path + "/ieee802-dot1q-sched-bridge:gate-parameter-table";
                forest->newPath(gpt + "/supported-list-max", std::to_string(hw_cfg.supportedListMax));
                forest->newPath(gpt + "/supported-cycle-max/numerator",
                                std::to_string(hw_cfg.supportedCycleMaxNumerator));
                forest->newPath(gpt + "/supported-cycle-max/denominator",
                                std::to_string(hw_cfg.supportedCycleMaxDenominator));
                forest->newPath(gpt + "/supported-interval-max", std::to_string(hw_cfg.supportedIntervalMax));
                forest->newPath(gpt + "/admin-cycle-time/numerator", std::to_string(hw_cfg.adminCycleTime.numerator));
                forest->newPath(gpt + "/admin-cycle-time/denominator",
                                std::to_string(hw_cfg.adminCycleTime.denominator));
            }

            if (!is_loop && !is_br) {
                std::cout << "[INIT] [LLDP] Enabling discovery on: " << name << std::endl;
                std::string lldp_port =
                    "/ieee802-dot1ab-lldp:lldp/port[name='" + name + "'][dest-mac-address='01-80-c2-00-00-0e']";
                forest->newPath(lldp_port + "/admin-status", "tx-and-rx");  // 'both' = tx and rx
            }
        }
        freeifaddrs(ifaddr);

        if (forest) {
            std::cout << "[INIT] [SYNC] Applying Batch to Datastore..." << std::endl;
            std::cout << "[INIT] [SYNC] Switching to first sibling..." << std::endl;
            forest = forest->firstSibling();
            std::cout << "  -> [SYNC DEBUG] Data forest:\n"
                      << forest->printStr(libyang::DataFormat::XML, libyang::PrintFlags::Siblings).value() << std::endl;
            std::cout << "[SYNC] Editing batch..." << std::endl;
            m_sess.editBatch(*forest, sysrepo::DefaultOperation::Merge);
            std::cout << "[SYNC] Applying changes..." << std::endl;
            m_sess.applyChanges();
            std::cout << "[INIT] [SYNC] Datastore synchronized." << std::endl;
        }
    }
    // Static Utils
    static bool is_bridge(const std::string& n) {
        return (access(("/sys/class/net/" + n + "/bridge").c_str(), F_OK) == 0);
    }
    static std::string get_bridge_master(const std::string& n) {
        char buf[256];
        ssize_t l = readlink(("/sys/class/net/" + n + "/master").c_str(), buf, 255);
        if (l == -1) return "";
        buf[l] = '\0';
        std::string s(buf);
        return s.substr(s.find_last_of('/') + 1);
    }
    static std::string mac_to_string(unsigned char* s, int len, char sep) {
        char b[18];
        snprintf(b, 18, "%02x%c%02x%c%02x%c%02x%c%02x%c%02x", s[0], sep, s[1], sep, s[2], sep, s[3], sep, s[4], sep,
                 s[5]);
        return std::string(b);
    }
    void start_notification_loop() {
        std::thread([this]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(15));
                try {
                    auto notif = m_sess.getContext().newPath("/ieee802-dot1ab-lldp:remote-table-change", std::nullopt);
                    m_sess.sendNotification(notif, sysrepo::Wait::No);
                } catch (...) {
                }
            }
        }).detach();
    }
};

int main() {
    std::cout.setf(std::ios::unitbuf);
    try {
        sysrepo::Connection conn;
        sysrepo::Session sess = conn.sessionStart();
        TsnManager manager(sess);
        manager.initialize();
        while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}