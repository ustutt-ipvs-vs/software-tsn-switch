#ifndef ENPRO_TAPRIOMODEL_H
#define ENPRO_TAPRIOMODEL_H
#include <linux/pkt_sched.h>

#include <array>
#include <cstdint>
#include <vector>

/**
 * @brief Represents one single entry in the schedule, contains what to do at this time, the gate mask, and how long
 * this entry lasts.
 */
struct TaprioSchedEntry {
    uint8_t command;
    uint32_t gateMask;
    uint32_t interval;
};

/**
 * @brief Represents a schedule passed to configure the qdisc. Contains the id of the clock to use, the actual
 * schedule-entries, and other data.
 */
struct TaprioSchedule {
    int32_t clockid;
    int64_t baseTime;
    int64_t cycleTime;
    int64_t cycleTimeExt;
    std::vector<TaprioSchedEntry> entries;
};

/**
 * @brief Represents one configuration entry that defines the maximum SDU of a traffic class, as well as its
 * "preemtibility".
 */
struct TaprioMaxSDU {
    uint32_t trafficClass;
    uint32_t queueMaxSdu;
    uint32_t preemtible;  // Either 1=TC_FP_EXPRESS or 2=TC_FP_PREEMPTIBLE
};

/**
 * @brief Represents the configuration passed to @ref QdiscManager::setQdisc() to configure the qdisc.
 */
struct TaprioConfig {
    uint32_t numTc;
    uint32_t numTxQs;
    std::array<uint8_t, TC_QOPT_BITMASK + 1> prioTc;
    std::array<TaprioMaxSDU, 8> maxSDUs;
    TaprioSchedule admin;
};

#endif  // ENPRO_TAPRIOMODEL_H
