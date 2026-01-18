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
 * @brief Define a new queueing discipline for a given interface
 * @param netlinkSocket The netlink socket
 */
void QdiscManager::newQdisc(NetlinkSocket& netlinkSocket, const std::string& ifname, TaprioConfig& taprioConfig) {
    struct {
        struct nlmsghdr nh;
        struct tcmsg tcm;
        char attrbuf[4096];
    } req;

    const int if_index = if_nametoindex(ifname.c_str());

    memset(&req, 0, sizeof(req));

    req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg));
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

    int optionsID = builder.addAttribute(&req.nh, TCA_OPTIONS, nullptr, 0);

    tc_mqprio_qopt qopt{};
    qopt.num_tc = taprioConfig.numTc;
    for (size_t i = 0; i < taprioConfig.prioTc.size(); ++i) {
        qopt.prio_tc_map[i] = taprioConfig.prioTc[i];
    }

    for (uint32_t tc = 0; tc < taprioConfig.numTc; ++tc) {
        qopt.count[tc] = 1;
        qopt.offset[tc] = tc;
    }
    builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_PRIOMAP, &qopt, sizeof(qopt));

    // Clock ID
    int32_t clockid = CLOCK_REALTIME;
    builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_CLOCKID, &clockid, sizeof(clockid));

    // Base Time
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t base_time = taprioConfig.baseTime;
    builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_BASE_TIME, &base_time, sizeof(base_time));

    // Cycle Time
    uint64_t cycle_time = taprioConfig.cycleTime;
    builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME, &cycle_time, sizeof(cycle_time));

    // TAPRIO Schedule Entry List
    int entryListID = builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST, nullptr, 0);

    for (const auto& entry : taprioConfig.schedule) {
        int entryId = builder.addAttribute(entryListID, TCA_TAPRIO_SCHED_ENTRY, nullptr, 0);
        builder.addAttribute(entryId, TCA_TAPRIO_SCHED_ENTRY_CMD, &entry.command, sizeof(entry.command));
        builder.addAttribute(entryId, TCA_TAPRIO_SCHED_ENTRY_GATE_MASK, &entry.gateMask, sizeof(entry.gateMask));
        builder.addAttribute(entryId, TCA_TAPRIO_SCHED_ENTRY_INTERVAL, &entry.interval, sizeof(entry.interval));
    }

    netlinkSocket.sendMessage(&req.nh, req.nh.nlmsg_len);
};

/**
 * @brief Deletes the queueing discipline for a given interface
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
};

/**
 * @brief Get the current queueing discipline for a given interface
 */
void QdiscManager::getQdisc(NetlinkSocket& netlinkSocket, const std::string& ifname) {
    struct {
        struct nlmsghdr nlh;
        struct tcmsg tcm;
    } req;

    memset(&req, 0, sizeof(req));

    int if_index = if_nametoindex(ifname.c_str());

    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg));
    req.nlh.nlmsg_type = RTM_GETQDISC;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP;
    req.nlh.nlmsg_seq = 2;
    req.nlh.nlmsg_pid = getpid();

    req.tcm.tcm_family = AF_UNSPEC;
    req.tcm.tcm_ifindex = if_index;
    req.tcm.tcm_handle = 0;
    req.tcm.tcm_parent = TC_H_ROOT;

    netlinkSocket.sendMessage(&req.nlh, req.nlh.nlmsg_len);
};

/**
 * @param sock The Netlink Socket
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
};

void QdiscManager::printTaprioOptions(const rtattr* rta, int len) {
    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        switch (rta->rta_type) {
            case TCA_TAPRIO_ATTR_SCHED_CLOCKID:
                printf("  clockid: %d\n", *(int32_t*)RTA_DATA(rta));
                break;
            case TCA_TAPRIO_ATTR_SCHED_BASE_TIME:
                printf("  base_time: %lu ns\n", *(uint64_t*)RTA_DATA(rta));
                break;
            case TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME:
                printf("  cycle_time: %lu ns\n", *(uint64_t*)RTA_DATA(rta));
                break;
            case TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST: {
                printf("  schedule:\n");
                int elen = RTA_PAYLOAD(rta);
                const rtattr* e = (rtattr*)RTA_DATA(rta);
                for (; RTA_OK(e, elen); e = RTA_NEXT(e, elen)) {
                    if (e->rta_type == TCA_TAPRIO_SCHED_ENTRY) {
                        printTaprioSchedEntry((rtattr*)RTA_DATA(e), RTA_PAYLOAD(e));
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}

void QdiscManager::printTaprioSchedEntry(const rtattr* rta, int len) {
    uint8_t cmd = 0;
    uint32_t gate = 0;
    uint64_t interval = 0;

    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        switch (rta->rta_type) {
            case TCA_TAPRIO_SCHED_ENTRY_CMD:
                cmd = *(uint8_t*)RTA_DATA(rta);
                break;
            case TCA_TAPRIO_SCHED_ENTRY_GATE_MASK:
                gate = *(uint32_t*)RTA_DATA(rta);
                break;
            case TCA_TAPRIO_SCHED_ENTRY_INTERVAL:
                interval = *(uint64_t*)RTA_DATA(rta);
                break;
        }
    }
    printf("    entry: cmd=%u gate_mask=0x%x interval=%lu ns\n", cmd, gate, interval);
}

/**
 * @param sock The Netlink Socket
 */
void QdiscManager::getInterfacesInResponse(const NetlinkSocket& sock, std::vector<ietfInterface_t>& interfacesOut) {
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

        auto it = std::find_if(interfacesOut.begin(), interfacesOut.end(),
                               [ifindex](const ietfInterface_t& iface) {
                                   return iface.ifindex == ifindex;
                               });

        ietfInterface_t* current;
        if (it != interfacesOut.end()) {
            current = &(*it);
        } else {
            interfacesOut.emplace_back();
            current = &interfacesOut.back();
            current->ifindex = ifindex;

            char nameBuf[16];
            if_indextoname(ifindex, nameBuf);
            current->name = nameBuf;
        }


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
                        fillTaprioOptions((rtattr*)RTA_DATA(rta), RTA_PAYLOAD(rta),
                                             current->bridgePort.gateParameterTable);
                    }
                    break;
                default:
                    break;
            }
        }
    }
};

void QdiscManager::fillTaprioOptions(const rtattr* rta, int len, GclConfig_t& toFill) {
    int32_t clockId;
    int64_t baseTime;
    int64_t cycleTime;
    tc_mqprio_qopt prioMapStruct;

    toFill.gateEnabled = true;

    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        switch (rta->rta_type) {
            case TCA_TAPRIO_ATTR_PRIOMAP:
                prioMapStruct = *(tc_mqprio_qopt*)RTA_DATA(rta);
                break;
            case TCA_TAPRIO_ATTR_SCHED_CLOCKID:
                clockId = *(int32_t*)RTA_DATA(rta);
                printf("  clockid: %d\n", clockId);
                break;
            case TCA_TAPRIO_ATTR_SCHED_BASE_TIME:
                baseTime = *(uint64_t*)RTA_DATA(rta);
                printf("  base_time: %lu ns\n", baseTime);
                toFill.operBaseTime = NetconfNetlinkMapper::fromNsToPtp(baseTime);
                break;
            case TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME:
                cycleTime = *(uint64_t*)RTA_DATA(rta);
                printf("  cycle_time: %lu ns\n", cycleTime);
                toFill.operCycleTime = NetconfNetlinkMapper::fromNsToRational(cycleTime);
                break;
            case TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST: {
                toFill.gateEnabled = true;
                printf("  schedule:\n");
                int elen = RTA_PAYLOAD(rta);
                const rtattr* e = (rtattr*)RTA_DATA(rta);
                for (; RTA_OK(e, elen); e = RTA_NEXT(e, elen)) {
                    if (e->rta_type == TCA_TAPRIO_SCHED_ENTRY) {
                        toFill.operDataSet = true;
                        fillTaprioSchedEntry((rtattr*)RTA_DATA(e), RTA_PAYLOAD(e), toFill.operControlList);
                    }
                }
                break;
            }
            case TCA_TAPRIO_ATTR_ADMIN_SCHED:
                printf("  admin schedule:\n   ...\n");
                toFill.adminDataSet = true;
                fillTaprioAdminSched((rtattr*)RTA_DATA(rta), RTA_PAYLOAD(rta), toFill);
                break;
            default:
                break;
        }
    }
}

void QdiscManager::fillTaprioAdminSched(const rtattr* rta, int len, GclConfig_t& toFill) {
for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
    switch (rta->rta_type) {
        case TCA_TAPRIO_ATTR_SCHED_BASE_TIME: {
            uint64_t baseTime = *(uint64_t*)RTA_DATA(rta);
            printf("    admin_base_time: %lu ns\n", baseTime);
            toFill.adminBaseTime = NetconfNetlinkMapper::fromNsToPtp(baseTime);
            break;
        }
        case TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME: {
            uint64_t cycleTime = *(uint64_t*)RTA_DATA(rta);
            printf("    admin_cycle_time: %lu ns\n", cycleTime);
            toFill.adminCycleTime = NetconfNetlinkMapper::fromNsToRational(cycleTime);
            break;
        }
        case TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST: {
            int elen = RTA_PAYLOAD(rta);
            const rtattr* e = (rtattr*)RTA_DATA(rta);
            for (; RTA_OK(e, elen); e = RTA_NEXT(e, elen)) {
                if (e->rta_type == TCA_TAPRIO_SCHED_ENTRY) {
                    // Pass the ADMIN list as the target
                    fillTaprioSchedEntry((rtattr*)RTA_DATA(e), RTA_PAYLOAD(e), toFill.adminControlList);
                }
            }
            break;
        }
    }
}
}



void QdiscManager::fillTaprioSchedEntry(const rtattr* rta, int len, std::vector<GclEntry_t>& toFill) {
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
                currentEntry.operationName = cmd == TC_TAPRIO_CMD_SET_GATES   ? "ieee802-dot1q-sched:set-gate-states"
                                             : cmd == TC_TAPRIO_CMD_SET_AND_HOLD ? "ieee802-dot1q-sched:set-and-hold-mac"
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
    toFill.push_back(currentEntry);
    printf("    entry: cmd=%u gate_mask=0x%x interval=%u ns index=%u\n", cmd, gate, interval, index);
}
