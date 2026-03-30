#ifndef ENPRO_SWITCH_PTPSTRUCTS_H
#define ENPRO_SWITCH_PTPSTRUCTS_H
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ptp {

/**
 * @brief Helper to enforce the YANG "maximum range" saturation rule
 */
inline int64_t toTimeInterval(double val) {
    if (std::isnan(val)) {
        return 0;
    }

    // Clamp to max/min limits to prevent undefined behavior and satisfy YANG
    if (val >= static_cast<double>(INT64_MAX)) {
        return INT64_MAX;
    }
    if (val <= static_cast<double>(INT64_MIN)) {
        return INT64_MIN;
    }

    // Use std::round to ensure we don't truncate fractional scaled nanoseconds
    return static_cast<int64_t>(std::round(val));
}

/**
 * @brief A PortIdentity, as defined by the IEEE 1588-2019 Standard.
 */
struct PortIdentity {
    std::array<uint8_t, 8> clockIdentity;
    uint16_t portNumber;
} __attribute__((packed));

/**
 * @brief A PTP message header, as defined by the IEEE 1588-2019 Standard. Multibyte fields must be reordered to the
 * correct endianess before using them.
 */
struct PtpHeader {
    uint8_t tsmt;
    uint8_t ver;
    uint16_t messageLength;
    uint8_t domainNumber;
    uint8_t reserved1;
    uint16_t flagField;
    int64_t correctionField;
    uint32_t reserved2;
    PortIdentity sourcePortIdentity;
    uint16_t sequenceId;
    uint8_t controlField;
    int8_t logMessageInterval;
} __attribute__((packed));

/**
 * @brief A PTP management message, as defined by the IEEE 1588-2019 Standard. It is followed directly by either a @ref
 * PtpManagementTlv or @ref PtpManagementErrorTlv, the first two bytes after this struct define which one it is.
 * Multibyte fields must be reordered to the correct endianess before using them.
 */
struct PtpManagementMsg {
    PtpHeader header;
    PortIdentity targetPortIdentity;
    uint8_t startingBoundaryHops;
    uint8_t boundaryHops;
    uint8_t actionField;
    uint8_t reserved3;
} __attribute__((packed));

/**
 * @brief A PTP managementTlv, as defined by the IEEE 1588-2019 Standard. The first two bytes define whether the memory
 * should be interpreted as this type, or rather a @ref PtpManagementErrorTlv. Multibyte fields must be reordered to the
 * correct endianess before using them.
 */
struct PtpManagementTlv {
    uint16_t tlvType;
    uint16_t lengthField;
    uint16_t managementId;
} __attribute__((packed));

/**
 * @brief A PTP managementTlv, as defined by the IEEE 1588-2019 Standard. The first two bytes define whether the memory
 * should be interpreted as this type, or rather a @ref PtpManagementTlv. Multibyte fields must be reordered to the
 * correct endianess before using them.
 */
struct PtpManagementErrorTlv {
    uint16_t tlvType;
    uint16_t lengthField;
    uint16_t managementErrorId;
    uint16_t managementId;
    uint64_t reserved;
} __attribute__((packed));

/**
 * @brief An enum representing the state a PTP port is in, as defined by the IEEE 1588-2019 Standard.
 */
enum class PortState : uint8_t {
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
 * @brief A struct representing the quality of a clock, as defined by the IEEE 1588-2019 Standard. Multibyte fields must
 * be reordered to the correct endianess before using them.
 */
struct ClockQuality {
    uint8_t clockClass;
    uint8_t clockAccuracy;
    uint16_t offsetScaledLogVariance;
} __attribute__((packed));

/**
 * @brief A struct representing a DEFAULT_DATA_SET, as it is sent over the wire, as defined by the IEEE 1588-2019
 * Standard. Multibyte fields must be reordered to the correct endianess before using them.
 */
struct DefaultDs {
    uint8_t flags;
    uint8_t reserved1;
    uint16_t numberPorts;
    uint8_t priority1;
    ClockQuality clockQuality;
    uint8_t priority2;
    std::array<uint8_t, 8> clockIdentity;
    uint8_t domainNumber;
    uint8_t reserved2;
} __attribute__((packed));

/**
 * @brief A struct representing a CURRENT_DATA_SET, as it is sent over the wire, as defined by the IEEE 1588-2019
 * Standard. Multibyte fields must be reordered to the correct endianess before using them.
 */
struct CurrentDs {
    uint16_t stepsRemoved;
    int64_t offsetFromTimeTransmitter;
    int64_t meanDelay;
} __attribute__((packed));

/**
 * @brief A struct representing a PARENT_DATA_SET, as it is sent over the wire, as defined by the IEEE 1588-2019
 * Standard. Multibyte fields must be reordered to the correct endianess before using them.
 */
struct ParentDS {
    PortIdentity parentPortIdentity;
    uint8_t parentStats;
    uint8_t reserved;
    uint16_t observedParentOffsetScaledLogVariance;
    uint32_t observedParentClockPhaseChangeRate;
    uint8_t grandmasterPriority1;
    ClockQuality grandmasterClockQuality;
    uint8_t grandmasterPriority2;
    std::array<uint8_t, 8> grandmasterIdentity;
} __attribute__((packed));

/**
 * @brief A struct representing a PORT_DATA_SET, as it is sent over the wire, as defined by the IEEE 1588-2019 Standard.
 * Multibyte fields must be reordered to the correct endianess before using them.
 */
struct PortDs {
    PortIdentity portIdentity;
    PortState portState;
    int8_t logMinDelayReqInterval;
    int64_t meanLinkDelay;
    int8_t logAnnounceInterval;
    uint8_t announceReceiptTimeout;
    int8_t logSyncInterval;
    uint8_t delayMechanism;
    int8_t logMinPdelayReqInterval;
    uint8_t versionNumber;
} __attribute__((packed));

/**
 * @brief The "protocol address" of a PTP port, as defined by the IEEE 1588-2019 Standard.
 */
struct PortAddress {
    uint16_t networkProtocol;
    uint16_t addressLength;
    std::vector<uint8_t> addressField;
};

/**
 * @brief A struct representing the conceptual CLOCK_DESCRIPTION, as defined by the IEEE 1588-2019 Standard.
 *
 * Because the standard defines this message such that one field may describe the number of bytes in another field,
 * there is no predefined size we can easily use. Instead, it needs to be parsed manually.
 */
struct ClockDescription {
    PortIdentity source;
    uint16_t clockType;
    std::string physicalLayerProtocol;
    std::string physicalAddress;
    PortAddress protocolAddress;
    std::array<uint8_t, 3> manufacturerIdentity;
    std::string productDescription;
    std::string revisionData;
    std::string userDescription;
    std::array<uint8_t, 6> profileIdentifier;
};

/**
 * @brief Helper struct to associate the data of a response with the port it came from, even if this @ref PortIdentity
 * is not present in the data itself.
 */
struct ResponseWithSourceIdentity {
    PortIdentity source;
    std::vector<uint8_t> bytes;
};

/**
 * @brief Struct that contains the statistics regarding a single metric. Used to aggregate them until the period is
 * completed.
 */
struct RunningStats {
    uint32_t count = 0;
    int64_t min = INT64_MAX;
    int64_t max = INT64_MIN;
    double mean = 0.0;
    double m2 = 0.0;  // Sum of squares of differences from the mean
    bool invalid = false;

    void update(int64_t val) {
        if (val == INT64_MAX || val == INT64_MIN) {
            invalid = true;
            min = std::min(val, min);
            max = std::max(val, max);
            return;
        }

        count++;
        min = std::min(val, min);
        max = std::max(val, max);

        auto val_d = static_cast<double>(val);

        double delta = val_d - mean;
        mean += delta / count;
        double delta2 = val_d - mean;
        m2 += delta * delta2;
    }

    void reset() {
        count = 0;
        min = INT64_MAX;
        max = INT64_MIN;
        mean = 0.0;
        m2 = 0.0;
        invalid = false;
    }

    [[nodiscard]] double getStdDev() const {
        return (count < 2) ? 0.0 : std::sqrt(m2 / (count - 1));
    }

    [[nodiscard]] bool isValid(size_t expectedEntries) const {
        if (expectedEntries == SIZE_MAX) {
            return !invalid;
        }
        return !invalid && expectedEntries == count;
    }

    [[nodiscard]] PtpPerformanceParameters_t toParameters() const {
        if (count == 0) {
            // If no data, return 0s. The 'measurementValid = false' flag
            // in the parent struct tells Sysrepo to ignore this record.
            return {.avg = 0, .min = 0, .max = 0, .stddev = 0};
        }

        int64_t final_stddev = 0;
        if (count > 1) {
            double variance = m2 / (count - 1);
            final_stddev = toTimeInterval(std::sqrt(variance));
        }

        return {.avg = toTimeInterval(mean),
                .min = min,  // min is already an int64_t, no conversion needed
                .max = max,  // max is already an int64_t, no conversion needed
                .stddev = final_stddev};
    }
};

/**
 * @brief Struct that holds the internal state of all the PTP statistics we track. Only used for the current period,
 * after which the values from this struct are used to populate the structs following the YANG model.
 */
struct PerformanceRecord {
    uint32_t startTime10ms = 0;

    // Instance-level stats
    RunningStats offsetFromMaster;
    RunningStats meanPathDelay;

    // Port-level stats (Key = portNumber)
    std::map<uint16_t, RunningStats> portMeanLinkDelay;

    void reset(uint32_t now10ms) {
        startTime10ms = now10ms;
        offsetFromMaster.reset();
        meanPathDelay.reset();
        portMeanLinkDelay.clear();
    }
};

}  // namespace ptp

#endif  // ENPRO_SWITCH_PTPSTRUCTS_H
