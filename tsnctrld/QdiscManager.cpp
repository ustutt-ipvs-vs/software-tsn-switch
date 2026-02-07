#include <QdiscManager.h>
#include <linux/netlink.h>
#include <linux/pkt_sched.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
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
    req.tcm.tcm_handle = 0x10000;
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
    for (uint32_t tc = 0; tc < taprioConfig.numTc; ++tc) {
        qopt.count[tc] = 1;
        qopt.offset[tc] = tc;
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
    req.tcm.tcm_handle = 0x10000;
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
    req.tcm.tcm_handle = 0x10000;
    req.tcm.tcm_parent = TC_H_ROOT;

    netlinkSocket.sendMessage(&req.nlh, req.nlh.nlmsg_len);

    for (const nlmsghdr* nlh : netlinkSocket.getResponse()) {
        if (nlh->nlmsg_type != RTM_NEWQDISC && nlh->nlmsg_type != RTM_GETQDISC) continue;

        const tcmsg* tcm = static_cast<const tcmsg*>(NLMSG_DATA(nlh));
        if (tcm->tcm_ifindex != if_index) continue;

        printSingleQdisc(nlh);
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
    req.tcm.tcm_handle = 0x10000;
    req.tcm.tcm_parent = TC_H_ROOT;

    netlinkSocket.sendMessage(&req.nlh, req.nlh.nlmsg_len);
    printKernelResponse(netlinkSocket);
}

void QdiscManager::printSingleQdisc(const nlmsghdr* nlh) {
    const tcmsg* tcm = static_cast<const tcmsg*>(NLMSG_DATA(nlh));

    printf("\n");
    printf("ifindex: %d\n", tcm->tcm_ifindex);
    printf("handle:  %x\n", tcm->tcm_handle);
    printf("parent:  %x\n", tcm->tcm_parent);

    int len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*tcm));
    const rtattr* rta = reinterpret_cast<const rtattr*>(reinterpret_cast<const char*>(tcm) + NLMSG_ALIGN(sizeof(*tcm)));
    std::string kind;

    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        switch (rta->rta_type) {
            case TCA_KIND:
                kind = static_cast<const char*>(RTA_DATA(rta));
                printf("kind: %s\n", kind.c_str());
                break;

            case TCA_OPTIONS:
                if (kind == "taprio") {
                    printTaprioOptions(static_cast<const rtattr*>(RTA_DATA(rta)), RTA_PAYLOAD(rta));
                }
                break;
        }
    }
}

/**
 * @brief Print the kernel response messages for qdisc queries
 *
 * @param sock The NetlinkSocket that contains saved kernel responses
 */
void QdiscManager::printKernelResponse(const NetlinkSocket& sock) {
    for (const nlmsghdr* nlh : sock.getResponse()) {
        if (nlh->nlmsg_type != RTM_NEWQDISC && nlh->nlmsg_type != RTM_GETQDISC) {
            continue;
        }

        tcmsg* tcm = (tcmsg*)NLMSG_DATA(nlh);
        printf("\n");
        printf("ifindex: %d\n", tcm->tcm_ifindex);
        printf("handle: %x\n", tcm->tcm_handle);
        printf("parent: %x\n", tcm->tcm_parent);

        int len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*tcm));
        rtattr* rta = (rtattr*)(((char*)tcm) + NLMSG_ALIGN(sizeof(*tcm)));

        std::string kind;
        for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
            switch (rta->rta_type) {
                case TCA_KIND:
                    kind = (char*)RTA_DATA(rta);
                    printf("kind: %s\n", kind.c_str());
                    break;
                case TCA_OPTIONS:
                    if (kind == "taprio") {
                        printTaprioOptions((rtattr*)RTA_DATA(rta), RTA_PAYLOAD(rta));
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

/**
 * @brief Print the options of a TAPRIO qdisc
 *
 * @param rta Pointer to the first rtattr of TAPRIO options
 * @param len Length of the rtattr payload
 */
void QdiscManager::printTaprioOptions(const rtattr* rta, int len) {
    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        switch (rta->rta_type & ~NLA_F_NESTED) {
            case TCA_TAPRIO_ATTR_PRIOMAP:
                printPriomap(rta);
                break;
            case TCA_TAPRIO_ATTR_SCHED_CLOCKID:
                printf("  clockID: %d\n", *(int32_t*)RTA_DATA(rta));
                break;
            case TCA_TAPRIO_ATTR_SCHED_BASE_TIME:
                printf("  baseTime: %lu ns\n", *(uint64_t*)RTA_DATA(rta));
                break;
            case TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME:
                printf("  cycleTime: %lu ns\n", *(uint64_t*)RTA_DATA(rta));
                break;
            case TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME_EXTENSION:
                printf("  cycleTimeExtension: %lu ns\n", *(uint64_t*)RTA_DATA(rta));
            case TCA_TAPRIO_ATTR_TXTIME_DELAY:
                printf("  txTimeDelay: %lu ns\n", *(uint64_t*)RTA_DATA(rta));
            case TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST:
                parseEntryList((rtattr*)RTA_DATA(rta), RTA_PAYLOAD(rta));
                break;
            case TCA_TAPRIO_ATTR_ADMIN_SCHED:
                printf("  --- Admin Schedule ---\n");
                parseAdminSchedule((rtattr*)RTA_DATA(rta), RTA_PAYLOAD(rta));
                break;
        }
    }
}

/**
 * @brief Parse the entry list
 *
 * @param rta Pointer to the first rtattr
 * @param len Length of the rtattr payload
 */
void QdiscManager::parseEntryList(const rtattr* rta, int len) {
    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        if ((rta->rta_type & ~NLA_F_NESTED) == TCA_TAPRIO_SCHED_ENTRY) {
            printTaprioSchedEntry((rtattr*)RTA_DATA(rta), RTA_PAYLOAD(rta));
        }
    }
}

/**
 * @brief Parse the Priomap containing the amount of traffic classes, priority mapping & the offset and count of the
 * queues
 * @param rta Pointer to the rtattr containing the tc_mqprio_qopt struct
 */
void QdiscManager::printPriomap(const rtattr* rta) {
    const auto* qopt = reinterpret_cast<const tc_mqprio_qopt*>(RTA_DATA(rta));
    printf("  priomap: tc %u\n", qopt->num_tc);
    printf("    map: ");
    for (int i = 0; i < 16; ++i) {
        printf("%u%c", qopt->prio_tc_map[i], (i == 15 ? '\n' : ' '));
    }
    printf("    queues: ");
    for (int i = 0; i < qopt->num_tc; ++i) {
        printf("offset %u count %u ", qopt->offset[i], qopt->count[i]);
    }
    printf("\n");
}

/**
 * @brief Print a single TAPRIO schedule entry
 *
 * @param rta Pointer to the rtattr containing the schedule entry
 * @param len Length of the rtattr payload
 */
void QdiscManager::printTaprioSchedEntry(const rtattr* rta, int len) {
    uint8_t cmd = 0;
    uint32_t gate = 0;
    uint64_t interval = 0;

    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        void* data = RTA_DATA(rta);
        switch (rta->rta_type) {
            case TCA_TAPRIO_SCHED_ENTRY_CMD:
                cmd = *reinterpret_cast<uint8_t*>(data);
                break;
            case TCA_TAPRIO_SCHED_ENTRY_GATE_MASK:
                gate = *reinterpret_cast<uint32_t*>(data);
                break;
            case TCA_TAPRIO_SCHED_ENTRY_INTERVAL:
                interval = *reinterpret_cast<uint32_t*>(data);
                break;
            default:
                break;
        }
    }
    printf("    entry: cmd=%u gate_mask=0x%x interval=%lu ns\n", cmd, gate, interval);
}

void QdiscManager::parseAdminSchedule(const rtattr* rta, int len) {
    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        switch (rta->rta_type) {
            case TCA_TAPRIO_ATTR_SCHED_BASE_TIME:
                printf("  adminBaseTime: %lu ns\n", *(uint64_t*)RTA_DATA(rta));
                break;
            case TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME:
                printf("  adminCycleTime: %lu ns\n", *(uint64_t*)RTA_DATA(rta));
                break;
            case TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST: {
                int elen = RTA_PAYLOAD(rta);
                const rtattr* e = (rtattr*)RTA_DATA(rta);
                for (; RTA_OK(e, elen); e = RTA_NEXT(e, elen)) {
                    if (e->rta_type == TCA_TAPRIO_SCHED_ENTRY) {
                        printTaprioSchedEntry((rtattr*)RTA_DATA(e), RTA_PAYLOAD(e));
                    }
                }
                break;
            }
        }
    }
}

/**
 * @param sock The Netlink Socket
 */
void QdiscManager::getInterfacesInResponse(const NetlinkSocket& sock, std::map<int, ietfInterface_t>& interfacesMap,
                                           uint32_t currentReqId) {
    for (const nlmsghdr* nlh : sock.getResponse()) {
        if (nlh->nlmsg_type != RTM_NEWQDISC && nlh->nlmsg_type != RTM_GETQDISC) {
            continue;
        }

        tcmsg* tcm = (tcmsg*)NLMSG_DATA(nlh);
        int ifindex = tcm->tcm_ifindex;
        printf("\n");
        printf("ifindex: %d\n", ifindex);
        printf("handle: %x\n", tcm->tcm_handle);
        printf("parent: %x\n", tcm->tcm_parent);

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
                    printf("kind: %s\n", kind.c_str());
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

void QdiscManager::fillTaprioOptions(const rtattr* rta, int len, ietfInterface_t& ifToFill) {
    int32_t clockId;
    int64_t baseTime;
    int64_t cycleTime;

    BridgePort_t& bpToFill = ifToFill.bridgePort;
    GclConfig_t& gptToFill = bpToFill.gateParameterTable;

    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        switch (rta->rta_type) {
            case TCA_TAPRIO_ATTR_PRIOMAP:
                parsePriomap(rta, ifToFill);
                break;
            case TCA_TAPRIO_ATTR_SCHED_CLOCKID:
                clockId = *(int32_t*)RTA_DATA(rta);
                printf("  clockid: %d\n", clockId);
                gptToFill.clockId = clockId;
                break;
            case TCA_TAPRIO_ATTR_SCHED_BASE_TIME:
                baseTime = *(uint64_t*)RTA_DATA(rta);
                printf("  base_time: %lu ns\n", baseTime);
                gptToFill.operBaseTime = NetconfNetlinkMapper::fromNsToPtp(baseTime);
                break;
            case TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME:
                cycleTime = *(uint64_t*)RTA_DATA(rta);
                printf("  cycle_time: %lu ns\n", cycleTime);
                gptToFill.operCycleTime = NetconfNetlinkMapper::fromNsToRational(cycleTime);
                break;
            case TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST: {
                gptToFill.operControlList.clear();
                printf("  schedule:\n");
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
                printf("  admin schedule:\n   ...\n");
                gptToFill.adminDataSet = true;
                fillTaprioAdminSched((rtattr*)RTA_DATA(rta), RTA_PAYLOAD(rta), ifToFill);
                break;
            default:
                break;
        }
    }
}

void QdiscManager::fillTaprioAdminSched(const rtattr* rta, int len, ietfInterface_t& ifToFill) {
    BridgePort_t& bpToFill = ifToFill.bridgePort;
    GclConfig_t& gptToFill = bpToFill.gateParameterTable;

    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        switch (rta->rta_type) {
            case TCA_TAPRIO_ATTR_SCHED_BASE_TIME: {
                uint64_t baseTime = *(uint64_t*)RTA_DATA(rta);
                printf("    admin_base_time: %lu ns\n", baseTime);
                gptToFill.adminBaseTime = NetconfNetlinkMapper::fromNsToPtp(baseTime);
                break;
            }
            case TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME: {
                uint64_t cycleTime = *(uint64_t*)RTA_DATA(rta);
                printf("    admin_cycle_time: %lu ns\n", cycleTime);
                gptToFill.adminCycleTime = NetconfNetlinkMapper::fromNsToRational(cycleTime);
                break;
            }
            case TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST: {
                gptToFill.adminControlList.clear();
                int elen = RTA_PAYLOAD(rta);
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
    printf("    entry: cmd=%u gate_mask=0x%x interval=%u ns index=%u\n", cmd, gate, interval, index);
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
    for (int i = 0; i < 8; ++i) {
        ifToFill.bridgePort.trafficClassData.priorityMap[i] = qopt->prio_tc_map[i];
    }
}
