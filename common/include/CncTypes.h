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
 * @brief Helper method that maps internal OperStatus to YANG enumeration labels.
 *
 * @param status An @ref OperStatus
 * @return An optional string as it can be used to set the value of a field in a @ref libyang::DataNode.
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

/**
 * @brief Helper method to convert an @ref IfType to a string accepted by a field of type `iana-interface-type` in a
 * YANG model.
 *
 * @param type An @ref IfType
 * @return An optional string we can use for setting the value of a field in a @ref libyang::DataNode.
 */
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
    // Metadata variable to be set by netlink parser, determines if it makes sense to read data relating to the
    // operational dataset.
    bool operDataSet = false;
    // Metadata variable to be set by netlink parser, determines if it makes sense to read data relating to the
    // administrative dataset.
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

/**
 * @brief Struct that represents the basic information about a system connected to an LLDP-port on a host. Corresponds
 * to one instance of `/lldp/port/remote-systems-data`.
 */
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
};

/**
 * @brief Enum that represents the type of a PTP instance, for gPTP either Ordinary Clock or Boundary Clock
 */
enum class PtpInstanceType_t : uint8_t {
    PTP_INSTANCE_OC = 0,
    PTP_INSTANCE_BC,
    PTP_INSTANCE_P2P_TC,
    PTP_INSTANCE_E2E_TC,
    PTP_INSTANCE_INVALID,
};

/**
 * @brief Helper method to convert an @ref PtpInstanceType_t to a string that is valid in the YANG model.
 *
 * @param type A @ref PtpInstanceType_t
 * @return An optional string as it can be used to set the value of a field in a @ref libyang::DataNode.
 */
inline const std::optional<std::string>& ptpInstanceTypeToEnum(PtpInstanceType_t type) {
    // These are initialized once the first time the function is called
    static const std::optional<std::string> s_oc = "oc";
    static const std::optional<std::string> s_bc = "bc";
    static const std::optional<std::string> s_p2p_tc = "p2p-tc";
    static const std::optional<std::string> s_e2e_tc = "e2e-tc";
    static const std::optional<std::string> s_none = std::nullopt;

    switch (type) {
        case PtpInstanceType_t::PTP_INSTANCE_OC:
            return s_oc;
        case PtpInstanceType_t::PTP_INSTANCE_BC:
            return s_bc;
        case PtpInstanceType_t::PTP_INSTANCE_P2P_TC:
            return s_p2p_tc;
        case PtpInstanceType_t::PTP_INSTANCE_E2E_TC:
            return s_e2e_tc;
        default:
            return s_none;
    }
}

/**
 * @brief Enum that defines which state a port is in. For gPTP only some of them are used.
 */
enum class PtpPortState_t : uint8_t {
    INITIALIZING = 1,
    FAULTY,
    DISABLED,
    LISTENING,
    PRE_TIME_TRANSMITTER,
    TIME_TRANSMITTER,
    PASSIVE,
    UNCALIBRATED,
    TIME_RECEIVER
};

/**
 * @brief Helper method to convert an @ref PtpPortState_t to a string that is valid in the YANG model.
 * @param state
 * @return An optional string as it can be used to set the value of a field in a @ref libyang::DataNode.
 */
inline const std::optional<std::string>& portStateToString(PtpPortState_t state) {
    // Initialized once on first call
    static const std::optional<std::string> s_initializing = "initializing";
    static const std::optional<std::string> s_faulty = "faulty";
    static const std::optional<std::string> s_disabled = "disabled";
    static const std::optional<std::string> s_listening = "listening";
    static const std::optional<std::string> s_pre_time_transmitter = "pre-time-transmitter";
    static const std::optional<std::string> s_time_transmitter = "time-transmitter";
    static const std::optional<std::string> s_passive = "passive";
    static const std::optional<std::string> s_uncalibrated = "uncalibrated";
    static const std::optional<std::string> s_time_receiver = "time-receiver";
    static const std::optional<std::string> s_none = std::nullopt;

    switch (state) {
        case PtpPortState_t::INITIALIZING:
            return s_initializing;
        case PtpPortState_t::FAULTY:
            return s_faulty;
        case PtpPortState_t::DISABLED:
            return s_disabled;
        case PtpPortState_t::LISTENING:
            return s_listening;
        case PtpPortState_t::PRE_TIME_TRANSMITTER:
            return s_pre_time_transmitter;
        case PtpPortState_t::TIME_TRANSMITTER:
            return s_time_transmitter;
        case PtpPortState_t::PASSIVE:
            return s_passive;
        case PtpPortState_t::UNCALIBRATED:
            return s_uncalibrated;
        case PtpPortState_t::TIME_RECEIVER:
            return s_time_receiver;
        default:
            return s_none;
    }
}

/**
 * @brief Struct that represents the 4 values for a statistic: Mean, Max, Min, and StdDev
 */
struct PtpPerformanceParameters_t {
    int64_t avg;
    int64_t min;
    int64_t max;
    int64_t stddev;
};

/**
 * @brief Struct that represents the statistics of the PTP performance metrics (specifically the offset from the
 * time-transmitter and the mean path delay) over a certain period.
 *
 * It also tracks when this period started, and whether it is complete and contains valid data.
 */
struct PtpPerformanceRecord_t {
    bool periodComplete;
    uint32_t pmTime;
    bool measurementValid;
    PtpPerformanceParameters_t offsetFromTimeTransmitter;
    PtpPerformanceParameters_t meanPathDelay;
};

/**
 * @brief Struct that represents the statistics of the PTP performance metrics of a single port, specifically the mean
 * link delay, over a certain period.
 */
struct PtpPortPerformanceRecord_t {
    uint32_t pmTime;
    PtpPerformanceParameters_t meanLinkDelay;
};

/**
 * @brief Struct that represents the fields we use from the "Default data set" of the ieee1588-ptp-tt YANG model.
 */
struct DefaultDs_t {
    uint16_t numPorts;
    uint8_t priority1;
    std::string clockIdentity;
    PtpTime_t currentTime;
    PtpInstanceType_t instanceType = PtpInstanceType_t::PTP_INSTANCE_INVALID;
};

/**
 * @brief Struct that represents the fields we use from the "Current data set" of the ieee1588-ptp-tt YANG model.
 */
struct CurrentDs_t {
    uint16_t stepsRemoved;
    int64_t offsetFromTimeTransmitter;
    int64_t meanDelay;
};

/**
 * @brief Struct that represents the fields we use from the "Parent data set" of the ieee1588-ptp-tt YANG model.
 */
struct ParentDs_t {
    std::string parentClockIdentity;
    uint16_t parentPortNumber;
    std::string grandParentClockIdentity;
};

/**
 * @brief Struct that represents the fields we use from the "Port data set" of the ieee1588-ptp-tt YANG model.
 */
struct PortDs_t {
    std::string portClockIdentity;
    uint16_t portPortNumber;
    PtpPortState_t portState;
    int64_t meanLinkDelay;
};

/**
 * @brief Struct that represents the fields we use from the `port` element of the ieee1588-ptp-tt YANG model.
 */
struct PtpPort_t {
    uint16_t portIndex;
    std::string underlyingInterface;
    PortDs_t portDs;
    std::vector<PtpPortPerformanceRecord_t> portPerformanceRecords15m;
    std::vector<PtpPortPerformanceRecord_t> portPerformanceRecords24h;
};

/**
 * @brief Struct that represents all the PTP information on one host we use, corresponds to one
 * `/ptp/instances/instance` of the ieee1588-ptp-tt YANG model.
 */
struct PtpNode_t {
    DefaultDs_t defaultDs;
    CurrentDs_t currentDs;
    ParentDs_t parentDs;
    std::vector<PtpPerformanceRecord_t> performanceRecords15m;
    std::vector<PtpPerformanceRecord_t> performanceRecords24h;
    std::vector<PtpPort_t> ports;
};

/**
 * @brief Struct that represents the information about the local system, as available to the LLDP agent.
 */
struct LldpLocalSystemData_t {
    std::string chassisIdSubtype;
    std::string chassisId;
    std::string systemName;
    std::string systemDescription;
    std::string systemCapabilitiesSupported;
    std::string systemCapabilitiesEnabled;
};

/**
 * @brief Struct that represents one "LLDP-port", aka one interface participating in LLDP. Contains a list of @ref
 * LldpNeighbor_t structs which correspond to basic information about an entry of `remote-systems-data`.
 */
struct LldpPort_t {
    std::string name;
    std::string destMacAddress;
    std::string adminStatus;
    std::vector<LldpNeighbor_t> neighbors;  // TODO: Needs to be implemented
};

/**
 * @brief Represents all LLDP information of a host. Includes the local configuration and system-data and a list of @ref
 * LldpPort_t structs, one for each interface participating in LLDP.
 */
struct LldpNode_t {
    uint32_t messageTxInterval;
    uint32_t messageTxHoldMultiplier;
    uint32_t messageFastTx;
    LldpLocalSystemData_t localSystemData;
    std::vector<LldpPort_t> ports;
};

/**
 * @brief This struct is used on the CNC side to represent one host with all its interfaces as well as its PTP and LLDP
 * information.
 */
struct CncNode_t {
    uint32_t id;
    std::string hostName;
    std::vector<ietfInterface_t> interfaces;
    std::string ipAddress;
    PtpNode_t ptpAllData;
    LldpNode_t lldpAllData;
};