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

/**
 * @brief Define a new queueing discipline for a given interface
 * @param netlinkSocket The netlink socket
 */
void QdiscManager::newQdisc(NetlinkSocket &netlinkSocket, const std::string& ifname, TaprioConfig &gclConfig) {
	struct {
		struct nlmsghdr nh;
		struct tcmsg tcm;
		char attrbuf[4096];
	} req;

	const int if_index = if_nametoindex(ifname.c_str());

	memset(&req, 0, sizeof(req));

	req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg));
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

	int optionsID = builder.addAttribute(&req.nh, TCA_OPTIONS, nullptr, 0);

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
	int32_t clockid = CLOCK_REALTIME;
	builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_CLOCKID, &clockid, sizeof(clockid));

	// Base Time
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	uint64_t base_time = ts.tv_sec * 1000000000LL + ts.tv_nsec + 1000000LL;
	builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_BASE_TIME, &base_time, sizeof(base_time));

	// Cycle Time
	uint64_t cycle_time = 1000000LL;
	builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME, &cycle_time, sizeof(cycle_time));


    //TAPRIO Schedule Entry List
    int entryListID = builder.addAttribute(optionsID, TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST, nullptr, 0);

	for (const auto& entry : gclConfig.schedule) {
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
};

/**
 * @brief Get the current queueing discipline for a given interface
 */
void QdiscManager::getQdisc(NetlinkSocket &netlinkSocket, const std::string &ifname) {
	struct {
		struct nlmsghdr nlh;
		struct tcmsg tcm;
	} req;

	memset(&req, 0, sizeof(req));

	int if_index = if_nametoindex(ifname.c_str());

	req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg));
	req.nlh.nlmsg_type = RTM_GETQDISC;
	req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nlh.nlmsg_seq = 2;
	req.nlh.nlmsg_pid = getpid();
	
	req.tcm.tcm_family = AF_UNSPEC;
	req.tcm.tcm_ifindex = if_index;
	req.tcm.tcm_handle = 0;
	req.tcm.tcm_parent = TC_H_ROOT;

	netlinkSocket.sendMessage(&req.nlh, req.nlh.nlmsg_len);
};

int main() {
	GclConfig_t gclConfig = {};
	static queueMaxSduEntry_t queueMaxSduTable[2] = {
		{ .trafficClass = 0, .queueMaxSdu = 1500, .transmissionOverrun = 0 },
		{ .trafficClass = 1, .queueMaxSdu = 1500, .transmissionOverrun = 0 }
	};
	static GclEntry_t adminGcl[2] = {
		{.index = 0, .gateStatesValue = 0x01, .timeIntervalValue = 500000 },
		{.index = 1, .gateStatesValue = 0x02, .timeIntervalValue = 500000}
	};
	gclConfig.queueMaxSduCount = 8;
	gclConfig.queueMaxSduTable = queueMaxSduTable;
	gclConfig.gateEnabled = true;
	gclConfig.adminGateStates = 0xFF;
	gclConfig.adminCycleTime = {.numerator = 1'000'000, .denominator = 1'000'000'000};
	gclConfig.adminCycleTimeExtensionNs = 0;
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	gclConfig.adminBaseTime = {.seconds = ts.tv_sec, .nanoseconds = ts.tv_nsec};
	gclConfig.adminControlListSize = 2;
	gclConfig.adminControlList = adminGcl;
	gclConfig.operGateStates = 0xFF;
	gclConfig.operCycleTime = gclConfig.adminCycleTime;
	gclConfig.operBaseTime = gclConfig.adminBaseTime;
	gclConfig.operControlListSize = 0;
	gclConfig.operControlList = nullptr;
	gclConfig.configChange = true;

	NetlinkSocket sock;
	QdiscManager qm;
	NetconfNetlinkMapper mapper;
	TaprioConfig taprioConf = mapper.mapToTaprio(gclConfig);
	std::string ifname = "enp2s0f3";

	qm.newQdisc(sock, ifname, taprioConf);

}
