#pragma once

#include <string>
#include <vector>
#include <sstream> // For XML construction
#include "CncTypes.h"

namespace cnc {
    class GclXmlBuilder {
        public:
            /**
             * @brief Constructor: Prevents instantiation of this utility class. Just tools.
             */
            GclXmlBuilder() = delete;

            /**
             * @brief Builds XML configuration for the given CncNode_t's GCL.
             * Function iterates over all Interfaces in CncNode_t and constructs
             * the appropriate XML structure for the Gate Control List (GCL) if the interface has GCL data.
             * @param node The CncNode_t containing GCL data.
             * @return A string containing the XML configuration.
             */
            static std::string buildXmlForNode(const CncNode_t& node);

        private:
            /**
             * @brief Appends XML for a single interface's GCL configuration.
             * @param ss The stringstream to append the XML to.
             * @param iface The ietfInterface_t containing GCL data.
             */
            static void appendInterfaceXml(std::stringstream& ss, const ietfInterface_t& iface);

            /**
             * @brief Appends XML for cycle time 
             * @param ss The stringstream to append the XML to.
             * @param time The given cycle time in the GCL.
             */
            static void appendCycleTime(std::stringstream& ss, const RationalTime_t& time);

            /**
             * @brief Appends the Gate Control List to Xml (AdminControlList)
             * @param ss The stringstream to append the XML to.
             * @param config The GCL holding all necessary information for node.
             */
            static void appendControlList(std::stringstream& ss, const GclConfig_t config);

            /**
             * @brief Appends the ptpTime to the Xml 
             * @param ss The stringstream to append the XML to.
             * @param time The ptp time.
             * @param tagName The name of the tag to use.
             */
            static void appendPtpTime(std::stringstream& ss, const PtpTime_t time, const std::string& tagName);
    };
}