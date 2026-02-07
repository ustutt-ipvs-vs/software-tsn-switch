#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class OperStatus : uint8_t {
    UP = 1,
    DOWN = 2,
    TESTING = 3,
    UNKNOWN = 4,
    DORMANT = 5,
    NOT_PRESENT = 6,
    LOWER_LAYER_DOWN = 7
};

/**
 * Maps internal OperStatus to YANG enumeration labels.
 * Returns a reference to a static optional string to avoid allocations.
 */
inline const std::optional<std::string>& operStatusToYangString(OperStatus status) {
    static const std::optional<std::string> s_up = "up";
    static const std::optional<std::string> s_down = "down";
    static const std::optional<std::string> s_testing = "testing";
    static const std::optional<std::string> s_unknown = "unknown";
    static const std::optional<std::string> s_dormant = "dormant";
    static const std::optional<std::string> s_notPresent = "not-present";
    static const std::optional<std::string> s_lowerDown = "lower-layer-down";
    static const std::optional<std::string> s_none = std::nullopt;

    switch (status) {
        case OperStatus::UP:
            return s_up;
        case OperStatus::DOWN:
            return s_down;
        case OperStatus::TESTING:
            return s_testing;
        case OperStatus::UNKNOWN:
            return s_unknown;
        case OperStatus::DORMANT:
            return s_dormant;
        case OperStatus::NOT_PRESENT:
            return s_notPresent;
        case OperStatus::LOWER_LAYER_DOWN:
            return s_lowerDown;
        default:
            return s_none;
    }
}

enum class IfType {
    ETHERNET,  // iana-if-type:ethernetCsmacd (Default)
    BRIDGE,    // iana-if-type:bridge
    LAG,       // iana-if-type:ieee8023adLag
    LOOPBACK   // iana-if-type:softwareLoopback
};

inline const std::optional<std::string>& ifTypeToIanaString(IfType type) {
    // These are initialized once the first time the function is called
    static const std::optional<std::string> s_ethernet = "iana-if-type:ethernetCsmacd";
    static const std::optional<std::string> s_bridge = "iana-if-type:bridge";
    static const std::optional<std::string> s_lag = "iana-if-type:ieee8023adLag";
    static const std::optional<std::string> s_loopback = "iana-if-type:softwareLoopback";
    static const std::optional<std::string> s_none = std::nullopt;

    switch (type) {
        case IfType::ETHERNET:
            return s_ethernet;
        case IfType::BRIDGE:
            return s_bridge;
        case IfType::LAG:
            return s_lag;
        case IfType::LOOPBACK:
            return s_loopback;
        default:
            return s_none;
    }
}

struct TrafficClassData_t {
    bool mapDataSet = false;
    uint8_t numTrafficClasses = 1;

    // IEEE 802.1Q defines priorities 0-7.
    // Kernel supports 16, but YANG usually exposes 8.
    std::array<uint8_t, 8> priorityMap = {0};
};

// Single entry for the Gate Control List
struct GclEntry_t {
    uint32_t index;
    uint8_t gateStatesValue;                              // Bitmask: 0=Closed, 1=Open (Bit 0 = TC0)
    uint32_t timeIntervalValue;                           // Duration in nanoseconds
    std::string operationName = "sched:set-gate-states";  // Default operation (maybe unnÃ¶tig)
};

// For admin-cycle-time (numerator/denominator)
struct RationalTime_t {
    uint32_t numerator;
    uint32_t denominator;  // Usually 1,000,000,000 for seconds
};

// PTP Timestamp for base-time
struct PtpTime_t {
    uint64_t seconds;
    uint32_t nanoseconds;
};

struct queueMaxSduEntry_t {
    uint8_t trafficClass;
    uint32_t queueMaxSdu;
    uint64_t transmissionOverrun;  // RO
};

// Main Configuration Struct (mapped to sched-parameters)
struct GclConfig_t {
    // Metadata variables to be set by netlink parser
    // Used determine which variables make sense to read
    bool operDataSet = false;
    bool adminDataSet = false;

    std::vector<queueMaxSduEntry_t> queueMaxSduTable;

    // 802.1Qbv Admin Parameters
    bool gateEnabled = false;       // Master switch for TSN
    uint8_t adminGateStates = 255;  // Initial state (255 = all open)

    RationalTime_t adminCycleTime = {.numerator = 1, .denominator = 1};  // Cycle duration
    uint32_t adminCycleTimeExtensionNs = 0;  // Max extension for updates (prevent packet loss)

    PtpTime_t adminBaseTime;  // Start time for the schedule (ConfigChangeTime)

    std::vector<GclEntry_t> adminControlList;  // The actual schedule list

    uint8_t operGateStates = 255;  // Initial state (255 = all open)

    RationalTime_t operCycleTime;           // Cycle duration
    uint32_t operCycleTimeExtensionNs = 0;  // Max extension for updates (prevent packet loss)

    PtpTime_t operBaseTime;  // Start time for the schedule (ConfigChangeTime)

    std::vector<GclEntry_t> operControlList;  // The actual schedule list

    bool configChange;  // RW: Set true to trigger apply

    uint64_t configChangeTimeSeconds;      // RO
    uint32_t configChangeTimeNanoseconds;  // RO

    bool configPending;          // RO
    uint64_t configChangeError;  // RO: Counter

    uint32_t tickGranularity;

    uint64_t currentTimeSeconds;
    uint32_t currentTimeNanoseconds;

    uint32_t supportedListMax = 31;
    uint32_t supportedIntervalMax = 1e9;

    uint32_t supportedCycleMaxNumerator = 1e9;
    uint32_t supportedCycleMaxDenominator = 1e9;
};

struct LldpNeighbor_t{
    bool hasNeighbor = false;
    std::string chassisId;
    std::string portId;
    std::string systemName;
    uint32_t ttl;
    std::string managementIp;
};

struct BridgePort_t {
    std::string bridgeName;
    int masterIndex = 0;
    TrafficClassData_t trafficClassData;
    GclConfig_t gateParameterTable;
};

struct ietfInterface_t {
    int ifindex;
    std::string name;

    uint32_t lastLinkUpdateId;
    uint32_t lastQdiscUpdateId;

    // --- IETF Interfaces Data ---
    IfType type = IfType::ETHERNET;  // Will be mapped to "iana-if-type:ethernetCsmacd", "iana-if-type:bridge", etc.
    bool adminEnabled = false;       // Derived from IFF_UP
    OperStatus operStatus = OperStatus::UNKNOWN;  // Derived from IFLA_OPERSTATE

    bool hasPhysAddr = false;
    std::array<uint8_t, 6> physAddress = {0};  // Derived from IFLA_ADDRESS

    uint32_t mtu = 0;  // Derived from IFLA_MTU

    // --- Capabilities / Logic ---
    uint32_t numTxQueues = 1;  // Derived from IFLA_NUM_TX_QUEUES (Crucial for TSN)

    BridgePort_t bridgePort;
    LldpNeighbor_t lldpNeighbor;
};

struct CncNode_t{
    uint32_t id;
    std::string hostName;
    std::vector<ietfInterface_t> interfaces;
    std::string ipAddress;
};