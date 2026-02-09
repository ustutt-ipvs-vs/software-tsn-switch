#include <QdiscManager.h>
#include <linux/netlink.h>
#include <linux/pkt_sched.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iostream>
#include <map>

#include "NestedAttBuilder.h"
#include "NetconfNetlinkMapper.h"
#include "NetlinkSocket.h"

/**
 * @brief Set or replace the admin TAPRIO qdisc on a network interface
 *
 * @param netlinkSocket The NetlinkSocket used to communicate with the kernel
 * @param ifname Name of the interface on which to install the TAPRIO qdisc
 * @param taprioConfig Configuration object defining traffic classes, priorities, and schedule
 */
void QdiscManager::setQdisc(NetlinkSocket& netlinkSocket, const std::string& ifname, TaprioConfig& taprioConfig) {
    struct {
        nlmsghdr nh;
        tcmsg tcm;
        char attrbuf[4096];
    } req{};

    const int if_index = if_nametoindex(ifname.c_str());

    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(tcmsg));
    req.nh.nlmsg_type = RTM_NEWQDISC;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_REPLACE | NLM_F_CREATE | NLM_F_ACK;
    req.nh.nlmsg_seq = 0;
    req.nh.nlmsg_pid = getpid();

    req.tcm.tcm_family = AF_UNSPEC;
    req.tcm.tcm_ifindex = if_index;
    req.tcm.tcm_handle = 0;
    req.tcm.tcm_parent = TC_H_ROOT;

    NestedAttrBuilder builder(sizeof(req.attrbuf));

    netlinkSocket.addRtaAttribute(&req.nh, sizeof(req.attrbuf), TCA_KIND, "taprio", strlen("taprio") + 1);

    int optionsID = builder.addAttribute(&req.nh, TCA_OPTIONS | NLA_F_NESTED, nullptr, 0);

    tc_mqprio_qopt qopt{};
    qopt.num_tc = taprioConfig.numTc;
    for (size_t i = 0; i < taprioConfig.prioTc.size(); ++i) {
        qopt.prio_tc_map[i] = taprioConfig.prioTc[i];
    }

    for (uint32_t tc = 0; tc < taprioConfig.numTc; ++tc) {
        if (taprioConfig.numTc <= taprioConfig.numTxQs) {
            // 1:1 Mapping
            qopt.offset[tc] = tc;
            qopt.count[tc] = 1;
        } else {
            // Multiple TCs share HW queues
            qopt.offset[tc] = tc % taprioConfig.numTxQs;
            qopt.count[tc] = 1;
        }
    }
    builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_PRIOMAP, &qopt, sizeof(qopt));

    // Clock ID
    int32_t clockid = CLOCK_TAI;
    builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_CLOCKID, &clockid, sizeof(clockid));

    // Base Time
    builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_BASE_TIME, &taprioConfig.admin.baseTime,
                         sizeof(taprioConfig.admin.baseTime));

    // Cycle Time
    builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME, &taprioConfig.admin.cycleTime,
                         sizeof(taprioConfig.admin.cycleTime));

    // Cycle Time Extension
    builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME_EXTENSION, &taprioConfig.admin.cycleTimeExt,
                         sizeof(taprioConfig.admin.cycleTimeExt));

    // TAPRIO Schedule Entry List
    int entryListID = builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST | NLA_F_NESTED, nullptr, 0);

    for (const auto& entry : taprioConfig.admin.entries) {
        int entryId = builder.addAttribute(entryListID, TCA_TAPRIO_SCHED_ENTRY | NLA_F_NESTED, nullptr, 0);
        builder.addAttribute(entryId, TCA_TAPRIO_SCHED_ENTRY_CMD, &entry.command, sizeof(entry.command));
        builder.addAttribute(entryId, TCA_TAPRIO_SCHED_ENTRY_GATE_MASK, &entry.gateMask, sizeof(entry.gateMask));
        builder.addAttribute(entryId, TCA_TAPRIO_SCHED_ENTRY_INTERVAL, &entry.interval, sizeof(entry.interval));
    }

    // Max SDU entries
    for (uint8_t i = 0; i < taprioConfig.numTc; i++) {
        const auto& entry = taprioConfig.maxSDUs[i];
        int sduId = builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_TC_ENTRY | NLA_F_NESTED, nullptr, 0);
        builder.addAttribute(sduId, TCA_TAPRIO_TC_ENTRY_INDEX, &entry.trafficClass, sizeof(entry.trafficClass));
        builder.addAttribute(sduId, TCA_TAPRIO_TC_ENTRY_MAX_SDU, &entry.queueMaxSdu, sizeof(entry.queueMaxSdu));
        builder.addAttribute(sduId, TCA_TAPRIO_TC_ENTRY_FP, &entry.preemtible, sizeof(entry.preemtible));
    }

    netlinkSocket.sendMessage(&req.nh, req.nh.nlmsg_len);
}

/**
 * @brief Delete the TAPRIO queueing discipline from a network interface
 * @param netlinkSocket The NetlinkSocket used to communicate with the kernel
 * @param ifname Name of the interface from which to remove the TAPRIO qdisc
 */
void QdiscManager::removeQdisc(NetlinkSocket& netlinkSocket, const std::string& ifname) {
    struct {
        struct nlmsghdr nh;
        struct tcmsg tcm;
    } req;

    memset(&req, 0, sizeof(req));

    int if_index = if_nametoindex(ifname.c_str());

    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg));
    req.nh.nlmsg_type = RTM_DELQDISC;
    req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    req.nh.nlmsg_seq = 1;
    req.nh.nlmsg_pid = getpid();

    req.tcm.tcm_family = AF_UNSPEC;
    req.tcm.tcm_ifindex = if_index;
    req.tcm.tcm_handle = 0;
    req.tcm.tcm_parent = TC_H_ROOT;

    netlinkSocket.sendMessage(&req.nh, req.nh.nlmsg_len);
}

/**
 * @brief Query the current qdisc configuration for a given interface
 *
 * @param netlinkSocket The NetlinkSocket used to communicate with the kernel
 * @param ifname Name of the interface to query
 */
void QdiscManager::getQdiscInfo(NetlinkSocket& netlinkSocket, const std::string& ifname) {
    struct {
        struct nlmsghdr nlh;
        struct tcmsg tcm;
    } req;

    memset(&req, 0, sizeof(req));
    int if_index = if_nametoindex(ifname.c_str());

    req.nlh.nlmsg_len = sizeof(req);
    req.nlh.nlmsg_type = RTM_GETQDISC;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP;

    req.tcm.tcm_family = AF_UNSPEC;
    req.tcm.tcm_ifindex = if_index;
    req.tcm.tcm_handle = 0;
    req.tcm.tcm_parent = TC_H_ROOT;

    netlinkSocket.sendMessage(&req.nlh, req.nlh.nlmsg_len);

    for (const nlmsghdr* nlh : netlinkSocket.getResponse()) {
        if (nlh->nlmsg_type != RTM_NEWQDISC && nlh->nlmsg_type != RTM_GETQDISC) continue;

        const tcmsg* tcm = static_cast<const tcmsg*>(NLMSG_DATA(nlh));
        if (tcm->tcm_ifindex != if_index) continue;
        // TODO: Move to actually parsing single qdisc, not just printing
        // printSingleQdisc(nlh);
    }
}

/**
 * @brief Query all qdisc configurations on the system
 *
 * @param netlinkSocket The NetlinkSocket used to communicate with the kernel
 */
void QdiscManager::getAllQdiscInfo(NetlinkSocket& netlinkSocket) {
    struct {
        struct nlmsghdr nlh;
        struct tcmsg tcm;
    } req;

    memset(&req, 0, sizeof(req));

    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg));
    req.nlh.nlmsg_type = RTM_GETQDISC;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP;
    req.nlh.nlmsg_seq = 2;
    req.nlh.nlmsg_pid = getpid();

    req.tcm.tcm_family = AF_UNSPEC;
    req.tcm.tcm_handle = 0;
    req.tcm.tcm_parent = TC_H_ROOT;

    netlinkSocket.sendMessage(&req.nlh, req.nlh.nlmsg_len);
}

/**
 * @brief This function is used to parse the information present in response to an `RTM_GETQDISC` qdisc request into a
 * usable format (a map of index to @ref ietfInterface_t).
 *
 * @param sock The NetlinkSocket instance that contains the response of a previous communication with the kernel.
 * @param interfacesMap A map from interface-index to an @ref ietfInterface_t struct. This map is filled with all
 * required data related to taprio qdiscs. If an instance of @ref ietfInterface_t does not already exist with the
 * correct index, it is created and the populated with the most basic information (index).
 * @param currentReqId RequestId as received from sysrepo. Used to ensure that only one `RTM_GETQDISC` dump request is
 * made per request to sysrepo, no matter how many callbacks activate.
 */
void QdiscManager::getInterfacesInResponse(const NetlinkSocket& sock, std::map<int, ietfInterface_t>& interfacesMap,
                                           uint32_t currentReqId) {
    spdlog::trace("[QM] [Parse Full Response] Iterating over interfaces in response...");
    for (const nlmsghdr* nlh : sock.getResponse()) {
        if (nlh->nlmsg_type != RTM_NEWQDISC && nlh->nlmsg_type != RTM_GETQDISC) {
            continue;
        }

        tcmsg* tcm = (tcmsg*)NLMSG_DATA(nlh);
        int ifindex = tcm->tcm_ifindex;
        spdlog::trace("[QM] [Parse Full Response] Current interface: index={}, handle={}, parent={}", ifindex,
                      tcm->tcm_handle, tcm->tcm_parent);

        ietfInterface_t& current = interfacesMap[ifindex];

        current.lastQdiscUpdateId = currentReqId;
        current.ifindex = ifindex;

        int len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*tcm));
        rtattr* rta = (rtattr*)(((char*)tcm) + NLMSG_ALIGN(sizeof(*tcm)));

        std::string kind;
        for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
            switch (rta->rta_type) {
                case TCA_KIND:
                    kind = (char*)RTA_DATA(rta);
                    spdlog::trace("[QM] [Parse Full Response] kind: {}", kind.c_str());
                    break;
                case TCA_OPTIONS:
                    if (kind == "taprio") {
                        fillTaprioOptions((rtattr*)RTA_DATA(rta), RTA_PAYLOAD(rta), current);
                    }
                    break;
                default:
                    break;
            }
        }
    }
};

/**
 * @brief Given an excerpt from a netlink response, this function interprets this data as the options for a taprio qdisc
 * and parses them into the provided @ref ietfInterface_t.
 * @param rta
 * @param len
 * @param ifToFill
 */
void QdiscManager::fillTaprioOptions(const rtattr* rta, int len, ietfInterface_t& ifToFill) {
    int32_t clockId;
    int64_t baseTime;
    int64_t cycleTime;
    spdlog::trace("[QM] [Parse Taprio] Parsing taprio options");
    BridgePort_t& bpToFill = ifToFill.bridgePort;
    GclConfig_t& gptToFill = bpToFill.gateParameterTable;

    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        switch (rta->rta_type) {
            case TCA_TAPRIO_ATTR_PRIOMAP:
                parsePriomap(rta, ifToFill);
                break;
            case TCA_TAPRIO_ATTR_SCHED_CLOCKID:
                clockId = *(int32_t*)RTA_DATA(rta);
                spdlog::trace("[QM] [Parse Taprio]  clockid: {}", clockId);
                gptToFill.clockId = clockId;
                break;
            case TCA_TAPRIO_ATTR_SCHED_BASE_TIME:
                baseTime = *(uint64_t*)RTA_DATA(rta);
                spdlog::trace("[QM] [Parse Taprio]  base_time: {}", baseTime);
                gptToFill.operBaseTime = NetconfNetlinkMapper::fromNsToPtp(baseTime);
                break;
            case TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME:
                cycleTime = *(uint64_t*)RTA_DATA(rta);
                spdlog::trace("[QM] [Parse Taprio]  cycle_time: {}", cycleTime);
                gptToFill.operCycleTime = NetconfNetlinkMapper::fromNsToRational(cycleTime);
                break;
            case TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST: {
                gptToFill.operControlList.clear();
                spdlog::trace("[QM] [Parse Taprio]  schedule:");
                int elen = RTA_PAYLOAD(rta);
                const rtattr* e = (rtattr*)RTA_DATA(rta);
                for (; RTA_OK(e, elen); e = RTA_NEXT(e, elen)) {
                    if (e->rta_type == TCA_TAPRIO_SCHED_ENTRY) {
                        fillTaprioSchedEntry((rtattr*)RTA_DATA(e), RTA_PAYLOAD(e), gptToFill.operControlList);
                    }
                }
                gptToFill.gateEnabled = true;
                gptToFill.operDataSet = true;
                break;
            }
            case TCA_TAPRIO_ATTR_ADMIN_SCHED:
                spdlog::trace("[QM] [Parse Taprio]  admin schedule:");
                gptToFill.adminDataSet = true;
                fillTaprioAdminSched((rtattr*)RTA_DATA(rta), RTA_PAYLOAD(rta), ifToFill);
                break;
            default:
                break;
        }
    }
}

/**
 * @brief Given an excerpt from a netlink response, this function interprets this data as the optional admin data of a
 * taprio qdisc and parses them into the provided @ref ietfInterface_t.
 *
 * @param rta
 * @param len
 * @param ifToFill
 */
void QdiscManager::fillTaprioAdminSched(const rtattr* rta, int len, ietfInterface_t& ifToFill) {
    BridgePort_t& bpToFill = ifToFill.bridgePort;
    GclConfig_t& gptToFill = bpToFill.gateParameterTable;

    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        switch (rta->rta_type) {
            case TCA_TAPRIO_ATTR_SCHED_BASE_TIME: {
                uint64_t baseTime = *(uint64_t*)RTA_DATA(rta);
                spdlog::trace("[QM] [Parse Admin]    admin_base_time: {}ns", baseTime);
                gptToFill.adminBaseTime = NetconfNetlinkMapper::fromNsToPtp(baseTime);
                break;
            }
            case TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME: {
                uint64_t cycleTime = *(uint64_t*)RTA_DATA(rta);
                spdlog::trace("[QM] [Parse Admin]    admin_cycle_time: {}ns", cycleTime);
                gptToFill.adminCycleTime = NetconfNetlinkMapper::fromNsToRational(cycleTime);
                break;
            }
            case TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST: {
                gptToFill.adminControlList.clear();
                int elen = RTA_PAYLOAD(rta);
                spdlog::trace("[QM] [Parse Admin]  schedule:");
                const rtattr* e = (rtattr*)RTA_DATA(rta);
                for (; RTA_OK(e, elen); e = RTA_NEXT(e, elen)) {
                    if (e->rta_type == TCA_TAPRIO_SCHED_ENTRY) {
                        // Pass the ADMIN list as the target
                        fillTaprioSchedEntry((rtattr*)RTA_DATA(e), RTA_PAYLOAD(e), gptToFill.adminControlList);
                    }
                }
                break;
            }
        }
    }
}

/**
 * @brief Given an excerpt from a netlink response, this function interprets this data as a schedule for a taprio qdisc
 * and parses this schedule into the provided list of @ref GclEntry_t structs.
 * @param rta
 * @param len
 * @param gclEntriesToFill
 */
void QdiscManager::fillTaprioSchedEntry(const rtattr* rta, int len, std::vector<GclEntry_t>& gclEntriesToFill) {
    uint8_t cmd = 0;
    uint32_t gate = 0;
    uint32_t index = 0;
    uint32_t interval = 0;

    GclEntry_t currentEntry;
    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        switch (rta->rta_type) {
            case TCA_TAPRIO_SCHED_ENTRY_INDEX:
                index = *(uint32_t*)RTA_DATA(rta);
                currentEntry.index = index;
                break;
            case TCA_TAPRIO_SCHED_ENTRY_CMD:
                cmd = *(uint8_t*)RTA_DATA(rta);
                currentEntry.operationName = cmd == TC_TAPRIO_CMD_SET_GATES ? "ieee802-dot1q-sched:set-gate-states"
                                             : cmd == TC_TAPRIO_CMD_SET_AND_HOLD
                                                 ? "ieee802-dot1q-sched:set-and-hold-mac"
                                                 : "ieee802-dot1q-sched:set-and-release-mac";
                break;
            case TCA_TAPRIO_SCHED_ENTRY_GATE_MASK:
                gate = *(uint32_t*)RTA_DATA(rta);
                currentEntry.gateStatesValue = gate;
                break;
            case TCA_TAPRIO_SCHED_ENTRY_INTERVAL:
                interval = *(uint32_t*)RTA_DATA(rta);
                currentEntry.timeIntervalValue = interval;
                break;
        }
    }
    gclEntriesToFill.push_back(currentEntry);
    spdlog::trace("[QM] [Schedule Entry]    cmd={} gate_mask=0x{:02x}={:08b} interval={}ns index={}", cmd, gate, gate,
                  interval, index);
}

/**
 * @brief Parse the Priomap containing the amount of traffic classes, priority mapping & the offset and count of the
 * queues
 * @param rta Pointer to the rtattr containing the tc_mqprio_qopt struct
 */
void QdiscManager::parsePriomap(const rtattr* rta, ietfInterface_t& ifToFill) {
    ifToFill.bridgePort.trafficClassData.mapDataSet = true;
    const auto* qopt = reinterpret_cast<const tc_mqprio_qopt*>(RTA_DATA(rta));
    ifToFill.bridgePort.trafficClassData.numTrafficClasses = qopt->num_tc;
    spdlog::trace("[QM] [Parse Priomap] Number of traffic classes: {}",
                  ifToFill.bridgePort.trafficClassData.numTrafficClasses);
    for (int i = 0; i < 8; ++i) {
        ifToFill.bridgePort.trafficClassData.priorityMap[i] = qopt->prio_tc_map[i];
        spdlog::trace("[QM] [Parse Priomap] Priority {} mapped to traffic class {}", i,
                      ifToFill.bridgePort.trafficClassData.priorityMap[i]);
    }
}
