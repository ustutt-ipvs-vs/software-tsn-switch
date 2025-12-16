#ifndef NETCONFNETLINKMAPPER_H
#define NETCONFNETLINKMAPPER_H
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

class NetconfNetlinkMapper {
public:
	struct nlmsghdr nh;
	struct tcmsg tcm;
	char attrbuf[1024];
};

#endif
