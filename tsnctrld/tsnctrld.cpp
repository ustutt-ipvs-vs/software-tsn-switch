#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include "include/tsnctrld.hpp"

#include <ifaddrs.h>
#include <spdlog/fmt/ostr.h>
#include <spdlog/spdlog.h>
#include <systemd/sd-bus.h>

#include <ctime>
#include <iostream>
#include <thread>

static constexpr std::array IEEE8021Q_DEFAULT_TC_MAP = {
    // TC count:                    1  2  3  4  5  6  7  8
    /* P0 */ std::array<uint8_t, 8>{0, 0, 0, 0, 0, 1, 1, 1},
    /* P1 */ std::array<uint8_t, 8>{0, 0, 0, 0, 0, 0, 0, 0},
    /* P2 */ std::array<uint8_t, 8>{0, 0, 0, 1, 1, 2, 2, 2},
    /* P3 */ std::array<uint8_t, 8>{0, 0, 0, 1, 1, 2, 3, 3},
    /* P4 */ std::array<uint8_t, 8>{0, 1, 1, 2, 2, 3, 4, 4},
    /* P5 */ std::array<uint8_t, 8>{0, 1, 1, 2, 2, 3, 4, 5},
    /* P6 */ std::array<uint8_t, 8>{0, 1, 2, 3, 3, 4, 5, 6},
    /* P7 */ std::array<uint8_t, 8>{0, 1, 2, 3, 4, 5, 6, 7}};

// Utils
/**
 * @brief Allows formatting sysrepo::Events for logging using spdlog/fmt
 */
template <>
struct fmt::formatter<sysrepo::Event> : ostream_formatter {};

/**
 * @brief Helper to convert a byte-array into the string representation of the MAC address it represents
 * @param sll_addr Pointer to the byte-array containing the supposed MAC address
 * @param len Length of the supposed MAC address.
 * @param separator Separator to use between each byte, e.g. IEEE (aka ieee802-dot1q-bridge) uses '-', and IETF (aka
 * ieft-interfaces) uses ':'.
 * @return The string representation of the byte-array. If it is not a regular 6 byte address, every octet is "00".
 */
std::string mac_to_string(const unsigned char *sll_addr, const int len, char separator) {
    if (len != 6) {
        return separator == ':' ? "00:00:00:00:00:00" : "00-00-00-00-00-00";
    }
    return fmt::format("{:02x}{}{:02x}{}{:02x}{}{:02x}{}{:02x}{}{:02x}", sll_addr[0], separator, sll_addr[1], separator,
                       sll_addr[2], separator, sll_addr[3], separator, sll_addr[4], separator, sll_addr[5]);
}

/**
 * @brief Helper to find the key of an element in a list in a given XPath.
 *
 * Only the key-value in lists that are both named @ref listName and have the (first) key @ref keyName are found.
 *
 * @param xpath The actual XPath to search for
 * @param listName The list in which to search for the value of the key @ref keyName
 * @param keyName The key to search for in the list @listName
 * @return The value of the key defined by @ref listName and @ref keyName
 */
std::string extractListKey(std::string_view xpath, const std::string_view listName, const std::string_view keyName) {
    size_t pos = xpath.find(listName);

    while (pos != std::string_view::npos) {
        // 1. Ensure we didn't just find a SUBSTRING of a longer name
        // (e.g., finding "eth" inside "ethernet")
        if (pos == 0 || xpath[pos - 1] == '/' || xpath[pos - 1] == ':') {
            // 2. Sequence Check: Must be directly followed by "[keyName='"
            if (std::string_view sub = xpath.substr(pos + listName.size()); sub.starts_with('[')) {
                sub.remove_prefix(1);
                if (sub.starts_with(keyName)) {
                    sub.remove_prefix(keyName.size());
                    if (sub.starts_with("='")) {
                        sub.remove_prefix(2);

                        if (const size_t valEnd = sub.find("']"); valEnd != std::string_view::npos) {
                            // This is the ONLY allocation in the whole process
                            return std::string(sub.substr(0, valEnd));
                        }
                    }
                }
            }
        }

        // 3. Jump Optimization:
        // Search for the next listName starting immediately after this one
        pos = xpath.find(listName, pos + listName.size());
    }
    return "";
}

/**
 * @brief Helper to print a single interface
 * @param iface The interface to print
 */
void printSingleInterface(const ietfInterface_t &iface) {
    SPDLOG_DEBUG("Interface: {} [{}]", iface.name, iface.adminEnabled ? "UP" : "DOWN");
    SPDLOG_DEBUG("  #TX-Queues: {}", iface.numActiveTxQueues);
    SPDLOG_DEBUG("  Bridge: {}", !iface.bridgePort.bridgeName.empty() ? iface.bridgePort.bridgeName : "N/A");

    const auto &gcl = iface.bridgePort.gateParameterTable;

    SPDLOG_DEBUG("    MaxSduTable ({}):", gcl.queueMaxSduTable.size());
    for (const auto &entry : gcl.queueMaxSduTable) {
        SPDLOG_DEBUG("      TC: {} | MaxSDU: {}", entry.trafficClass, entry.queueMaxSdu);
    }

    SPDLOG_DEBUG("    GCL Admin Entries ({}):", gcl.adminControlList.size());
    for (const auto &entry : gcl.adminControlList) {
        SPDLOG_DEBUG("      Idx: {} | States: 0x{:02x}={:08b} | Interval: {}ns", entry.index, entry.gateStatesValue,
                     entry.gateStatesValue, entry.timeIntervalValue);
    }
    SPDLOG_DEBUG("    GCL Oper Entries ({}):", gcl.operControlList.size());
    for (const auto &entry : gcl.operControlList) {
        SPDLOG_DEBUG("      Idx: {} | States: 0x{:02x}={:08b} | Interval: {}ns", entry.index, entry.gateStatesValue,
                     entry.gateStatesValue, entry.timeIntervalValue);
    }
}

/**
 * @brief Helper to print multiple interfaces, uses @ref printSingleInterface internally in a loop.
 * @param interfaces A list of interfaces to print
 */
void print_interfaces(const std::vector<ietfInterface_t> &interfaces) {
    for (const auto &iface : interfaces) {
        printSingleInterface(iface);
    }
}

/**
 * @brief Helper
 * @param clk The id of a clock
 * @return The string representation of the clock id
 */
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

/**
 * @brief Helper to print a @ref TaprioConfig struct, the struct used to configure a Qdisc.
 * @param cfg The TaprioConfig struct to print.
 */
void printTaprioConfig(const TaprioConfig &cfg) {
    SPDLOG_DEBUG("============================================");
    SPDLOG_DEBUG("          TAPRIO CONFIGURATION              ");
    SPDLOG_DEBUG("============================================");

    // 1. Basic Parameters
    SPDLOG_DEBUG("Traffic Classes (numTc): {}", cfg.numTc);
    ;
    SPDLOG_DEBUG("Hardware Queues (numTxQs): {}", cfg.numTxQs);
    ;
    SPDLOG_DEBUG("Admin Clock Source:            {}", getClockName(cfg.admin.clockid));
    SPDLOG_DEBUG("Admin Base Time (ns):          {}", cfg.admin.baseTime);
    SPDLOG_DEBUG("Admin Cycle Time (ns):         {}", cfg.admin.cycleTime);
    // 2. Priority to Traffic Class Mapping
    SPDLOG_DEBUG("Priority-to-TC Mapping:");
    SPDLOG_DEBUG("  Prio | TC ");
    for (size_t i = 0; i < cfg.prioTc.size(); ++i) {
        SPDLOG_DEBUG("  {:2} | {}", i, cfg.prioTc[i]);
    }

    SPDLOG_DEBUG(" SDU: ");
    for (size_t i = 0; i < cfg.numTc; ++i) {
        SPDLOG_DEBUG("  tc={}, max={}, pre={}", cfg.maxSDUs[i].trafficClass, cfg.maxSDUs[i].queueMaxSdu,
                     cfg.maxSDUs[i].preemtible);
    }

    // 3. The Schedule (Gate Control List)
    SPDLOG_DEBUG("Admin Gate Control List (Schedule):");
    SPDLOG_DEBUG("--------------------------------------------");
    SPDLOG_DEBUG(" Index | Command | Gate Mask | Interval (ns) ");
    SPDLOG_DEBUG("-------|---------|-----------|---------------");

    if (cfg.admin.entries.empty()) {
        SPDLOG_DEBUG("          [ Schedule is empty ]             ");
    } else {
        int idx = 0;
        for (const auto &entry : cfg.admin.entries) {
            SPDLOG_DEBUG(" {:5} |   {:5} |     0x{:02x}    | {:13}", idx++, (int)entry.command, (int)entry.gateMask,
                         entry.interval);
        }
    }
    SPDLOG_DEBUG("--------------------------------------------");
}

/**
 * @brief Called to set the leaf given by @param xpath in the RUNNING datastore to false
 * @param xpath
 */
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

            SPDLOG_DEBUG("[RESET] Successfully reset {} to false.", xpath);
        } catch (const std::exception &e) {
            spdlog::error("[RESET] [ERROR] {}.", e.what());
        }
    }).detach();  // Fire and forget
}

/**
 * @brief Helper to get the value of type @ref T of a leaf with XPath @ref path relative to the DataNode @ref node.
 *
 * @tparam T The type of the desired value.
 * @param node The DataNode relative to which the leaf at Xpath @ref path is located.
 * @param path An XPath pointing to the desired leaf.
 * @return The value of the leaf pointed to by the combination of @ref node and @ref path, casted to type @ref T.
 */
template <typename T>
T tsnctrld::getLeaf(const std::optional<libyang::DataNode> &node, const std::string &path) {
    if (!node) {
        return T{};
    }
    auto leaf = node->findPath(path);
    if (!leaf) {
        return T{};
    }
    if constexpr (std::is_same_v<T, std::string>) {
        return std::string(leaf->asTerm().valueStr());
    } else {
        return std::get<T>(leaf->asTerm().value());
    }
}

/**
 * @brief Given a list of @ref GclEntry_t struct, these are used to fill the @ref libyang::DataNode representing a
 * `sched-gate-control-entries` grouping in the yang model.
 *
 * @param entries List of `GclEntry_t`s to be added to the datanode
 * @param listToFill A DataNode representing either oper-control-list or admin-control-list
 */
void fillControlList(const std::vector<GclEntry_t> &entries, std::optional<libyang::DataNode> &listToFill) {
    if (!listToFill.has_value()) {
        spdlog::warn(
            "[FILL CONTROLLIST] Parameter listToFill of type std::optional has no value, makes no sense, returning...");
        return;
    }
    SPDLOG_DEBUG("[FILL CONTROLLIST] Filling of values from passed control list to passed datanode...");
    for (const auto &entry : entries) {
        SPDLOG_DEBUG("[FILL CONTROLLIST] Current entry: idx={}, gsv={}, interval={}, opr={}", entry.index,
                     entry.gateStatesValue, entry.timeIntervalValue, entry.operationName);

        auto entry_res = listToFill->newPath2(fmt::format("gate-control-entry[index='{}']", entry.index), std::nullopt);
        auto entry_node = entry_res.createdNode;
        if (!entry_node.has_value()) {
            spdlog::warn(
                "[FILL CONTROLLIST] createdNode entry_node of type std::optional has no value, makes no sense, "
                "returning...");
            return;
        }
        entry_node->newPath2("operation-name", entry.operationName);
        entry_node->newPath2("time-interval-value", std::to_string(entry.timeIntervalValue));
        entry_node->newPath2("gate-states-value", std::to_string(entry.gateStatesValue));
    }
}

/**
 * @brief Given a @ref GclConfig_t and @ref GclFillOptions, this function fills the @param toFill in an appropriate
 * manner.
 *
 * @param hw_cfg The configuration to be written to the datastore. The `*DataSet` variables can be used to declare which
 * set of variables contains valid data.
 * @param toFill A DataNode representing a GPT path for an interface:
 * "/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port/ieee802-dot1q-sched:gate-parameter-table"
 * @param options Which values are supposed to be filled, Admin/Oper/Both or only the minimum possible?
 */
void fillGptNode(const GclConfig_t &hw_cfg, std::optional<libyang::DataNode> &toFill,
                 GclFillOptions options = GclFillOptions::OnlyDefault) {
    if (!toFill.has_value()) {
        spdlog::warn(
            "[FILL DATANODE] Parameter listToFill of type std::optional has no value, makes no sense, returning...");
        return;
    }
    if (options & GclFillOptions::FillAdmin) {
        SPDLOG_DEBUG("[FILL DATANODE] Filling of Admin data requested...");
        SPDLOG_DEBUG("[FILL DATANODE] Writing \"supported-*\" nodes...");
        toFill->newPath2("supported-list-max", std::to_string(hw_cfg.supportedListMax));
        toFill->newPath2("supported-cycle-max/numerator", std::to_string(hw_cfg.supportedCycleMaxNumerator));
        toFill->newPath2("supported-cycle-max/denominator", std::to_string(hw_cfg.supportedCycleMaxDenominator));
        toFill->newPath2("supported-interval-max", std::to_string(hw_cfg.supportedIntervalMax));

        toFill->newPath2("gate-enabled", hw_cfg.gateEnabled ? "true" : "false");
        if (hw_cfg.operDataSet && !hw_cfg.adminDataSet) {
            SPDLOG_DEBUG("[FILL DATANODE] Admin data not set, but Oper data is, using that...");
            toFill->newPath2("admin-base-time/seconds", std::to_string(hw_cfg.operBaseTime.seconds));
            toFill->newPath2("admin-base-time/nanoseconds", std::to_string(hw_cfg.operBaseTime.nanoseconds));
            auto admin_res = toFill->newPath2("admin-control-list", std::nullopt);
            auto admin_node = admin_res.createdNode;
            fillControlList(hw_cfg.operControlList, admin_node);

            SPDLOG_DEBUG("[FILL DATANODE] Using operCycleTime for mandatory adminCycleTime...");
            toFill->newPath2("admin-cycle-time/numerator", std::to_string(hw_cfg.operCycleTime.numerator));
            toFill->newPath2("admin-cycle-time/denominator", std::to_string(hw_cfg.operCycleTime.denominator));
        } else if (hw_cfg.adminDataSet) {
            SPDLOG_DEBUG("[FILL DATANODE] Admin data is set, using that...");

            toFill->newPath2("admin-base-time/seconds", std::to_string(hw_cfg.adminBaseTime.seconds));
            toFill->newPath2("admin-base-time/nanoseconds", std::to_string(hw_cfg.adminBaseTime.nanoseconds));
            auto admin_res = toFill->newPath2("admin-control-list", std::nullopt);
            auto admin_node = admin_res.createdNode;
            fillControlList(hw_cfg.adminControlList, admin_node);

            SPDLOG_DEBUG("[FILL DATANODE] Using set adminCycleTime for mandatory element...");
            toFill->newPath2("admin-cycle-time/numerator", std::to_string(hw_cfg.adminCycleTime.numerator));
            toFill->newPath2("admin-cycle-time/denominator", std::to_string(hw_cfg.adminCycleTime.denominator));
        } else {
            SPDLOG_DEBUG("[FILL DATANODE] Neither admin nor oper is set, assume nothing TSN is configured...");
            SPDLOG_DEBUG("[FILL DATANODE] Using default adminCycleTime for mandatory adminCycleTime...");
            toFill->newPath2("admin-cycle-time/numerator", std::to_string(hw_cfg.adminCycleTime.numerator));
            toFill->newPath2("admin-cycle-time/denominator", std::to_string(hw_cfg.adminCycleTime.denominator));
        }
    }

    if (options & GclFillOptions::FillOper) {
        SPDLOG_DEBUG("[FILL DATANODE] Filling of Oper data requested...");
        if (hw_cfg.operDataSet) {
            SPDLOG_DEBUG("[FILL DATANODE] Oper data is set, using that...");
            toFill->newPath2("oper-base-time/seconds", std::to_string(hw_cfg.operBaseTime.seconds));
            toFill->newPath2("oper-base-time/nanoseconds", std::to_string(hw_cfg.operBaseTime.nanoseconds));
            auto oper_res = toFill->newPath2("oper-control-list", std::nullopt);
            auto oper_node = oper_res.createdNode;
            fillControlList(hw_cfg.operControlList, oper_node);
        } else {
            spdlog::warn(
                "[FILL DATANODE] Oper data requested but not set, assume nothing TSN is configured and ignoring...");
        }
        toFill->newPath2("tick-granularity", std::to_string(hw_cfg.tickGranularity));
    }
}

/**
 * @brief Get the interface with a given name from the cache and fill its struct with all relevant data from the
 * datastore.
 *
 * Most data is always present in the datastore, for potentially missing data we use sensible defaults.
 *
 * @param sess
 * @param ifname
 * @param requestId
 * @return The interface struct filled with all necessary data.
 */
ietfInterface_t *tsnctrld::syncInterfaceFromSysrepo(sysrepo::Session &sess, const std::string &ifname,
                                                    uint32_t requestId) {
    m_ifcache.setCurrentRequestId(requestId);
    m_ifcache.ensureFullLinkData(m_sock, m_ethtool_sock);

    ietfInterface_t *iface_ptr = m_ifcache.getInterface(ifname);
    if (iface_ptr == nullptr) {
        spdlog::warn("[SYSREPO->STRUCT] [DEBUG] no interface found with name {}", ifname);
        return nullptr;
    }

    ietfInterface_t &iface = *iface_ptr;
    BridgePort_t &bp = iface.bridgePort;
    GclConfig_t &gcl = bp.gateParameterTable;

    gcl.adminControlList.clear();  // Reuse the vector's capacity!
    gcl.queueMaxSduTable.clear();

    // 2. Fetch the subtree once
    std::string basePath =
        fmt::format("/ietf-interfaces:interfaces/interface[name='{}']/ieee802-dot1q-bridge:bridge-port", ifname);

    // Use sess.getData() to get the proposed tree
    auto bpRoot = sess.getData(basePath);
    if (!bpRoot) {
        spdlog::error("[SYSREPO->STRUCT] bpData is null, failed...");
        return nullptr;
    }
    auto bpData = bpRoot->findPath(basePath);

    SPDLOG_DEBUG("[SYSREPO->STRUCT] [DEBUG] after root->bpData...");
    if (!bpData.has_value()) {
        spdlog::warn(
            "[SYSREPO->STRUCT] [DEBUG] Parameter bpData of type std::optional has no value, makes no sense, "
            "returning...");
        return nullptr;
    }

    SPDLOG_TRACE("  -> [SYSREPO->STRUCT] [DEBUG] Data forest:\n {}",
                 bpData->printStr(libyang::DataFormat::XML, libyang::PrintFlags::Siblings).value());

    auto gclData = bpData->findPath("ieee802-dot1q-sched-bridge:gate-parameter-table");
    if (!gclData.has_value()) {
        spdlog::warn(
            "[SYSREPO->STRUCT] [DEBUG] Parameter gclData of type std::optional has no value, makes no sense, "
            "returning...");
        return nullptr;
    }
    gcl.gateEnabled = getLeaf<bool>(gclData, "gate-enabled");
    if (!gcl.gateEnabled) {
        SPDLOG_DEBUG(
            "[SYSREPO->STRUCT] [DEBUG] gate-enabled is false, skipping fetch of remaining (irrelevant) data...");
        return &iface;
    }

    auto tcData = bpData->findPath("traffic-class/traffic-class-table");
    if (tcData.has_value()) {
        SPDLOG_TRACE("  -> [SYSREPO->STRUCT] [DEBUG] tcData:\n {}",
                     tcData
                         ->printStr(libyang::DataFormat::XML, libyang::PrintFlags::Siblings |
                                                                  libyang::PrintFlags::EmptyContainers |
                                                                  libyang::PrintFlags::WithDefaultsAll)
                         .value_or("Missing"));
    }
    bp.trafficClassData.mapDataSet = true;
    bool useDsConfig = false;
    uint8_t numTCs = 1;
    if (tcData.has_value() && tcData->child().has_value()) {
        SPDLOG_DEBUG("[SYSREPO->STRUCT] [DEBUG] found traffic-class/traffic-class-table...");
        numTCs = getLeaf<uint8_t>(tcData, "number-of-traffic-classes");
        if (numTCs <= iface.numActiveTxQueues) {
            useDsConfig = true;
        } else {
            spdlog::warn("Datastore has more traffic classes configured than interface has active: {}>{}", numTCs,
                         iface.numActiveTxQueues);
        }
    }
    if (useDsConfig) {
        SPDLOG_DEBUG("[SYSREPO->STRUCT] [DEBUG] traffic-class/traffic-class-table is valid, using it...");
        bp.trafficClassData.numTrafficClasses = numTCs;
        bp.trafficClassData.priorityMap[0] = getLeaf<uint8_t>(tcData, "priority0");
        bp.trafficClassData.priorityMap[1] = getLeaf<uint8_t>(tcData, "priority1");
        bp.trafficClassData.priorityMap[2] = getLeaf<uint8_t>(tcData, "priority2");
        bp.trafficClassData.priorityMap[3] = getLeaf<uint8_t>(tcData, "priority3");
        bp.trafficClassData.priorityMap[4] = getLeaf<uint8_t>(tcData, "priority4");
        bp.trafficClassData.priorityMap[5] = getLeaf<uint8_t>(tcData, "priority5");
        bp.trafficClassData.priorityMap[6] = getLeaf<uint8_t>(tcData, "priority6");
        bp.trafficClassData.priorityMap[7] = getLeaf<uint8_t>(tcData, "priority7");
    } else {
        SPDLOG_DEBUG(
            "[SYSREPO->STRUCT] [DEBUG] traffic-class/traffic-class-table not found, empty or configured number of "
            "tx-queues larger than number of active tx-queues, using defaults based on number of tx-queues...");
        bp.trafficClassData.numTrafficClasses = iface.numActiveTxQueues < 8 ? iface.numActiveTxQueues : 8;
        uint8_t colIndex = bp.trafficClassData.numTrafficClasses - 1;
        for (int priority = 0; priority < 8; ++priority) {
            bp.trafficClassData.priorityMap[priority] = IEEE8021Q_DEFAULT_TC_MAP[priority][colIndex];
        }
    }

    gcl.adminCycleTime.numerator = getLeaf<uint32_t>(gclData, "admin-cycle-time/numerator");
    gcl.adminCycleTime.denominator = getLeaf<uint32_t>(gclData, "admin-cycle-time/denominator");
    SPDLOG_DEBUG("[SYSREPO->STRUCT] admin-cycle-time: {}/{}", gcl.adminCycleTime.numerator,
                 gcl.adminCycleTime.denominator);

    // 4. Update the Control List (The expensive part)
    auto entries = gclData->findXPath("admin-control-list/gate-control-entry");

    for (const auto &entryNode : entries) {
        // We emplace directly into the existing vector
        auto &e = gcl.adminControlList.emplace_back();

        // Use typed access for every leaf
        e.index = getLeaf<uint32_t>(entryNode, "index");
        e.gateStatesValue = getLeaf<uint8_t>(entryNode, "gate-states-value");
        e.timeIntervalValue = getLeaf<uint32_t>(entryNode, "time-interval-value");
        e.operationName = getLeaf<std::string>(entryNode, "operation-name");
    }

    auto sduEntries = gclData->findXPath("queue-max-sdu-table");
    if (sduEntries.size() == bp.trafficClassData.numTrafficClasses) {
        for (const auto &entryNode : sduEntries) {
            auto &e = gcl.queueMaxSduTable.emplace_back();
            e.trafficClass = getLeaf<uint32_t>(entryNode, "traffic-class");
            e.queueMaxSdu = getLeaf<uint8_t>(entryNode, "queue-max-sdu");
        }
    } else {
        for (uint8_t tcIndex = 0; tcIndex < bp.trafficClassData.numTrafficClasses; ++tcIndex) {
            gcl.queueMaxSduTable.push_back((queueMaxSduEntry_t){.trafficClass = tcIndex, .queueMaxSdu = 0});
        }
    }

    gcl.adminBaseTime.seconds = getLeaf<uint64_t>(gclData, "admin-base-time/seconds");
    gcl.adminBaseTime.nanoseconds = getLeaf<uint32_t>(gclData, "admin-base-time/nanoseconds");

    return &iface;
}

// tsnctrld

/**
 * @brief Ensure a clean slate and initialize
 *
 * First delete all top-level trees from the datastore for which this daemon should be considered the ultimate authority
 * on the current host. Afterwards the datastore is repopulated and callbacks are initialized.
 */
void tsnctrld::initialize() {
    // m_lm.getAllInterfaces(m_sock);
    // m_lm.getInterfacesInResponse(m_sock, m_interfaces);
    // exit(1337);

    spdlog::info("[INIT] Initializing tsnctrld...");

    static const std::vector<std::string> SERVICES_TO_START = {"netopeer2-server.service", "lldpd.service"};
    int r = ensureRunningDaemons(SERVICES_TO_START);
    if (r != 0) {
        spdlog::critical(
            "A required daemon could not be started. Ensure no non-service instances of the daemon are already "
            "running");
        exit(EXIT_FAILURE);
    }

    // In order for our program to be the source of truth, start with an empty datastore.
    SPDLOG_DEBUG("[INIT] [DEBUG] Cleaning interfaces from datastore ");
    m_sess.deleteItem("/ietf-interfaces:interfaces");
    SPDLOG_DEBUG("[INIT] [DEBUG] Cleaning bridges from datastore ");
    m_sess.deleteItem("/ieee802-dot1q-bridge:bridges");
    SPDLOG_DEBUG("[INIT] [DEBUG] Cleaning lldp from datastore ");
    m_sess.deleteItem("/ieee802-dot1ab-lldp:lldp");
    m_sess.applyChanges();

    syncHardwareToRunning();
    setupSubscriptions();
}

/**
 * @brief Uses libsystemd to ensure the necessary daemons are running on the system.
 */
int tsnctrld::ensureRunningDaemons(const std::vector<std::string> &services) {
    spdlog::info("Ensuring running daemon services...");
    sd_bus *bus = nullptr;
    std::map<std::string, std::string> pendingJobs;
    bool globalSuccess = true;
    int r;

    // 1. Connect to System Bus
    r = sd_bus_default_system(&bus);
    if (r < 0) {
        spdlog::error("Failed to connect to system bus: {}", strerror(-r));
    }

    // 2. Add a "Match" rule
    // We tell the bus: "Send us a copy of all 'JobRemoved' signals from systemd"
    // We do this BEFORE starting the unit so we don't miss the signal if it's very fast.
    r = sd_bus_add_match(bus, nullptr,
                         "type='signal',"
                         "sender='org.freedesktop.systemd1',"
                         "interface='org.freedesktop.systemd1.Manager',"
                         "member='JobRemoved'",
                         nullptr, nullptr);
    if (r < 0) {
        spdlog::error("Failed to add match rule: {}", strerror(-r));
    }

    // 3. Start the Unit
    for (const auto &service : services) {
        SPDLOG_DEBUG("Requesting start for {}...", service);
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message *reply = nullptr;  // Reply from StartUnit call
        const char *jobPath = nullptr;
        r = sd_bus_call_method(bus,
                               "org.freedesktop.systemd1",          // Service
                               "/org/freedesktop/systemd1",         // Object Path
                               "org.freedesktop.systemd1.Manager",  // Interface
                               "StartUnit",                         // Method
                               &error,                              // Error return
                               &reply,                              // Reply (contains Job Path)
                               "ss",                                // Signature (string, string)
                               service.c_str(), "replace");

        if (r < 0) {
            spdlog::error("Failed to queue unit: {}", error.message);
            sd_bus_error_free(&error);
            globalSuccess = false;
        } else {
            // 4. Get our specific Job Path from the reply
            // The signature is "o" (Object Path)
            r = sd_bus_message_read(reply, "o", &jobPath);
            if (r < 0) {
                spdlog::error("Failed parsing StartUnit reply: {}", strerror(-r));
            }
            pendingJobs[jobPath] = service;
            SPDLOG_DEBUG("Job queued, waiting for completion: {}", jobPath);
        }
        sd_bus_message_unref(reply);
    }

    if (pendingJobs.empty()) {
        SPDLOG_DEBUG("No jobs started (or all failed immediately).");
        sd_bus_unref(bus);
        return globalSuccess ? 0 : 1;
    }

    while (!pendingJobs.empty()) {
        // Process requests/signals in the queue
        // Returns >0 if a message was processed, 0 if queue empty
        r = sd_bus_wait(bus, (uint64_t)-1);
        if (r < 0) {
            spdlog::error("Failed waiting on bus: {}", strerror(-r));
        }
        sd_bus_message *m = nullptr;
        while ((r = sd_bus_process(bus, &m)) > 0) {
            if (sd_bus_message_is_signal(m, "org.freedesktop.systemd1.Manager", "JobRemoved") != 0) {
                uint32_t id;
                const char *path;
                const char *unit;
                const char *result;

                sd_bus_message_read(m, "uoss", &id, &path, &unit, &result);

                // Is this a job we are tracking?
                auto it = pendingJobs.find(path);
                if (it != pendingJobs.end()) {
                    std::string svc_name = it->second;

                    if (strcmp(result, "done") == 0) {
                        SPDLOG_DEBUG("SUCCESS: Service {} started.", svc_name);
                    } else {
                        spdlog::error("FAILURE: Service {} failed to start with reason {}.", svc_name, result);
                        globalSuccess = false;
                    }

                    // Remove from tracking
                    pendingJobs.erase(it);
                }
            }
            sd_bus_message_unref(m);
        }
        if (r < 0) {
            spdlog::error("Failed processing bus: {}", strerror(-r));
        }
    }

    sd_bus_unref(bus);
    return globalSuccess ? 0 : 1;
}

/**
 * @brief Fills the "running" datastore with the current values as received from the kernel, builds the "ground truth"
 * of this program
 *
 */
void tsnctrld::syncHardwareToRunning() {
    spdlog::info("[SYNC] Reading info from kernel and writing to RUNNING datastore");
    SPDLOG_DEBUG("[SYNC] Populating internal list m_interfaces from kernel");
    m_sess.switchDatastore(sysrepo::Datastore::Running);
    auto ctx = m_sess.getContext();
    std::optional<libyang::DataNode> forest;

    m_ifcache.setCurrentRequestId(0);
    m_ifcache.ensureFullLinkData(m_sock, m_ethtool_sock);
    m_ifcache.ensureFullQdiscData(m_sock);

    SPDLOG_DEBUG("[SYNC] Iterating over interfaces...");

    const auto &allIfaces = m_ifcache.getAllInterfaces();
    for (const auto &[idx, iface] : allIfaces) {
        printSingleInterface(iface);
    }
    SPDLOG_DEBUG("[SYNC] Print done...");

    for (auto &[idx, current] : m_ifcache.getAllInterfaces()) {
        std::string &name = current.name;

        SPDLOG_DEBUG("[SYNC] Current interface: {}...", name);
        // std::string mac_ietf = mac_to_string(s->sll_addr, s->sll_halen, ':');
        // std::string mac_ieee = mac_to_string(s->sll_addr, s->sll_halen, '-');

        // --- PASS 1: Bridges (Only if it's a bridge and NOT loopback) ---
        if (current.type == IfType::BRIDGE) {
            SPDLOG_DEBUG("[SYNC] [BR] Interface is bridge...");
            std::string br_path = fmt::format("/ieee802-dot1q-bridge:bridges/bridge[name='{}']", name);
            auto br_res = forest ? forest->newPath2(br_path, std::nullopt) : ctx.newPath2(br_path, std::nullopt);
            if (!forest && br_res.createdParent) {
                forest = br_res.createdParent;
            }
            auto br_node = br_res.createdNode;

            std::string mac_ieee = mac_to_string(current.physAddress.data(), 6, '-');
            br_node->newPath2("address", mac_ieee);
            br_node->newPath2("bridge-type", "ieee802-dot1q-bridge:customer-vlan-bridge");

            auto comp_res = br_node->newPath2(fmt::format("component[name='{}']", name), std::nullopt);
            auto comp_node = comp_res.createdNode;
            comp_node->newPath2("id", "1");
            comp_node->newPath2("type", "ieee802-dot1q-bridge:c-vlan-component");
        }

        // --- PASS 2: Interface Core ---
        std::string if_path = std::string("/ietf-interfaces:interfaces/interface[name='").append(name).append("']");

        SPDLOG_DEBUG("[SYNC] [IF] Creating \"root\" interface node...");
        auto if_res = forest ? forest->newPath2(if_path, std::nullopt) : ctx.newPath2(if_path, std::nullopt);
        if (!forest && if_res.createdParent) {
            forest = if_res.createdParent;
        }
        auto if_node = if_res.createdNode;

        if_node->newPath2("type", ifTypeToIanaString(current.type));
        if_node->newPath2("enabled", current.adminEnabled ? "true" : "false");

        // --- PASS 3: Bridge-Port & TAS (Skip for Loopback) ---
        if (current.type == IfType::BRIDGE || current.type == IfType::ETHERNET) {
            auto bp_res = if_node->newPath2("ieee802-dot1q-bridge:bridge-port", std::nullopt);
            auto bp_node = bp_res.createdNode;

            if (current.bridgePort.masterIndex > 0) {
                SPDLOG_DEBUG("[SYNC] [BR] Current interface \"{}\" is attached to bridge \"{}\" with id {}...",
                             current.name, current.bridgePort.bridgeName, current.bridgePort.bridgeName);
                bp_node->newPath2("bridge-name", current.bridgePort.bridgeName);
                bp_node->newPath2("component-name", current.bridgePort.bridgeName);
            }
            auto tc_res = bp_node->newPath2("traffic-class/traffic-class-table", std::nullopt);
            auto tc_node = tc_res.createdNode;

            if (current.bridgePort.trafficClassData.mapDataSet) {
                SPDLOG_DEBUG("[SYNC] [BR] Current interface \"{}\" has a priority map...", current.name);

            } else {
                current.bridgePort.trafficClassData.mapDataSet = true;
                current.bridgePort.trafficClassData.numTrafficClasses =
                    current.numActiveTxQueues < 8 ? current.numActiveTxQueues : 8;
                SPDLOG_DEBUG(
                    "[SYNC] [BR] Current interface \"{}\" has no priority map, using defaults based on supported "
                    "number of active TX queues ({}/{}/{})...",
                    current.name, current.bridgePort.trafficClassData.numTrafficClasses, current.numActiveTxQueues,
                    current.numTxQueues);
                uint8_t colIndex = current.bridgePort.trafficClassData.numTrafficClasses - 1;
                for (int priority = 0; priority < 8; ++priority) {
                    current.bridgePort.trafficClassData.priorityMap[priority] =
                        IEEE8021Q_DEFAULT_TC_MAP[priority][colIndex];
                }
            }
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

            // Physical ports and Bridges get TAS capabilities to satisfy validation
            auto gpt_res = bp_node->newPath2("ieee802-dot1q-sched-bridge:gate-parameter-table", std::nullopt);
            auto gpt_node = gpt_res.createdNode;

            GclConfig_t &hw_cfg = current.bridgePort.gateParameterTable;

            SPDLOG_DEBUG("[SYNC] [BP] Filling DataNode of gate-parameter-table with values from GclConfig_t struct");
            fillGptNode(hw_cfg, gpt_node, GclFillOptions::FillAdmin);
        }

        if (current.type == IfType::ETHERNET) {
            // TODO: Get real values
            SPDLOG_DEBUG("[SYNC] [LLDP] Enabling discovery on: {}", current.name);
            std::string lldp_path = fmt::format(
                "/ieee802-dot1ab-lldp:lldp/port[name='{}'][dest-mac-address='01-80-c2-00-00-0e']", current.name);

            auto lldp_res = forest->newPath2(lldp_path, std::nullopt);
            auto lldp_node = lldp_res.createdNode;
            lldp_node->newPath2("admin-status", "tx-and-rx");
        }
    }

    if (forest) {
        SPDLOG_DEBUG("[SYNC] Applying Batch to Datastore...");
        SPDLOG_DEBUG("[SYNC] Switching to first sibling...");
        forest = forest->firstSibling();
        SPDLOG_TRACE("  -> [SYNC DEBUG] Data forest:\n {}",
                     forest->printStr(libyang::DataFormat::XML, libyang::PrintFlags::Siblings).value());
        SPDLOG_DEBUG("[SYNC] Editing batch...");
        m_sess.editBatch(*forest, sysrepo::DefaultOperation::Merge);
        SPDLOG_DEBUG("[SYNC] Applying changes...");
        m_sess.applyChanges();
        SPDLOG_DEBUG("[SYNC] Datastore synchronized.");
    }

    m_lldpDaemon = std::make_unique<LldpDaemon>(m_operSess);
    m_lldpDaemon->syncInitialNeighbors();
    m_lldpDaemon->startWatching();
}

/**
 * @brief Most basic callback for operational data, only prints the parameters.
 * @param sess
 * @param subId
 * @param moduleName
 * @param subXPath
 * @param requestXPath
 * @param requestId
 * @param parent
 * @return
 */
sysrepo::ErrorCode tsnctrld::defaultOperCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                                 const std::optional<std::string> &subXPath,
                                                 const std::optional<std::string> &requestXPath, uint32_t requestId,
                                                 std::optional<libyang::DataNode> &parent) {
    SPDLOG_DEBUG("[CB_OPER] [DEFAULT] Received oper callback for module {}...", moduleName);
    SPDLOG_DEBUG("[CB_OPER] [DEFAULT] subXPath {}...", subXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_OPER] [DEFAULT] requestXPath {}...", requestXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_OPER] [DEFAULT] requestId {}...", requestId);
    auto ctx = sess.getContext();
    if (!parent) {
        SPDLOG_DEBUG("[CB_OPER] [DEFAULT] parent is falsy...");
    } else {
        SPDLOG_DEBUG("[CB_OPER] [DEFAULT] parent is truthy...");
    }
    return sysrepo::ErrorCode::Ok;
}

/**
 * @brief Callback for getting the operational data of interfaces.
 *
 * First, this function ensures that it has the current data regarding the interfaces and any taprio qdiscs. It then
 * iterates over all found interfaces and populates the provided @param parent with the data from these structs.
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
    SPDLOG_DEBUG("[CB_OPER] [IF] Received oper callback for module {}...", moduleName);
    SPDLOG_DEBUG("[CB_OPER] [IF] subXPath {}...", subXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_OPER] [IF] requestXPath {}...", requestXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_OPER] [IF] requestId {}...", requestId);
    SPDLOG_DEBUG("[CB_OPER] [IF] module of parent node {}...", parent->schema().module().name());
    SPDLOG_DEBUG("[CB_OPER] [IF] name of parent node {}...", parent->schema().name());

    SPDLOG_DEBUG("[CB_OPER] [IF] Refreshing interface status...");

    m_ifcache.setCurrentRequestId(requestId);
    m_ifcache.ensureFullLinkData(m_sock, m_ethtool_sock);
    m_ifcache.ensureFullQdiscData(m_sock);

    auto ctx = sess.getContext();
    SPDLOG_DEBUG("[CB_OPER] [IF] Refreshing caches refreshed, start iterating...");
    for (auto &[idx, current] : m_ifcache.getAllInterfaces()) {
        std::string &name = current.name;
        SPDLOG_DEBUG("[CB_OPER] [IF] Current: {}", name);

        std::string if_path = fmt::format("/ietf-interfaces:interfaces/interface[name='{}']", name);

        auto if_res = parent ? parent->newPath2(if_path, std::nullopt) : ctx.newPath2(if_path, std::nullopt);
        if (!parent && if_res.createdParent) {
            parent = if_res.createdParent;
        }
        auto if_node = if_res.createdNode;
        if (!if_node.has_value()) {
            spdlog::warn("[CB_OPER] [IF] if_node of type std::optional has no value, makes no sense, returning...");
            return sysrepo::ErrorCode::OperationFailed;
        }

        if_node->newPath("oper-status", operStatusToYangString(current.operStatus));
        if (current.type == IfType::BRIDGE || current.type == IfType::ETHERNET) {
            if_node->newPath("phys-address", mac_to_string(current.physAddress.data(), 6, ':'));
            auto bp_res = if_node->newPath2("ieee802-dot1q-bridge:bridge-port", std::nullopt);
            auto bp_node = bp_res.createdNode;
            if (!bp_node.has_value()) {
                spdlog::warn("[CB_OPER] [IF] bp_node of type std::optional has no value, makes no sense, returning...");
                return sysrepo::ErrorCode::OperationFailed;
            }
            auto gpt_res = bp_node->newPath2("ieee802-dot1q-sched-bridge:gate-parameter-table", std::nullopt);
            auto gpt_node = gpt_res.createdNode;
            if (!gpt_node.has_value()) {
                spdlog::warn(
                    "[CB_OPER] [IF] gpt_node of type std::optional has no value, makes no sense, returning...");
                return sysrepo::ErrorCode::OperationFailed;
            }

            GclConfig_t &hw_cfg = current.bridgePort.gateParameterTable;
            fillGptNode(hw_cfg, gpt_node, GclFillOptions::FillOper);
        }
    }
    return sysrepo::ErrorCode::Ok;
}

/**
 * @brief Callback for operational data under the /ieee802-dot1q-bridge:bridges top level element.
 *
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
    SPDLOG_DEBUG("[CB_OPER] [BR] Received oper callback for module {}...", moduleName);
    SPDLOG_DEBUG("[CB_OPER] [BR] subXPath {}...", subXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_OPER] [BR] requestXPath {}...", requestXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_OPER] [BR] requestId {}...", requestId);
    SPDLOG_DEBUG("[CB_OPER] [BR] module of parent node {}...", parent->schema().module().name());
    SPDLOG_DEBUG("[CB_OPER] [BR] name of parent node {}...", parent->schema().name());

    SPDLOG_DEBUG("[CB_OPER] [BR] Scanning interfaces for bridge members...");

    m_ifcache.setCurrentRequestId(requestId);
    m_ifcache.ensureFullLinkData(m_sock, m_ethtool_sock);
    const auto &allIfaces = m_ifcache.getAllInterfaces();

    auto ctx = sess.getContext();

    std::unordered_map<int, std::vector<const std::string *> > masterToSlaves;
    for (const auto &[idx, iface] : allIfaces) {
        if (iface.bridgePort.masterIndex > 0) {
            // No string copy here, just pushing an 8-byte pointer
            masterToSlaves[iface.bridgePort.masterIndex].push_back(&(iface.name));
        }
    }

    // 2. Iterate through bridges
    for (const auto &[bridgeIdx, slaveNamePtrs] : masterToSlaves) {
        auto *bridgeIface = m_ifcache.getInterface(bridgeIdx);
        if (bridgeIface == nullptr) {
            continue;
        }

        const std::string &bridgeName = bridgeIface->name;

        // Path building - unfortunately some string manipulation is unavoidable
        // to create the XPath, but we keep it to one per bridge.
        std::string comp_path =
            fmt::format("/ieee802-dot1q-bridge:bridges/bridge[name='{}']/component[name='{}']", bridgeName, bridgeName);
        auto comp_res = parent ? parent->newPath2(comp_path, std::nullopt) : ctx.newPath2(comp_path, std::nullopt);
        if (!parent && comp_res.createdParent) {
            parent = comp_res.createdParent;
        }
        auto comp_node = comp_res.createdNode;
        if (!comp_node.has_value()) {
            spdlog::warn("[CB_OPER] [BR] comp_node of type std::optional has no value, makes no sense, returning...");
            return sysrepo::ErrorCode::OperationFailed;
        }

        // 3. Add the leaf-list entries using the pointers
        for (const std::string *slaveName : slaveNamePtrs) {
            // We dereference the pointer here.
            // libyang will take the string value and store it in its internal tree.
            comp_node->newPath2("bridge-port", *slaveName);
        }
    }
    return sysrepo::ErrorCode::Ok;
}

/**
 * @brief Callback for getting operational data under /interfaces/interface/bridge-port.
 * @param sess
 * @param subId
 * @param moduleName
 * @param subXPath
 * @param requestXPath
 * @param requestId
 * @param parent
 * @return
 */
sysrepo::ErrorCode tsnctrld::operBridgePortCallback(sysrepo::Session sess, uint32_t subId,
                                                    const std::string &moduleName,
                                                    const std::optional<std::string> &subXPath,
                                                    const std::optional<std::string> &requestXPath, uint32_t requestId,
                                                    std::optional<libyang::DataNode> &parent) {
    SPDLOG_DEBUG("[CB_OPER] [BP] Received oper callback for module {}...", moduleName);
    SPDLOG_DEBUG("[CB_OPER] [BP] subXPath {}...", subXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_OPER] [BP] requestXPath {}...", requestXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_OPER] [BP] requestId {}...", requestId);
    SPDLOG_DEBUG("[CB_OPER] [BP] module of parent node {}...", parent->schema().module().name());
    SPDLOG_DEBUG("[CB_OPER] [BP] name of parent node {}...", parent->schema().name());

    auto ctx = sess.getContext();
    if (!parent) {
        SPDLOG_DEBUG("[CB_OPER] [BP] parent is falsy...");
    } else {
        SPDLOG_DEBUG("[CB_OPER] [BP] parent is truthy...");
    }
    return sysrepo::ErrorCode::Ok;
}

/**
 * @brief A callback for getting operational data related to lldp. May be unused and could possibly be removed.
 * @param sess
 * @param subId
 * @param moduleName
 * @param subXPath
 * @param requestXPath
 * @param requestId
 * @param parent
 * @return
 */
sysrepo::ErrorCode tsnctrld::operLldpCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                              const std::optional<std::string> &subXPath,
                                              const std::optional<std::string> &requestXPath, uint32_t requestId,
                                              std::optional<libyang::DataNode> &parent) {
    SPDLOG_DEBUG("[CB_OPER] [LLDP] Received oper callback for module {}...", moduleName);
    SPDLOG_DEBUG("[CB_OPER] [LLDP] subXPath {}...", subXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_OPER] [LLDP] requestXPath {}...", requestXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_OPER] [LLDP] requestId {}...", requestId);

    return sysrepo::ErrorCode::Ok;
}

/**
 * @brief The default callback for changes in the datastore. Only prints basic information about the callback and
 * accepts any changes.
 *
 * @param sess
 * @param subId
 * @param moduleName
 * @param subXPath
 * @param event
 * @param requestId
 * @return
 */
sysrepo::ErrorCode tsnctrld::defaultChangeCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                                   const std::optional<std::string> &subXPath, sysrepo::Event event,
                                                   uint32_t requestId) {
    SPDLOG_DEBUG("[CB_CHANGE] [DEFAULT] Received change callback for module {}...", moduleName);
    SPDLOG_DEBUG("[CB_CHANGE] [DEFAULT] subXPath {}...", subXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_CHANGE] [DEFAULT] event {}...", event);
    SPDLOG_DEBUG("[CB_CHANGE] [DEFAULT] requestId {}...", requestId);
    return sysrepo::ErrorCode::Ok;
}

/**
 * @brief Callback when a change on any node under the `ietf-interfaces` module is attempted.
 *
 * This callback checks for each attempted change if the changed node actually belongs to the `ietf-interfaces` module
 * and does not block any changes that do not belong. Any changes to nodes on a whitelist (currently only the
 * non-existent "TEMP") are also not blocked. If any change is not allowed by the two previous conditions, the entire
 * attempt is blocked.
 * This is used to restrict changes only to explicitly supported nodes.
 *
 * @param sess
 * @param subId
 * @param moduleName
 * @param subXPath
 * @param event
 * @param requestId
 * @return
 */
sysrepo::ErrorCode tsnctrld::changeInterfaceCallback(sysrepo::Session sess, uint32_t subId,
                                                     const std::string &moduleName,
                                                     const std::optional<std::string> &subXPath, sysrepo::Event event,
                                                     uint32_t requestId) {
    static const std::string handledModuleName = "ietf-interfaces";
    SPDLOG_DEBUG("[CB_CHANGE] [IF] Received change callback for module {}...", moduleName);
    SPDLOG_DEBUG("[CB_CHANGE] [IF] subXPath {}...", subXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_CHANGE] [IF] event {}...", event);
    SPDLOG_DEBUG("[CB_CHANGE] [IF] requestId {}...", requestId);
    SPDLOG_DEBUG("[CB_CHANGE] [IF] Actually trying to handle module {}...", handledModuleName);
    if (event != sysrepo::Event::Change) {
        return sysrepo::ErrorCode::Ok;
    }

    for (const auto &change : sess.getChanges("//.")) {
        std::string nodeModuleName = change.node.schema().module().name();
        std::string nodeName = change.node.schema().name();

        if (nodeModuleName != moduleName) {
            SPDLOG_DEBUG("[CB] [IF-CONFIG] [DEBUG] Change does not belong to this module, skipping");
            continue;
        }
        if (nodeName == "TEMP") {
            SPDLOG_DEBUG("[CB] [IF-CONFIG] [ALLOW] Attempting to change whitelisted node, continuing for now");
            continue;
        }
        spdlog::error(
            "[CB] [IF-CONFIG] [REJECT] Edits to 'ietf-interfaces' (specifically the top-level `/interfaces` "
            "element) are currently not implemented. Attempted change to path: {}",
            change.node.path());
        return sysrepo::ErrorCode::Unsupported;
    }
    SPDLOG_DEBUG("[CB] [IF-CONFIG] [ALLOW] All attempted changes were allowed, accepting");
    return sysrepo::ErrorCode::Ok;
}

/**
 * @brief Callback when a change on any node under the `ieee802-dot1q-bridge` module is attempted.
 *
 * This callback checks for each attempted change if the changed node actually belongs to the `ieee802-dot1q-bridge`
 * module and does not block any changes that do not belong. Any changes to nodes on a whitelist (currently only the
 * non-existent "TEMP") are also not blocked. If any change is not allowed by the two previous conditions, the entire
 * attempt is blocked.
 * This is used to restrict changes only to explicitly supported nodes.
 *
 * @param sess
 * @param subId
 * @param moduleName
 * @param subXPath
 * @param event
 * @param requestId
 * @return
 */
sysrepo::ErrorCode tsnctrld::changeBridgeCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                                  const std::optional<std::string> &subXPath, sysrepo::Event event,
                                                  uint32_t requestId) {
    static const std::string handledModuleName = "ieee802-dot1q-bridge";
    SPDLOG_DEBUG("[CB_CHANGE] [BR] Received change callback for module {}...", moduleName);
    SPDLOG_DEBUG("[CB_CHANGE] [BR] subXPath {}...", subXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_CHANGE] [BR] event {}...", event);
    SPDLOG_DEBUG("[CB_CHANGE] [BR] requestId {}...", requestId);
    SPDLOG_DEBUG("[CB_CHANGE] [BR] Actually trying to handle module {}...", handledModuleName);
    if (event != sysrepo::Event::Change) {
        return sysrepo::ErrorCode::Ok;
    }

    for (const auto &change : sess.getChanges("//.")) {
        std::string nodeModuleName = change.node.schema().module().name();
        std::string nodeName = change.node.schema().name();

        if (nodeModuleName != moduleName) {
            SPDLOG_DEBUG("[CB] [BR-CONFIG] [DEBUG] Change does not belong to this module, skipping");
            continue;
        }
        if (nodeName == "TEMP") {
            SPDLOG_DEBUG("[CB] [BR-CONFIG] [ALLOW] Attempting to change whitelisted node, continuing for now");
            continue;
        }
        spdlog::error(
            "[CB] [BR-CONFIG] [REJECT] Edits to 'ieee802-dot1q-bridge' (specifically the top-level `/bridges` element) "
            "are currently not implemented. Attempted change to path: {}",
            change.node.path());
        return sysrepo::ErrorCode::Unsupported;
    }
    SPDLOG_DEBUG("[CB] [BR-CONFIG] [ALLOW] All attempted changes were allowed, accepting");
    return sysrepo::ErrorCode::Ok;
}
/**
 * @brief Callback when a change on any node under the `ieee802-dot1q-bridge` module, specifically the
 * /interfaces/interface/bridge-port subtree, is attempted.
 *
 * This callback checks for each attempted change if the changed node actually belongs to the `ieee802-dot1q-bridge`
 * module and does not block any changes that do not belong. Any changes to nodes on a whitelist (currently only the
 * non-existent "TEMP") are also not blocked. If any change is not allowed by the two previous conditions, the entire
 * attempt is blocked.
 * This is used to restrict changes only to explicitly supported nodes.
 *
 * @param sess
 * @param subId
 * @param moduleName
 * @param subXPath
 * @param event
 * @param requestId
 * @return
 */
sysrepo::ErrorCode tsnctrld::changeBridgePortCallback(sysrepo::Session sess, uint32_t subId,
                                                      const std::string &moduleName,
                                                      const std::optional<std::string> &subXPath, sysrepo::Event event,
                                                      uint32_t requestId) {
    static const std::string handledModuleName = "ieee802-dot1q-bridge";
    SPDLOG_DEBUG("[CB_CHANGE] [BP] Received change callback for module {}...", moduleName);
    SPDLOG_DEBUG("[CB_CHANGE] [BP] subXPath {}...", subXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_CHANGE] [BP] event {}...", event);
    SPDLOG_DEBUG("[CB_CHANGE] [BP] requestId {}...", requestId);
    SPDLOG_DEBUG("[CB_CHANGE] [BP] Actually trying to handle module {}...", handledModuleName);
    if (event != sysrepo::Event::Change) {
        return sysrepo::ErrorCode::Ok;
    }

    for (const auto &change : sess.getChanges("//.")) {
        std::string nodeModuleName = change.node.schema().module().name();
        std::string nodeName = change.node.schema().name();

        if (nodeModuleName != handledModuleName) {
            SPDLOG_DEBUG("[CB] [BP-CONFIG] [DEBUG] Change does not belong to this handled module, skipping");
            continue;
        }
        if (nodeName == "TEMP") {
            SPDLOG_DEBUG("[CB] [BP-CONFIG] [ALLOW] Attempting to change whitelisted node, continuing for now");
            continue;
        }
        spdlog::error(
            "[CB] [BP-CONFIG] [REJECT] Edits to 'ieee802-dot1q-bridge' (specifically the `bridge-port` element) "
            "are currently not implemented. Attempted change to path: {}",
            change.node.path());
        return sysrepo::ErrorCode::Unsupported;
    }
    SPDLOG_DEBUG("[CB] [BP-CONFIG] [ALLOW] All attempted changes were allowed, accepting");
    return sysrepo::ErrorCode::Ok;
}
/**
 * @brief The most important callback, listens for changes in the `gate-parameter-table` and sets/modifies or removes
 * the qdisc on an interface when the `config-change` node is set to true.
 *
 * This callback checks if `config-change` is changed to true for any interface, and for all interfaces where it is
 * changed to true, an attempt, to change the corresponding qdisc as intended, is started.
 * For that, the current state of the interfaces is cached and all relevant data is read from the datastore and used to
 * populate the @ref ietfInterface_t struct as received prepopulated from the cache. Depending on if the `gate-enabled`
 * node in the datastore is true or false, the qdisc is set/modified or deleted respectively.
 * Since we interpret the `config-change` node as a trigger, we need to reset it once all callbacks successfully
 * returned for the CHANGE event. This happens in the DONE event triggered by sysrepo after all changes were
 * successful.
 *
 * @param sess
 * @param subId
 * @param moduleName
 * @param subXPath
 * @param event
 * @param requestId
 * @return
 */
sysrepo::ErrorCode tsnctrld::changeGptCallback(sysrepo::Session sess, uint32_t subId, const std::string &moduleName,
                                               const std::optional<std::string> &subXPath, sysrepo::Event event,
                                               uint32_t requestId) {
    static const std::string handledModuleName = "ieee802-dot1q-sched-bridge";
    SPDLOG_DEBUG("[CB_CHANGE] [GPT] Received change callback for module {}...", moduleName);
    SPDLOG_DEBUG("[CB_CHANGE] [GPT] subXPath {}...", subXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_CHANGE] [GPT] event {}...", event);
    SPDLOG_DEBUG("[CB_CHANGE] [GPT] requestId {}...", requestId);
    SPDLOG_DEBUG("[CB_CHANGE] [BP] Actually trying to handle module {}...", handledModuleName);

    if (sess.getOriginatorName() == "tsnctrld-internal") {
        SPDLOG_DEBUG("[CB_CHANGE] [GPT] Whatever just happened, we did it, so we can trust it...");
        return sysrepo::ErrorCode::Ok;
    }

    // 1. PHASE: VALIDATION (Event::Change)
    if (event == sysrepo::Event::Change) {
        // We filter for changes specifically on the config-change leaf
        std::string filter =
            "/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port/"
            "ieee802-dot1q-sched-bridge:gate-parameter-table/config-change";

        // getChanges returns a ChangeCollection which we can iterate over
        auto changes = sess.getChanges(filter);

        for (const auto &change : changes) {
            std::string nodeModuleName = change.node.schema().module().name();

            if (nodeModuleName != handledModuleName) {
                SPDLOG_DEBUG("[CB_CHANGE] [GPT] [DEBUG] Change does not belong to this handled module, skipping");
                continue;
            }

            // Check if node was deleted (ignore)
            if (change.operation == sysrepo::ChangeOperation::Deleted) {
                continue;
            }
            SPDLOG_TRACE("module of change: {}", change.node.schema().module().name());
            // Only trigger if changed to "true"
            if (change.node.asTerm().valueStr() == "true") {
                std::string ifname = extractListKey(change.node.path(), "interface", "name");

                SPDLOG_DEBUG("[CB_CHANGE] [GPT] Applying new config for {}", ifname);

                try {
                    // Update our internal cache and apply to hardware
                    // This function should be the one we optimized earlier (Zero-Copy)
                    SPDLOG_DEBUG("[CB_CHANGE] [GPT] Getting data from sysrepo ");
                    ietfInterface_t *iface = this->syncInterfaceFromSysrepo(sess, ifname, requestId);

                    if (iface != nullptr) {
                        SPDLOG_DEBUG("[CB_CHANGE] [GPT] Returned data:");
                        printSingleInterface(*iface);

                        if (iface->bridgePort.gateParameterTable.gateEnabled) {
                            SPDLOG_DEBUG("[CB_CHANGE] [GPT] Gate is enabled, setting qdisc:");
                            SPDLOG_DEBUG("[CB_CHANGE] [GPT] Converting to TaprioConfig struct:");
                            TaprioConfig taprioCfg = NetconfNetlinkMapper::mapToTaprio(*iface);

                            printTaprioConfig(taprioCfg);
                            SPDLOG_DEBUG("[CB_CHANGE] [GPT] Sending qdisc");
                            QdiscManager::setQdisc(m_sock, ifname, taprioCfg);
                            SPDLOG_DEBUG("[CB_CHANGE] [GPT] Qdisc \"sent\"");
                            m_pathsToReset.push_back(std::string(change.node.path()));
                        } else {
                            SPDLOG_DEBUG("[CB_CHANGE] [GPT] Gate is disabled, removing qdisc:");
                            QdiscManager::removeQdisc(m_sock, ifname);
                            SPDLOG_DEBUG("[CB_CHANGE] [GPT] Remove-request sent:");
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
        for (const auto &path : m_pathsToReset) {
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
    SPDLOG_DEBUG("[CB_CHANGE] [LLDP] Received change callback for module {}...", moduleName);
    SPDLOG_DEBUG("[CB_CHANGE] [LLDP] subXPath {}...", subXPath.value_or("MISSING"));
    SPDLOG_DEBUG("[CB_CHANGE] [LLDP] event {}...", event);
    SPDLOG_DEBUG("[CB_CHANGE] [LLDP] requestId {}...", requestId);

    if (event != sysrepo::Event::Change) {
        return sysrepo::ErrorCode::Ok;
    }

    for (const auto &change : sess.getChanges("//.")) {
        if (change.node.schema().module().name() != moduleName) {
            continue;
        }

        spdlog::error(
            "[CB] [LLDP-CONFIG] [REJECT] Edits to 'ieee802-dot1ab-lldp' (LLDP) are currently not implemented. "
            "Attempted change: {}",
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
    spdlog::info("[INIT] [SUBS] Initializing callbacks...");
    SPDLOG_DEBUG("[INIT] [SUBS] Registering granular callbacks...");
    auto defaultChangeCb = std::bind_front(&tsnctrld::defaultChangeCallback, this);

    auto changeInterfaceCb = std::bind_front(&tsnctrld::changeInterfaceCallback, this);
    m_subs.push_back(m_sess.onModuleChange("ietf-interfaces", changeInterfaceCb, std::nullopt, 100));

    auto changeBridgeCb = std::bind_front(&tsnctrld::changeBridgeCallback, this);
    m_subs.push_back(m_sess.onModuleChange("ieee802-dot1q-bridge", changeBridgeCb, std::nullopt, 90));

    auto changeBridgePortCb = std::bind_front(&tsnctrld::changeBridgePortCallback, this);
    m_subs.push_back(m_sess.onModuleChange("ietf-interfaces", changeBridgePortCb,
                                           "/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port",
                                           85));

    auto changeGptCb = std::bind_front(&tsnctrld::changeGptCallback, this);
    m_subs.push_back(m_sess.onModuleChange("ietf-interfaces", changeGptCb,
                                           "/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port/"
                                           "ieee802-dot1q-sched-bridge:gate-parameter-table",
                                           80));

    auto changeLldpCb = std::bind_front(&tsnctrld::changeLldpCallback, this);
    m_subs.push_back(m_sess.onModuleChange("ieee802-dot1ab-lldp", changeLldpCb, std::nullopt, 70));

    SPDLOG_DEBUG("[INIT] [SUBS] Registered change callbacks...");

    auto defaultOperCb = std::bind_front(&tsnctrld::defaultOperCallback, this);

    auto operInterfaceCb = std::bind_front(&tsnctrld::operInterfaceCallback, this);
    m_subs.push_back(m_sess.onOperGet("ietf-interfaces", operInterfaceCb, "/ietf-interfaces:interfaces/interface",
                                      sysrepo::SubscribeOptions::OperMerge));

    auto operBridgeCb = std::bind_front(&tsnctrld::operBridgeCallback, this);
    m_subs.push_back(m_sess.onOperGet("ieee802-dot1q-bridge", operBridgeCb, "/ieee802-dot1q-bridge:bridges",
                                      sysrepo::SubscribeOptions::OperMerge));

    auto operBridgePortCb = std::bind_front(&tsnctrld::operBridgePortCallback, this);
    m_subs.push_back(m_sess.onOperGet("ietf-interfaces", operBridgePortCb,
                                      "/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port",
                                      sysrepo::SubscribeOptions::OperMerge));

    auto operLldpCb = std::bind_front(&tsnctrld::operLldpCallback, this);
    m_subs.push_back(m_sess.onOperGet("ieee802-dot1ab-lldp", operLldpCb, "/ieee802-dot1ab-lldp:lldp",
                                      sysrepo::SubscribeOptions::OperMerge));

    SPDLOG_DEBUG("[INIT] [SUBS] Registered oper callbacks...");
}
/**
 * @brief Instantiates the tsnctrld daemon.
 *
 * Instances of @ref sysrepo::Session cannot be created "empty", so we instantiate them directly from a new session of
 * the connection. Additionally, we need the @ref m_ethtool_sock for calls to @ref LinkManager::getActiveQueues, so we
 * open this socket directly in the beginning.
 */
tsnctrld::tsnctrld() : m_sess(m_conn.sessionStart()), m_operSess(m_conn.sessionStart()) {
    m_operSess.switchDatastore(sysrepo::Datastore::Operational);
    m_ethtool_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_ethtool_sock < 0) {
        throw std::runtime_error("Could not open ethtool socket");
    }
}

/**
 * Makes sure to close the socket when destroying the daemon.
 */
tsnctrld::~tsnctrld() {
    // Close the socket when the tsnctrld is destroyed
    if (m_ethtool_sock >= 0) {
        close(m_ethtool_sock);
    }
}

int main() {
    spdlog::set_level(spdlog::level::trace);
    tsnctrld daemon = tsnctrld();
    daemon.initialize();
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
