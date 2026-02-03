#include "include/tsnctrld.hpp"

#include <ifaddrs.h>

#include <ctime>
#include <iostream>
#include <thread>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>

// Utils
template<>
struct fmt::formatter<sysrepo::Event> : fmt::ostream_formatter {
};

std::string mac_to_string(unsigned char *sll_addr, int len, char separator) {
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

std::string extractListKey(const std::string &xpath, const std::string &listName, const std::string &keyName) {
    // Search for "interface[name='" specifically
    std::string searchPattern = listName + "[" + keyName + "='";

    size_t start = xpath.find(searchPattern);
    if (start == std::string::npos) return "";

    start += searchPattern.length();
    size_t end = xpath.find("']", start);
    if (end == std::string::npos) return "";

    return xpath.substr(start, end - start);
}

void printSingleInterface(const ietfInterface_t &iface) {
    spdlog::debug("Interface: {} [{}]", iface.name, iface.adminEnabled ? " UP" : " DOWN");
    spdlog::debug("  #TX-Queues: {}", iface.numTxQueues);
    spdlog::debug("  Bridge: {}", iface.bridgePort.bridgeName);

    const auto &gcl = iface.bridgePort.gateParameterTable;
    spdlog::debug("    GCL Admin Entries ({}):", gcl.adminControlList.size());
    for (const auto &entry: gcl.adminControlList) {
        spdlog::debug("      Idx: {} | States: {} | Interval: {}ns", entry.index, (int) entry.gateStatesValue,
                      entry.timeIntervalValue);
    }
    spdlog::debug("    GCL Oper Entries ({}):", gcl.operControlList.size());
    for (const auto &entry: gcl.operControlList) {
        spdlog::debug("      Idx: {} | States: {} | Interval: {}ns", entry.index, (int) entry.gateStatesValue,
                      entry.timeIntervalValue);
    }
}

void print_interfaces(const std::vector<ietfInterface_t> &interfaces) {
    for (const auto &iface: interfaces) {
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
            return "CLOCK_TAI"; // Usually what TAPRIO uses
        default:
            return "Unknown (" + std::to_string(clk) + ")";
    }
}

void printTaprioConfig(const TaprioConfig &cfg) {
    spdlog::debug("============================================");
    spdlog::debug("          TAPRIO CONFIGURATION              ");
    spdlog::debug("============================================");

    // 1. Basic Parameters
    spdlog::debug("Traffic Classes (numTc): {}", cfg.numTc);;
    spdlog::debug("Hardware Queues (numTxQs): {}", cfg.numTxQs);;
    spdlog::debug("Admin Clock Source:            {}", getClockName(cfg.admin.clockid));
    spdlog::debug("Admin Base Time (ns):          {}", cfg.admin.baseTime);
    spdlog::debug("Admin Cycle Time (ns):         {}", cfg.admin.cycleTime);
    // 2. Priority to Traffic Class Mapping
    spdlog::debug("Priority-to-TC Mapping:");
    spdlog::debug("  Prio | TC ");
    for (size_t i = 0; i < cfg.prioTc.size(); ++i) {
        spdlog::debug("  {:2} | {}", i, cfg.prioTc[i]);
    }

    spdlog::debug(" SDU: ");
    for (size_t i = 0; i < cfg.maxSDUs.size(); ++i) {
        spdlog::debug("  tc={}, max={}, pre={}", cfg.maxSDUs[i].trafficClass, cfg.maxSDUs[i].queueMaxSdu,
                      cfg.maxSDUs[i].preemtible);
    }

    // 3. The Schedule (Gate Control List)
    spdlog::debug("Admin Gate Control List (Schedule):");
    spdlog::debug("--------------------------------------------");
    spdlog::debug(" Index | Command | Gate Mask | Interval (ns) ");
    spdlog::debug("-------|---------|-----------|---------------");

    if (cfg.admin.entries.empty()) {
        spdlog::debug("          [ Schedule is empty ]             ");
    } else {
        int idx = 0;
        for (const auto &entry: cfg.admin.entries) {
            spdlog::debug(" {:5} |   {:5} |     0x{:02x}    | {:13}",
                          idx++, (int) entry.command, (int) entry.gateMask, entry.interval);
        }
    }
    spdlog::debug("--------------------------------------------");
}

void tsnctrld::resetTriggerLeaf(const std::string &xpath) {
    std::thread([this, xpath]() {
        try {
            // 1. Start a new session
            auto sess = m_sess.getConnection().sessionStart();

            // 2. SET THE ORIGINATOR NAME
            // This is how the callback will know 'we' did this
            sess.setOriginatorName("tsnctrld-internal");

            // 3. Perform the reset
            sess.setItem(xpath, "false");
            sess.applyChanges();

            spdlog::debug("[RESET] Successfully reset {} to false.", xpath);
        } catch (const std::exception &e) {
            spdlog::error("[RESET] [ERROR] {}.", e.what());
        }
    }).detach(); // Fire and forget
}

template<typename T>
T tsnctrld::getLeaf(const std::optional<libyang::DataNode> &node, const std::string &path) {
    if (!node) {
        return T{};
    }
    auto leaf = node->findPath(path);
    if (!leaf) {
        return T{};
    }
    return std::get<T>(leaf->asTerm().value());
}

/**
 *
 * @param entries List of `GclEntry_t`s to be added to the datanode
 * @param listToFill A DataNode representing either oper-control-list or admin-control-list
 */
void fillControlList(const std::vector<GclEntry_t> &entries, std::optional<libyang::DataNode> &listToFill) {
    spdlog::debug("[FILL CONTROLLIST] Filling of values from passed control list to passed datanode...");
    for (const auto &entry: entries) {
        spdlog::debug("[FILL CONTROLLIST] Current entry: idx={}, gsv={}, intr={}, opr={}",
                      entry.index,
                      entry.gateStatesValue,
                      entry.timeIntervalValue,
                      entry.operationName);

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
void fillGptNode(const GclConfig_t &hw_cfg, std::optional<libyang::DataNode> &toFill,
                 GclFillOptions options = GclFillOptions::OnlyDefault) {
    if (options & GclFillOptions::FillAdmin) {
        spdlog::debug("[FILL DATANODE] Filling of Admin data requested...");
        spdlog::debug("[FILL DATANODE] Writing \"supported-*\" nodes...");
        toFill->newPath2("supported-list-max", std::to_string(hw_cfg.supportedListMax));
        toFill->newPath2("supported-cycle-max/numerator", std::to_string(hw_cfg.supportedCycleMaxNumerator));
        toFill->newPath2("supported-cycle-max/denominator", std::to_string(hw_cfg.supportedCycleMaxDenominator));
        toFill->newPath2("supported-interval-max", std::to_string(hw_cfg.supportedIntervalMax));

        toFill->newPath2("gate-enabled", hw_cfg.gateEnabled ? "true" : "false");
        if (hw_cfg.operDataSet && !hw_cfg.adminDataSet) {
            spdlog::debug("[FILL DATANODE] Admin data not set, but Oper data is, using that...");
            toFill->newPath2("admin-base-time/seconds", std::to_string(hw_cfg.operBaseTime.seconds));
            toFill->newPath2("admin-base-time/nanoseconds", std::to_string(hw_cfg.operBaseTime.nanoseconds));
            auto admin_res = toFill->newPath2("admin-control-list", std::nullopt);
            auto admin_node = admin_res.createdNode;
            fillControlList(hw_cfg.operControlList, admin_node);

            spdlog::debug("[FILL DATANODE] Using operCycleTime for mandatory adminCycleTime...");
            toFill->newPath2("admin-cycle-time/numerator", std::to_string(hw_cfg.operCycleTime.numerator));
            toFill->newPath2("admin-cycle-time/denominator", std::to_string(hw_cfg.operCycleTime.denominator));
        } else if (hw_cfg.adminDataSet) {
            spdlog::debug("[FILL DATANODE] Admin data is set, using that...");

            toFill->newPath2("admin-base-time/seconds", std::to_string(hw_cfg.adminBaseTime.seconds));
            toFill->newPath2("admin-base-time/nanoseconds", std::to_string(hw_cfg.adminBaseTime.nanoseconds));
            auto admin_res = toFill->newPath2("admin-control-list", std::nullopt);
            auto admin_node = admin_res.createdNode;
            fillControlList(hw_cfg.adminControlList, admin_node);

            spdlog::debug("[FILL DATANODE] Using set adminCycleTime for mandatory element...");
            toFill->newPath2("admin-cycle-time/numerator", std::to_string(hw_cfg.adminCycleTime.numerator));
            toFill->newPath2("admin-cycle-time/denominator", std::to_string(hw_cfg.adminCycleTime.denominator));
        } else {
            spdlog::debug("[FILL DATANODE] Neither admin nor oper is set, assume nothing TSN is configured...");
            spdlog::debug("[FILL DATANODE] Using default adminCycleTime for mandatory adminCycleTime...");
            toFill->newPath2("admin-cycle-time/numerator", std::to_string(hw_cfg.adminCycleTime.numerator));
            toFill->newPath2("admin-cycle-time/denominator", std::to_string(hw_cfg.adminCycleTime.denominator));
        }
    }

    if (options & GclFillOptions::FillOper) {
        spdlog::debug("[FILL DATANODE] Filling of Oper data requested...");
        if (hw_cfg.operDataSet) {
            spdlog::debug("[FILL DATANODE] Oper data is set, using that...");
            toFill->newPath2("oper-base-time/seconds", std::to_string(hw_cfg.operBaseTime.seconds));
            toFill->newPath2("oper-base-time/nanoseconds", std::to_string(hw_cfg.operBaseTime.nanoseconds));
            auto oper_res = toFill->newPath2("oper-control-list", std::nullopt);
            auto oper_node = oper_res.createdNode;
            fillControlList(hw_cfg.operControlList, oper_node);
        } else {
            spdlog::warn(
                "[FILL DATANODE] Oper data requested but not set, assume nothing TSN is configured and ignoring...");
        }
    }
}

ietfInterface_t *tsnctrld::syncInterfaceFromSysrepo(sysrepo::Session &sess, const std::string &ifname,
                                                    uint32_t requestId) {
    static const uint8_t ieee8021q_default_tc_map[8][8] = {
        // TC count: 1  2  3  4  5  6  7  8
        /* P0 */ {0, 0, 0, 0, 0, 1, 1, 1},
        /* P1 */ {0, 0, 0, 0, 0, 0, 0, 0},
        /* P2 */ {0, 0, 0, 1, 1, 2, 2, 2},
        /* P3 */ {0, 0, 0, 1, 1, 2, 3, 3},
        /* P4 */ {0, 1, 1, 2, 2, 3, 4, 4},
        /* P5 */ {0, 1, 1, 2, 2, 3, 4, 5},
        /* P6 */ {0, 1, 2, 3, 3, 4, 5, 6},
        /* P7 */ {0, 1, 2, 3, 4, 5, 6, 7}
    };

    m_ifcache.setCurrentRequestId(requestId);
    m_ifcache.ensureFullLinkData(m_sock);

    ietfInterface_t *iface_ptr = m_ifcache.getInterface(ifname);
    if (!iface_ptr) {
        spdlog::warn("[SYSREPO->STRUCT] [DEBUG] no interface found with name {}", ifname);
        return nullptr;
    }

    ietfInterface_t &iface = *iface_ptr;
    BridgePort_t &bp = iface.bridgePort;
    GclConfig_t &gcl = bp.gateParameterTable;

    // 2. Fetch the subtree once
    std::string basePath = "/ietf-interfaces:interfaces/interface[name='" + ifname +
                           "']/"
                           "ieee802-dot1q-bridge:bridge-port";

    // Use sess.getData() to get the proposed tree
    auto bpRoot = sess.getData(basePath);
    if (!bpRoot) {
        spdlog::error("[SYSREPO->STRUCT] bpData is null, failed...");
        return nullptr;
    }
    auto bpData = bpRoot->findPath(basePath);
    spdlog::debug("[SYSREPO->STRUCT] [DEBUG] after root->bpData...");
    spdlog::debug("  -> [SYSREPO->STRUCT] [DEBUG] Data forest:\n {}",
                  bpData->printStr(libyang::DataFormat::XML, libyang::PrintFlags::Siblings).value());


    auto tcData = bpData->findPath("traffic-class/traffic-class-table");
    spdlog::debug("  -> [SYSREPO->STRUCT] [DEBUG] tcData:\n {}",
                  tcData->printStr(libyang::DataFormat::XML,
                                   libyang::PrintFlags::Siblings | libyang::PrintFlags::EmptyContainers |
                                   libyang::PrintFlags::WithDefaultsAll).value_or("Missing"));

    bp.trafficClassData.mapDataSet = true;
    if (tcData.has_value() && tcData->child().has_value()) {
        spdlog::debug("[SYSREPO->STRUCT] [DEBUG] found traffic-class/traffic-class-table...");
        bp.trafficClassData.numTrafficClasses = getLeaf<uint8_t>(tcData, "number-of-traffic-classes");
        bp.trafficClassData.priorityMap[0] = getLeaf<uint8_t>(tcData, "priority0");
        bp.trafficClassData.priorityMap[1] = getLeaf<uint8_t>(tcData, "priority1");
        bp.trafficClassData.priorityMap[2] = getLeaf<uint8_t>(tcData, "priority2");
        bp.trafficClassData.priorityMap[3] = getLeaf<uint8_t>(tcData, "priority3");
        bp.trafficClassData.priorityMap[4] = getLeaf<uint8_t>(tcData, "priority4");
        bp.trafficClassData.priorityMap[5] = getLeaf<uint8_t>(tcData, "priority5");
        bp.trafficClassData.priorityMap[6] = getLeaf<uint8_t>(tcData, "priority6");
        bp.trafficClassData.priorityMap[7] = getLeaf<uint8_t>(tcData, "priority7");
    } else {
        // Commented code to create invalid config
        spdlog::debug(
            "[SYSREPO->STRUCT] [DEBUG] traffic-class/traffic-class-table not found or empty, using defaults based on number of tx-queues...");
        bp.trafficClassData.numTrafficClasses = iface.numTxQueues < 8 ? iface.numTxQueues : 8;
        uint8_t colIndex = bp.trafficClassData.numTrafficClasses - 1;
        for (int priority = 0; priority < 8; ++priority) {
            bp.trafficClassData.priorityMap[priority] = ieee8021q_default_tc_map[priority][colIndex];
        }
    }


    auto gclData = bpData->findPath("ieee802-dot1q-sched-bridge:gate-parameter-table");
    // Update members directly in the cache (No copy of the whole struct)
    gcl.gateEnabled = getLeaf<bool>(gclData, "gate-enabled");

    gcl.adminCycleTime.numerator = getLeaf<uint32_t>(gclData, "admin-cycle-time/numerator");
    gcl.adminCycleTime.denominator = getLeaf<uint32_t>(gclData, "admin-cycle-time/denominator");
    spdlog::debug("[SYSREPO->STRUCT] admin-cycle-time: {}/{}", gcl.adminCycleTime.numerator,
                  gcl.adminCycleTime.denominator);

    // 4. Update the Control List (The expensive part)
    gcl.adminControlList.clear(); // Reuse the vector's capacity!
    auto entries = gclData->findXPath("admin-control-list/gate-control-entry");

    for (const auto &entryNode: entries) {
        // We emplace directly into the existing vector
        auto &e = gcl.adminControlList.emplace_back();

        // Use typed access for every leaf
        e.index = getLeaf<uint32_t>(entryNode, "index");
        e.gateStatesValue = getLeaf<uint8_t>(entryNode, "gate-states-value");
        e.timeIntervalValue = getLeaf<uint32_t>(entryNode, "time-interval-value");
        e.operationName = entryNode.findPath("operation-name")->asTerm().valueStr();
    }
    // Todo: Remove fake gclData, get from DS instead
    static queueMaxSduEntry_t queueMaxSduTable[8] = {
        {.trafficClass = 0, .queueMaxSdu = 1500, .transmissionOverrun = 0},
        {.trafficClass = 1, .queueMaxSdu = 1500, .transmissionOverrun = 0},
        {.trafficClass = 2, .queueMaxSdu = 1500, .transmissionOverrun = 0},
        {.trafficClass = 3, .queueMaxSdu = 1500, .transmissionOverrun = 0},
        {.trafficClass = 4, .queueMaxSdu = 1500, .transmissionOverrun = 0},
        {.trafficClass = 5, .queueMaxSdu = 1500, .transmissionOverrun = 0},
        {.trafficClass = 6, .queueMaxSdu = 1500, .transmissionOverrun = 0},
        {.trafficClass = 7, .queueMaxSdu = 1500, .transmissionOverrun = 0}
    };
    gcl.queueMaxSduTable.assign(queueMaxSduTable, queueMaxSduTable + bp.trafficClassData.numTrafficClasses);

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

    spdlog::debug("[INIT] Initializing tsnctrld...");

    // In order for our program to be the source of truth, start with an empty datastore.
    spdlog::debug("[INIT] [DEBUG] Cleaning interfaces from datastore ");
    m_sess.deleteItem("/ietf-interfaces:interfaces");
    spdlog::debug("[INIT] [DEBUG] Cleaning bridges from datastore ");
    m_sess.deleteItem("/ieee802-dot1q-bridge:bridges");
    spdlog::debug("[INIT] [DEBUG] Cleaning lldp from datastore ");
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
    spdlog::debug("[SYNC] Populating internal list m_interfaces from kernel");
    m_sess.switchDatastore(sysrepo::Datastore::Running);
    auto ctx = m_sess.getContext();
    std::optional<libyang::DataNode> forest;

    m_ifcache.setCurrentRequestId(0);
    m_ifcache.ensureFullLinkData(m_sock);
    m_ifcache.ensureFullQdiscData(m_sock);

    spdlog::debug("[SYNC] Iterating over interfaces...");

    const auto &allIfaces = m_ifcache.getAllInterfaces();
    for (const auto &[idx, iface]: allIfaces) {
        printSingleInterface(iface);
    }
    spdlog::debug("[SYNC] Print done...");

    for (auto &[idx, current]: m_ifcache.getAllInterfaces()) {
        std::string &name = current.name;

        spdlog::debug("[SYNC] Current interface: {}...", name);
        // std::string mac_ietf = mac_to_string(s->sll_addr, s->sll_halen, ':');
        // std::string mac_ieee = mac_to_string(s->sll_addr, s->sll_halen, '-');

        // --- PASS 1: Bridges (Only if it's a bridge and NOT loopback) ---
        if (current.type == IfType::BRIDGE) {
            spdlog::debug("[SYNC] [BR] Interface is bridge...");
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

        spdlog::debug("[SYNC] [IF] Creating \"root\" interface node...");
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
                spdlog::debug("[SYNC] [BR] Current interface \"{}\" is attached to bridge \"{}\" with id {}...",
                              current.name, current.bridgePort.bridgeName, current.bridgePort.bridgeName);
                bp_node->newPath2("bridge-name", current.bridgePort.bridgeName);
                bp_node->newPath2("component-name", current.bridgePort.bridgeName);
            }

            if (current.bridgePort.trafficClassData.mapDataSet) {
                spdlog::debug("[SYNC] [BR] Current interface \"{}\" has a priority map...", current.name);
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

            GclConfig_t &hw_cfg = current.bridgePort.gateParameterTable;

            spdlog::debug("[SYNC] [BP] Filling DataNode of gate-parameter-table with values from GclConfig_t struct");
            fillGptNode(hw_cfg, gpt_node, GclFillOptions::FillAdmin);
        }

        if (current.type == IfType::ETHERNET) {
            // TODO: Get real values
            spdlog::debug("[SYNC] [LLDP] Enabling discovery on: {}", current.name);
            std::string lldp_path =
                    "/ieee802-dot1ab-lldp:lldp/port[name='" + current.name + "'][dest-mac-address='01-80-c2-00-00-0e']";

            auto lldp_res = forest->newPath2(lldp_path, std::nullopt);
            auto lldp_node = lldp_res.createdNode;
            lldp_node->newPath2("admin-status", "tx-and-rx");
        }
    }
    // freeifaddrs(ifaddr);

    if (forest) {
        spdlog::debug("[SYNC] Applying Batch to Datastore...");
        spdlog::debug("[SYNC] Switching to first sibling...");
        forest = forest->firstSibling();
        spdlog::debug("  -> [SYNC DEBUG] Data forest:\n {}",
                      forest->printStr(libyang::DataFormat::XML, libyang::PrintFlags::Siblings).value());
        spdlog::debug("[SYNC] Editing batch...");
        m_sess.editBatch(*forest, sysrepo::DefaultOperation::Merge);
        spdlog::debug("[SYNC] Applying changes...");
        m_sess.applyChanges();
        spdlog::debug("[SYNC] Datastore synchronized.");
    }

    m_lldpDaemon = std::make_unique<LldpDaemon>(m_operSess);
    m_lldpDaemon->syncInitialNeighbors();
    m_lldpDaemon->startWatching();
}

sysrepo::ErrorCode tsnctrld::defaultOperCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                                 const std::optional<std::string> &subXPath,
                                                 const std::optional<std::string> &requestXPath, uint32_t requestId,
                                                 std::optional<libyang::DataNode> &parent) {
    spdlog::debug("[CB_OPER] [DEFAULT] Received oper callback for module {}...", moduleName);
    spdlog::debug("[CB_OPER] [DEFAULT] subXPath {}...", subXPath.value_or("MISSING"));
    spdlog::debug("[CB_OPER] [DEFAULT] requestXPath {}...", requestXPath.value_or("MISSING"));
    spdlog::debug("[CB_OPER] [DEFAULT] requestId {}...", requestId);
    auto ctx = sess.getContext();
    if (!parent)
        spdlog::debug("[CB_OPER] [DEFAULT] parent is falsy...");
    else
        spdlog::debug("[CB_OPER] [DEFAULT] parent is truthy...");
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
sysrepo::ErrorCode tsnctrld::operInterfaceCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                                   const std::optional<std::string> &subXPath,
                                                   const std::optional<std::string> &requestXPath, uint32_t requestId,
                                                   std::optional<libyang::DataNode> &parent) {
    spdlog::debug("[CB_OPER] [IF] Received oper callback for module {}...", moduleName);
    spdlog::debug("[CB_OPER] [IF] subXPath {}...", subXPath.value_or("MISSING"));
    spdlog::debug("[CB_OPER] [IF] requestXPath {}...", requestXPath.value_or("MISSING"));
    spdlog::debug("[CB_OPER] [IF] requestId {}...", requestId);

    spdlog::debug("[CB_OPER] [IF] Refreshing interface status...");

    m_ifcache.setCurrentRequestId(requestId);
    m_ifcache.ensureFullLinkData(m_sock);
    m_ifcache.ensureFullQdiscData(m_sock);

    auto ctx = sess.getContext();
    spdlog::debug("[CB_OPER] [IF] Refreshing caches refreshed, start iterating...");
    for (auto &[idx, current]: m_ifcache.getAllInterfaces()) {
        std::string &name = current.name;
        spdlog::debug("[CB_OPER] [IF] Current: {}", name);

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

            GclConfig_t &hw_cfg = current.bridgePort.gateParameterTable;
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
sysrepo::ErrorCode tsnctrld::operBridgeCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                                const std::optional<std::string> &subXPath,
                                                const std::optional<std::string> &requestXPath, uint32_t requestId,
                                                std::optional<libyang::DataNode> &parent) {
    spdlog::debug("[CB_OPER] [BR] Received oper callback for module {}...", moduleName);
    spdlog::debug("[CB_OPER] [BR] subXPath {}...", subXPath.value_or("MISSING"));
    spdlog::debug("[CB_OPER] [BR] requestXPath {}...", requestXPath.value_or("MISSING"));
    spdlog::debug("[CB_OPER] [BR] requestId {}...", requestId);

    spdlog::debug("[CB_OPER] [BR] Scanning interfaces for bridge members...");

    m_ifcache.setCurrentRequestId(requestId);
    m_ifcache.ensureFullLinkData(m_sock);
    const auto &allIfaces = m_ifcache.getAllInterfaces();

    auto ctx = sess.getContext();

    std::unordered_map<int, std::vector<const std::string *> > masterToSlaves;
    for (const auto &[idx, iface]: allIfaces) {
        if (iface.bridgePort.masterIndex > 0) {
            // No string copy here, just pushing an 8-byte pointer
            masterToSlaves[iface.bridgePort.masterIndex].push_back(&(iface.name));
        }
    }

    // 2. Iterate through bridges
    for (const auto &[bridgeIdx, slaveNamePtrs]: masterToSlaves) {
        auto *bridgeIface = m_ifcache.getInterface(bridgeIdx);
        if (!bridgeIface) continue;

        const std::string &bridgeName = bridgeIface->name;

        // Path building - unfortunately some string manipulation is unavoidable
        // to create the XPath, but we keep it to one per bridge.
        std::string comp_path =
                "/ieee802-dot1q-bridge:bridges/bridge[name='" + bridgeName + "']/component[name='" + bridgeName + "']";
        auto comp_res = parent ? parent->newPath2(comp_path, std::nullopt) : ctx.newPath2(comp_path, std::nullopt);
        if (!parent && comp_res.createdParent) parent = comp_res.createdParent;
        auto comp_node = comp_res.createdNode;

        // 3. Add the leaf-list entries using the pointers
        for (const std::string *slaveName: slaveNamePtrs) {
            // We dereference the pointer here.
            // libyang will take the string value and store it in its internal tree.
            comp_node->newPath2("bridge-port", *slaveName);
        }
    }
    return sysrepo::ErrorCode::Ok;
}

sysrepo::ErrorCode tsnctrld::operLldpCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                              const std::optional<std::string> &subXPath,
                                              const std::optional<std::string> &requestXPath, uint32_t requestId,
                                              std::optional<libyang::DataNode> &parent) {
    spdlog::debug("[CB_OPER] [LLDP] Received oper callback for module {}...", moduleName);
    spdlog::debug("[CB_OPER] [LLDP] subXPath {}...", subXPath.value_or("MISSING"));
    spdlog::debug("[CB_OPER] [LLDP] requestXPath {}...", requestXPath.value_or("MISSING"));
    spdlog::debug("[CB_OPER] [LLDP] requestId {}...", requestId);

    return sysrepo::ErrorCode::Ok;
}

sysrepo::ErrorCode tsnctrld::defaultChangeCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                                   const std::optional<std::string> &subXPath, sysrepo::Event event,
                                                   uint32_t requestId) {
    spdlog::debug("[CB_CHANGE] [DEFAULT] Received change callback for module {}...", moduleName);
    spdlog::debug("[CB_CHANGE] [DEFAULT] subXPath {}...", subXPath.value_or("MISSING"));
    spdlog::debug("[CB_CHANGE] [DEFAULT] event {}...", event);
    spdlog::debug("[CB_CHANGE] [DEFAULT] requestId {}...", requestId);
    this;
    return sysrepo::ErrorCode::Ok;
}

sysrepo::ErrorCode tsnctrld::changeInterfaceCallback(sysrepo::Session sess, uint32_t subId,
                                                     const std::string &moduleName,
                                                     const std::optional<std::string> &subXPath, sysrepo::Event event,
                                                     uint32_t requestId) {
    spdlog::debug("[CB_CHANGE] [IF] Received change callback for module {}...", moduleName);
    spdlog::debug("[CB_CHANGE] [IF] subXPath {}...", subXPath.value_or("MISSING"));
    spdlog::debug("[CB_CHANGE] [IF] event {}...", event);
    spdlog::debug("[CB_CHANGE] [IF] requestId {}...", requestId);
    if (event != sysrepo::Event::Change) return sysrepo::ErrorCode::Ok;

    for (const auto &change: sess.getChanges("//.")) {
        std::string nodeModuleName = change.node.schema().module().name();
        std::string nodeName = change.node.schema().name();

        if (nodeModuleName != moduleName) {
            spdlog::debug("[CB] [IF-CONFIG] [DEBUG] Change does not belong to this module, skipping");
            continue;
        }
        if (nodeName == "TEMP") {
            spdlog::debug("[CB] [IF-CONFIG] [ALLOW] Attempting to change whitelisted node, continuing for now");
            continue;
        }
        spdlog::error("[CB] [IF-CONFIG] [REJECT] Edits to 'ietf-interfaces' (specifically the top-level `/interfaces` "
                      "element) are currently not implemented. Attempted change to path: {}", change.node.path());
        return sysrepo::ErrorCode::Unsupported;
    }
    spdlog::debug("[CB] [IF-CONFIG] [ALLOW] All attempted chages were allowed, accepting");
    return sysrepo::ErrorCode::Ok;
}

sysrepo::ErrorCode tsnctrld::changeBridgeCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                                  const std::optional<std::string> &subXPath, sysrepo::Event event,
                                                  uint32_t requestId) {
    spdlog::debug("[CB_CHANGE] [BR] Received change callback for module {}...", moduleName);
    spdlog::debug("[CB_CHANGE] [BR] subXPath {}...", subXPath.value_or("MISSING"));
    spdlog::debug("[CB_CHANGE] [BR] event {}...", event);
    spdlog::debug("[CB_CHANGE] [BR] requestId {}...", requestId);
    if (event != sysrepo::Event::Change) return sysrepo::ErrorCode::Ok;

    for (const auto &change: sess.getChanges("//.")) {
        std::string nodeModuleName = change.node.schema().module().name();
        std::string nodeName = change.node.schema().name();

        if (nodeModuleName != moduleName) {
            spdlog::debug("[CB] [BR-CONFIG] [DEBUG] Change does not belong to this module, skipping");
            continue;
        }
        if (nodeName == "TEMP") {
            spdlog::debug("[CB] [BR-CONFIG] [ALLOW] Attempting to change whitelisted node, continuing for now");
            continue;
        }
        spdlog::error(
            "[CB] [BR-CONFIG] [REJECT] Edits to 'ieee802-dot1q-bridge' (specifically the top-level `/bridges` element) are currently not implemented. Attempted change to path: {}",
            change.node.path());
        return sysrepo::ErrorCode::Unsupported;
    }
    spdlog::debug("[CB] [BR-CONFIG] [ALLOW] All attempted chages were allowed, accepting");
    return sysrepo::ErrorCode::Ok;
}

sysrepo::ErrorCode tsnctrld::changeGptCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                               const std::optional<std::string> &subXPath, sysrepo::Event event,
                                               uint32_t requestId) {
    spdlog::debug("[CB_CHANGE] [GPT] Received change callback for module {}...", moduleName);
    spdlog::debug("[CB_CHANGE] [GPT] subXPath {}...", subXPath.value_or("MISSING"));
    spdlog::debug("[CB_CHANGE] [GPT] event {}...", event);
    spdlog::debug("[CB_CHANGE] [GPT] requestId {}...", requestId);

    if (sess.getOriginatorName() == "tsnctrld-internal") {
        spdlog::debug("[CB_CHANGE] [GPT] Whatever just happened, we did it, so we can trust it...");
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

        for (const auto &change: changes) {
            // Check if node was deleted (ignore)
            if (change.operation == sysrepo::ChangeOperation::Deleted) continue;

            // Only trigger if changed to "true"
            if (change.node.asTerm().valueStr() == "true") {
                std::string ifname = extractListKey(change.node.path(), "interface", "name");

                spdlog::debug("[CB_CHANGE] [GPT] Applying new config for {}", ifname);

                try {
                    // Update our internal cache and apply to hardware
                    // This function should be the one we optimized earlier (Zero-Copy)
                    spdlog::debug("[CB_CHANGE] [GPT] Getting data from sysrepo ");
                    ietfInterface_t *iface = this->syncInterfaceFromSysrepo(sess, ifname, requestId);
                    spdlog::debug("[CB_CHANGE] [GPT] Returned data:");
                    printSingleInterface(*iface);

                    if (iface) {
                        if (iface->bridgePort.gateParameterTable.gateEnabled) {
                            spdlog::debug("[CB_CHANGE] [GPT] Gate is enabled, setting qdisc:");
                            spdlog::debug("[CB_CHANGE] [GPT] Converting to TaprioConfig struct:");
                            TaprioConfig taprioCfg =
                                    NetconfNetlinkMapper::mapToTaprio(*iface);

                            printTaprioConfig(taprioCfg);
                            spdlog::debug("[CB_CHANGE] [GPT] Sending qdisc");
                            m_qm.setQdisc(m_sock, ifname, taprioCfg);
                            spdlog::debug("[CB_CHANGE] [GPT] Qdisc \"sent\"");
                            m_pathsToReset.push_back(std::string(change.node.path()));
                        } else {
                            spdlog::debug("[CB_CHANGE] [GPT] Gate is disabled, removing qdisc:");
                            QdiscManager::removeQdisc(m_sock, ifname);
                            spdlog::debug("[CB_CHANGE] [GPT] Remove-request sent:");
                            m_pathsToReset.push_back(std::string(change.node.path()));
                        }
                    }
                } catch (const std::exception &e) {
                    spdlog::error("[CB_CHANGE] [GPT] [ERROR] Hardware rejected config: {}", e.what());
                    m_pathsToReset.clear();
                    return sysrepo::ErrorCode::OperationFailed;
                }
            }
        }
    }

    // 2. PHASE: RESET TRIGGER (Event::Done)
    if (event == sysrepo::Event::Done) {
        for (const auto &path: m_pathsToReset) {
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
sysrepo::ErrorCode tsnctrld::changeLldpCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                                const std::optional<std::string> &subXPath, sysrepo::Event event,
                                                uint32_t requestId) {
    spdlog::debug("[CB_CHANGE] [LLDP] Received change callback for module {}...", moduleName);
    spdlog::debug("[CB_CHANGE] [LLDP] subXPath {}...", subXPath.value_or("MISSING"));
    spdlog::debug("[CB_CHANGE] [LLDP] event {}...", event);
    spdlog::debug("[CB_CHANGE] [LLDP] requestId {}...", requestId);

    if (event != sysrepo::Event::Change) return sysrepo::ErrorCode::Ok;

    for (const auto &change: sess.getChanges("//.")) {
        if (change.node.schema().module().name() != moduleName) continue;

        spdlog::error(
            "[CB] [LLDP-CONFIG] [REJECT] Edits to 'ieee802-dot1ab-lldp' (LLDP) are currently not implemented. Attempted change: {}",
            change.node.path());
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
    spdlog::debug("[INIT] [SUBS] Registering granular callbacks...");
    auto defaultChangeCb = std::bind_front(&tsnctrld::defaultChangeCallback, this);

    auto changeInterfaceCb = std::bind_front(&tsnctrld::changeInterfaceCallback, this);
    m_subs.push_back(m_sess.onModuleChange("ietf-interfaces", changeInterfaceCb, std::nullopt, 100));

    auto changeBridgeCb = std::bind_front(&tsnctrld::changeBridgeCallback, this);
    m_subs.push_back(m_sess.onModuleChange("ieee802-dot1q-bridge", changeBridgeCb, std::nullopt, 90));

    auto changeGptCb = std::bind_front(&tsnctrld::changeGptCallback, this);
    m_subs.push_back(m_sess.onModuleChange("ietf-interfaces", changeGptCb,
                          "/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port/"
                          "ieee802-dot1q-sched-bridge:gate-parameter-table", 80));

    auto changeLldpCb = std::bind_front(&tsnctrld::changeLldpCallback, this);
    m_subs.push_back(m_sess.onModuleChange("ieee802-dot1ab-lldp", changeLldpCb, std::nullopt, 70));

    spdlog::debug("[INIT] [SUBS] Registered change callbacks...");

    auto defaultOperCb = std::bind_front(&tsnctrld::defaultOperCallback, this);

    auto operInterfaceCb = std::bind_front(&tsnctrld::operInterfaceCallback, this);
    m_subs.push_back(m_sess.onOperGet("ietf-interfaces", operInterfaceCb, "/ietf-interfaces:interfaces/interface",
                     sysrepo::SubscribeOptions::OperMerge));

    auto operBridgeCb = std::bind_front(&tsnctrld::operBridgeCallback, this);
    m_subs.push_back(m_sess.onOperGet("ieee802-dot1q-bridge", operBridgeCb, "/ieee802-dot1q-bridge:bridges",
                     sysrepo::SubscribeOptions::OperMerge));

    auto operLldpCb = std::bind_front(&tsnctrld::operLldpCallback, this);
    m_subs.push_back(m_sess.onOperGet("ieee802-dot1ab-lldp", operLldpCb, "/ieee802-dot1ab-lldp:lldp",
                     sysrepo::SubscribeOptions::OperMerge));

    spdlog::debug("[INIT] [SUBS] Registered oper callbacks...");
}

tsnctrld::tsnctrld() : m_sess(m_conn.sessionStart()), m_operSess(m_conn.sessionStart()) {
    m_operSess.switchDatastore(sysrepo::Datastore::Operational);
}

int main() {
    spdlog::set_level(spdlog::level::trace);
    tsnctrld daemon = tsnctrld();
    daemon.initialize();
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
