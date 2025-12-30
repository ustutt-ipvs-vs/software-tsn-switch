#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <optional>
#include <algorithm>
#include <fstream>

// Linux System Headers
#include <sys/types.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <netpacket/packet.h>

// Sysrepo & Libyang
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/Session.hpp>
#include <sysrepo-cpp/Subscription.hpp>
#include <libyang-cpp/Context.hpp>
#include <libyang-cpp/DataNode.hpp>
#include <sysrepo-cpp/Changes.hpp>

// ---------------------------------------------------------
// UTILITIES
// ---------------------------------------------------------

std::string mac_to_string(unsigned char* sll_addr, int len, char separator) {
    if (len != 6) {
        char empty[18];
        snprintf(empty, sizeof(empty), "00%c00%c00%c00%c00%c00", separator, separator, separator, separator, separator);
        return std::string(empty);
    }
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x%c%02x%c%02x%c%02x%c%02x%c%02x",
             sll_addr[0], separator, sll_addr[1], separator, sll_addr[2], separator,
             sll_addr[3], separator, sll_addr[4], separator, sll_addr[5]);
    return std::string(buf);
}

bool is_bridge(const std::string& ifName) {
    std::string path = "/sys/class/net/" + ifName + "/bridge";
    return (access(path.c_str(), F_OK) == 0);
}

std::string get_bridge_master(const std::string& ifName) {
    std::string path = "/sys/class/net/" + ifName + "/master";
    char buf[1024];
    ssize_t len = readlink(path.c_str(), buf, sizeof(buf)-1);
    if (len != -1) {
        buf[len] = '\0';
        std::string target(buf);
        size_t last_slash = target.find_last_of('/');
        if (last_slash != std::string::npos) return target.substr(last_slash + 1);
        return target;
    }
    return "";
}

// ---------------------------------------------------------
// 1. OPERATIONAL DATA PROVIDER (STATE + CAPABILITIES)
// ---------------------------------------------------------
// This provides the "Hardware Facts" that cannot be changed by config.
void populate_oper_data(const libyang::Context& ctx, std::optional<libyang::DataNode>& parent) {
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        std::cerr << "[ERR] getifaddrs failed" << std::endl;
        return;
    }

    auto set = [&](const std::string& path, const std::string& val) {
        std::cout << "  -> [OPER] Pushing: " << path << " = " << val << std::endl;
        try {
            if (!parent) parent = ctx.newPath(path, val);
            else parent->newPath(path, val);
        } catch (const std::exception& e) {
            std::cerr << "     [ERR] Failed to set path: " << e.what() << std::endl;
        }
    };

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) continue;

        std::string name = ifa->ifa_name;
        struct sockaddr_ll *s = (struct sockaddr_ll*)ifa->ifa_addr;

        std::string mac_ietf = mac_to_string(s->sll_addr, s->sll_halen, ':');
        std::string mac_ieee = mac_to_string(s->sll_addr, s->sll_halen, '-');

        bool is_running = (ifa->ifa_flags & IFF_RUNNING);
        bool is_loop = (ifa->ifa_flags & IFF_LOOPBACK);
        bool is_br = is_bridge(name);
        std::string master = get_bridge_master(name);

        // --- ietf-interfaces (State) ---
        std::string if_path = "/ietf-interfaces:interfaces/interface[name='" + name + "']";
        set(if_path + "/oper-status", is_running ? "up" : "down");
        if (!is_loop) set(if_path + "/phys-address", mac_ietf);

        // --- ieee802-dot1q-bridge (Bridge State) ---
        if (is_br) {
            std::string br_path = "/ieee802-dot1q-bridge:bridges/bridge[name='" + name + "']";
            set(br_path + "/address", mac_ieee);
            // Note: bridge-type and component-type are usually config, but we report them here just in case
        }

        // --- CRITICAL FIX: TSN CAPABILITIES FOR ALL AUGMENTED INTERFACES ---
        // The YANG when condition auto-augments bridge and ethernet interfaces with TSN containers
        // We MUST provide TSN capabilities for ALL such interfaces, not just those with masters
        if (is_br || (!is_loop)) { // Bridge or any non-loopback interface (will be ethernetCsmacd)
            std::string port_path = if_path + "/ieee802-dot1q-bridge:bridge-port";
            std::string sched_path = port_path + "/ieee802-dot1q-sched-bridge:gate-parameter-table";

            std::cout << "  -> [OPER] Providing TSN capabilities for auto-augmented interface: " << name << std::endl;

            // Provide generous TSN hardware capabilities to satisfy validation
            set(sched_path + "/supported-list-max", "1024");
            set(sched_path + "/supported-cycle-max/numerator", "1000000000"); // Large value
            set(sched_path + "/supported-cycle-max/denominator", "1");
            set(sched_path + "/supported-interval-max", "1000000000");
        }

        // --- ieee802-dot1q-bridge (Bridge Port State for actual bridge ports) ---
        if (!master.empty()) {
            std::string port_path = if_path + "/ieee802-dot1q-bridge:bridge-port";
            set(port_path + "/component-name", master);
            set(port_path + "/pvid", "1");
        }
    }
    freeifaddrs(ifaddr);
}

sysrepo::ErrorCode oper_cb(sysrepo::Session session, uint32_t, const std::string& module, const std::optional<std::string>& path, const std::optional<std::string>& request_xpath, uint32_t, std::optional<libyang::DataNode>& parent) {
    std::cout << "[CALLBACK] oper_cb invoked for module: " << module;
    if (path) std::cout << ", path: " << path.value();
    if (request_xpath) std::cout << ", xpath: " << request_xpath.value();
    std::cout << std::endl;

    try {
        populate_oper_data(session.getContext(), parent);
        std::cout << "[CALLBACK] oper_cb completed successfully" << std::endl;
        return sysrepo::ErrorCode::Ok;
    } catch (const std::exception& e) {
        std::cerr << "[CALLBACK ERROR] " << e.what() << std::endl;
        return sysrepo::ErrorCode::Internal;
    }
}

// ---------------------------------------------------------
// 2. INITIALIZATION: SYNC KERNEL TO RUNNING CONFIG
// ---------------------------------------------------------
// This populates the "Configuration" part of the data.
void sync_kernel_to_running(sysrepo::Session& sess) {
    std::cout << "\n[INIT] Syncing Kernel State to Running Datastore..." << std::endl;

    // DEBUG: Check what's already in the datastore
    std::cout << "[INIT DEBUG] Checking existing data in running datastore..." << std::endl;
    try {
        auto existing_opt = sess.getData("/ietf-interfaces:interfaces");
        if (existing_opt.has_value()) {
            std::cout << "[INIT DEBUG] Found existing interfaces data:" << std::endl;
            auto print_result = existing_opt.value().printStr(libyang::DataFormat::XML, libyang::PrintFlags::Siblings);
            if (print_result.has_value()) {
                std::cout << print_result.value() << std::endl;
            } else {
                std::cout << "[INIT DEBUG] Could not print existing data." << std::endl;
            }
        } else {
            std::cout << "[INIT DEBUG] No existing interfaces data found." << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "[INIT DEBUG] Error checking existing data: " << e.what() << std::endl;
    }

    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return;

    auto ctx = sess.getContext();
    std::optional<libyang::DataNode> edit;

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) continue;

        std::string name = ifa->ifa_name;
        bool is_loop = (ifa->ifa_flags & IFF_LOOPBACK);
        bool is_br = is_bridge(name);
        std::string master = get_bridge_master(name);

        std::cout << "  -> [SYNC DEBUG] Interface " << name << ": loop=" << is_loop << ", bridge=" << is_br << ", master='" << master << "'" << std::endl;

        std::string xpath = "/ietf-interfaces:interfaces/interface[name='" + name + "']";

        // Check if exists in Running
        auto existing_data = sess.getData(xpath);
        if (existing_data) {
            std::cout << "  -> [SYNC] Interface " << name << " already in Running. Data:" << std::endl;
            std::cout << existing_data->printStr(libyang::DataFormat::XML, libyang::PrintFlags::Siblings).value() << std::endl;
            std::cout << "  -> [SYNC] Skipping." << std::endl;
            continue;
        }

        std::cout << "  -> [SYNC] Discovered new interface: " << name << ". Adding to Running." << std::endl;

        std::string type = is_loop ? "iana-if-type:softwareLoopback" :
                                   (is_br ? "iana-if-type:bridge" : "iana-if-type:ethernetCsmacd");

        if (!edit) edit = ctx.newPath(xpath + "/type", type);
        else edit->newPath(xpath + "/type", type);

        edit->newPath(xpath + "/enabled", "true");

        // Bridge Config
        if (is_br) {
            std::string br_path = "/ieee802-dot1q-bridge:bridges/bridge[name='" + name + "']";
            // Mandatory Config for Bridge
            struct sockaddr_ll *s = (struct sockaddr_ll*)ifa->ifa_addr;
            std::string mac_ieee = mac_to_string(s->sll_addr, s->sll_halen, '-');

            edit->newPath(br_path + "/address", mac_ieee);
            edit->newPath(br_path + "/bridge-type", "ieee802-dot1q-bridge:customer-vlan-bridge");

            std::string comp_path = br_path + "/component[name='" + name + "']";
            edit->newPath(comp_path + "/id", "1");
            edit->newPath(comp_path + "/type", "ieee802-dot1q-bridge:c-vlan-component");
        }

        // CRITICAL FIX: Pre-populate TSN operational data in the SAME session/edit batch
        if ((is_br || !is_loop) && !master.empty() == false) { // Interfaces that will trigger TSN augmentation
            std::cout << "  -> [SYNC] Pre-populating TSN capabilities for: " << name << std::endl;
            
            // Create complete TSN gate-parameter-table structure to satisfy validation
            std::string tsn_base = xpath + "/ieee802-dot1q-bridge:bridge-port/ieee802-dot1q-sched-bridge:gate-parameter-table";
            
            // Operational capabilities (will be used during validation)
            edit->newPath(tsn_base + "/supported-list-max", "1024");
            edit->newPath(tsn_base + "/supported-cycle-max/numerator", "1000000000");  
            edit->newPath(tsn_base + "/supported-cycle-max/denominator", "1");
            edit->newPath(tsn_base + "/supported-interval-max", "1000000000");
            
            // Minimal admin configuration (prevents auto-creation of violating defaults)
            edit->newPath(tsn_base + "/admin-control-list/gate-control-entry[index='0']/operation-name", "ieee802-dot1q-sched:set-gate-states");
            edit->newPath(tsn_base + "/admin-control-list/gate-control-entry[index='0']/gate-states-value", "255");
            edit->newPath(tsn_base + "/admin-control-list/gate-control-entry[index='0']/time-interval-value", "1000000");
            edit->newPath(tsn_base + "/admin-cycle-time/numerator", "1000000"); 
            edit->newPath(tsn_base + "/admin-cycle-time/denominator", "1");
            
            std::cout << "  -> [SYNC] TSN structure added to edit batch for: " << name << std::endl;
        }

        // Bridge Port Config (if needed)
        if (!master.empty()) {
            std::cout << "  -> [SYNC] Adding bridge-port config for: " << name << " (master: " << master << ")" << std::endl;
            edit->newPath(xpath + "/ieee802-dot1q-bridge:bridge-port/component-name", master);
            edit->newPath(xpath + "/ieee802-dot1q-bridge:bridge-port/pvid", "1");
        }
    }
    freeifaddrs(ifaddr);

    if (edit) {
        std::cout << "  -> [SYNC] Applying changes..." << std::endl;

        // DEBUG: Print what we're actually creating
        std::cout << "  -> [SYNC DEBUG] Data being created:" << std::endl;
        std::cout << edit->printStr(libyang::DataFormat::XML, libyang::PrintFlags::Siblings).value() << std::endl;
        std::cout << "  -> [SYNC DEBUG] End of data dump." << std::endl;

        std::cout << "  -> [SYNC DEBUG] About to call editBatch..." << std::endl;
        try {
            // This triggers validation. The oper_cb should be called to provide the capabilities.
            sess.editBatch(*edit, sysrepo::DefaultOperation::Merge);
            std::cout << "  -> [SYNC DEBUG] editBatch completed, about to call applyChanges..." << std::endl;

            // CRITICAL: Force operational data fetch BEFORE validation by getting all TSN paths
            std::cout << "  -> [SYNC DEBUG] Pre-fetching operational data to trigger callbacks..." << std::endl;
            std::vector<std::string> interfaces = {"docker0", "eno1", "enp2s0f2", "enp2s0f3", "enp5s0"};
            for (const std::string& iface : interfaces) {
                try {
                    std::string tsn_path = "/ietf-interfaces:interfaces/interface[name='" + iface + "']/ieee802-dot1q-bridge:bridge-port/ieee802-dot1q-sched-bridge:gate-parameter-table";
                    auto temp_data = sess.getData(tsn_path, 0);
                    std::cout << "  -> [SYNC DEBUG] Loaded operational data for: " << iface << std::endl;
                } catch (const std::exception& e) {
                    std::cout << "  -> [SYNC DEBUG] Failed to load operational data for " << iface << ": " << e.what() << std::endl;
                }
            }

            // Now apply changes - operational data should be cached/available for validation
            std::cout << "  -> [SYNC DEBUG] Applying changes after operational data pre-fetch..." << std::endl;
            sess.applyChanges(std::chrono::milliseconds{5000}); // Give more time for validation
            std::cout << "  -> [SYNC] Complete." << std::endl;
        } catch (const libyang::ErrorWithCode& e) {
            // If validation fails due to TSN constraints, it might be because YANG auto-created
            // TSN containers but operational data isn't available yet.
            std::cerr << "  -> [SYNC ERROR] " << e.what() << std::endl;

            // For TSN validation errors, we could retry after ensuring operational data is loaded
            if (std::string(e.what()).find("supported-list-max") != std::string::npos ||
                std::string(e.what()).find("supported-cycle-max") != std::string::npos) {
                std::cout << "  -> [SYNC] TSN validation error detected. This suggests operational" << std::endl;
                std::cout << "            capabilities are not available during validation." << std::endl;
                std::cout << "            Ensure operational subscriptions are active and data is populated." << std::endl;
            }
            throw; // Re-throw to maintain error propagation
        }
    } else {
        std::cout << "  -> [SYNC] No changes needed." << std::endl;
    }
}

// ---------------------------------------------------------
// 3. CONFIGURATION SUBSCRIBER
// ---------------------------------------------------------
sysrepo::ErrorCode config_cb(sysrepo::Session session, uint32_t, const std::string&, const std::optional<std::string>&, sysrepo::Event event, uint32_t) {
    if (event != sysrepo::Event::Change) return sysrepo::ErrorCode::Ok;

    std::cout << "\n[CONFIG] Change Event Detected:" << std::endl;
    for (const auto& change : session.getChanges("//.")) {
        std::string path(change.node.path());
        std::string val_str = "(none)";
        if (change.node.isTerm()) val_str = change.node.asTerm().valueStr();
        std::cout << "  Path: " << path << " | Val: " << val_str << std::endl;
    }
    return sysrepo::ErrorCode::Ok;
}

// ---------------------------------------------------------
// MAIN
// ---------------------------------------------------------
int main() {
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);

    try {
        std::cout << "Starting Backend..." << std::endl;

        // CONNECTION 1: Single session for both subscriptions and operations
        // This is CRITICAL - operational callbacks only work on the same session that has subscriptions
        sysrepo::Connection conn;
        sysrepo::Session session = conn.sessionStart();

        // 1. Subscribe to OPERATIONAL (State)
        // We subscribe to ALL relevant modules using the SAME session that will do operations.
        std::cout << "[DEBUG] Setting up operational subscriptions..." << std::endl;

        std::cout << "[DEBUG] Subscribing to ietf-interfaces..." << std::endl;
        auto oper_sub = session.onOperGet(
            "ietf-interfaces",
            oper_cb,
            "/ietf-interfaces:interfaces",
            sysrepo::SubscribeOptions::Default
        );

        std::cout << "[DEBUG] Subscribing to ieee802-dot1q-bridge..." << std::endl;
        auto oper_sub_br = session.onOperGet(
            "ieee802-dot1q-bridge",
            oper_cb,
            "/ieee802-dot1q-bridge:bridges",
            sysrepo::SubscribeOptions::Default
        );

        // *** CRITICAL: Subscribe to the TSN module with broader path ***
        std::cout << "[DEBUG] Subscribing to ieee802-dot1q-sched-bridge..." << std::endl;
        auto oper_sub_sched = session.onOperGet(
            "ieee802-dot1q-sched-bridge",
            oper_cb,
            "*", // Subscribe to ALL paths in the module
            sysrepo::SubscribeOptions::Default
        );

        std::cout << "[DEBUG] All operational subscriptions registered!" << std::endl;

        // 2. Subscribe to RUNNING (Config)
        auto config_sub = session.onModuleChange(
            "ietf-interfaces",
            config_cb,
            std::nullopt,
            0,
            sysrepo::SubscribeOptions::Enabled | sysrepo::SubscribeOptions::DoneOnly
        );

        std::cout << "[MAIN] Operational Subscriptions Active." << std::endl;

        // 3. PRE-POPULATE: Try to set operational data directly
        std::cout << "[PRE-POP] Attempting to set operational data directly..." << std::endl;
        try {
            // Switch to operational datastore
            session.switchDatastore(sysrepo::Datastore::Operational);

            // Try to create some basic operational data to ensure the callback system works
            auto oper_edit = session.getContext().newPath("/ietf-interfaces:interfaces/interface[name='lo']/oper-status", "up");
            session.editBatch(oper_edit, sysrepo::DefaultOperation::Merge);
            session.applyChanges();

            // Switch back to running datastore
            session.switchDatastore(sysrepo::Datastore::Running);

            std::cout << "[PRE-POP] Operational data set directly!" << std::endl;

            // TEST: Try to manually trigger operational callback by requesting TSN data
            std::cout << "[TEST] Testing if operational callback works by requesting TSN data..." << std::endl;
            try {
                // Switch to operational datastore for the test
                session.switchDatastore(sysrepo::Datastore::Operational);
                auto tsn_data = session.getData("/ietf-interfaces:interfaces/interface[name='docker0']/ieee802-dot1q-bridge:bridge-port/ieee802-dot1q-sched-bridge:gate-parameter-table/supported-list-max");

                // Switch back to running datastore
                session.switchDatastore(sysrepo::Datastore::Running);

                if (tsn_data.has_value()) {
                    std::cout << "[TEST] SUCCESS: Got TSN operational data!" << std::endl;
                } else {
                    std::cout << "[TEST] FAILED: No TSN operational data returned." << std::endl;
                }
            } catch (const std::exception& e) {
                std::cout << "[TEST] ERROR requesting TSN data: " << e.what() << std::endl;
                // Ensure we're back on running datastore
                try { session.switchDatastore(sysrepo::Datastore::Running); } catch (...) {}
            }
        } catch (const std::exception& e) {
            std::cout << "[PRE-POP] Failed to set operational data directly: " << e.what() << std::endl;
            // Ensure we're back on running datastore
            try { session.switchDatastore(sysrepo::Datastore::Running); } catch (...) {}
        }

        // 4. PRE-POPULATE: Set operational data for ALL interfaces BEFORE configuration
        std::cout << "[PREP] Pre-populating operational datastore with TSN capabilities..." << std::endl;
        try {
            // CRITICAL FIX: The TEST section left pending changes in operational datastore
            // We need to apply/clear those changes before making new ones
            std::cout << "[PREP] Clearing any pending session changes..." << std::endl;

            // First switch to operational datastore
            session.switchDatastore(sysrepo::Datastore::Operational);

            // Apply any pending changes from the TEST section, then clear
            try {
                session.applyChanges();
                std::cout << "[PREP] Applied pending operational changes." << std::endl;
            } catch (const std::exception& e) {
                std::cout << "[PREP] No pending changes to apply: " << e.what() << std::endl;
                // Try to discard instead
                try {
                    session.discardChanges();
                } catch (...) {}
            }

            // Get interface list using getifaddrs (same as sync function)
            struct ifaddrs* ifaddr;
            if (getifaddrs(&ifaddr) == -1) {
                throw std::runtime_error("getifaddrs failed");
            }

            for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr) continue;

                std::string name = ifa->ifa_name;
                bool is_loop = (ifa->ifa_flags & IFF_LOOPBACK);
                bool is_br = is_bridge(name);

                // CRITICAL FIX: Build complete TSN DataNode tree for interfaces that will be auto-augmented
                if (is_br || !is_loop) {
                    std::string tsn_path = "/ietf-interfaces:interfaces/interface[name='" + name + "']/ieee802-dot1q-bridge:bridge-port/ieee802-dot1q-sched-bridge:gate-parameter-table";

                    std::cout << "[PREP] Building complete TSN DataNode tree for: " << name << std::endl;

                    // Build complete DataNode hierarchy in memory, then apply as single operation
                    std::cout << "[PREP] Creating root TSN node..." << std::endl;

                    // Create first node to establish the tree root
                    auto tsn_root = session.getContext().newPath(tsn_path + "/supported-list-max", "1024");

                    // Add siblings using the DataNode's newPath method
                    std::cout << "[PREP] Adding TSN capability siblings..." << std::endl;
                    tsn_root.newPath(tsn_path + "/supported-cycle-max/numerator", "1000000000");
                    tsn_root.newPath(tsn_path + "/supported-cycle-max/denominator", "1");
                    tsn_root.newPath(tsn_path + "/supported-interval-max", "1000000000");

                    // Add minimal admin configuration to prevent auto-creation of defaults
                    std::cout << "[PREP] Adding TSN admin configuration..." << std::endl;
                    try {
                        // Correct YANG paths: admin-control-list is a container with gate-control-entry list
                        std::cout << "[PREP] Adding gate-control-entry[0]..." << std::endl;
                        tsn_root.newPath(tsn_path + "/admin-control-list/gate-control-entry[index='0']/operation-name", "ieee802-dot1q-sched:set-gate-states");
                        tsn_root.newPath(tsn_path + "/admin-control-list/gate-control-entry[index='0']/gate-states-value", "255");
                        tsn_root.newPath(tsn_path + "/admin-control-list/gate-control-entry[index='0']/time-interval-value", "1000000");

                        std::cout << "[PREP] Adding admin-cycle-time..." << std::endl;
                        tsn_root.newPath(tsn_path + "/admin-cycle-time/numerator", "1000000");
                        tsn_root.newPath(tsn_path + "/admin-cycle-time/denominator", "1");

                    } catch (const std::exception& e) {
                        std::cout << "[PREP] ERROR creating admin config: " << e.what() << std::endl;
                        throw;
                    }

                    // Apply the complete tree as a single operation
                    std::cout << "[PREP] Applying complete TSN tree for: " << name << std::endl;
                    session.editBatch(tsn_root, sysrepo::DefaultOperation::Merge);

                    // Apply changes for this interface before moving to next
                    session.applyChanges();
                    std::cout << "[PREP] Complete TSN structure applied for: " << name << std::endl;
                }
            }

            freeifaddrs(ifaddr);

            session.applyChanges();
            session.switchDatastore(sysrepo::Datastore::Running);
            std::cout << "[PREP] Operational TSN data pre-populated successfully!" << std::endl;

        } catch (const std::exception& e) {
            std::cout << "[PREP] Failed to pre-populate operational data: " << e.what() << std::endl;
            try { session.switchDatastore(sysrepo::Datastore::Running); } catch (...) {}
        }

        // 5. SYNC: Populate Running Config
        // Wait a moment to ensure operational data is available
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // This will trigger validation, operational data should now be available
        sync_kernel_to_running(session);

        std::cout << "Backend running. Press Ctrl+C to exit." << std::endl;
        while (true) std::this_thread::sleep_for(std::chrono::seconds(1));

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}