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

struct TaprioMaxSDU {
    uint32_t trafficClass;
    uint32_t queueMaxSdu;
    uint32_t preemtible;  // Either 1=TC_FP_EXPRESS or 2=TC_FP_PREEMPTIBLE
};

struct TaprioConfig {
    uint32_t numTc;
    uint32_t numTxQs;
    std::array<uint8_t, TC_QOPT_BITMASK + 1> prioTc;
    std::array<TaprioMaxSDU, 8> maxSDUs;
    TaprioSchedule admin;
};

#endif  // ENPRO_TAPRIOMODEL_H
