#include <cstring>
#include "NetlinkSocket.h"
#include <QdiscManager.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/pkt_sched.h>
#include <unistd.h>
#include <net/if.h>
#include <cstdint>
#include "NestedAttBuilder.h"
#include "NetconfNetlinkMapper.h"
#include <ctime>
#include <iostream>

/**
 * @brief Set or replace the admin TAPRIO qdisc on a network interface
 *
 * @param netlinkSocket The NetlinkSocket used to communicate with the kernel
 * @param ifname Name of the interface on which to install the TAPRIO qdisc
 * @param gclConfig Configuration object defining traffic classes, priorities, and schedule
 */
void QdiscManager::setQdisc(NetlinkSocket &netlinkSocket, const std::string& ifname, TaprioConfig &gclConfig) {
	struct {
		nlmsghdr nh;
		tcmsg tcm;
		char attrbuf[4096];
	} req{};

	const int if_index = if_nametoindex(ifname.c_str());

	req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(tcmsg));
	req.nh.nlmsg_type = RTM_NEWQDISC;
	req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_REPLACE | NLM_F_CREATE |  NLM_F_ACK;
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
	qopt.num_tc = gclConfig.numTc;
	for (size_t i = 0; i < gclConfig.prioTc.size(); ++i) {
		qopt.prio_tc_map[i] = gclConfig.prioTc[i];
	}

	for (uint32_t tc = 0; tc < gclConfig.numTc; ++tc) {
		qopt.count[tc]  = 1;
		qopt.offset[tc] = tc;
	}
	builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_PRIOMAP, &qopt, sizeof(qopt));

	// Clock ID
	int32_t clockid = CLOCK_TAI;
	builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_CLOCKID, &clockid, sizeof(clockid));

	// Base Time
	builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_BASE_TIME, &gclConfig.admin.baseTime, sizeof(gclConfig.admin.baseTime));

	// Cycle Time
	builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME, &gclConfig.admin.cycleTime, sizeof(gclConfig.admin.cycleTime));

	// Cycle Time Extension
	builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME_EXTENSION, &gclConfig.admin.cycleTimeExt, sizeof(gclConfig.admin.cycleTimeExt));

    //TAPRIO Schedule Entry List
    int entryListID = builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST | NLA_F_NESTED, nullptr, 0);

	for (const auto& entry : gclConfig.admin.entries) {
		int entryId = builder.addAttribute(entryListID, TCA_TAPRIO_SCHED_ENTRY | NLA_F_NESTED, nullptr, 0);
		builder.addAttribute(entryId, TCA_TAPRIO_SCHED_ENTRY_CMD, &entry.command, sizeof(entry.command));
		builder.addAttribute(entryId, TCA_TAPRIO_SCHED_ENTRY_GATE_MASK, &entry.gateMask, sizeof(entry.gateMask));
		builder.addAttribute(entryId, TCA_TAPRIO_SCHED_ENTRY_INTERVAL, &entry.interval, sizeof(entry.interval));
	}

	printf("nlmsg_len = %u\n", req.nh.nlmsg_len);
	netlinkSocket.sendMessage(&req.nh, req.nh.nlmsg_len);
}

/**
 * @brief Delete the TAPRIO queueing discipline from a network interface
 * @param netlinkSocket The NetlinkSocket used to communicate with the kernel
 * @param ifname Name of the interface from which to remove the TAPRIO qdisc
 */
void QdiscManager::removeQdisc(NetlinkSocket &netlinkSocket, const std::string &ifname) {
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
void QdiscManager::getQdiscInfo(NetlinkSocket &netlinkSocket, const std::string &ifname) {
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

	for (const nlmsghdr *nlh : netlinkSocket.getResponse()) {
		if (nlh->nlmsg_type != RTM_NEWQDISC &&
			nlh->nlmsg_type != RTM_GETQDISC)
			continue;

		const tcmsg *tcm = static_cast<const tcmsg *>(NLMSG_DATA(nlh));
		if (tcm->tcm_ifindex != if_index)
			continue;

		printSingleQdisc(nlh);
	}
}

/**
 * @brief Query all qdisc configurations on the system
 *
 * @param netlinkSocket The NetlinkSocket used to communicate with the kernel
 */
void QdiscManager::getAllQdiscInfo(NetlinkSocket &netlinkSocket) {
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

void QdiscManager::printSingleQdisc(const nlmsghdr *nlh) {
	const tcmsg *tcm = static_cast<const tcmsg *>(NLMSG_DATA(nlh));

	printf("\n");
	printf("ifindex: %d\n", tcm->tcm_ifindex);
	printf("handle:  %x\n", tcm->tcm_handle);
	printf("parent:  %x\n", tcm->tcm_parent);

	int len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*tcm));
	const rtattr *rta = reinterpret_cast<const rtattr *>(reinterpret_cast<const char *>(tcm) + NLMSG_ALIGN(sizeof(*tcm)));
	std::string kind;

	for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
		switch (rta->rta_type) {
			case TCA_KIND:
				kind = static_cast<const char *>(RTA_DATA(rta));
				printf("kind: %s\n", kind.c_str());
				break;

			case TCA_OPTIONS:
				if (kind == "taprio") {
					printTaprioOptions(static_cast<const rtattr *>(RTA_DATA(rta)), RTA_PAYLOAD(rta));
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
void QdiscManager::printTaprioOptions(const rtattr* rta, int len)
{
	for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
		switch (rta->rta_type & ~NLA_F_NESTED) {
			case TCA_TAPRIO_ATTR_PRIOMAP:
				parsePriomap(rta);
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
 * @brief Parse the Priomap containing the amount of traffic classes, priority mapping & the offset and count of the queues
 * @param rta Pointer to the rtattr containing the tc_mqprio_qopt struct
 */
void QdiscManager::parsePriomap(const rtattr* rta) {
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
void QdiscManager::printTaprioSchedEntry(const rtattr* rta, int len)
{
	uint8_t  cmd = 0;
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

int main() {
	return 0;
}
