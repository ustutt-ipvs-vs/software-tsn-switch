#include <spdlog/spdlog.h>

#include <iostream>
#include <vector>

#include "CncTypes.h"
#include "GclXmlBuilder.h"

// Simple demo to showcase GclXmlBuilder usage
int main() {
    spdlog::info("--- Starting XML Builder Demo ---");

    // 1. Mocking: Building a dummy CncNode_t with one interface and GCL data
    CncNode_t dummyNode;
    dummyNode.id = 1;
    dummyNode.hostName = "test-switch-01";
    dummyNode.ipAddress = "192.168.0.10";

    // 2. Create a dummy interface with GCL data
    ietfInterface_t iface;
    iface.name = "sw0p1";  // Important: The port name

    // 3. Now simulate the GCL config (the complex structure)
    // Access gateParameterTable in BridgePort
    GclConfig_t& gcl = iface.bridgePort.gateParameterTable;

    // set Admin Cycle Time to 1ms (1/1000000)
    gcl.adminCycleTime.numerator = 1000000;
    gcl.adminCycleTime.denominator = 1000000000;

    // Admin Base Time (PTP Start)
    gcl.adminBaseTime.seconds = 1600000000;
    gcl.adminBaseTime.nanoseconds = 0;

    // Gate Control List (Create the array)
    // Simulating 2 entries
    gcl.adminControlList.resize(2);  // Resize vector to hold 2 entries

    // Entry 0: All open (255) for 500us
    gcl.adminControlList[0].index = 0;
    gcl.adminControlList[0].operationName = "sched:set-gate-states";
    gcl.adminControlList[0].gateStatesValue = 255;  // 0xFF
    gcl.adminControlList[0].timeIntervalValue = 500000;

    // Entry 1: All closed (0) for 500us
    gcl.adminControlList[1].index = 1;
    gcl.adminControlList[1].operationName = "sched:set-gate-states";
    gcl.adminControlList[1].gateStatesValue = 0;  // 0x00
    gcl.adminControlList[1].timeIntervalValue = 500000;

    // Set config change flag
    gcl.configChange = true;

    // Add the interface to the node
    dummyNode.interfaces.push_back(iface);

    // 4. Action: Call the builder
    spdlog::info("Generating XML for {}...", dummyNode.hostName);
    std::string xmlOutput = cnc::GclXmlBuilder::buildXmlForNode(dummyNode);

    // 5. Check output
    spdlog::info("=== GENERATED XML START ===");
    spdlog::info(xmlOutput);
    spdlog::info("=== GENERATED XML END ===");

    // Cleanup handled automatically by std::vector

    return 0;
}