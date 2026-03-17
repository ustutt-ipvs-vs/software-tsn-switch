#ifndef ENPRO_SWITCH_PTPSTRUCTS_H
#define ENPRO_SWITCH_PTPSTRUCTS_H
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ptp {

#define MID_CLOCK_DESCRIPTION 0x0001

#define MID_DEFAULT_DATA_SET 0x2000
#define MID_CURRENT_DATA_SET 0x2001
#define MID_PARENT_DATA_SET 0x2002
#define MID_TIME_PROPERTIES_DATA_SET 0x2003
#define MID_PORT_DATA_SET 0x2004

// Helper to enforce the YANG "maximum range" saturation rule
inline int64_t toTimeInterval(double val) {
    if (std::isnan(val)) return 0;

    // Clamp to max/min limits to prevent undefined behavior and satisfy YANG
    if (val >= static_cast<double>(INT64_MAX)) return INT64_MAX;
    if (val <= static_cast<double>(INT64_MIN)) return INT64_MIN;

    // Use std::round to ensure we don't truncate fractional scaled nanoseconds
    return static_cast<int64_t>(std::round(val));
}

struct PortIdentity {
    std::array<uint8_t, 8> clockIdentity;
    uint16_t portNumber;
} __attribute__((packed));

// Standard IEEE 1588 Message Header (34 bytes)
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

struct PtpManagementMsg {
    PtpHeader header;
    PortIdentity targetPortIdentity;
    uint8_t startingBoundaryHops;
    uint8_t boundaryHops;
    uint8_t actionField;
    uint8_t reserved3;
} __attribute__((packed));

struct PtpManagementTlv {
    uint16_t tlvType;
    uint16_t lengthField;
    uint16_t managementId;
} __attribute__((packed));

struct PtpManagementErrorTlv {
    uint16_t tlvType;
    uint16_t lengthField;
    uint16_t managementErrorId;
    uint16_t managementId;
    uint64_t reserved;
} __attribute__((packed));

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

struct ClockQuality {
    uint8_t clockClass;
    uint8_t clockAccuracy;
    uint16_t offsetScaledLogVariance;
} __attribute__((packed));

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

struct CurrentDs {
    uint16_t stepsRemoved;
    int64_t offsetFromTimeTransmitter;
    int64_t meanDelay;
} __attribute__((packed));

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

struct PortAddress {
    uint16_t networkProtocol;
    uint16_t addressLength;
    std::vector<uint8_t> addressField;
};

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

struct ResponseWithSourceIdentity {
    PortIdentity source;
    std::vector<uint8_t> bytes;
};

struct RunningStats {
    uint32_t count = 0;
    int64_t min = INT64_MAX;
    int64_t max = INT64_MIN;
    double mean = 0.0;
    double m2 = 0.0;  // Sum of squares of differences from the mean
    uint32_t startTime10ms = 0;

    void reset(uint32_t now10ms) {
        count = 0;
        min = INT64_MAX;
        max = INT64_MIN;
        mean = 0.0;
        m2 = 0.0;
        startTime10ms = now10ms;
    }

    void update(int64_t val) {
        count++;
        if (val < min) min = val;
        if (val > max) max = val;

        double delta = val - mean;
        mean += delta / count;
        double delta2 = val - mean;
        m2 += delta * delta2;
    }

    void reset() {
        count = 0;
        min = INT64_MAX;
        max = INT64_MIN;
        mean = 0.0;
        m2 = 0.0;
    }

    double getStdDev() const {
        return (count < 2) ? 0.0 : std::sqrt(m2 / (count - 1));
    }

    PtpPerformanceParameters_t toParameters() const {
        if (count == 0) {
            // If no data, return 0s. The 'measurementValid = false' flag
            // in the parent struct tells Sysrepo to ignore this record.
            return {0, 0, 0, 0};
        }

        int64_t final_stddev = 0;
        if (count > 1) {
            double variance = m2 / (count - 1);
            final_stddev = toTimeInterval(std::sqrt(variance));
        }

        return {toTimeInterval(mean),
                min,  // min is already an int64_t, no conversion needed
                max,  // max is already an int64_t, no conversion needed
                final_stddev};
    }
};

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

// struct parentDs {
//     portIdentity parentPortIdentity;
//     std::string grandmasterIdentity;
// };

}  // namespace ptp

#endif  // ENPRO_SWITCH_PTPSTRUCTS_H
