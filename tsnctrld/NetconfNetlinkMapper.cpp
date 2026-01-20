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
uint64_t NetconfNetlinkMapper::rationalToNs(const RationalTime_t& rationalTime) {
    return (static_cast<uint64_t>(rationalTime.numerator) * 1'000'000'000ULL) / rationalTime.denominator;
}

/**
 * @brief Convert a PTP time to nanoseconds.
 *
 * @param time Time represented as a PtpTime_t (seconds + nanoseconds)
 * @return uint64_t Time in nanoseconds
 */
uint64_t NetconfNetlinkMapper::ptpToNs(const PtpTime_t& time) {
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

RationalTime_t NetconfNetlinkMapper::fromNsToRational(uint64_t ns) {
// To get back to the original value using (N * 10^9) / D:
// If we set the denominator to 1,000,000,000, the numerator is simply the nanoseconds.
return {
    .numerator = static_cast<uint32_t>(ns),
    .denominator = 1'000'000'000U
};
}

PtpTime_t NetconfNetlinkMapper::fromNsToPtp(uint64_t ns) {
return {
    .seconds = ns / 1'000'000'000ULL,           // Integer division gets total seconds
    .nanoseconds = static_cast<uint32_t>(ns % 1'000'000'000ULL) // Modulo gets remaining nanos
};
}




TaprioConfig NetconfNetlinkMapper::mapToTaprio(const GclConfig_t& gcl) {
    TaprioConfig taprioConf{};
    taprioConf.numTc = gcl.queueMaxSduTable.size();

    for (uint8_t i = 0; i < taprioConf.numTc; ++i) {
        //todo check if this config is valid in regards to the standard
        taprioConf.prioTc[i] = i;
    }

    taprioConf.admin.clockid = CLOCK_TAI;
    taprioConf.admin.baseTime = ptpToNs(gcl.adminBaseTime);
    taprioConf.admin.cycleTime = rationalToNs(gcl.adminCycleTime);
    taprioConf.admin.cycleTimeExt = gcl.adminCycleTimeExtensionNs;

    taprioConf.admin.entries.reserve(gcl.adminControlList.size());
    for (const GclEntry_t& entry : gcl.adminControlList) {
        taprioConf.admin.entries.push_back({
            .command  = TC_TAPRIO_CMD_SET_GATES,
            .gateMask = entry.gateStatesValue,
            .interval = entry.timeIntervalValue
        });
    }

    taprioConf.oper.clockid = CLOCK_TAI;
    taprioConf.oper.baseTime = ptpToNs(gcl.operBaseTime);
    taprioConf.oper.cycleTime = rationalToNs(gcl.operCycleTime);
    taprioConf.oper.cycleTimeExt = gcl.operCycleTimeExtensionNs;

    taprioConf.oper.entries.reserve(gcl.operControlList.size());
    for (const GclEntry_t& entry : gcl.operControlList) {
        taprioConf.oper.entries.push_back({
            .command  = TC_TAPRIO_CMD_SET_GATES,
            .gateMask = entry.gateStatesValue,
            .interval = entry.timeIntervalValue
        });
    }

    return taprioConf;
}
