#include "LldpParser.h"
#include "PerformanceLogger.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <iostream>
#include <libyang/libyang.h>
#include "spdlog/spdlog.h"

namespace cnc {
    static std::string getXPathValue(const struct lyd_node* contextNode, const char* xpath) {
        struct lyd_node* foundNode = nullptr;
        if (lyd_find_path(contextNode, xpath, 0, &foundNode) == LY_SUCCESS && foundNode != nullptr) {
            const char* val = lyd_get_value(foundNode);
            if (val != nullptr) {
                return std::string(val);
            }
        }
        return "";
    }

    static uint32_t getXpathValueUint32(const struct lyd_node* contextNode, const char* xpath, uint32_t defaultValue = 0) {
        std::string valStr = getXPathValue(contextNode, xpath);
        if (!valStr.empty()) {
            try {
                return static_cast<uint32_t>(std::stoul(valStr));
            } catch (const std::exception& e) {
                spdlog::error("[LLDP Parser] Error converting value '{}' to uint32: {}", valStr, e.what());
            }
        }
        return defaultValue; // Default value if not found or conversion fails
    }


    bool LldpParser::parseLldpData(const struct lyd_node* rootNode, CncNode_t& node) {
        PERFORMANCE_LOGGING("[LLDP Parser::parseLldpData]", "Start LLDP data parsing");
        if (rootNode == nullptr) {
            spdlog::error("[LLDP Parser] Root node is null.");
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
            spdlog::error("[LLDP Parser] datatree is empty!");
            return false;
        }

        // Find all LLDP port nodes using XPath
        struct ly_set* lldpSet = nullptr;

        if (lyd_find_xpath(actualData, "//ieee802-dot1ab-lldp:lldp", &lldpSet) == LY_SUCCESS && lldpSet->count > 0) {
            struct lyd_node* lldpNode = lldpSet->dnodes[0]; // Assuming only one LLDP node per device

            node.lldpAllData.messageTxInterval = getXpathValueUint32(lldpNode, "message-tx-interval", 0);
            node.lldpAllData.messageTxHoldMultiplier = getXpathValueUint32(lldpNode, "message-tx-hold-multiplier", 0);
            node.lldpAllData.messageFastTx = getXpathValueUint32(lldpNode, "message-fast-tx", 0);
            // Local system data
            struct lyd_node* localSysNode = nullptr;
            if (lyd_find_path(lldpNode, "local-system-data", 0, &localSysNode) == LY_SUCCESS && localSysNode != nullptr) {
                node.lldpAllData.localSystemData.chassisIdSubtype = getXPathValue(localSysNode, "chassis-id-subtype");
                node.lldpAllData.localSystemData.chassisId = getXPathValue(localSysNode, "chassis-id");
                node.lldpAllData.localSystemData.systemName = getXPathValue(localSysNode, "system-name");
                node.lldpAllData.localSystemData.systemDescription = getXPathValue(localSysNode, "system-description");
                node.lldpAllData.localSystemData.systemCapabilitiesSupported = getXPathValue(localSysNode, "system-capabilities-supported");
                node.lldpAllData.localSystemData.systemCapabilitiesEnabled = getXPathValue(localSysNode, "system-capabilities-enabled");
            }
        }

        if (lldpSet != nullptr) {
            ly_set_free(lldpSet, nullptr);
        }

        node.lldpAllData.ports.clear();

        struct ly_set* portSet = nullptr;
        if (lyd_find_xpath(actualData, "//ieee802-dot1ab-lldp:port", &portSet) != LY_SUCCESS || portSet->count == 0) {
            spdlog::error("[LLDP Parser] No LLDP port data found for node '{}'.", node.hostName);
            ly_set_free(portSet, nullptr);
            return false;
        }

        for (uint32_t i = 0; i< portSet->count; ++i) {
            struct lyd_node* portNode = portSet->dnodes[i];

            LldpPort_t currentPort;

            currentPort.name = getXPathValue(portNode, "name");
            if (currentPort.name.empty()) {
                spdlog::warn("[LLDP Parser] Warning: LLDP port with empty name found, skipping.");
                continue; // Skip ports without a valid name
            }

            currentPort.destMacAddress = getXPathValue(portNode, "dest-mac-address");
            currentPort.adminStatus = getXPathValue(portNode, "admin-status");

            struct ly_set* remoteDataSet = nullptr;
            lyd_find_xpath(portNode, "remote-systems-data", &remoteDataSet);

            if (remoteDataSet != nullptr && remoteDataSet->count > 0) {
                for (uint32_t j = 0; j < remoteDataSet->count; ++j) {
                    struct lyd_node* remoteDataNode = remoteDataSet->dnodes[j];

                    LldpNeighbor_t neighbor;
                    neighbor.hasNeighbor = true;

                    neighbor.chassisId = getXPathValue(remoteDataNode, "chassis-id");
                    neighbor.portId = getXPathValue(remoteDataNode, "port-id");
                    neighbor.systemName = getXPathValue(remoteDataNode, "system-name");
                    neighbor.ttl = 0; // NO SUCH VALUE IN YANG, todo what is this?

                    struct ly_set* mgmtAddrSet = nullptr;
                    if (lyd_find_xpath(remoteDataNode, "management-address", &mgmtAddrSet) == LY_SUCCESS && mgmtAddrSet->count > 0) {
                        neighbor.managementIp = getXPathValue(mgmtAddrSet->dnodes[0], "address");
                        ly_set_free(mgmtAddrSet, nullptr);
                    }

                    currentPort.neighbors.push_back(neighbor);
                }
                ly_set_free(remoteDataSet, nullptr);
            }
            node.lldpAllData.ports.push_back(currentPort);
        }

        ly_set_free(portSet, nullptr);

        PERFORMANCE_LOGGING("[LLDP Parser::parseLldpData]", "Finished LLDP data parsing");
        return true;
    }
}