#include "GclXmlBuilder.h"
#include <iomanip> // Used for std::setfill, std::setw

namespace cnc {
    // Namespaces from YANG Files
    // [cite: 2] ietf-interfaces
    const std::string NS_IETF_IF = "urn:ietf:params:xml:ns:yang:ietf-interfaces";
    // [cite: 165] ieee802-dot1q-bridge
    const std::string NS_DOT1Q_BRIDGE = "urn:ieee:std:802.1Q:yang:ieee802-dot1q-bridge";
    // [cite: 696] ieee802-dot1q-sched
    const std::string NS_DOT1Q_SCHED = "urn:ieee:std:802.1Q:yang:ieee802-dot1q-sched";

    std::string GclXmlBuilder::buildXmlForNode(const CncNode_t& node) {
        std::stringstream ss;

        // Start interfaces element
        ss << "<interfaces xmlns=\"" << NS_IETF_IF << "\">";

        // Iterate over interfaces and append GCL XML
        for (const auto& iface : node.interfaces) {
            // Assume GCL data is present if gateParameterTable has entries
            // TODO: Add hasGcl flag to check for GCL presence
            appendInterfaceXml(ss, iface);
        }

        // End interfaces element
        ss << "</interfaces>";
        return ss.str();
    }

    void GclXmlBuilder::appendInterfaceXml(std::stringstream& ss, const ietfInterface_t& iface) {
        ss << "<interface>";
        ss << "<name>" << iface.name << "</name>";

        // ieee802-dot1q-bridge:bridge-port <bridge-port>
        // Bridge Port containing GCL configuration
        ss << "<bridge-port xmlns=\"" << NS_DOT1Q_BRIDGE << "\">";
        
        // ieee802-dot1q-sched:gate-parameter-table <gate-parameter-table>
        // GCL Configuration
        ss << "<gate-parameter-table xmlns=\"" << NS_DOT1Q_SCHED << "\">";

        // Extract GCL configuration
        const GclConfig_t& gclConfig = iface.bridgePort.gateParameterTable;

        // Append admin base time
        appendPtpTime(ss, gclConfig.adminBaseTime, "admin-base-time");

        // Append cycle time
        appendCycleTime(ss, gclConfig.adminCycleTime);

        // Append control list
        appendControlList(ss, gclConfig);

        // Append config change flag based on configChange boolean
        if (gclConfig.configChange) {
            ss << "<config-change>true</config-change>";
        } else {
            ss << "<config-change>false</config-change>";
        }

        // Close gate-parameter-table, bridge-port and interface tags
        ss << "</gate-parameter-table>";
        ss << "</bridge-port>";
        ss << "</interface>";
    }

    void GclXmlBuilder::appendCycleTime(std::stringstream& ss, const RationalTime_t& time) {
        ss << "<admin-cycle-time>";
        ss << "<numerator>" << time.numerator << "</numerator>";
        ss << "<denominator>" << time.denominator << "</denominator>";
        ss << "</admin-cycle-time>";
    }

    void GclXmlBuilder::appendPtpTime(std::stringstream& ss, const PtpTime_t time, const std::string& tagName) {
        ss << "<" << tagName << ">";
        ss << "<seconds>" << time.seconds << "</seconds>";
        ss << "<nanoseconds>" << time.nanoseconds << "</nanoseconds>";
        ss << "</" << tagName << ">";
    }

    void GclXmlBuilder::appendControlList(std::stringstream& ss, const GclConfig_t config) {
        // Check pointer validity
        if (config.adminControlList == nullptr || config.adminControlListSize == 0) {
            return; // No control list to append
        }

        ss << "<admin-control-list>";

        for (uint32_t i = 0; i < config.adminControlListSize; ++i) {
            const GclEntry_t& entry = config.adminControlList[i];
            ss << "<gcl-entry>";
            ss << "<index>" << entry.index << "</index>";
            ss << "<operation-name>" << entry.operationName << "</operation-name>";
            ss << "<gate-states-value>" << static_cast<uint32_t>(entry.gateStatesValue) << "</gate-states-value>";
            ss << "<time-interval-value>" << entry.timeIntervalValue << "</time-interval-value>";
            ss << "</gcl-entry>";
        }

        ss << "</admin-control-list>";
    }
}