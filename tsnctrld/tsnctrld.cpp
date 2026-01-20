#include "include/tsnctrld.hpp"

#include <ifaddrs.h>
#include <net/if.h>
#include <netpacket/packet.h>

#include <ctime>
#include <iostream>
#include <thread>

// Utils
static bool is_bridge(const std::string& n) {
    return (access(("/sys/class/net/" + n + "/bridge").c_str(), F_OK) == 0);
}
std::string mac_to_string(unsigned char* sll_addr, int len, char separator) {
    if (len != 6) {
        char empty[18];
        snprintf(empty, sizeof(empty), "00%c00%c00%c00%c00%c00", separator, separator, separator, separator, separator);
        return {empty};
    }
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x%c%02x%c%02x%c%02x%c%02x%c%02x", sll_addr[0], separator, sll_addr[1], separator,
             sll_addr[2], separator, sll_addr[3], separator, sll_addr[4], separator, sll_addr[5]);
    return {buf};
}
static std::string get_bridge_master(const std::string& n) {
    char buf[256];
    ssize_t l = readlink(("/sys/class/net/" + n + "/master").c_str(), buf, 255);
    if (l == -1) return "";
    buf[l] = '\0';
    std::string s(buf);
    return s.substr(s.find_last_of('/') + 1);
}
std::string extractListKey(const std::string& xpath, const std::string& listName, const std::string& keyName) {
    // Search for "interface[name='" specifically
    std::string searchPattern = listName + "[" + keyName + "='";

    size_t start = xpath.find(searchPattern);
    if (start == std::string::npos) return "";

    start += searchPattern.length();
    size_t end = xpath.find("']", start);
    if (end == std::string::npos) return "";

    return xpath.substr(start, end - start);
}
void printSingleInterface(const ietfInterface_t& iface) {
    std::cout << "Interface: " << iface.name << (iface.enabled ? " [UP]" : " [DOWN]") << "\n";
    std::cout << "  Bridge: " << iface.bridgePort.bridgeName << "\n";

    const auto& gcl = iface.bridgePort.gateParameterTable;
    std::cout << "    GCL Admin Entries (" << gcl.adminControlList.size() << "):\n";
    for (const auto& entry : gcl.adminControlList) {
        std::cout << "      Idx: " << entry.index << " | States: " << (int)entry.gateStatesValue
                  << " | Interval: " << entry.timeIntervalValue << "ns\n";
    }
    std::cout << "    GCL Oper Entries (" << gcl.operControlList.size() << "):\n";
    for (const auto& entry : gcl.operControlList) {
        std::cout << "      Idx: " << entry.index << " | States: " << (int)entry.gateStatesValue
                  << " | Interval: " << entry.timeIntervalValue << "ns\n";
    }
}
void print_interfaces(const std::vector<ietfInterface_t>& interfaces) {
    for (const auto& iface : interfaces) {
        printSingleInterface(iface);
    }
}

// Helper to translate clockid to string
std::string getClockName(__clockid_t clk) {
    switch (clk) {
        case CLOCK_REALTIME:
            return "CLOCK_REALTIME";
        case CLOCK_MONOTONIC:
            return "CLOCK_MONOTONIC";
        case CLOCK_BOOTTIME:
            return "CLOCK_BOOTTIME";
        case CLOCK_TAI:
            return "CLOCK_TAI";  // Usually what TAPRIO uses
        default:
            return "Unknown (" + std::to_string(clk) + ")";
    }
}

void printTaprioConfig(const TaprioConfig& cfg) {
    std::cout << "============================================" << std::endl;
    std::cout << "          TAPRIO CONFIGURATION              " << std::endl;
    std::cout << "============================================" << std::endl;

    // 1. Basic Parameters
    std::cout << "Traffic Classes (numTc): " << cfg.numTc << std::endl;
    std::cout << "Admin Clock Source:            " << getClockName(cfg.admin.clockid) << std::endl;
    std::cout << "Admin Base Time (ns):          " << cfg.admin.baseTime << std::endl;
    std::cout << "Admin Cycle Time (ns):         " << cfg.admin.cycleTime << std::endl;
    std::cout << "Oper Clock Source:            " << getClockName(cfg.oper.clockid) << std::endl;
    std::cout << "Oper Base Time (ns):          " << cfg.oper.baseTime << std::endl;
    std::cout << "Oper Cycle Time (ns):         " << cfg.oper.cycleTime << std::endl;

    // 2. Priority to Traffic Class Mapping
    std::cout << "Priority-to-TC Mapping:" << std::endl;
    std::cout << "  Prio: ";
    for (size_t i = 0; i < cfg.prioTc.size(); ++i) {
        std::cout << std::setw(3) << i << " ";
    }
    std::cout << "\n  TC:   ";
    for (auto tc : cfg.prioTc) {
        std::cout << std::setw(3) << (int)tc << " ";
    }
    std::cout << "\n" << std::endl;

    // 3. The Schedule (Gate Control List)
    std::cout << "Admin Gate Control List (Schedule):" << std::endl;
    std::cout << "--------------------------------------------" << std::endl;
    std::cout << " Index | Command | Gate Mask | Interval (ns) " << std::endl;
    std::cout << "-------|---------|-----------|---------------" << std::endl;

    if (cfg.admin.entries.empty()) {
        std::cout << "          [ Schedule is empty ]             " << std::endl;
    } else {
        int idx = 0;
        for (const auto& entry : cfg.admin.entries) {
            std::cout << " " << std::setw(5) << idx++ << " | "
                      << "  " << std::setw(5) << (int)entry.command << " | "
                      << "    0x" << std::hex << std::setw(2) << std::setfill('0') << (int)entry.gateMask << std::dec
                      << std::setfill(' ') << "    | " << std::setw(13) << entry.interval << std::endl;
        }
    }
    std::cout << "--------------------------------------------" << std::endl;
    std::cout << std::endl;
    // 3. The Schedule (Gate Control List)
    std::cout << "Oper Gate Control List (Schedule):" << std::endl;
    std::cout << "--------------------------------------------" << std::endl;
    std::cout << " Index | Command | Gate Mask | Interval (ns) " << std::endl;
    std::cout << "-------|---------|-----------|---------------" << std::endl;

    if (cfg.oper.entries.empty()) {
        std::cout << "          [ Schedule is empty ]             " << std::endl;
    } else {
        int idx = 0;
        for (const auto& entry : cfg.oper.entries) {
            std::cout << " " << std::setw(5) << idx++ << " | "
                      << "  " << std::setw(5) << (int)entry.command << " | "
                      << "    0x" << std::hex << std::setw(2) << std::setfill('0') << (int)entry.gateMask << std::dec
                      << std::setfill(' ') << "    | " << std::setw(13) << entry.interval << std::endl;
        }
    }
    std::cout << "--------------------------------------------" << std::endl;
    std::cout << std::endl;
}

void tsnctrld::resetTriggerLeaf(const std::string& xpath) {
    std::thread([this, xpath]() {
        try {
            // 1. Start a new session
            auto sess = m_sess.getConnection().sessionStart();

            // 2. SET THE ORIGINATOR NAME
            // This is how the callback will know 'we' did this
            sess.setOriginatorName("tsn-daemon-internal");

            // 3. Perform the reset
            sess.setItem(xpath, "false");
            sess.applyChanges();

            std::cout << "[RESET] Successfully reset " << xpath << " to false." << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[RESET] [ERROR] " << e.what() << std::endl;
        }
    }).detach();  // Fire and forget
}

//

/**
 * @brief Returns a pointer to the GclConfig_t of an interface if it was already retrieved, otherwise creates a new
 * ietfInterface_t with default values and uses that.
 *
 * @param ifname Name of the interface for which a GclConfig_t is desired.
 * @param interfaces A list of structs describing each interface. Should be filled before calling this function, use
 * QdiscManager::getInterfacesInResponse for that. If not filled, a new, default entry is created, named, and returned.
 * @return A pointer to the GclConfig_t of the desired interface
 */
ietfInterface_t& tsnctrld::getExistingOrNewInterface(const std::string& ifname,
                                                     std::vector<ietfInterface_t>& interfaces) {
    auto it = std::find_if(interfaces.begin(), interfaces.end(),
                           [ifname](const ietfInterface_t& iface) { return iface.name == ifname; });

    ietfInterface_t* current;
    std::cout << "[HW-FETCH] [DEBUG] Fetching state for " << ifname << "..." << std::endl;
    if (it != interfaces.end()) {
        std::cout << "[HW-FETCH] [DEBUG] Found interface " << ifname << " in list..." << std::endl;
        current = &(*it);
    } else {
        std::cout << "[HW-FETCH] [DEBUG] Interface " << ifname << " not found..." << std::endl;
        std::cout << "[HW-FETCH] [DEBUG] Adding entry with default values to interface list..." << std::endl;
        interfaces.emplace_back();
        current = &interfaces.back();
        current->name = ifname;
    }
    return *current;
}

/**
 *
 * @param entries List of `GclEntry_t`s to be added to the datanode
 * @param listToFill A DataNode representing either oper-control-list or admin-control-list
 */
void fillControlList(const std::vector<GclEntry_t>& entries, std::optional<libyang::DataNode>& listToFill) {
    std::cout << "[FILL CONTROLLIST] Filling of values from passed control list to passed datanode..." << std::endl;
    for (const auto& entry : entries) {
        auto entry_res =
            listToFill->newPath2("gate-control-entry[index='" + std::to_string(entry.index) + "']", std::nullopt);
        auto entry_node = entry_res.createdNode;
        entry_node->newPath2("operation-name", entry.operationName);
        entry_node->newPath2("time-interval-value", std::to_string(entry.timeIntervalValue));
        entry_node->newPath2("gate-states-value", std::to_string(entry.gateStatesValue));
    }
}
/**
 *
 * @param hw_cfg The configuration to be written to the datastore. The `*DataSet` variables can be used to declare which
 * set of variables contains valid data.
 * @param toFill A DataNode representing a GPT path for an interface:
 * "/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port/ieee802-dot1q-sched:gate-parameter-table"
 * @param options Which values are supposed to be filled, Admin/Oper/Both or only the minimum possible?
 */
void fillGptNode(const GclConfig_t* hw_cfg, std::optional<libyang::DataNode>& toFill,
                 GclFillOptions options = GclFillOptions::OnlyDefault) {
    if (options & GclFillOptions::FillAdmin) {
        std::cout << "[FILL DATANODE] Filling of Admin data requested..." << std::endl;
        std::cout << "[FILL DATANODE] Writing \"supported-*\" nodes..." << std::endl;
        toFill->newPath2("supported-list-max", std::to_string(hw_cfg->supportedListMax));
        toFill->newPath2("supported-cycle-max/numerator", std::to_string(hw_cfg->supportedCycleMaxNumerator));
        toFill->newPath2("supported-cycle-max/denominator", std::to_string(hw_cfg->supportedCycleMaxDenominator));
        toFill->newPath2("supported-interval-max", std::to_string(hw_cfg->supportedIntervalMax));

        toFill->newPath2("gate-enabled", hw_cfg->gateEnabled ? "true" : "false");
        if (hw_cfg->operDataSet && !hw_cfg->adminDataSet) {
            std::cout << "[FILL DATANODE] Admin data not set, but Oper data is, using that..." << std::endl;
            toFill->newPath2("admin-base-time/seconds", std::to_string(hw_cfg->operBaseTime.seconds));
            toFill->newPath2("admin-base-time/nanoseconds", std::to_string(hw_cfg->operBaseTime.nanoseconds));
            auto admin_res = toFill->newPath2("admin-control-list", std::nullopt);
            auto admin_node = admin_res.createdNode;
            fillControlList(hw_cfg->operControlList, admin_node);

            std::cout << "[FILL DATANODE] Using operCycleTime for mandatory adminCycleTime..." << std::endl;
            toFill->newPath2("admin-cycle-time/numerator", std::to_string(hw_cfg->operCycleTime.numerator));
            toFill->newPath2("admin-cycle-time/denominator", std::to_string(hw_cfg->operCycleTime.denominator));

        } else if (hw_cfg->adminDataSet) {
            std::cout << "[FILL DATANODE] Admin data is set, using that..." << std::endl;

            toFill->newPath2("admin-base-time/seconds", std::to_string(hw_cfg->adminBaseTime.seconds));
            toFill->newPath2("admin-base-time/nanoseconds", std::to_string(hw_cfg->adminBaseTime.nanoseconds));
            auto admin_res = toFill->newPath2("admin-control-list", std::nullopt);
            auto admin_node = admin_res.createdNode;
            fillControlList(hw_cfg->adminControlList, admin_node);

            std::cout << "[FILL DATANODE] Using set adminCycleTime for mandatory element..." << std::endl;
            toFill->newPath2("admin-cycle-time/numerator", std::to_string(hw_cfg->adminCycleTime.numerator));
            toFill->newPath2("admin-cycle-time/denominator", std::to_string(hw_cfg->adminCycleTime.denominator));
        } else {
            std::cout << "[FILL DATANODE] Neither admin nor oper is set, assume nothing TSN is configured..."
                      << std::endl;
            std::cout << "[FILL DATANODE] Using default adminCycleTime for mandatory adminCycleTime..." << std::endl;
            toFill->newPath2("admin-cycle-time/numerator", std::to_string(hw_cfg->adminCycleTime.numerator));
            toFill->newPath2("admin-cycle-time/denominator", std::to_string(hw_cfg->adminCycleTime.denominator));
        }
    }

    if (options & GclFillOptions::FillOper) {
        std::cout << "[FILL DATANODE] Filling of Oper data requested..." << std::endl;
        if (hw_cfg->operDataSet) {
            std::cout << "[FILL DATANODE] Oper data is set, using that..." << std::endl;
            toFill->newPath2("oper-base-time/seconds", std::to_string(hw_cfg->operBaseTime.seconds));
            toFill->newPath2("oper-base-time/nanoseconds", std::to_string(hw_cfg->operBaseTime.nanoseconds));
            auto oper_res = toFill->newPath2("oper-control-list", std::nullopt);
            auto oper_node = oper_res.createdNode;
            fillControlList(hw_cfg->operControlList, oper_node);
        } else {
            std::cout
                << "[FILL DATANODE] Oper data requested but not set, assume nothing TSN is configured and ignoring..."
                << std::endl;
        }
    }
    return;
}

ietfInterface_t* tsnctrld::syncInterfaceFromSysrepo(sysrepo::Session sess, const std::string& ifname) {
    ietfInterface_t& iface = getExistingOrNewInterface(ifname, m_interfaces);
    GclConfig_t& gcl = iface.bridgePort.gateParameterTable;

    // 2. Fetch the subtree once
    std::string basePath = "/ietf-interfaces:interfaces/interface[name='" + ifname +
                           "']/"
                           "ieee802-dot1q-bridge:bridge-port/ieee802-dot1q-sched-bridge:gate-parameter-table";

    // Use sess.getData() to get the proposed tree
    auto data = sess.getData(basePath);
    if (!data) {
        std::cout << "[SYSREPO->STRUCT] data is null, failed..." << std::endl;
        return nullptr;
    }

    // 3. Use libyang's internal value types to avoid string parsing
    auto getUint32 = [&](const std::string& rel) -> uint32_t {
        auto node = data->findPath(basePath + "/" + rel);
        if (!node) return 0;
        // .asTerm().value() returns a std::variant in modern libyang-cpp
        // This is MUCH faster than std::stoul(string)
        auto val = node->asTerm().value();
        return std::get<uint32_t>(val);
    };

    // Update members directly in the cache (No copy of the whole struct)
    gcl.adminCycleTime.numerator = getUint32("admin-cycle-time/numerator");
    gcl.adminCycleTime.denominator = getUint32("admin-cycle-time/denominator");
    std::cout << "[SYSREPO->STRUCT] admin-cycle-time: " << gcl.adminCycleTime.numerator << "/"
              << gcl.adminCycleTime.denominator << std::endl;

    // 4. Update the Control List (The expensive part)
    gcl.adminControlList.clear();  // Reuse the vector's capacity!
    auto entries = data->findXPath(basePath + "/admin-control-list/gate-control-entry");

    for (const auto& entryNode : entries) {
        // We emplace directly into the existing vector
        auto& e = gcl.adminControlList.emplace_back();

        // Use typed access for every leaf
        e.index = std::get<uint32_t>(entryNode.findPath("index")->asTerm().value());
        e.gateStatesValue = std::get<uint8_t>(entryNode.findPath("gate-states-value")->asTerm().value());
        e.timeIntervalValue = std::get<uint32_t>(entryNode.findPath("time-interval-value")->asTerm().value());

        // IdentityRefs are still strings, but we can move them
        auto opName = entryNode.findPath("operation-name")->asTerm().valueStr();
        e.operationName = std::move(opName);
    }
    static queueMaxSduEntry_t queueMaxSduTable[2] = {
        {.trafficClass = 0, .queueMaxSdu = 1500, .transmissionOverrun = 0},
        {.trafficClass = 1, .queueMaxSdu = 1500, .transmissionOverrun = 0}};
    gcl.queueMaxSduTable.assign(queueMaxSduTable, queueMaxSduTable + 2);
    //gcl.adminCycleTime = {.numerator = 1'500'000, .denominator = 1'000'000'000};
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    gcl.adminBaseTime = {.seconds = static_cast<uint64_t>(ts.tv_sec), .nanoseconds = static_cast<uint32_t>(ts.tv_nsec)};

    return &iface;
}

// tsnctrld

/**
 * @brief Ensure a clean slate and initialize
 */
void tsnctrld::initialize() {
    std::cout << "[INIT] Initializing tsnctrld..." << std::endl;

    // In order for our program to be the source of truth, start with an empty datastore.
    std::cout << "[INIT] [DEBUG] Cleaning interfaces from datastore " << std::endl;
    m_sess.deleteItem("/ietf-interfaces:interfaces");
    std::cout << "[INIT] [DEBUG] Cleaning bridges from datastore " << std::endl;
    m_sess.deleteItem("/ieee802-dot1q-bridge:bridges");
    std::cout << "[INIT] [DEBUG] Cleaning lldp from datastore " << std::endl;
    m_sess.deleteItem("/ieee802-dot1ab-lldp:lldp");
    m_sess.applyChanges();

    syncHardwareToRunning();
    setupSubscriptions();
}

void tsnctrld::ensureCurrentNetlinkResponseInterfaces(uint32_t currentRequestId, const std::string& ifname) {
    if (currentRequestId == m_lastNetlinkId) {
        std::cout << "[CACHE] [QUERY_NL] Netlink was already queried for this request (id=" << currentRequestId
                  << "), data is current." << std::endl;
        return;
    }
    std::cout << "[CACHE] [QUERY_NL] Different requestId (current:" << currentRequestId << ") from record ("
              << m_lastNetlinkId << "), querying netlink for latest state." << std::endl;
    m_interfaces.clear();
    m_qm.getAllQdiscInfo(m_sock);

    std::cout << "[CACHE] [QUERY_NL] Parsing response into internal list " << std::endl;
    m_qm.getInterfacesInResponse(m_sock, m_interfaces);

    m_lastNetlinkId = currentRequestId;
    print_interfaces(m_interfaces);
}

struct ifaddrs* tsnctrld::ensureCurrentIfAddrsInterfaces(uint32_t currentRequestId) {
    if (currentRequestId != 0 && currentRequestId == m_lastIfAddrsId) return m_ifa_cache;

    std::cout << "[CACHE] [QUERY_IF] Lazy-loading getifaddrs data..." << std::endl;

    if (m_ifa_cache) freeifaddrs(m_ifa_cache);

    if (getifaddrs(&m_ifa_cache) == -1) {
        m_ifa_cache = nullptr;
    }

    m_lastIfAddrsId = currentRequestId;
    return m_ifa_cache;
}

/**
 * @brief Fills the "running" datastore with the current values as received from the kernel, builds the "ground truth"
 * of this program
 *
 */
void tsnctrld::syncHardwareToRunning() {
    std::cout << "[SYNC] Populating internal list m_interfaces from kernel" << std::endl;
    ensureCurrentNetlinkResponseInterfaces(0, "");

    m_sess.switchDatastore(sysrepo::Datastore::Running);
    auto ctx = m_sess.getContext();
    std::optional<libyang::DataNode> forest;

    struct ifaddrs *ifaddr, *ifa;
    ifaddr = ensureCurrentIfAddrsInterfaces(0);
    if (ifaddr == nullptr) {
        std::cout << "[SYNC] [ERROR] getifaddrs failed..." << std::endl;
        return;
    };
    std::cout << "[SYNC] Iterating over interfaces..." << std::endl;
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) continue;

        std::string name = ifa->ifa_name;
        // Determine properties via flags
        bool is_loop = (ifa->ifa_flags & IFF_LOOPBACK);
        bool is_br = is_bridge(name);
        bool is_up = (ifa->ifa_flags & IFF_UP);
        std::cout << "[SYNC] Current interface: " << name << "..." << std::endl;

        struct sockaddr_ll* s = (struct sockaddr_ll*)ifa->ifa_addr;
        std::string mac_ietf = mac_to_string(s->sll_addr, s->sll_halen, ':');
        std::string mac_ieee = mac_to_string(s->sll_addr, s->sll_halen, '-');

        // --- PASS 1: Bridges (Only if it's a bridge and NOT loopback) ---
        if (is_br && !is_loop) {
            std::cout << "[SYNC] [BR] Interface is bridge..." << std::endl;
            std::string br_path = "/ieee802-dot1q-bridge:bridges/bridge[name='" + name + "']";
            auto br_res = forest ? forest->newPath2(br_path, std::nullopt) : ctx.newPath2(br_path, std::nullopt);
            if (!forest && br_res.createdParent) forest = br_res.createdParent;
            auto br_node = br_res.createdNode;

            br_node->newPath2("address", mac_ieee);
            br_node->newPath2("bridge-type", "ieee802-dot1q-bridge:customer-vlan-bridge");

            auto comp_res = br_node->newPath2("component[name='" + name + "']", std::nullopt);
            auto comp_node = comp_res.createdNode;
            comp_node->newPath2("id", "1");
            comp_node->newPath2("type", "ieee802-dot1q-bridge:c-vlan-component");
        }

        // --- PASS 2: Interface Core ---
        std::string if_path = std::string("/ietf-interfaces:interfaces/interface[name='").append(name).append("']");
        std::string type =
            is_loop ? "iana-if-type:softwareLoopback" : (is_br ? "iana-if-type:bridge" : "iana-if-type:ethernetCsmacd");

        std::cout << "[SYNC] [IF] Creating \"root\" interface node..." << std::endl;
        auto if_res = forest ? forest->newPath2(if_path, std::nullopt) : ctx.newPath2(if_path, std::nullopt);
        if (!forest && if_res.createdParent) forest = if_res.createdParent;
        auto if_node = if_res.createdNode;

        if_node->newPath2("type", type);
        if_node->newPath2("enabled", is_up ? "true" : "false");

        // --- PASS 3: Bridge-Port & TAS (Skip for Loopback) ---
        if (!is_loop) {
            std::string master = get_bridge_master(name);
            std::string bp_path = if_path + "/ieee802-dot1q-bridge:bridge-port";

            auto bp_res = if_node->newPath2("ieee802-dot1q-bridge:bridge-port", std::nullopt);
            auto bp_node = bp_res.createdNode;

            if (!master.empty()) {
                bp_node->newPath2("bridge-name", master);
                bp_node->newPath2("component-name", master);
            }

            // Physical ports and Bridges get TAS capabilities to satisfy validation
            auto gpt_res = bp_node->newPath2("ieee802-dot1q-sched-bridge:gate-parameter-table", std::nullopt);
            auto gpt_node = gpt_res.createdNode;

            GclConfig_t* hw_cfg = &getExistingOrNewInterface(name, m_interfaces).bridgePort.gateParameterTable;

            std::cout << "[SYNC] [BP] Filling DataNode of gate-parameter-table with values from GclConfig_t struct"
                      << std::endl;
            fillGptNode(hw_cfg, gpt_node, GclFillOptions::FillAdmin);
        }

        if (!is_loop && !is_br) {
            // TODO: Get real values
            std::cout << "[SYNC] [LLDP] Enabling discovery on: " << name << std::endl;
            std::string lldp_path =
                "/ieee802-dot1ab-lldp:lldp/port[name='" + name + "'][dest-mac-address='01-80-c2-00-00-0e']";

            auto lldp_res = forest->newPath2(lldp_path, std::nullopt);
            auto lldp_node = lldp_res.createdNode;
            lldp_node->newPath2("admin-status", "tx-and-rx");
        }
    }
    // freeifaddrs(ifaddr);

    if (forest) {
        std::cout << "[SYNC] Applying Batch to Datastore..." << std::endl;
        std::cout << "[SYNC] Switching to first sibling..." << std::endl;
        forest = forest->firstSibling();
        std::cout << "  -> [SYNC DEBUG] Data forest:\n"
                  << forest->printStr(libyang::DataFormat::XML, libyang::PrintFlags::Siblings).value() << std::endl;
        std::cout << "[SYNC] Editing batch..." << std::endl;
        m_sess.editBatch(*forest, sysrepo::DefaultOperation::Merge);
        std::cout << "[SYNC] Applying changes..." << std::endl;
        m_sess.applyChanges();
        std::cout << "[SYNC] Datastore synchronized." << std::endl;
    }
}

sysrepo::ErrorCode tsnctrld::defaultOperCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                                 const std::optional<std::string>& subXPath,
                                                 const std::optional<std::string>& requestXPath, uint32_t requestId,
                                                 std::optional<libyang::DataNode>& parent) {
    std::cout << "[CB_OPER] [DEFAULT] Received oper callback for module " << moduleName << "..." << std::endl;
    std::cout << "[CB_OPER] [DEFAULT] subXPath " << subXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_OPER] [DEFAULT] requestXPath " << requestXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_OPER] [DEFAULT] requestId " << requestId << "..." << std::endl;
    auto ctx = sess.getContext();
    if (!parent)
        std::cout << "[CB_OPER] [DEFAULT] parent is falsy..." << std::endl;
    else
        std::cout << "[CB_OPER] [DEFAULT] parent is truthy..." << std::endl;
    this;
    return sysrepo::ErrorCode::Ok;
}

/**
 *
 * @param sess
 * @param subId
 * @param moduleName
 * @param subXPath
 * @param requestXPath
 * @param requestId
 * @param parent
 * @return
 */
sysrepo::ErrorCode tsnctrld::operInterfaceCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                                   const std::optional<std::string>& subXPath,
                                                   const std::optional<std::string>& requestXPath, uint32_t requestId,
                                                   std::optional<libyang::DataNode>& parent) {
    std::cout << "[CB_OPER] [IF] Refreshing interface status..." << std::endl;
    struct ifaddrs *ifaddr, *ifa;
    ifaddr = ensureCurrentIfAddrsInterfaces(requestId);
    if (ifaddr == nullptr) return sysrepo::ErrorCode::Internal;
    auto ctx = sess.getContext();

    ensureCurrentNetlinkResponseInterfaces(requestId, "");

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) continue;
        std::string name = ifa->ifa_name;
        std::string if_path = "/ietf-interfaces:interfaces/interface[name='" + name + "']";
        bool is_loop = (ifa->ifa_flags & IFF_LOOPBACK);

        auto if_res = parent ? parent->newPath2(if_path, std::nullopt) : ctx.newPath2(if_path, std::nullopt);
        if (!parent && if_res.createdParent) parent = if_res.createdParent;
        auto if_node = if_res.createdNode;

        if_node->newPath("oper-status", (ifa->ifa_flags & IFF_RUNNING) ? "up" : "down");
        struct sockaddr_ll* s = (struct sockaddr_ll*)ifa->ifa_addr;
        if (!is_loop) {
            if_node->newPath("phys-address", mac_to_string(s->sll_addr, 6, ':'));
            auto bp_res = if_node->newPath2("ieee802-dot1q-bridge:bridge-port", std::nullopt);
            auto bp_node = bp_res.createdNode;
            auto gpt_res = bp_node->newPath2("ieee802-dot1q-sched-bridge:gate-parameter-table", std::nullopt);
            auto gpt_node = gpt_res.createdNode;

            GclConfig_t* hw_cfg = &getExistingOrNewInterface(name, m_interfaces).bridgePort.gateParameterTable;
            fillGptNode(hw_cfg, gpt_node, GclFillOptions::FillOper);
        }
    }
    //freeifaddrs(ifaddr);
    return sysrepo::ErrorCode::Ok;
}

/**
 * When a <get> request for the path `/ieee802-dot1q-bridge:bridges` happens, this callback populates the `bridge-port`s
 * in every "active" bridges `component`. The parameters are as defined by sysrepo-cpp
 *
 * @param sess
 * @param subId
 * @param moduleName
 * @param subXPath
 * @param requestXPath
 * @param requestId
 * @param parent
 * @return
 */
sysrepo::ErrorCode tsnctrld::operBridgeCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                                const std::optional<std::string>& subXPath,
                                                const std::optional<std::string>& requestXPath, uint32_t requestId,
                                                std::optional<libyang::DataNode>& parent) {
    std::cout << "[CB_OPER] [BR] Scanning interfaces for bridge members..." << std::endl;
    struct ifaddrs *ifaddr, *ifa;
    ifaddr = ensureCurrentIfAddrsInterfaces(requestId);
    if (ifaddr == nullptr) return sysrepo::ErrorCode::Internal;
    auto ctx = sess.getContext();

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_PACKET) continue;
        std::string ifname = ifa->ifa_name;
        std::string master = get_bridge_master(ifname);

        if (!master.empty()) {
            // Populate config-false leaf-list in bridge model
            std::string path = "/ieee802-dot1q-bridge:bridges/bridge[name='" + master + "']/component[name='" + master +
                               "']/bridge-port";
            parent = parent ? parent->newPath(path, ifname) : ctx.newPath(path, ifname);
        }
    }
    //freeifaddrs(ifaddr);
    return sysrepo::ErrorCode::Ok;
}
sysrepo::ErrorCode tsnctrld::operLldpCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                              const std::optional<std::string>& subXPath,
                                              const std::optional<std::string>& requestXPath, uint32_t requestId,
                                              std::optional<libyang::DataNode>& parent) {
    return sysrepo::ErrorCode::Ok;
}

sysrepo::ErrorCode tsnctrld::defaultChangeCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                                   const std::optional<std::string>& subXPath, sysrepo::Event event,
                                                   uint32_t requestId) {
    std::cout << "[CB_CHANGE] [DEFAULT] Received change callback for module " << moduleName << "..." << std::endl;
    std::cout << "[CB_CHANGE] [DEFAULT] subXPath " << subXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_CHANGE] [DEFAULT] event " << event << "..." << std::endl;
    std::cout << "[CB_CHANGE] [DEFAULT] requestId " << requestId << "..." << std::endl;
    this;
    return sysrepo::ErrorCode::Ok;
}
sysrepo::ErrorCode tsnctrld::changeInterfaceCallback(sysrepo::Session sess, uint32_t subId,
                                                     const std::string& moduleName,
                                                     const std::optional<std::string>& subXPath, sysrepo::Event event,
                                                     uint32_t requestId) {
    std::cout << "[CB_CHANGE] [IF] Received change callback for module " << moduleName << "..." << std::endl;
    std::cout << "[CB_CHANGE] [IF] subXPath " << subXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_CHANGE] [IF] event " << event << "..." << std::endl;
    std::cout << "[CB_CHANGE] [IF] requestId " << requestId << "..." << std::endl;
    if (event != sysrepo::Event::Change) return sysrepo::ErrorCode::Ok;

    for (const auto& change : sess.getChanges("//.")) {
        std::string nodeModuleName = change.node.schema().module().name();
        std::string nodeName = change.node.schema().name();

        if (nodeModuleName != moduleName) {
            std::cerr << "[CB] [IF-CONFIG] [DEBUG] Change does not belong to this module, skipping" << std::endl;
            continue;
        }
        if (nodeName == "TEMP") {
            std::cerr << "[CB] [IF-CONFIG] [ALLOW] Attempting to change whitelisted node, continuing for now"
                      << std::endl;
            continue;
        }
        std::cerr << "[CB] [IF-CONFIG] [REJECT] Edits to 'ietf-interfaces' (specifically the top-level `/interfaces` "
                     "element) are currently not implemented. Attempted change to path: "
                  << change.node.path() << std::endl;
        return sysrepo::ErrorCode::Unsupported;
    }
    std::cerr << "[CB] [IF-CONFIG] [ALLOW] All attempted chages were allowed, accepting" << std::endl;
    return sysrepo::ErrorCode::Ok;
}
sysrepo::ErrorCode tsnctrld::changeBridgeCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                                  const std::optional<std::string>& subXPath, sysrepo::Event event,
                                                  uint32_t requestId) {
    std::cout << "[CB_CHANGE] [BR] Received change callback for module " << moduleName << "..." << std::endl;
    std::cout << "[CB_CHANGE] [BR] subXPath " << subXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_CHANGE] [BR] event " << event << "..." << std::endl;
    std::cout << "[CB_CHANGE] [BR] requestId " << requestId << "..." << std::endl;
    if (event != sysrepo::Event::Change) return sysrepo::ErrorCode::Ok;

    for (const auto& change : sess.getChanges("//.")) {
        std::string nodeModuleName = change.node.schema().module().name();
        std::string nodeName = change.node.schema().name();

        if (nodeModuleName != moduleName) {
            std::cerr << "[CB] [BR-CONFIG] [DEBUG] Change does not belong to this module, skipping" << std::endl;
            continue;
        }
        if (nodeName == "TEMP") {
            std::cerr << "[CB] [BR-CONFIG] [ALLOW] Attempting to change whitelisted node, continuing for now"
                      << std::endl;
            continue;
        }
        std::cerr << "[CB] [BR-CONFIG] [REJECT] Edits to 'ieee802-dot1q-bridge' (specifically the top-level `/bridges` "
                     "element) are currently not implemented. Attempted change to path: "
                  << change.node.path() << std::endl;
        return sysrepo::ErrorCode::Unsupported;
    }
    std::cerr << "[CB] [BR-CONFIG] [ALLOW] All attempted chages were allowed, accepting" << std::endl;
    return sysrepo::ErrorCode::Ok;
}
sysrepo::ErrorCode tsnctrld::changeGptCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                               const std::optional<std::string>& subXPath, sysrepo::Event event,
                                               uint32_t requestId) {
    std::cout << "[CB_CHANGE] [GPT] Received change callback for module " << moduleName << "..." << std::endl;
    std::cout << "[CB_CHANGE] [GPT] subXPath " << subXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_CHANGE] [GPT] event " << event << "..." << std::endl;
    std::cout << "[CB_CHANGE] [GPT] requestId " << requestId << "..." << std::endl;

    if (sess.getOriginatorName() == "tsnctrld-internal") {
        std::cout << "[CB_CHANGE] [GPT] Whatever just happened, we did it, so we can trust it..." << std::endl;
        return sysrepo::ErrorCode::Ok;
    }

    // 1. PHASE: VALIDATION (Event::Change)
    if (event == sysrepo::Event::Change) {
        // We filter for changes specifically on the config-change leaf
        std::string filter =
            "/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port/"
            "ieee802-dot1q-sched-bridge:gate-parameter-table/config-change";

        // getChanges returns a ChangeCollection which we can iterate over
        auto changes = sess.getChanges(filter.c_str());

        for (const auto& change : changes) {
            // Check if node was deleted (ignore)
            if (change.operation == sysrepo::ChangeOperation::Deleted) continue;

            // Only trigger if changed to "true"
            if (change.node.asTerm().valueStr() == "true") {
                std::string ifname = extractListKey(std::string(change.node.path()), "interface", "name");

                std::cout << "[CB_CHANGE] [GPT] Applying new config for " << ifname << std::endl;

                try {
                    // Update our internal cache and apply to hardware
                    // This function should be the one we optimized earlier (Zero-Copy)
                    std::cout << "[CB_CHANGE] [GPT] Getting data from sysrepo " << std::endl;
                    ietfInterface_t* iface = this->syncInterfaceFromSysrepo(sess, ifname);
                    std::cout << "[CB_CHANGE] [GPT] Returned data:" << std::endl;
                    printSingleInterface(*iface);

                    if (iface) {
                        std::cout << "[CB_CHANGE] [GPT] Converting to TaprioConfig struct:" << std::endl;
                        TaprioConfig taprioCfg =
                            NetconfNetlinkMapper::mapToTaprio(iface->bridgePort.gateParameterTable);

                        printTaprioConfig(taprioCfg);
                        std::cout << "[CB_CHANGE] [GPT] Sending qdisc" << std::endl;
                        m_qm.setQdisc(m_sock, ifname, taprioCfg);
                        std::cout << "[CB_CHANGE] [GPT] Qdisc \"sent\"" << std::endl;
                        m_pathsToReset.push_back(std::string(change.node.path()));
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[CB_CHANGE] [GPT] [ERROR] Hardware rejected config: " << e.what() << std::endl;
                    return sysrepo::ErrorCode::OperationFailed;
                }
            }
        }
    }

    // 2. PHASE: RESET TRIGGER (Event::Done)
    if (event == sysrepo::Event::Done) {
        for (const auto& path : m_pathsToReset) {
            this->resetTriggerLeaf(path);
        }
        m_pathsToReset.clear();
    }


    // 3. PHASE: ROLLBACK (Event::Abort)
    if (event == sysrepo::Event::Abort) {
        // TODO
    }

    return sysrepo::ErrorCode::Ok;
}
/**
 * @brief Called for attempted changes on the "ieee802-dot1ab-lldp" module and denies everything.
 *
 * This function checks if the nodes where changes are attempted even belongs to this module, if not, skip because we
 * are not responsible for validating that.
 */
sysrepo::ErrorCode tsnctrld::changeLldpCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                                const std::optional<std::string>& subXPath, sysrepo::Event event,
                                                uint32_t requestId) {
    std::cout << "[CB_CHANGE] [LLDP] Received change callback for module " << moduleName << "..." << std::endl;
    std::cout << "[CB_CHANGE] [LLDP] subXPath " << subXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_CHANGE] [LLDP] event " << event << "..." << std::endl;
    std::cout << "[CB_CHANGE] [LLDP] requestId " << requestId << "..." << std::endl;

    if (event != sysrepo::Event::Change) return sysrepo::ErrorCode::Ok;

    for (const auto& change : sess.getChanges("//.")) {
        if (change.node.schema().module().name() != moduleName) continue;

        std::cerr << "[CB] [LLDP-CONFIG] [REJECT] Edits to 'ieee802-dot1ab-lldp' (LLDP) are currently not implemented. "
                     "Attempted change: "
                  << change.node.path() << std::endl;
        return sysrepo::ErrorCode::Unsupported;
    }
    return sysrepo::ErrorCode::Ok;
}

/**
 * @brief Subscribe to changes/oper-gets as necessary, everything is managed as one subscription object.
 *
 * Because sysrepo requires static functions as callbacks, but we need access to the member variables, we use
 * `std::bind_front` to fake static-ness of our member-functions
 */
void tsnctrld::setupSubscriptions() {
    std::cout << "[INIT] [SUBS] Registering granular callbacks..." << std::endl;
    auto defaultChangeCb = std::bind_front(&tsnctrld::defaultChangeCallback, this);

    auto changeInterfaceCb = std::bind_front(&tsnctrld::changeInterfaceCallback, this);
    m_sub = m_sess.onModuleChange("ietf-interfaces", changeInterfaceCb, std::nullopt);

    auto changeBridgeCb = std::bind_front(&tsnctrld::changeBridgeCallback, this);
    m_sub->onModuleChange("ieee802-dot1q-bridge", changeBridgeCb);

    auto changeGptCb = std::bind_front(&tsnctrld::changeGptCallback, this);
    m_sub->onModuleChange("ietf-interfaces", changeGptCb,
                          "/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port/"
                          "ieee802-dot1q-sched-bridge:gate-parameter-table");

    auto changeLldpCb = std::bind_front(&tsnctrld::changeLldpCallback, this);
    m_sub->onModuleChange("ieee802-dot1ab-lldp", changeLldpCb, std::nullopt);

    std::cout << "[INIT] [SUBS] Registered change callbacks..." << std::endl;

    auto defaultOperCb = std::bind_front(&tsnctrld::defaultOperCallback, this);

    auto operInterfaceCb = std::bind_front(&tsnctrld::operInterfaceCallback, this);
    m_sub->onOperGet("ietf-interfaces", operInterfaceCb, "/ietf-interfaces:interfaces/interface");

    auto operBridgeCb = std::bind_front(&tsnctrld::operBridgeCallback, this);
    m_sub->onOperGet("ieee802-dot1q-bridge", operBridgeCb, "/ieee802-dot1q-bridge:bridges");

    auto operLldpCb = std::bind_front(&tsnctrld::operLldpCallback, this);
    m_sub->onOperGet("ieee802-dot1ab-lldp", operLldpCb, "/ieee802-dot1ab-lldp:lldp");

    std::cout << "[INIT] [SUBS] Registered oper callbacks..." << std::endl;
}

tsnctrld::tsnctrld() : m_sess(m_conn.sessionStart()) {
}

int main() {
    tsnctrld daemon = tsnctrld();
    daemon.initialize();
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}