#include "NetconfNetlinkMapper.h"
#include <cstdint>
#include <bits/time.h>
#include "../common/include/CncTypes.h"
#include "include/TaprioModel.h"

/**
 * @brief Convert a rational time to nanoseconds.
 *
 * @param rationalTime Time represented as a RationalTime_t (numerator/denominator)
 * @return uint64_t Time in nanoseconds
 */
uint64_t NetconfNetlinkMapper::toNs(const RationalTime_t& rationalTime) const {
    return (static_cast<uint64_t>(rationalTime.numerator) * 1'000'000'000ULL) / rationalTime.denominator;
}

/**
 * @brief Convert a PTP time to nanoseconds.
 *
 * @param time Time represented as a PtpTime_t (seconds + nanoseconds)
 * @return uint64_t Time in nanoseconds
 */
uint64_t NetconfNetlinkMapper::toNs(const PtpTime_t& time) const {
    return time.seconds * 1'000'000'000ULL + time.nanoseconds;
}

/**
 * @brief Map a Netconf configuration to a TAPRIO qdisc configuration.
 *
 * Converts an operational/configuration GCL (Gate Control List) configuration
 * to a TAPRIO configuration suitable for netlink message submission.
 *
 * @param gcl The GCL configuration
 * @return TaprioConfig The resulting TAPRIO configuration for the kernel
 */
TaprioConfig NetconfNetlinkMapper::mapToTaprio(const GclConfig_t& gcl) {
    TaprioConfig taprioConf{};
    taprioConf.numTc = gcl.queueMaxSduTable.size();

    for (uint8_t i = 0; i < taprioConf.numTc; ++i) {
        //todo check if this config is valid in regards to the standard
        taprioConf.prioTc[i] = i;
    }

    taprioConf.clockid = CLOCK_TAI;
    taprioConf.baseTime = toNs(gcl.adminBaseTime);
    taprioConf.cycleTime = toNs(gcl.adminCycleTime);
    taprioConf.schedule.reserve(gcl.adminControlList.size());

    for (uint32_t i = 0; i < gcl.adminControlList.size(); ++i) {
        const GclEntry_t& entry = gcl.adminControlList[i];
        TaprioSchedEntry taprioEntry{};
        taprioEntry.command = TC_TAPRIO_CMD_SET_GATES;
        taprioEntry.gateMask = entry.gateStatesValue;
        taprioEntry.interval = entry.timeIntervalValue;
        taprioConf.schedule.push_back(taprioEntry);
    }

    return taprioConf;
}
