#ifndef ENPRO_TAPRIOMODEL_H
#define ENPRO_TAPRIOMODEL_H
#include <cstdint>
#include <vector>
#include <linux/pkt_sched.h>
#include <array>

struct TaprioSchedEntry {
    uint8_t command;
    uint8_t gateMask;
    uint64_t interval;
};

struct TaprioConfig {
    uint32_t numTc;
    std::array<uint8_t, TC_QOPT_BITMASK + 1> prioTc;
    __clockid_t clockid;
    uint64_t baseTime;
    uint64_t cycleTime;
    std::pmr::vector<TaprioSchedEntry> schedule;
};

#endif //ENPRO_TAPRIOMODEL_H
