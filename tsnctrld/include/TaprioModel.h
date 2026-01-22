#ifndef ENPRO_TAPRIOMODEL_H
#define ENPRO_TAPRIOMODEL_H
#include <linux/pkt_sched.h>

#include <array>
#include <cstdint>
#include <vector>

struct TaprioSchedEntry {
    uint8_t command;
    uint32_t gateMask;
    uint32_t interval;
};

struct TaprioSchedule {
    int32_t clockid;
    int64_t baseTime;
    int64_t cycleTime;
    int64_t cycleTimeExt;
    std::vector<TaprioSchedEntry> entries;
};

struct TaprioConfig {
    uint32_t numTc;
    std::array<uint8_t, TC_QOPT_BITMASK + 1> prioTc;
    TaprioSchedule admin;
    TaprioSchedule oper;
};

#endif  // ENPRO_TAPRIOMODEL_H
