#include "NetconfNetlinkMapper.h"
#include <cstdint>
#include <bits/time.h>
#include "../common/include/CncTypes.h"
#include "include/TaprioModel.h"

uint64_t NetconfNetlinkMapper::toNs(const RationalTime_t& rationalTime) {
    return (static_cast<uint64_t>(rationalTime.numerator) * 1'000'000'000ULL) / rationalTime.denominator;
}

uint64_t NetconfNetlinkMapper::toNs(const PtpTime_t& time) {
    return time.seconds * 1'000'000'000ULL + time.nanoseconds;
}


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
