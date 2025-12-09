#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <cstring>
#include <linux/if_link.h>
#include <linux/netlink.h>
#include <optional>
#include <ostream>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <linux/pkt_sched.h>
#include <stdexcept>
#include <vector>

#define NLMSG_TAIL(nmsg) \
    ((struct rtattr *) (((char *)(nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

struct attr_builder_item {
	rtattr* attr{};
	std::optional<int> parent_id;
};

std::vector<attr_builder_item*> attrs;
int max_payload_length;
const int BUF_SIZE = 4096;
char buffer[BUF_SIZE];

void clear_attrs_vector() {
	for(attr_builder_item* item : attrs) {
		std::free(item);
	}
	attrs.clear();
}

void add_attr_length(const int attr_builder_id, const int length, nlmsghdr* nlh) {
    attr_builder_item* item = attrs.at(attr_builder_id);

    int added_len = RTA_ALIGN(length);
    item->attr->rta_len += added_len;

    if(item->parent_id.has_value()) {
        add_attr_length(item->parent_id.value(), length, nlh);
    } else {
        const int nlh_len = NLMSG_ALIGN(nlh->nlmsg_len) + item->attr->rta_len;

        if(nlh_len > max_payload_length) {
            throw std::runtime_error("Message exceeds maximum length");
        }

        nlh->nlmsg_len = nlh_len;
    }
}

int insert_attr(attr_builder_item *item, const int type, const void *data, const int len, nlmsghdr* nlh) {
	attrs.emplace_back(item);
	const int id = attrs.size() - 1;
	add_attr_length(id, RTA_LENGTH(len), nlh);
	item->attr->rta_type = type;
	if(data != nullptr) std:memcpy(RTA_DATA(item->attr), data, len);
	return id;
}

int add_attribute(nlmsghdr* nlh, const int type, const void *data, const int len) {
	clear_attrs_vector();
	rtattr* attr = NLMSG_TAIL(nlh);
	return insert_attr(new attr_builder_item{.attr = attr}, type, data, len, nlh);
}

int add_attribute(const int attr_builder_parent_id, const int type, const void *data, const int len, nlmsghdr* nlh) {
	attr_builder_item* parent = attrs.at(attr_builder_parent_id);
	rtattr* attr = (struct rtattr*)((char*)parent->attr + RTA_ALIGN(parent->attr->rta_len));
	return insert_attr(new attr_builder_item{.attr = attr, .parent_id = attr_builder_parent_id}, type, data, len, nlh);
}

rtattr* add_rta_attribute(nlmsghdr* nlh, const int maxlen, const int type, const void *data, const int len) {
	struct rtattr *rta = NLMSG_TAIL(nlh);
	int rtalen = RTA_LENGTH(len);
	if(NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(rtalen) > maxlen) {
		throw std::runtime_error("Message exceeds maximum length");
	}
	rta->rta_type = type;
	rta->rta_len = rtalen;
	if(data != nullptr) memcpy(RTA_DATA(rta), data, len);
	nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(rtalen);
	return rta;
}

int main() {
	const char* dev_name = "enp2s0f0";
	int if_index = if_nametoindex(dev_name);
	struct {
		struct nlmsghdr nh;
		struct tcmsg tcm;
		char attrbuf[1024];
	} req;

	//Socket erstellen
    	int sock_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    	memset(&req, 0, sizeof(req));

	req.nh.nlmsg_len = NLMSG_LENGTH(sizeof(struct tcmsg));
    	req.nh.nlmsg_type = RTM_NEWQDISC;
	req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE | NLM_F_ACK;
    	req.nh.nlmsg_seq = 0;
    	req.nh.nlmsg_pid = getpid();

    	// RTNetlink Header
    	req.tcm.tcm_family = AF_UNSPEC;
    	req.tcm.tcm_ifindex = if_index;
    	req.tcm.tcm_handle = 0x10000;
    	req.tcm.tcm_parent = TC_H_ROOT;

    	// Pointer auf
	nlmsghdr* nlh = &req.nh;
	max_payload_length = BUF_SIZE;

     	// Attribute

	// TCA_KIND = taprio
	add_rta_attribute(&req.nh, sizeof(req.attrbuf), TCA_KIND, "taprio", strlen("taprio") + 1);

	//TCA_OPTIONS container
	int options_id = add_attribute(&req.nh, TCA_OPTIONS, nullptr, 0);

    	struct tc_mqprio_qopt qopt = {};
    	qopt.num_tc = 1;
    	for(int i = 0; i < TC_QOPT_BITMASK + 1; i++) {
       	qopt.prio_tc_map[i] = 0;
    	}

   	qopt.count[0] = 1;
	qopt.offset[0] = 0;
	add_attribute(options_id, TCA_TAPRIO_ATTR_PRIOMAP, &qopt, sizeof(qopt), nlh);

	// Clock id
	int32_t clockid = CLOCK_REALTIME;
	add_attribute(options_id, TCA_TAPRIO_ATTR_SCHED_CLOCKID, &clockid, sizeof(clockid), nlh);

	// Base Time
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	uint64_t base_time = ts.tv_sec * 1000000000LL + ts.tv_nsec + 1000000LL;
	add_attribute(options_id, TCA_TAPRIO_ATTR_SCHED_BASE_TIME, &base_time, sizeof(base_time), nlh);

	// Cycle Time
	uint64_t cycle_time = 1000000LL;
	add_attribute(options_id, TCA_TAPRIO_ATTR_SCHED_CYCLE_TIME, &cycle_time, sizeof(cycle_time), nlh);

	//taprio sched entry list
	int entry_list_id = add_attribute(options_id, TCA_TAPRIO_ATTR_SCHED_ENTRY_LIST | NLA_F_NESTED, nullptr, 0, nlh);

	uint8_t command1    = 0;
	uint8_t gate_mask1  = 0x01;
	uint64_t interval1  = 500000LL;
	int entry1_id = add_attribute(entry_list_id, TCA_TAPRIO_SCHED_ENTRY | NLA_F_NESTED, nullptr, 0, nlh);
	add_attribute(entry1_id, TCA_TAPRIO_SCHED_ENTRY_CMD, &command1, sizeof(command1), nlh);
	add_attribute(entry1_id, TCA_TAPRIO_SCHED_ENTRY_GATE_MASK, &gate_mask1, sizeof(gate_mask1), nlh);
	add_attribute(entry1_id, TCA_TAPRIO_SCHED_ENTRY_INTERVAL, &interval1, sizeof(interval1), nlh);

	uint8_t command2 = 0;
	uint8_t gate_mask2 = 0x02;
	uint64_t interval2 = 500000LL;
	int entry2_id = add_attribute(entry_list_id, TCA_TAPRIO_SCHED_ENTRY | NLA_F_NESTED, nullptr, 0, nlh);
	add_attribute(entry2_id, TCA_TAPRIO_SCHED_ENTRY_CMD, &command2, sizeof(command2), nlh);
	add_attribute(entry2_id, TCA_TAPRIO_SCHED_ENTRY_GATE_MASK, &gate_mask2, sizeof(gate_mask2), nlh);
	add_attribute(entry2_id, TCA_TAPRIO_SCHED_ENTRY_INTERVAL, &interval2, sizeof(interval2), nlh);

    //Nachricht senden
    struct sockaddr_nl socket_address;
    memset(&socket_address, 0, sizeof(socket_address));
    socket_address.nl_family = AF_NETLINK;
    ssize_t sent_bytes = sendto(sock_fd, &req, req.nh.nlmsg_len, 0, (struct sockaddr *)&socket_address, sizeof(socket_address));

    if (sent_bytes < 0) {
        perror("Fehler beim Senden der Netlink-Nachricht");
    } else {
        std::cout << "Netlink-Nachricht erfolgreich gesendet (" << sent_bytes << " Bytes)." << std::endl;
    }

	char rec_buffer[4096];
	struct sockaddr_nl nladdr;
	socklen_t nladrr_len = sizeof(nladdr);

	ssize_t recv_bytes = recvfrom(sock_fd, rec_buffer, sizeof(rec_buffer), 0, (struct sockaddr *)&nladdr, &nladrr_len);

	if (recv_bytes < 0) {
    		perror("Fehler beim Empfangen");
	} else {
    		struct nlmsghdr *nh;
    		for (nh = (struct nlmsghdr *)rec_buffer;
         	NLMSG_OK(nh, recv_bytes);
         	nh = NLMSG_NEXT(nh, recv_bytes))
    		{
        	if (nh->nlmsg_type == NLMSG_ERROR) {
            	struct nlmsgerr *err = (struct nlmsgerr *)NLMSG_DATA(nh);
            	if (err->error == 0) {
                std::cout << "Erfolgreich erstellt" << std::endl;
            	} else {
                	std::cerr << "Kernel-Fehler: "
                          << strerror(-err->error)
                          << " Code:" << -err->error << std::endl;
            	}
        		}
    		}
	}

        close(sock_fd);
        return 0;
}
