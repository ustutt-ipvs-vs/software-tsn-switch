#ifndef QDISCMANAGER_H
#define QDISCMANAGER_H
#include <NetlinkSocket.h>
#include <linux/rtnetlink.h>
#include <TaprioModel.h>
#include <string>

#define BUFFER_SIZE 8192

class QdiscManager {
public:
	static void setOperationalQdisc(NetlinkSocket& netlink_socket, const std::string& ifname, TaprioConfig& gclConfig);
	static void removeQdisc(NetlinkSocket& netlink_socket, const std::string& ifname);
	void getQdiscInfo(NetlinkSocket& netlink_socket, const std::string& ifname);
	void getAllQdiscInfo(NetlinkSocket& netlink_socket);
	void printKernelResponse(const NetlinkSocket& sock);
	void printTaprioOptions(const rtattr* rta, int len);
	void printTaprioSchedEntry(const rtattr* rta, int len);
};

#endif
