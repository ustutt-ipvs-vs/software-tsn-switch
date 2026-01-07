#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstring>
#include <optional>
#include <algorithm>
#include <fstream>
#include <array>

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


const std::string SUPPORTED_LIST_MAX = "128";
const std::string SUPPORTED_CYCLE_MAX_NUMERATOR = "1000000000"; //1e9
const std::string SUPPORTED_CYCLE_MAX_DENOMINATOR = "1000000000";
const std::string SUPPORTED_INTERVAL_MAX = "1000000000";

const std::string ADMIN_CYCLE_TIME_NUMERATOR = "1000000"; //1e6
const std::string ADMIN_CYCLE_TIME_DENOMINATOR = "1000000000";


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
    if (access(path.c_str(), F_OK) == 0) return true;
    return false;
}

std::string get_bridge_master(const std::string& ifName) {
    std::string path = "/sys/class/net/" + ifName + "/master";
    char buf[1024];
    ssize_t len = readlink(path.c_str(), buf, sizeof(buf)-1);

    if (len != -1) {
        buf[len] = '\0';
        std::string target(buf);
        size_t last_slash = target.find_last_of('/');
        std::string master = (last_slash != std::string::npos) ? target.substr(last_slash + 1) : target;

        if (master == "ovs-system") {
            std::cerr << "Open vSwitch not supported" <<std::endl;
            return "";
        }
        return master;
    }
    return "";
}

// ---------------------------------------------------------
// 1. PUSH OPERATIONAL DATA
// ---------------------------------------------------------
void push_operational_state(sysrepo::Session& sess) {
    std::cout << "\n[INIT] Pushing Hardware State to Operational Datastore..." << std::endl;

    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) {
        std::cerr << "[ERR] getifaddrs failed" << std::endl;
        return;
    }

    auto ctx = sess.getContext();
    std::optional<libyang::DataNode> oper_tree;

    auto set = [&](const std::string& path, const std::string& val) {
        try {
            if (!oper_tree) oper_tree = ctx.newPath(path, val);
            else oper_tree->newPath(path, val);
        } catch (...) {}
    };

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) continue;

        std::string name = ifa->ifa_name;
        if (name == "ovs-system") continue;

        struct sockaddr_ll *s = (struct sockaddr_ll*)ifa->ifa_addr;
        std::string mac_ietf = mac_to_string(s->sll_addr, s->sll_halen, ':');
        std::string mac_ieee = mac_to_string(s->sll_addr, s->sll_halen, '-');

        bool is_running = (ifa->ifa_flags & IFF_RUNNING);
        bool is_loop = (ifa->ifa_flags & IFF_LOOPBACK);
        bool is_br = is_bridge(name);
        std::string master = get_bridge_master(name);
        if (!master.empty() && !is_bridge(master)) master = "";

        // --- ietf-interfaces ---
        std::string if_path = "/ietf-interfaces:interfaces/interface[name='" + name + "']";
        set(if_path + "/type", is_loop ? "iana-if-type:softwareLoopback" : (is_br ? "iana-if-type:bridge" : "iana-if-type:ethernetCsmacd"));
        set(if_path + "/oper-status", is_running ? "up" : "down");
        if (!is_loop) set(if_path + "/phys-address", mac_ietf);
    }

    freeifaddrs(ifaddr);

    if (oper_tree) {
        sess.switchDatastore(sysrepo::Datastore::Operational);
        std::cout << "  -> [SYNC DEBUG] Data forest:\n" << oper_tree->printStr(libyang::DataFormat::XML, libyang::PrintFlags::Siblings).value() << std::endl;
        sess.editBatch(*oper_tree, sysrepo::DefaultOperation::Merge);
        sess.applyChanges();
        std::cout << "[INIT] Operational State pushed successfully." << std::endl;
        sess.switchDatastore(sysrepo::Datastore::Running);
    }
}

// ---------------------------------------------------------
// 2. SYNC KERNEL TO RUNNING CONFIG
// ---------------------------------------------------------
void sync_kernel_to_running(sysrepo::Session& sess) {
    std::cout << "\n[INIT] Syncing Kernel State to Running Datastore..." << std::endl;

    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == -1) return;

    auto ctx = sess.getContext();
    std::optional<libyang::DataNode> edit;

    // --- PASS 1: CREATE BRIDGES ---
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) continue;
        std::string name = ifa->ifa_name;
        if (name == "ovs-system") continue;

        if (is_bridge(name)) {
            std::string br_xpath = "/ieee802-dot1q-bridge:bridges/bridge[name='" + name + "']";
            struct sockaddr_ll *s = (struct sockaddr_ll*)ifa->ifa_addr;
            std::string mac_ieee = mac_to_string(s->sll_addr, s->sll_halen, '-');

            if (!edit) edit = ctx.newPath(br_xpath + "/address", mac_ieee);
            else edit->newPath(br_xpath + "/address", mac_ieee);

            edit->newPath(br_xpath + "/bridge-type", "ieee802-dot1q-bridge:customer-vlan-bridge");
            edit->newPath(br_xpath + "/component[name='" + name + "_comp']/id", "1");
            edit->newPath(br_xpath + "/component[name='" + name + "_comp']/type", "ieee802-dot1q-bridge:c-vlan-component");

            std::string if_xpath = "/ietf-interfaces:interfaces/interface[name='" + name + "']";
            edit->newPath(if_xpath + "/type", "iana-if-type:bridge");
            edit->newPath(if_xpath + "/enabled", "true");
        }
    }

    // --- PASS 2: CREATE INTERFACES & CAPABILITIES ---
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) continue;
        std::string name = ifa->ifa_name;
        if (name == "ovs-system") continue;
        if (is_bridge(name)) continue;

        bool is_loop = (ifa->ifa_flags & IFF_LOOPBACK);
        std::string master = get_bridge_master(name);
        if (!master.empty() && !is_bridge(master)) master = "";

        std::string xpath = "/ietf-interfaces:interfaces/interface[name='" + name + "']";
        std::string type = is_loop ? "iana-if-type:softwareLoopback" : "iana-if-type:ethernetCsmacd";

        if (!edit) edit = ctx.newPath(xpath + "/type", type);
        else edit->newPath(xpath + "/type", type);

        edit->newPath(xpath + "/enabled", "true");

        if (!master.empty()) {
            std::string port_path = xpath + "/ieee802-dot1q-bridge:bridge-port";
            edit->newPath(port_path + "/bridge-name", master);
            edit->newPath(port_path + "/component-name", master + "_comp");
            edit->newPath(port_path + "/pvid", "1");
        }
    }

    // --- PASS 3: UNIVERSAL CAPABILITIES (FIX) ---
    // We iterate ALL interfaces (including Bridges from Pass 1) and add capabilities to Running.
    // This satisfies the validation logic for the auto-created TSN containers.
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) continue;
        std::string name = ifa->ifa_name;
        if (name == "ovs-system") continue;
        bool is_loop = (ifa->ifa_flags & IFF_LOOPBACK);

        if (!is_loop) {
            std::string sched_path = "/ietf-interfaces:interfaces/interface[name='" + name + "']/ieee802-dot1q-bridge:bridge-port/ieee802-dot1q-sched-bridge:gate-parameter-table";

            // 1. Write Capabilities (Config True)
            if (!edit) edit = ctx.newPath(sched_path + "/supported-list-max", SUPPORTED_LIST_MAX);
            else edit->newPath(sched_path + "/supported-list-max", SUPPORTED_LIST_MAX);

            edit->newPath(sched_path + "/supported-cycle-max/numerator", SUPPORTED_CYCLE_MAX_NUMERATOR);
            edit->newPath(sched_path + "/supported-cycle-max/denominator", SUPPORTED_CYCLE_MAX_DENOMINATOR);
            edit->newPath(sched_path + "/supported-interval-max", SUPPORTED_INTERVAL_MAX);

            // 2. Write Safe Defaults for Admin Cycle Time
            // This ensures 'admin-cycle-time <= supported-cycle-max' passes even if user didn't set it.
            edit->newPath(sched_path + "/admin-cycle-time/numerator", ADMIN_CYCLE_TIME_NUMERATOR);
            edit->newPath(sched_path + "/admin-cycle-time/denominator", ADMIN_CYCLE_TIME_DENOMINATOR);
        }
    }
    freeifaddrs(ifaddr);

    if (edit) {
        std::cout << "  -> [SYNC] Applying changes..." << std::endl;
        std::cout << "  -> [SYNC DEBUG] Data forest:\n" << edit->printStr(libyang::DataFormat::XML, libyang::PrintFlags::Siblings).value() << std::endl;
        sess.editBatch(*edit, sysrepo::DefaultOperation::Merge);
        sess.applyChanges();
        std::cout << "  -> [SYNC] Complete." << std::endl;
    }
}

// ---------------------------------------------------------
// 3. CONFIGURATION SUBSCRIBER
// ---------------------------------------------------------
sysrepo::ErrorCode config_cb(sysrepo::Session session, uint32_t, const std::string&, const std::optional<std::string>&, sysrepo::Event event, uint32_t) {
    std::cout << "\n[CONFIG] config_cb called with event:" << event << std::endl;
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

    // Enable Sysrepo Debug Logging
    //sysrepo::setLogLevelStderr(sysrepo::LogLevel::Debug);

    try {
        std::cout << "Starting Backend..." << std::endl;

        sysrepo::Connection conn;
        sysrepo::Session session = conn.sessionStart();

        // 1. PUSH OPERATIONAL DATA
        push_operational_state(session);

        // 2. SYNC CONFIG
        sync_kernel_to_running(session);

        // 3. SUBSCRIBE
        auto config_sub = session.onModuleChange(
            "ietf-interfaces",
            config_cb,
            std::nullopt,
            0,
            sysrepo::SubscribeOptions::Enabled //| sysrepo::SubscribeOptions::DoneOnly
        );

        std::cout << "Backend running. Press Ctrl+C to exit." << std::endl;
        while (true) std::this_thread::sleep_for(std::chrono::seconds(1));

    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}