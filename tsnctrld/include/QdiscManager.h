#ifndef QDISCMANAGER_H
#define QDISCMANAGER_H
#include <NetlinkSocket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define BUFFER_SIZE 8192

class QdiscManager {
public:
	static void newQdisc(NetlinkSocket& netlink_socket);
	static void removeQdisc(NetlinkSocket& netlink_socket);
	nlmsghdr getQdisc(NetlinkSocket& netlink_socket);
};

#endif
