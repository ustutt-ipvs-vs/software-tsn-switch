#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
/**
 * @brief Enum that represents the operational status of an interface, as given by the IFLA_OPERSTATE netlink attribute
 * and required by the oper-status leaf of the ietf-interfaces yang model.
 */
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

/**
 * @brief Enum that represents the iana-if-type of an interface, interpreted from the IFLA_INFO_KIND netlink attribute
 * and required for the mandatory type leaf of the ietf-interfaces model.
 */
enum class IfType : uint8_t {
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

/**
 * @brief Struct to represent the traffic-class/traffic-class-table container of the ieee802-dot1q-bridge model.
 *
 * Includes the metadata @ref mapDataSet to indicate if the values are initialized.
 */
struct TrafficClassData_t {
    bool mapDataSet = false;
    uint8_t numTrafficClasses = 1;

    // IEEE 802.1Q defines priorities 0-7.
    // Kernel supports 16, but YANG usually exposes 8.
    std::array<uint8_t, 8> priorityMap = {0};
};

/**
 * @brief Struct to represent a single gate-control-entry in a base-gate-control-entries grouping (aka the
 * admin-control-list or oper-control-list container) of the ieee802-dot1q-sched model.
 */
struct GclEntry_t {
    uint32_t index;
    uint8_t gateStatesValue;                              // Bitmask: 0=Closed, 1=Open (Bit 0 = TC0)
    uint32_t timeIntervalValue;                           // Duration in nanoseconds
    std::string operationName = "sched:set-gate-states";  // Default operation (maybe unnÃ¶tig)
};

/**
 * @brief Struct to represent an ieee802:rational-grouping.
 */
struct RationalTime_t {
    uint32_t numerator;
    uint32_t denominator;  // Usually 1,000,000,000 for seconds
};

/**
 * @brief Struct to represent an ieee802:ptp-time-grouping.
 */
struct PtpTime_t {
    uint64_t seconds;
    uint32_t nanoseconds;
};

/**
 * @brief Struct to represent a single entry of the queue-max-sdu-table list of the ieee802-dot1q-sched model.
 */
struct queueMaxSduEntry_t {
    uint8_t trafficClass;
    uint32_t queueMaxSdu;
    uint64_t transmissionOverrun;  // RO
};

/**
 * @brief Struct to hold information about the gate-parameter-table of an interface, according to the
 * ieee802-dot1q-sched yang model.
 *
 * Includes the metadata variables @ref operDataSet and @ref adminDataSet to indicate which fields of this instance are
 * actually populated and usable.
 */
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

    uint32_t tickGranularity = 10'000;  // RO: Resolution of shaper in tenths of nanoseconds, using 1us for simplicity

    uint64_t currentTimeSeconds;
    uint32_t currentTimeNanoseconds;
    int32_t clockId;

    uint32_t supportedListMax = 31;
    uint32_t supportedIntervalMax = 1e9;

    uint32_t supportedCycleMaxNumerator = 1e9;
    uint32_t supportedCycleMaxDenominator = 1e9;
};

struct LldpNeighbor_t {
    bool hasNeighbor = false;
    std::string chassisId;
    std::string portId;
    std::string systemName;
    uint32_t ttl;
    std::string managementIp;
};
/**
 * @brief Struct to hold information about and interfaces bridge-port, as defined by the ieee802-dot1q-bridge yang
 * model.
 *
 *
 */
struct BridgePort_t {
    std::string bridgeName;
    int masterIndex = 0;
    TrafficClassData_t trafficClassData;
    GclConfig_t gateParameterTable;
};
/**
 * @brief Struct to hold information about an interface, inspired by the ietf-interfaces yang model.
 *
 * Contains @ref ifindex and @ref name for identification, and more values following the ietf-interfaces model.
 * @ref lastLinkUpdateId and @ref lastQdiscUpdateId contain the sysrepo request-id of the last update of this struct,
 * and are used to ensure refreshes as needed.
 */
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
    uint32_t numTxQueues = 1;        // Derived from IFLA_NUM_TX_QUEUES, contains the maximum supported number of queues
    uint32_t numActiveTxQueues = 1;  // Derived via ethtools, contains the number of active queues
    uint64_t speed = 0;              // Derived via ethtools

    BridgePort_t bridgePort;
    LldpNeighbor_t lldpNeighbor;
};

struct CncNode_t {
    uint32_t id;
    std::string hostName;
    std::vector<ietfInterface_t> interfaces;
    std::string ipAddress;
};

struct DefaultDs_t {
    uint16_t numPorts;
    uint8_t priority1;
    std::string clockIdentity;
    PtpTime_t currentTime;
};
struct CurrentDs_t {};
struct ParentDs_t {};
struct PortDs {};

struct PerformanceRecord_t {
    uint16_t index;
    bool periodComplete;
    uint32_t pmTime;
};

struct PtpPort_t {
    uint16_t portIndex;
    std::string underlyingInterface;
    PortDs portDs;
};

struct PtpNode_t {
    DefaultDs_t defaultDs;
    CurrentDs_t currentDs;
    ParentDs_t parentDs;
    std::vector<PerformanceRecord_t> performanceRecords15m;
    std::vector<PerformanceRecord_t> performanceRecords24h;
    std::vector<PtpPort_t> ports;
};
