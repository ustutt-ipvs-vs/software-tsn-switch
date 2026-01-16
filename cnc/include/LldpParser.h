#pragma once
#include <string>
#include <vector>
#include "Topology.h"

namespace cnc {
    class LldpParser {
    public:
        /**
         * @brief Parses LLDP data from the provided XML string and populates the given CncNode_t structure.
         * @param xmlData The LLDP data in XML format.
         * @param node Reference to the CncNode_t structure to populate.
         * @return true if parsing is successful, false otherwise.
         */
        static bool parseLldpData(const std::string& xmlData, CncNode_t& node);

    private:
        /**
         * @brief Helper function to extract the value of a specific XML tag from the provided XML string.
         * @param xmlData The XML data as a string.
         * @param tagName The name of the XML tag to extract.
         * @param startPos The position in the string to start searching from (updated after extraction).
         * @return The extracted tag value as a string.
         */
        static std::string extractTagValue(const std::string& xmlData, const std::string& tagName, size_t startPos = 0);

        /**
         * @brief Helper function to find the start position of a specific XML tag in the provided XML string.
         * @param xmlData The XML data as a string.
         * @param tagName The name of the XML tag to find.
         * @param startPos The position in the string to start searching from.
         * @return The position of the start of the tag, or std::string::npos if not found.
         */
        static size_t findTagStart(const std::string& xmlData, const std::string& tagName, size_t startPos);
    };
}