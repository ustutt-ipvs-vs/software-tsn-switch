#ifndef NETLINKSOCKET_H
#define NETLINKSOCKET_H

#include <cstddef>
#include <vector>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#define NLMSG_TAIL(nmsg) ((struct rtattr *)(((char *)(nmsg)) + NLMSG_ALIGN((nmsg) -> nlmsg_len)))
#define BUFFER_SIZE_REC 8192

class NetlinkSocket {
private:
	const int socketFd;
	std::vector<nlmsghdr*> response;
	int connectSocket();
	void saveResponse();
	void clearResponse();
public:
	NetlinkSocket();
	static rtattr* addRtaAttribute(struct nlmsghdr *nlh, int maxlen, int type, const void *data, int len);
	void sendMessage(nlmsghdr *nlh, size_t len);
	std::vector<nlmsghdr*> getResponse() const;
	~NetlinkSocket();

};

#endif 
