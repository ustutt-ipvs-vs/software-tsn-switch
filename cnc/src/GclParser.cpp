#include "../include/GclParser.h"

#include <libyang/libyang.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <iostream>
#include <string>

#include "PerformanceLogger.h"
#include "spdlog/spdlog.h"

namespace cnc {

static std::string getXpathValue(const struct lyd_node* contextNode, const char* xpath) {
    struct lyd_node* foundNode = nullptr;
    if (lyd_find_path(contextNode, xpath, 0, &foundNode) == LY_SUCCESS && foundNode != nullptr) {
        const char* val = lyd_get_value(foundNode);
        return val ? std::string(val) : "";
    }
    return "";
}

static uint32_t getXpathValueUint32(const struct lyd_node* contextNode, const char* xpath, uint32_t defaultVal = 0) {
    std::string valStr = getXpathValue(contextNode, xpath);
    if (!valStr.empty()) {
        try {
            return std::stoul(valStr);
        } catch (...) {
        }
    }
    return defaultVal;
}

static uint64_t getXpathValueUint64(const struct lyd_node* contextNode, const char* xpath, uint64_t defaultVal = 0) {
    std::string valStr = getXpathValue(contextNode, xpath);
    if (!valStr.empty()) {
        try {
            return std::stoull(valStr);
        } catch (...) {
        }
    }
    return defaultVal;
}

static bool getXpathValueBool(const struct lyd_node* contextNode, const char* xpath, bool defaultVal = false) {
    std::string valStr = getXpathValue(contextNode, xpath);
    if (valStr == "true" || valStr == "1") return true;
    if (valStr == "false" || valStr == "0") return false;
    return defaultVal;
}

bool GclParser::parseOperationalGclData(const struct lyd_node* rootNode, CncNode_t& node) {
    PERFORMANCE_LOGGING("[GclParser::parseOperationalGclData]", "Start operational GCL data parsing");
    if (rootNode == nullptr) {
        spdlog::error("[GclParser] Error: Root XML node is null.");
        return false;
    }

    const struct lyd_node* actualData = rootNode;

    if (actualData != nullptr && actualData->schema != nullptr && std::string(actualData->schema->name) == "get") {
        actualData = lyd_child(actualData);
    }

    if (actualData != nullptr && actualData->schema != nullptr && std::string(actualData->schema->name) == "data") {
        struct lyd_node_any* anyNode = (struct lyd_node_any*)actualData;
        actualData = anyNode->value.tree;
    }

    if (actualData == nullptr) {
        spdlog::error("datatree is empty!");
        return false;
    }

    struct ly_set* ifaceSet = nullptr;
    if (lyd_find_xpath(actualData, "//ietf-interfaces:interface", &ifaceSet) != LY_SUCCESS || ifaceSet->count == 0) {
        spdlog::error("[GclParser] Error: No interface nodes found.");
        if (ifaceSet != nullptr) {
            ly_set_free(ifaceSet, nullptr);
        }
        return false;
    }

    // Iterate over all interface nodes in the lyd tree
    for (uint32_t i = 0; i < ifaceSet->count; ++i) {
        struct lyd_node* ifaceNode = ifaceSet->dnodes[i];

        struct lyd_node* nameNode = nullptr;
        lyd_find_path(ifaceNode, "name", 0, &nameNode);
        if (nameNode == nullptr) {
            continue;  // Skip if no name found
        }

        std::string ifaceName = lyd_get_value(nameNode);
        if (ifaceName.empty()) {
            continue;  // Skip if name is empty
        }

        // Find the corresponding interface in our struct
        ietfInterface_t* targetIface = nullptr;
        for (auto& iface : node.interfaces) {
            if (iface.name == ifaceName) {
                targetIface = &iface;
                break;
            }
        }

        if (targetIface != nullptr) {
            struct lyd_node* gclNode = nullptr;
            lyd_find_path(ifaceNode, "ieee802-dot1q-bridge:bridge-port/ieee802-dot1q-sched-bridge:gate-parameter-table",
                          0, &gclNode);

            if (gclNode != nullptr) {
                parseInterfaceGcl(gclNode, *targetIface);
            } else {
                spdlog::warn("[GclParser] Warning: No GCL data found for interface '{}'.", ifaceName);
            }
        }
    }

    ly_set_free(ifaceSet, nullptr);
    PERFORMANCE_LOGGING("[GclParser::parseOperationalGclData]", "Finished operational GCL data parsing");
    return true;
}

void GclParser::parseInterfaceGcl(const struct lyd_node* gclNode, ietfInterface_t& iface) {
    if (gclNode == nullptr) {
        return;
    }

    GclConfig_t& gclConfig = iface.bridgePort.gateParameterTable;
    struct lyd_node* valNode = nullptr;

    gclConfig.operDataSet = true;
    gclConfig.gateEnabled = getXpathValueBool(gclNode, "gate-enabled");
    gclConfig.configPending = getXpathValueBool(gclNode, "config-pending");
    gclConfig.configChangeError = getXpathValueUint64(gclNode, "config-change-error");
    gclConfig.tickGranularity =
        getXpathValueUint32(gclNode, "tick-granularity", 10000);  // Default to 10,000ns (10us) if not provided

    // Config change time
    struct lyd_node* changeTimeNode = nullptr;
    if (lyd_find_path(gclNode, "config-change-time", 0, &changeTimeNode) == LY_SUCCESS && changeTimeNode != nullptr) {
        gclConfig.configChangeTimeSeconds = getXpathValueUint64(changeTimeNode, "seconds");
        gclConfig.configChangeTimeNanoseconds = getXpathValueUint32(changeTimeNode, "nanoseconds");
    }

    // Current time
    struct lyd_node* currentTimeNode = nullptr;
    if (lyd_find_path(gclNode, "current-time", 0, &currentTimeNode) == LY_SUCCESS && currentTimeNode != nullptr) {
        gclConfig.currentTimeSeconds = getXpathValueUint64(currentTimeNode, "seconds");
        gclConfig.currentTimeNanoseconds = getXpathValueUint32(currentTimeNode, "nanoseconds");
    }

    // Switch capabilities
    gclConfig.supportedListMax = getXpathValueUint32(gclNode, "supported-list-max", 31);
    gclConfig.supportedIntervalMax = getXpathValueUint32(gclNode, "supported-interval-max", 1000000000);

    struct lyd_node* cycleMaxNode = nullptr;
    if (lyd_find_path(gclNode, "supported-cycle-max", 0, &cycleMaxNode) == LY_SUCCESS && cycleMaxNode != nullptr) {
        gclConfig.supportedCycleMaxNumerator = getXpathValueUint32(cycleMaxNode, "numerator", 1000000000);
        gclConfig.supportedCycleMaxDenominator = getXpathValueUint32(cycleMaxNode, "denominator", 1000000000);
    }

    // oper-gate-states
    gclConfig.operGateStates = static_cast<uint8_t>(
        getXpathValueUint32(gclNode, "oper-gate-states", 255));  // default bad --> change in future

    // oper-cycle-time-extension
    gclConfig.operCycleTimeExtensionNs = getXpathValueUint32(gclNode, "oper-cycle-time-extension", 0);

    gclConfig.adminDataSet = true;

    // oper-cycle-time
    struct lyd_node* operCycleTimeNode = nullptr;
    if (lyd_find_path(gclNode, "oper-cycle-time", 0, &operCycleTimeNode) == LY_SUCCESS &&
        operCycleTimeNode != nullptr) {
        gclConfig.operCycleTime.numerator = getXpathValueUint32(operCycleTimeNode, "numerator");
        gclConfig.operCycleTime.denominator = getXpathValueUint32(operCycleTimeNode, "denominator");
    }

    // Normalize when numerator returns zero
    if (gclConfig.operCycleTime.numerator == 0) {
        gclConfig.operCycleTime.numerator = 0;
        gclConfig.operCycleTime.denominator = 0;
    }

    // oper-base-time
    struct lyd_node* operBaseTimeNode = nullptr;
    if (lyd_find_path(gclNode, "oper-base-time", 0, &operBaseTimeNode) == LY_SUCCESS && operBaseTimeNode != nullptr) {
        gclConfig.operBaseTime.seconds = getXpathValueUint64(operBaseTimeNode, "seconds");
        gclConfig.operBaseTime.nanoseconds = getXpathValueUint32(operBaseTimeNode, "nanoseconds");
    }

    // oper-control-list
    struct ly_set* operEntrySet = nullptr;
    if (lyd_find_xpath(gclNode, "oper-control-list/gate-control-entry", &operEntrySet) == LY_SUCCESS &&
        operEntrySet != nullptr) {
        gclConfig.operControlList.clear();
        gclConfig.operControlList.resize(operEntrySet->count);

        for (uint32_t i = 0; i < operEntrySet->count; ++i) {
            struct lyd_node* entryNode = operEntrySet->dnodes[i];
            GclEntry_t& entry = gclConfig.operControlList[i];

            entry.index = getXpathValueUint32(entryNode, "index", i);  // fallback to i if no index provided
            entry.gateStatesValue = static_cast<uint8_t>(getXpathValueUint32(entryNode, "gate-states-value"));
            entry.timeIntervalValue = getXpathValueUint32(entryNode, "time-interval-value");

            std::string opName = getXpathValue(entryNode, "operation-name");
            entry.operationName = opName.empty() ? "sched:set-gate-states" : opName;
        }
    }
    if (operEntrySet != nullptr) {
        ly_set_free(operEntrySet, nullptr);
    }

    // admin-gate-states
    gclConfig.adminGateStates = static_cast<uint8_t>(getXpathValueUint32(gclNode, "admin-gate-states", 255));

    // admin-cycle-time-extension
    gclConfig.adminCycleTimeExtensionNs = getXpathValueUint32(gclNode, "admin-cycle-time-extension", 0);

    // admin-cycle-time
    struct lyd_node* cycleTimeNode = nullptr;
    if (lyd_find_path(gclNode, "admin-cycle-time", 0, &cycleTimeNode) == LY_SUCCESS && cycleTimeNode != nullptr) {
        gclConfig.adminCycleTime.numerator = getXpathValueUint32(cycleTimeNode, "numerator");
        gclConfig.adminCycleTime.denominator = getXpathValueUint32(cycleTimeNode, "denominator");
    }

    // admin-base-time
    struct lyd_node* adminBaseTimeNode = nullptr;
    if (lyd_find_path(gclNode, "admin-base-time", 0, &adminBaseTimeNode) == LY_SUCCESS &&
        adminBaseTimeNode != nullptr) {
        gclConfig.adminBaseTime.seconds = getXpathValueUint64(adminBaseTimeNode, "seconds");
        gclConfig.adminBaseTime.nanoseconds = getXpathValueUint32(adminBaseTimeNode, "nanoseconds");
    }

    // admin-control-list
    struct ly_set* adminEntrySet = nullptr;
    if (lyd_find_xpath(gclNode, "admin-control-list/gate-control-entry", &adminEntrySet) == LY_SUCCESS &&
        adminEntrySet != nullptr) {
        gclConfig.adminControlList.clear();
        gclConfig.adminControlList.resize(adminEntrySet->count);

        for (uint32_t i = 0; i < adminEntrySet->count; ++i) {
            struct lyd_node* entryNode = adminEntrySet->dnodes[i];
            GclEntry_t& entry = gclConfig.adminControlList[i];

            entry.index = getXpathValueUint32(entryNode, "index", i);  // fallback to i if no index provided
            entry.gateStatesValue = static_cast<uint8_t>(getXpathValueUint32(entryNode, "gate-states-value"));
            entry.timeIntervalValue = getXpathValueUint32(entryNode, "time-interval-value");

            std::string opName = getXpathValue(entryNode, "operation-name");
            entry.operationName = opName.empty() ? "sched:set-gate-states" : opName;
        }
    }
    if (adminEntrySet != nullptr) {
        ly_set_free(adminEntrySet, nullptr);
    }

    // queue-max-sdu-table
    struct ly_set* sduSet = nullptr;
    if (lyd_find_xpath(gclNode, "queue-max-sdu-table/queue-max-sdu-entry", &sduSet) == LY_SUCCESS &&
        sduSet != nullptr) {
        gclConfig.queueMaxSduTable.clear();
        for (uint32_t i = 0; i < sduSet->count; ++i) {
            struct lyd_node* sduNode = sduSet->dnodes[i];
            queueMaxSduEntry_t sduEntry;

            sduEntry.trafficClass = static_cast<uint8_t>(getXpathValueUint32(sduNode, "traffic-class"));
            sduEntry.queueMaxSdu = getXpathValueUint32(sduNode, "queue-max-sdu");
            sduEntry.transmissionOverrun = getXpathValueUint64(sduNode, "transmission-overrun");

            gclConfig.queueMaxSduTable.push_back(sduEntry);
        }
        ly_set_free(sduSet, nullptr);
    }
}
}  // namespace cnc