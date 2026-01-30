#include "include/tsnctrld.hpp"

#include <ifaddrs.h>
#include <ctime>
#include <iostream>
#include <thread>

// Utils
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
    std::cout << "Interface: " << iface.name << (iface.adminEnabled ? " [UP]" : " [DOWN]") << "\n";
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

template <typename T>
T tsnctrld::getLeaf(const std::optional<libyang::DataNode>& node, const std::string& path) {
    if (!node) return T{};
    auto leaf = node->findPath(path);
    if (!leaf) return T{};
    return std::get<T>(leaf->asTerm().value());
}

/**
 *
 * @param entries List of `GclEntry_t`s to be added to the datanode
 * @param listToFill A DataNode representing either oper-control-list or admin-control-list
 */
void fillControlList(const std::vector<GclEntry_t>& entries, std::optional<libyang::DataNode>& listToFill) {
    std::cout << "[FILL CONTROLLIST] Filling of values from passed control list to passed datanode..." << std::endl;
    for (const auto& entry : entries) {
        std::cout << "[FILL CONTROLLIST] Current entry: idx=" << entry.index << ", gsv=" << entry.gateStatesValue
                  << ", intr=" << entry.timeIntervalValue << ", opr=" << entry.operationName << std::endl;
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
void fillGptNode(const GclConfig_t& hw_cfg, std::optional<libyang::DataNode>& toFill,
                 GclFillOptions options = GclFillOptions::OnlyDefault) {
    if (options & GclFillOptions::FillAdmin) {
        std::cout << "[FILL DATANODE] Filling of Admin data requested..." << std::endl;
        std::cout << "[FILL DATANODE] Writing \"supported-*\" nodes..." << std::endl;
        toFill->newPath2("supported-list-max", std::to_string(hw_cfg.supportedListMax));
        toFill->newPath2("supported-cycle-max/numerator", std::to_string(hw_cfg.supportedCycleMaxNumerator));
        toFill->newPath2("supported-cycle-max/denominator", std::to_string(hw_cfg.supportedCycleMaxDenominator));
        toFill->newPath2("supported-interval-max", std::to_string(hw_cfg.supportedIntervalMax));

        toFill->newPath2("gate-enabled", hw_cfg.gateEnabled ? "true" : "false");
        if (hw_cfg.operDataSet && !hw_cfg.adminDataSet) {
            std::cout << "[FILL DATANODE] Admin data not set, but Oper data is, using that..." << std::endl;
            toFill->newPath2("admin-base-time/seconds", std::to_string(hw_cfg.operBaseTime.seconds));
            toFill->newPath2("admin-base-time/nanoseconds", std::to_string(hw_cfg.operBaseTime.nanoseconds));
            auto admin_res = toFill->newPath2("admin-control-list", std::nullopt);
            auto admin_node = admin_res.createdNode;
            fillControlList(hw_cfg.operControlList, admin_node);

            std::cout << "[FILL DATANODE] Using operCycleTime for mandatory adminCycleTime..." << std::endl;
            toFill->newPath2("admin-cycle-time/numerator", std::to_string(hw_cfg.operCycleTime.numerator));
            toFill->newPath2("admin-cycle-time/denominator", std::to_string(hw_cfg.operCycleTime.denominator));

        } else if (hw_cfg.adminDataSet) {
            std::cout << "[FILL DATANODE] Admin data is set, using that..." << std::endl;

            toFill->newPath2("admin-base-time/seconds", std::to_string(hw_cfg.adminBaseTime.seconds));
            toFill->newPath2("admin-base-time/nanoseconds", std::to_string(hw_cfg.adminBaseTime.nanoseconds));
            auto admin_res = toFill->newPath2("admin-control-list", std::nullopt);
            auto admin_node = admin_res.createdNode;
            fillControlList(hw_cfg.adminControlList, admin_node);

            std::cout << "[FILL DATANODE] Using set adminCycleTime for mandatory element..." << std::endl;
            toFill->newPath2("admin-cycle-time/numerator", std::to_string(hw_cfg.adminCycleTime.numerator));
            toFill->newPath2("admin-cycle-time/denominator", std::to_string(hw_cfg.adminCycleTime.denominator));
        } else {
            std::cout << "[FILL DATANODE] Neither admin nor oper is set, assume nothing TSN is configured..."
                      << std::endl;
            std::cout << "[FILL DATANODE] Using default adminCycleTime for mandatory adminCycleTime..." << std::endl;
            toFill->newPath2("admin-cycle-time/numerator", std::to_string(hw_cfg.adminCycleTime.numerator));
            toFill->newPath2("admin-cycle-time/denominator", std::to_string(hw_cfg.adminCycleTime.denominator));
        }
    }

    if (options & GclFillOptions::FillOper) {
        std::cout << "[FILL DATANODE] Filling of Oper data requested..." << std::endl;
        if (hw_cfg.operDataSet) {
            std::cout << "[FILL DATANODE] Oper data is set, using that..." << std::endl;
            toFill->newPath2("oper-base-time/seconds", std::to_string(hw_cfg.operBaseTime.seconds));
            toFill->newPath2("oper-base-time/nanoseconds", std::to_string(hw_cfg.operBaseTime.nanoseconds));
            auto oper_res = toFill->newPath2("oper-control-list", std::nullopt);
            auto oper_node = oper_res.createdNode;
            fillControlList(hw_cfg.operControlList, oper_node);
        } else {
            std::cout
                << "[FILL DATANODE] Oper data requested but not set, assume nothing TSN is configured and ignoring..."
                << std::endl;
        }
    }
}

ietfInterface_t* tsnctrld::syncInterfaceFromSysrepo(sysrepo::Session sess, const std::string& ifname,
                                                    uint32_t requestId) {
    m_ifcache.setCurrentRequestId(requestId);
    m_ifcache.ensureFullLinkData(m_sock);

    ietfInterface_t* iface_ptr = m_ifcache.getInterface(ifname);
    if (!iface_ptr) {
        std::cout << "[SYSREPO->STRUCT] [DEBUG] no interface found with name " << ifname << std::endl;
        return nullptr;
    }

    ietfInterface_t& iface = *iface_ptr;
    BridgePort_t& bp = iface.bridgePort;
    GclConfig_t& gcl = bp.gateParameterTable;

    // 2. Fetch the subtree once
    std::string basePath = "/ietf-interfaces:interfaces/interface[name='" + ifname +
                           "']/"
                           "ieee802-dot1q-bridge:bridge-port/ieee802-dot1q-sched-bridge:gate-parameter-table";

    // Use sess.getData() to get the proposed tree
    auto root = sess.getData(basePath);
    if (!root) {
        std::cout << "[SYSREPO->STRUCT] gclData is null, failed..." << std::endl;
        return nullptr;
    }
    auto gclData = root->findPath(basePath);
    std::cout << "[SYSREPO->STRUCT] [DEBUG] after root->gclData..." << std::endl;

    // Update members directly in the cache (No copy of the whole struct)
    gcl.adminCycleTime.numerator = getLeaf<uint32_t>(gclData, "admin-cycle-time/numerator");
    gcl.adminCycleTime.denominator = getLeaf<uint32_t>(gclData, "admin-cycle-time/denominator");
    std::cout << "[SYSREPO->STRUCT] admin-cycle-time: " << gcl.adminCycleTime.numerator << "/"
              << gcl.adminCycleTime.denominator << std::endl;

    // 4. Update the Control List (The expensive part)
    gcl.adminControlList.clear();  // Reuse the vector's capacity!
    auto entries = gclData->findXPath("admin-control-list/gate-control-entry");

    for (const auto& entryNode : entries) {
        // We emplace directly into the existing vector
        auto& e = gcl.adminControlList.emplace_back();

        // Use typed access for every leaf
        e.index = getLeaf<uint32_t>(entryNode, "index");
        e.gateStatesValue = getLeaf<uint8_t>(entryNode, "gate-states-value");
        e.timeIntervalValue = getLeaf<uint32_t>(entryNode, "time-interval-value");
        e.operationName = entryNode.findPath("operation-name")->asTerm().valueStr();
    }
    // Todo: Remove fake gclData, get from DS instead
    static queueMaxSduEntry_t queueMaxSduTable[2] = {
        {.trafficClass = 0, .queueMaxSdu = 1500, .transmissionOverrun = 0},
        {.trafficClass = 1, .queueMaxSdu = 1500, .transmissionOverrun = 0}};
    gcl.queueMaxSduTable.assign(queueMaxSduTable, queueMaxSduTable + 2);

    gcl.adminBaseTime.seconds = getLeaf<uint64_t>(gclData, "admin-base-time/seconds");
    gcl.adminBaseTime.nanoseconds = getLeaf<uint32_t>(gclData, "admin-base-time/nanoseconds");

    return &iface;
}

// tsnctrld

/**
 * @brief Ensure a clean slate and initialize
 */
void tsnctrld::initialize() {
    // m_lm.getAllInterfaces(m_sock);
    // m_lm.getInterfacesInResponse(m_sock, m_interfaces);
    // exit(1337);

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

/**
 * @brief Fills the "running" datastore with the current values as received from the kernel, builds the "ground truth"
 * of this program
 *
 */
void tsnctrld::syncHardwareToRunning() {
    std::cout << "[SYNC] Populating internal list m_interfaces from kernel" << std::endl;
    m_sess.switchDatastore(sysrepo::Datastore::Running);
    auto ctx = m_sess.getContext();
    std::optional<libyang::DataNode> forest;

    m_ifcache.setCurrentRequestId(0);
    m_ifcache.ensureFullLinkData(m_sock);
    m_ifcache.ensureFullQdiscData(m_sock);

    std::cout << "[SYNC] Iterating over interfaces..." << std::endl;

    const auto& allIfaces = m_ifcache.getAllInterfaces();
    for (const auto& [idx, iface] : allIfaces) {
        printSingleInterface(iface);
    }
    std::cout << "[SYNC] Print done..." << std::endl;

    for (auto& [idx, current] : m_ifcache.getAllInterfaces()) {
        std::string& name = current.name;

        std::cout << "[SYNC] Current interface: " << name << "..." << std::endl;
        // std::string mac_ietf = mac_to_string(s->sll_addr, s->sll_halen, ':');
        // std::string mac_ieee = mac_to_string(s->sll_addr, s->sll_halen, '-');

        // --- PASS 1: Bridges (Only if it's a bridge and NOT loopback) ---
        if (current.type == IfType::BRIDGE) {
            std::cout << "[SYNC] [BR] Interface is bridge..." << std::endl;
            std::string br_path = "/ieee802-dot1q-bridge:bridges/bridge[name='" + name + "']";
            auto br_res = forest ? forest->newPath2(br_path, std::nullopt) : ctx.newPath2(br_path, std::nullopt);
            if (!forest && br_res.createdParent) forest = br_res.createdParent;
            auto br_node = br_res.createdNode;

            std::string mac_ieee = mac_to_string(current.physAddress.data(), 6, '-');
            br_node->newPath2("address", mac_ieee);
            br_node->newPath2("bridge-type", "ieee802-dot1q-bridge:customer-vlan-bridge");

            auto comp_res = br_node->newPath2("component[name='" + name + "']", std::nullopt);
            auto comp_node = comp_res.createdNode;
            comp_node->newPath2("id", "1");
            comp_node->newPath2("type", "ieee802-dot1q-bridge:c-vlan-component");
        }

        // --- PASS 2: Interface Core ---
        std::string if_path = std::string("/ietf-interfaces:interfaces/interface[name='").append(name).append("']");

        std::cout << "[SYNC] [IF] Creating \"root\" interface node..." << std::endl;
        auto if_res = forest ? forest->newPath2(if_path, std::nullopt) : ctx.newPath2(if_path, std::nullopt);
        if (!forest && if_res.createdParent) forest = if_res.createdParent;
        auto if_node = if_res.createdNode;

        if_node->newPath2("type", ifTypeToIanaString(current.type));
        if_node->newPath2("enabled", current.adminEnabled ? "true" : "false");

        // --- PASS 3: Bridge-Port & TAS (Skip for Loopback) ---
        if (current.type == IfType::BRIDGE || current.type == IfType::ETHERNET) {
            std::string bp_path = if_path + "/ieee802-dot1q-bridge:bridge-port";

            auto bp_res = if_node->newPath2("ieee802-dot1q-bridge:bridge-port", std::nullopt);
            auto bp_node = bp_res.createdNode;

            if (current.bridgePort.masterIndex > 0) {
                bp_node->newPath2("bridge-name", current.bridgePort.bridgeName);
                bp_node->newPath2("component-name", current.bridgePort.bridgeName);
            }

            if (current.bridgePort.trafficClassData.mapDataSet) {
                auto tc_res = bp_node->newPath2("traffic-class/traffic-class-table", std::nullopt);
                auto tc_node = tc_res.createdNode;

                tc_node->newPath2("number-of-traffic-classes",
                                  std::to_string(current.bridgePort.trafficClassData.numTrafficClasses));
                tc_node->newPath2("priority0", std::to_string(current.bridgePort.trafficClassData.priorityMap[0]));
                tc_node->newPath2("priority1", std::to_string(current.bridgePort.trafficClassData.priorityMap[1]));
                tc_node->newPath2("priority2", std::to_string(current.bridgePort.trafficClassData.priorityMap[2]));
                tc_node->newPath2("priority3", std::to_string(current.bridgePort.trafficClassData.priorityMap[3]));
                tc_node->newPath2("priority4", std::to_string(current.bridgePort.trafficClassData.priorityMap[4]));
                tc_node->newPath2("priority5", std::to_string(current.bridgePort.trafficClassData.priorityMap[5]));
                tc_node->newPath2("priority6", std::to_string(current.bridgePort.trafficClassData.priorityMap[6]));
                tc_node->newPath2("priority7", std::to_string(current.bridgePort.trafficClassData.priorityMap[7]));
            }

            // Physical ports and Bridges get TAS capabilities to satisfy validation
            auto gpt_res = bp_node->newPath2("ieee802-dot1q-sched-bridge:gate-parameter-table", std::nullopt);
            auto gpt_node = gpt_res.createdNode;

            GclConfig_t& hw_cfg = current.bridgePort.gateParameterTable;

            std::cout << "[SYNC] [BP] Filling DataNode of gate-parameter-table with values from GclConfig_t struct"
                      << std::endl;
            fillGptNode(hw_cfg, gpt_node, GclFillOptions::FillAdmin);
        }

        if (current.type == IfType::ETHERNET) {
            // TODO: Get real values
            std::cout << "[SYNC] [LLDP] Enabling discovery on: " << current.name << std::endl;
            std::string lldp_path =
                "/ieee802-dot1ab-lldp:lldp/port[name='" + current.name + "'][dest-mac-address='01-80-c2-00-00-0e']";

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

    m_lldpDaemon = std::make_unique<LldpDaemon>(m_operSess);
    m_lldpDaemon->syncInitialNeighbors();
    m_lldpDaemon->startWatching();
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
    std::cout << "[CB_OPER] [IF] Received oper callback for module " << moduleName << "..." << std::endl;
    std::cout << "[CB_OPER] [IF] subXPath " << subXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_OPER] [IF] requestXPath " << requestXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_OPER] [IF] requestId " << requestId << "..." << std::endl;

    std::cout << "[CB_OPER] [IF] Refreshing interface status..." << std::endl;

    m_ifcache.setCurrentRequestId(requestId);
    m_ifcache.ensureFullLinkData(m_sock);
    m_ifcache.ensureFullQdiscData(m_sock);

    auto ctx = sess.getContext();
    std::cout << "[CB_OPER] [IF] Refreshing caches refreshed, start iterating..." << std::endl;
    for (auto& [idx, current] : m_ifcache.getAllInterfaces()) {
        std::string& name = current.name;
        std::cout << "[CB_OPER] [IF] Current: " << name << std::endl;

        std::string if_path = "/ietf-interfaces:interfaces/interface[name='" + name + "']";

        auto if_res = parent ? parent->newPath2(if_path, std::nullopt) : ctx.newPath2(if_path, std::nullopt);
        if (!parent && if_res.createdParent) parent = if_res.createdParent;
        auto if_node = if_res.createdNode;

        if_node->newPath("oper-status", operStatusToYangString(current.operStatus));
        if (current.type == IfType::BRIDGE || current.type == IfType::ETHERNET) {
            if_node->newPath("phys-address", mac_to_string(current.physAddress.data(), 6, ':'));
            auto bp_res = if_node->newPath2("ieee802-dot1q-bridge:bridge-port", std::nullopt);
            auto bp_node = bp_res.createdNode;
            auto gpt_res = bp_node->newPath2("ieee802-dot1q-sched-bridge:gate-parameter-table", std::nullopt);
            auto gpt_node = gpt_res.createdNode;

            GclConfig_t& hw_cfg = current.bridgePort.gateParameterTable;
            fillGptNode(hw_cfg, gpt_node, GclFillOptions::FillOper);
        }
    }
    // freeifaddrs(ifaddr);
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
    std::cout << "[CB_OPER] [BR] Received oper callback for module " << moduleName << "..." << std::endl;
    std::cout << "[CB_OPER] [BR] subXPath " << subXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_OPER] [BR] requestXPath " << requestXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_OPER] [BR] requestId " << requestId << "..." << std::endl;

    std::cout << "[CB_OPER] [BR] Scanning interfaces for bridge members..." << std::endl;

    m_ifcache.setCurrentRequestId(requestId);
    m_ifcache.ensureFullLinkData(m_sock);
    const auto& allIfaces = m_ifcache.getAllInterfaces();

    auto ctx = sess.getContext();

    std::unordered_map<int, std::vector<const std::string*>> masterToSlaves;
    for (const auto& [idx, iface] : allIfaces) {
        if (iface.bridgePort.masterIndex > 0) {
            // No string copy here, just pushing an 8-byte pointer
            masterToSlaves[iface.bridgePort.masterIndex].push_back(&(iface.name));
        }
    }

    // 2. Iterate through bridges
    for (const auto& [bridgeIdx, slaveNamePtrs] : masterToSlaves) {
        auto* bridgeIface = m_ifcache.getInterface(bridgeIdx);
        if (!bridgeIface) continue;

        const std::string& bridgeName = bridgeIface->name;

        // Path building - unfortunately some string manipulation is unavoidable
        // to create the XPath, but we keep it to one per bridge.
        std::string comp_path =
            "/ieee802-dot1q-bridge:bridges/bridge[name='" + bridgeName + "']/component[name='" + bridgeName + "']";
        auto comp_res = parent ? parent->newPath2(comp_path, std::nullopt) : ctx.newPath2(comp_path, std::nullopt);
        if (!parent && comp_res.createdParent) parent = comp_res.createdParent;
        auto comp_node = comp_res.createdNode;

        // 3. Add the leaf-list entries using the pointers
        for (const std::string* slaveName : slaveNamePtrs) {
            // We dereference the pointer here.
            // libyang will take the string value and store it in its internal tree.
            comp_node->newPath2("bridge-port", *slaveName);
        }
    }
    return sysrepo::ErrorCode::Ok;
}

sysrepo::ErrorCode tsnctrld::operLldpCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                              const std::optional<std::string>& subXPath,
                                              const std::optional<std::string>& requestXPath, uint32_t requestId,
                                              std::optional<libyang::DataNode>& parent) {
    std::cout << "[CB_OPER] [LLDP] Received oper callback for module " << moduleName << "..." << std::endl;
    std::cout << "[CB_OPER] [LLDP] subXPath " << subXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_OPER] [LLDP] requestXPath " << requestXPath.value_or("MISSING") << "..." << std::endl;
    std::cout << "[CB_OPER] [LLDP] requestId " << requestId << "..." << std::endl;

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
                std::string ifname = extractListKey(change.node.path(), "interface", "name");

                std::cout << "[CB_CHANGE] [GPT] Applying new config for " << ifname << std::endl;

                try {
                    // Update our internal cache and apply to hardware
                    // This function should be the one we optimized earlier (Zero-Copy)
                    std::cout << "[CB_CHANGE] [GPT] Getting data from sysrepo " << std::endl;
                    ietfInterface_t* iface = this->syncInterfaceFromSysrepo(sess, ifname, requestId);
                    std::cout << "[CB_CHANGE] [GPT] Returned data:" << std::endl;
                    printSingleInterface(*iface);

                    if (iface) {
                        if (iface->bridgePort.gateParameterTable.gateEnabled) {
                            std::cout << "[CB_CHANGE] [GPT] Gate is enabled, setting qdisc:" << std::endl;
                            std::cout << "[CB_CHANGE] [GPT] Converting to TaprioConfig struct:" << std::endl;
                            TaprioConfig taprioCfg =
                                NetconfNetlinkMapper::mapToTaprio(iface->bridgePort.gateParameterTable);

                            printTaprioConfig(taprioCfg);
                            std::cout << "[CB_CHANGE] [GPT] Sending qdisc" << std::endl;
                            m_qm.setQdisc(m_sock, ifname, taprioCfg);
                            std::cout << "[CB_CHANGE] [GPT] Qdisc \"sent\"" << std::endl;
                            m_pathsToReset.push_back(std::string(change.node.path()));
                        } else {
                            std::cout << "[CB_CHANGE] [GPT] Gate is disabled, removing qdisc:" << std::endl;
                            QdiscManager::removeQdisc(m_sock, ifname);
                            std::cout << "[CB_CHANGE] [GPT] Remove-request sent:" << std::endl;
                            m_pathsToReset.push_back(std::string(change.node.path()));
                        }
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
    m_sub->onOperGet("ietf-interfaces", operInterfaceCb, "/ietf-interfaces:interfaces/interface",
                     sysrepo::SubscribeOptions::OperMerge);

    auto operBridgeCb = std::bind_front(&tsnctrld::operBridgeCallback, this);
    m_sub->onOperGet("ieee802-dot1q-bridge", operBridgeCb, "/ieee802-dot1q-bridge:bridges",
                     sysrepo::SubscribeOptions::OperMerge);

    auto operLldpCb = std::bind_front(&tsnctrld::operLldpCallback, this);
    m_sub->onOperGet("ieee802-dot1ab-lldp", operLldpCb, "/ieee802-dot1ab-lldp:lldp",
                     sysrepo::SubscribeOptions::OperMerge);

    std::cout << "[INIT] [SUBS] Registered oper callbacks..." << std::endl;
}

tsnctrld::tsnctrld() : m_sess(m_conn.sessionStart()), m_operSess(m_conn.sessionStart()) {
    m_operSess.switchDatastore(sysrepo::Datastore::Operational);
}

int main() {
    tsnctrld daemon = tsnctrld();
    daemon.initialize();
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}