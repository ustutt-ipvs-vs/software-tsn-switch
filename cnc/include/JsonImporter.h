#pragma once
#include <string>
#include <vector>

#include "CncTypes.h"
#include "Inventory.h"
#include "Topology.h"

namespace cnc {
/**
 * @brief Imports CNC topology and inventory data from JSON sources.
 *
 * This utility class provides static import routines for reading node/interface configuration into
 * `Topology` objects and for loading device access credentials into an `InventoryMap`. It is intended
 * as the JSON ingestion boundary between external configuration files and internal CNC data models.
 */
class JsonImporter {
   public:
    /**
     * @brief Imports CNC node configurations from a JSON file.
     * @param filename The path to the JSON file.
     * @return True if the import was successful, false otherwise.
     */
    static bool importFromFile(const std::string& filename, Topology& topology);

    /**
     * @brief Imports device inventory (credentials) from a JSON file.
     * @param filename The path to the inventory JSON file.
     * @return An InventoryMap mapping hostnames to their credentials (username and password).
     */
    static InventoryMap importInventory(const std::string& filename);
};
}  // namespace cnc