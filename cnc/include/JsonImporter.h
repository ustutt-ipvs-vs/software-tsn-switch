#pragma once
#include <vector>
#include <string>
#include "CncTypes.h"
#include "Topology.h"
#include "Inventory.h"

namespace cnc {
    class JsonImporter {
    public:
        /**
         * @brief Imports CNC node configurations from a JSON file.
         * @param filePath The path to the JSON file.
         * @return True if the import was successful, false otherwise.
         */
        static bool importFromFile(const std::string& filePath, Topology& topology);

        /**
         * @brief Imports device inventory (credentials) from a JSON file.
         * @param filename The path to the inventory JSON file.
         * @return An InventoryMap mapping hostnames to their credentials (username and password).
         */
        static InventoryMap importInventory(const std::string& filename);
    };
}