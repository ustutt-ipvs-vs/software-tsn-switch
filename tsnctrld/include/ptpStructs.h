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

enum class PortState {
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
    uint8_t portState;
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

// struct parentDs {
//     portIdentity parentPortIdentity;
//     std::string grandmasterIdentity;
// };

}  // namespace ptp

#endif  // ENPRO_SWITCH_PTPSTRUCTS_H
