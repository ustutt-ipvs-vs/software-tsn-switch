#include "InterfaceParser.h"

#include <spdlog/spdlog.h>

#include <iostream>

#include "PerformanceLogger.h"

namespace cnc {

bool InterfaceParser::parseInterface(const struct lyd_node* rootNode, CncNode_t& node) {
    PERFORMANCE_LOGGING("[InterfaceParser::parseInterface]", "Start interface parsing");
    if (rootNode == nullptr) {
        spdlog::error("Root node is null");
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
        spdlog::error("datatree is empty after unwrapping!");
        return false;
    }

    struct ly_set* ifaceSet = nullptr;

    if (lyd_find_xpath(actualData, "//ietf-interfaces:interface", &ifaceSet) != LY_SUCCESS || ifaceSet->count == 0) {
        spdlog::error("No interface data found for node '{}'.", node.hostName);
        if (ifaceSet != nullptr) {
            ly_set_free(ifaceSet, nullptr);
        }
        return false;
    }

    for (uint32_t i = 0; i < ifaceSet->count; ++i) {
        struct lyd_node* ifaceNode = ifaceSet->dnodes[i];

        struct lyd_node* nameNode = nullptr;
        lyd_find_path(ifaceNode, "name", 0, &nameNode);
        if (nameNode == nullptr) {
            continue;  // Skip if no name found
        }
        std::string ifaceName = lyd_get_value(nameNode);

        ietfInterface_t* targetInterface = nullptr;
        for (auto& existing : node.interfaces) {
            if (existing.name == ifaceName) {
                targetInterface = &existing;
                break;
            }
        }

        if (targetInterface == nullptr) {
            ietfInterface_t newInterface;
            newInterface.name = ifaceName;
            node.interfaces.push_back(newInterface);

            targetInterface = &node.interfaces.back();
            spdlog::info("Added new interface '{}' to node '{}'.", ifaceName, node.hostName);
        } else {
            spdlog::info("Updating existing interface '{}' in node '{}'.", ifaceName, node.hostName);
        }

        struct lyd_node* enabledNode = nullptr;
        if (lyd_find_path(ifaceNode, "enabled", 0, &enabledNode) == LY_SUCCESS && enabledNode != nullptr) {
            std::string enabledValue = lyd_get_value(enabledNode);
            targetInterface->adminEnabled = (enabledValue == "true");
        }

        struct lyd_node* operStatusNode = nullptr;
        if (lyd_find_path(ifaceNode, "oper-status", 0, &operStatusNode) == LY_SUCCESS && operStatusNode != nullptr) {
            std::string operStatusValue = lyd_get_value(operStatusNode);
            if (operStatusValue == "up") {
                targetInterface->operStatus = OperStatus::UP;
            } else if (operStatusValue == "down") {
                targetInterface->operStatus = OperStatus::DOWN;
            } else {
                targetInterface->operStatus = OperStatus::UNKNOWN;
            }
        }
    }
    ly_set_free(ifaceSet, nullptr);
    PERFORMANCE_LOGGING("[InterfaceParser::parseInterface]", "Finished interface parsing");
    return true;
}
}  // namespace cnc