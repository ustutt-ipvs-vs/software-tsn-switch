#include "NetconfNetlinkMapper.h"
#include <cstdint>
#include <bits/time.h>
#include "../common/include/CncTypes.h"
#include "include/TaprioModel.h"

uint64_t NetconfNetlinkMapper::toNs(const RationalTime_t& rationalTime) const {
    return (static_cast<uint64_t>(rationalTime.numerator) * 1'000'000'000ULL) / rationalTime.denominator;
}

uint64_t NetconfNetlinkMapper::toNs(const PtpTime_t& time) const {
    return time.seconds * 1'000'000'000ULL + time.nanoseconds;
}

TaprioConfig NetconfNetlinkMapper::mapToTaprio(const GclConfig_t& gcl) {
    TaprioConfig taprioConf{};
    taprioConf.numTc = gcl.queueMaxSduCount;

    for (uint8_t i = 0; i < taprioConf.numTc; ++i) {
        //todo check if this config is valid in regards to the standard
        taprioConf.prioTc[i] = i;
    }

    taprioConf.clockid = CLOCK_TAI;
    taprioConf.baseTime = toNs(gcl.adminBaseTime);
    taprioConf.cycleTime = toNs(gcl.adminCycleTime);
    taprioConf.schedule.reserve(gcl.adminControlListSize);

    for (uint32_t i = 0; i < gcl.adminControlListSize; ++i) {
        const GclEntry_t& entry = gcl.adminControlList[i];
        TaprioSchedEntry taprioEntry{};
        taprioEntry.command = TC_TAPRIO_CMD_SET_GATES;
        taprioEntry.gateMask = entry.gateStatesValue;
        taprioEntry.interval = entry.timeIntervalValue;
        taprioConf.schedule.push_back(taprioEntry);
    }

    return taprioConf;
}
