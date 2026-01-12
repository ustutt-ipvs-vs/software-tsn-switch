#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>

// Single entry for the Gate Control List
struct GclEntry_t {
    uint32_t index;
    uint8_t gateStatesValue;      // Bitmask: 0=Closed, 1=Open (Bit 0 = TC0)
    uint32_t timeIntervalValue;   // Duration in nanoseconds
    std::string operationName = "sched:set-gate-states"; // Default operation (maybe unnÃ¶tig)
};

// For admin-cycle-time (numerator/denominator)
struct RationalTime_t {
    uint32_t numerator;
    uint32_t denominator; // Usually 1,000,000,000 for seconds
};

// PTP Timestamp for base-time
struct PtpTime_t {
    uint64_t seconds;
    uint32_t nanoseconds;
};

struct queueMaxSduEntry_t{
    uint8_t trafficClass;
    uint32_t queueMaxSdu;
    uint64_t transmissionOverrun; // RO
};

// Main Configuration Struct (mapped to sched-parameters)
struct GclConfig_t {

    std::vector<queueMaxSduEntry_t> queueMaxSduTable;

    // 802.1Qbv Admin Parameters
    bool gateEnabled = true;          // Master switch for TSN
    uint8_t adminGateStates = 255;    // Initial state (255 = all open)

    RationalTime_t adminCycleTime;      // Cycle duration
    uint32_t adminCycleTimeExtensionNs = 0; // Max extension for updates (prevent packet loss)
    
    PtpTime_t adminBaseTime;            // Start time for the schedule (ConfigChangeTime)
    
    std::vector<GclEntry_t> adminControlList; // The actual schedule list

    uint8_t operGateStates = 255;    // Initial state (255 = all open)

    RationalTime_t operCycleTime;      // Cycle duration
    uint32_t operCycleTimeExtensionNs = 0; // Max extension for updates (prevent packet loss)
    
    PtpTime_t operBaseTime;            // Start time for the schedule (ConfigChangeTime)
    
    std::vector<GclEntry_t> operControlList; // The actual schedule list

    bool configChange;                 // RW: Set true to trigger apply
    
    uint64_t configChangeTimeSeconds;    // RO
    uint32_t configChangeTimeNanoseconds;// RO
    
    bool     configPending;                // RO
    uint64_t configChangeError;           // RO: Counter

    uint32_t tickGranularity;
    
    uint64_t currentTimeSeconds;
    uint32_t currentTimeNanoseconds;

    uint32_t supportedListMax;
    uint32_t supportedIntervalMax;
    
    uint32_t supportedCycleMaxNumerator;
    uint32_t supportedCycleMaxDenominator;

};

struct BridgePort_t{
    std::string bridgeName;
    GclConfig_t gateParameterTable;
};

struct ietfInterface_t{
    std::string name;
    bool enabled;
    BridgePort_t bridgePort;
};