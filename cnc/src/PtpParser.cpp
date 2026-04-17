#include "../include/PtpParser.h"

#include <libyang/libyang.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <iostream>
#include <string>

#include "PerformanceLogger.h"

namespace cnc {
// Helper function to extract string value from XPath
static std::string getXpathValue(const struct lyd_node* contextNode, const char* xpath) {
    struct lyd_node* foundNode = nullptr;
    if (lyd_find_path(contextNode, xpath, 0, &foundNode) == LY_SUCCESS && foundNode != nullptr) {
        const char* val = lyd_get_value(foundNode);
        return val ? std::string(val) : "";
    }
    return "";
}

// Helper function to extract uint32_t value from XPath with default fallback
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

// Helper function to extract uint64_t value from XPath with default fallback
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

// Helper function to extract int64_t value from XPath with default fallback
static int64_t getXpathValueInt64(const struct lyd_node* contextNode, const char* xpath, int64_t defaultVal = 0) {
    std::string valStr = getXpathValue(contextNode, xpath);
    if (!valStr.empty()) {
        try {
            return std::stoll(valStr);
        } catch (...) {
        }
    }
    return defaultVal;
}

// Helper function to extract boolean value from XPath with default fallback
static bool getXpathValueBool(const struct lyd_node* contextNode, const char* xpath, bool defaultVal = false) {
    std::string valStr = getXpathValue(contextNode, xpath);
    if (valStr == "true" || valStr == "1") return true;
    if (valStr == "false" || valStr == "0") return false;
    return defaultVal;
}

// Helper function to convert instance type string to enum
static PtpInstanceType_t stringToInstanceType(const std::string& str) {
    if (str == "oc") {
        return PtpInstanceType_t::PTP_INSTANCE_OC;
    } else if (str == "bc") {
        return PtpInstanceType_t::PTP_INSTANCE_BC;
    } else if (str == "p2p-tc") {
        return PtpInstanceType_t::PTP_INSTANCE_P2P_TC;
    } else if (str == "e2e-tc") {
        return PtpInstanceType_t::PTP_INSTANCE_E2E_TC;
    } else {
        return PtpInstanceType_t::PTP_INSTANCE_INVALID;
    }
}

// Helper function to convert port state string to enum
static PtpPortState_t stringToPortState(const std::string& str) {
    if (str == "initializing") {
        return PtpPortState_t::INITIALIZING;
    } else if (str == "faulty") {
        return PtpPortState_t::FAULTY;
    } else if (str == "disabled") {
        return PtpPortState_t::DISABLED;
    } else if (str == "listening") {
        return PtpPortState_t::LISTENING;
    } else if (str == "pre-time-transmitter") {
        return PtpPortState_t::PRE_TIME_TRANSMITTER;
    } else if (str == "time-transmitter") {
        return PtpPortState_t::TIME_TRANSMITTER;
    } else if (str == "passive") {
        return PtpPortState_t::PASSIVE;
    } else if (str == "uncalibrated") {
        return PtpPortState_t::UNCALIBRATED;
    } else if (str == "time-receiver") {
        return PtpPortState_t::TIME_RECEIVER;
    } else {
        return PtpPortState_t::INITIALIZING;  // default fallback
    }
}

// Helper function to parse performance parameters from a given context node and prefix with optional short min/max
// naming
static PtpPerformanceParameters_t parsePerfParams(const struct lyd_node* contextNode, const std::string& prefix,
                                                  bool useShortMinMax = false) {
    PtpPerformanceParameters_t params = {0, 0, 0, 0};

    // Durchschnitt und Standardabweichung sind immer gleich benannt
    params.avg = getXpathValueInt64(contextNode, ("average-" + prefix).c_str(), 0);
    params.stddev = getXpathValueInt64(contextNode, ("stddev-" + prefix).c_str(), 0);

    // Je nach Flag suchen wir gezielt nach dem richtigen Namen
    if (useShortMinMax) {
        params.min = getXpathValueInt64(contextNode, ("min-" + prefix).c_str(), 0);
        params.max = getXpathValueInt64(contextNode, ("max-" + prefix).c_str(), 0);
    } else {
        params.min = getXpathValueInt64(contextNode, ("minimum-" + prefix).c_str(), 0);
        params.max = getXpathValueInt64(contextNode, ("maximum-" + prefix).c_str(), 0);
    }

    return params;
}

bool PtpParser::parseOperationalPtpData(const struct lyd_node* rootNode, CncNode_t& node) {
    PERFORMANCE_LOGGING("[PtpParser::parseOperationalPtpData]", "Start PTP data parsing");
    if (rootNode == nullptr) {
        spdlog::error("[PTP Parser] Root node is null.");
        return false;
    }

    const struct lyd_node* actualData = rootNode;
    if (actualData->schema && std::string(actualData->schema->name) == "get") {
        actualData = lyd_child(actualData);
    }
    if (actualData->schema && std::string(actualData->schema->name) == "data") {
        actualData = ((struct lyd_node_any*)actualData)->value.tree;
    }

    if (actualData == nullptr) {
        spdlog::error("[PTP Parser] Data tree is empty.");
        return false;
    }

    struct ly_set* instanceSet = nullptr;
    if (lyd_find_xpath(actualData, "//ieee1588-ptp-tt:ptp/instances/instance", &instanceSet) != LY_SUCCESS ||
        instanceSet->count == 0) {
        spdlog::error("[PTP Parser] No PTP instance data found.");
        if (instanceSet != nullptr) {
            ly_set_free(instanceSet, nullptr);
        }
        return true;  // No instances found, but not necessarily an error - just means no PTP data available
    }

    struct lyd_node* instanceNode = instanceSet->dnodes[0];  // Assuming only one instance per device for now
    PtpNode_t& ptpData = node.ptpAllData;

    // Default data set
    struct lyd_node* defaultDsNode = nullptr;
    if (lyd_find_path(instanceNode, "default-ds", 0, &defaultDsNode) == LY_SUCCESS && defaultDsNode != nullptr) {
        ptpData.defaultDs.numPorts = static_cast<uint16_t>(getXpathValueUint32(defaultDsNode, "number-ports"));
        ptpData.defaultDs.priority1 = static_cast<uint8_t>(getXpathValueUint32(defaultDsNode, "priority1"));
        ptpData.defaultDs.clockIdentity = getXpathValue(defaultDsNode, "clock-identity");
        ptpData.defaultDs.instanceType = stringToInstanceType(getXpathValue(defaultDsNode, "instance-type"));

        struct lyd_node* timeNode = nullptr;
        if (lyd_find_path(defaultDsNode, "current-time", 0, &timeNode) == LY_SUCCESS && timeNode != nullptr) {
            ptpData.defaultDs.currentTime.seconds = getXpathValueUint64(timeNode, "seconds-field");
            ptpData.defaultDs.currentTime.nanoseconds = getXpathValueUint32(timeNode, "nanoseconds-field");
        }
    }

    // Current data set
    struct lyd_node* currentDsNode = nullptr;
    if (lyd_find_path(instanceNode, "current-ds", 0, &currentDsNode) == LY_SUCCESS && currentDsNode != nullptr) {
        ptpData.currentDs.stepsRemoved = static_cast<uint16_t>(getXpathValueUint32(currentDsNode, "steps-removed"));
        ptpData.currentDs.offsetFromTimeTransmitter = getXpathValueInt64(currentDsNode, "offset-from-time-transmitter");
        ptpData.currentDs.meanDelay = getXpathValueInt64(currentDsNode, "mean-delay");
    }

    // Parent data set
    struct lyd_node* parentDsNode = nullptr;
    if (lyd_find_path(instanceNode, "parent-ds", 0, &parentDsNode) == LY_SUCCESS && parentDsNode != nullptr) {
        ptpData.parentDs.parentClockIdentity = getXpathValue(parentDsNode, "parent-port-identity/clock-identity");
        ptpData.parentDs.parentPortNumber =
            static_cast<uint16_t>(getXpathValueUint32(parentDsNode, "parent-port-identity/port-number"));
        ptpData.parentDs.grandParentClockIdentity = getXpathValue(parentDsNode, "grandmaster-identity");
    }

    // Node performance records (15m and 24h)
    ptpData.performanceRecords15m.clear();
    ptpData.performanceRecords24h.clear();
    struct ly_set* perfSet = nullptr;
    if (lyd_find_xpath(instanceNode, "performance-monitoring-ds/record-list", &perfSet) == LY_SUCCESS &&
        perfSet != nullptr) {
        for (uint32_t i = 0; i < perfSet->count; ++i) {
            struct lyd_node* recordNode = perfSet->dnodes[i];
            uint16_t index = static_cast<uint16_t>(getXpathValueUint32(recordNode, "index"));

            PtpPerformanceRecord_t record;
            record.periodComplete = getXpathValueBool(recordNode, "period-complete");
            record.pmTime = getXpathValueUint32(recordNode, "pm-time");
            record.measurementValid = getXpathValueBool(recordNode, "measurement-valid");
            record.offsetFromTimeTransmitter = parsePerfParams(recordNode, "offset-from-time-transmitter", false);
            record.meanPathDelay = parsePerfParams(recordNode, "mean-path-delay", false);

            if (index <= 96) {
                ptpData.performanceRecords15m.push_back(record);
            } else if (index >= 97) {
                ptpData.performanceRecords24h.push_back(record);
            }
        }
        ly_set_free(perfSet, nullptr);
    }

    // Parse Port data
    ptpData.ports.clear();
    struct ly_set* portSet = nullptr;
    if (lyd_find_xpath(instanceNode, "ports/port", &portSet) == LY_SUCCESS && portSet != nullptr) {
        for (uint32_t i = 0; i < portSet->count; ++i) {
            struct lyd_node* portNode = portSet->dnodes[i];
            PtpPort_t currentPort;

            currentPort.portIndex = static_cast<uint16_t>(getXpathValueUint32(portNode, "port-index"));
            currentPort.underlyingInterface = getXpathValue(portNode, "underlying-interface");

            // Port data set
            struct lyd_node* portDsNode = nullptr;
            if (lyd_find_path(portNode, "port-ds", 0, &portDsNode) == LY_SUCCESS && portDsNode != nullptr) {
                currentPort.portDs.portClockIdentity = getXpathValue(portDsNode, "port-identity/clock-identity");
                currentPort.portDs.portPortNumber =
                    static_cast<uint16_t>(getXpathValueUint32(portDsNode, "port-identity/port-number"));
                currentPort.portDs.portState = stringToPortState(getXpathValue(portDsNode, "port-state"));
                currentPort.portDs.meanLinkDelay = getXpathValueInt64(portDsNode, "mean-link-delay");
            }

            // port performance records (15m and 24h)
            struct ly_set* portPerfSet = nullptr;
            if (lyd_find_xpath(portNode, "performance-monitoring-port-ds/record-list-peer-delay", &portPerfSet) ==
                    LY_SUCCESS &&
                portPerfSet != nullptr) {
                for (uint32_t j = 0; j < portPerfSet->count; ++j) {
                    struct lyd_node* perfNode = portPerfSet->dnodes[j];
                    uint16_t index = static_cast<uint16_t>(getXpathValueUint32(perfNode, "index"));

                    PtpPortPerformanceRecord_t perfRecord;
                    perfRecord.pmTime = getXpathValueUint32(perfNode, "pm-time");
                    perfRecord.meanLinkDelay = parsePerfParams(perfNode, "mean-link-delay", true);

                    if (index <= 96) {
                        currentPort.portPerformanceRecords15m.push_back(perfRecord);
                    } else if (index >= 97) {
                        currentPort.portPerformanceRecords24h.push_back(perfRecord);
                    }
                }
                ly_set_free(portPerfSet, nullptr);
            }
            ptpData.ports.push_back(currentPort);
        }
        ly_set_free(portSet, nullptr);
    }
    ly_set_free(instanceSet, nullptr);
    PERFORMANCE_LOGGING("[PtpParser::parseOperationalPtpData]", "Finished PTP data parsing");
    return true;
}
}  // namespace cnc