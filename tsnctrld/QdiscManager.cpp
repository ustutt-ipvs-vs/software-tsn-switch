#include <cstring>
#include <iostream>
#include "NetlinkSocket.h"
#include <QdiscManager.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>

/**
 * @brief Define a new queueing discipline for a given interface
 * @param netlinkSocket The netlink socket
 */
void QdiscManager::newQdisc(NetlinkSocket &netlinkSocket) {
	struct {
		struct nlmsghdr nh;
		struct tcmsg tcm;
		char attrbuf[1024];
	} req;

	memset(&req, 0, sizeof(req));
};

/**
 * @brief Deletes the queueing discipline for a given interface
 */
void QdiscManager::removeQdisc(NetlinkSocket &netlinkSocket) {

};

/**
 * @brief Get the current queueing discipline for a given interface
 */
nlmsghdr QdiscManager::getQdisc(NetlinkSocket &netlinkSocket) {

};

int main() {
	std::cout << "Qdisc Manager" << std::endl;
}
