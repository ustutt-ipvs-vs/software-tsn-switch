/**
 * In part taken from @kupkabn implementation
 */
#include "NetlinkSocket.h"
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/socket.h>
#include <stdexcept>
#include <cstdlib>
#include <iostream>

int NetlinkSocket::connectSocket() {
    return socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
}

/**
 * @brief Initialization of the NetlinkSocket class with member initialization socket_fd with the return value of connect_socket()
 * @param 
 * @return
 */
NetlinkSocket::NetlinkSocket(): socketFd(connectSocket()) {
    if(this->socketFd < 0) {
        std::cerr << "Error creating netlink socket." << std::endl;
	exit(-1);
    }

    sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;

    if (bind(socketFd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        std::cerr << "Error binding Netlink socket" << std::endl;
        close(socketFd);
        exit(-1);
    }
}

NetlinkSocket::~NetlinkSocket() {
    this->clearResponse();
    close(this->socketFd);
}

/**
 * @brief Adds a specific attribute to the netlink message header struct and aligns it.
 * @param nlh The netlink message header data structure
 * @param maxlen Maximum length that the netlink message can have
 * @param type The type of attribute
 * @param data The data of the added attribute
 * @param The length of the added attribute
 * @return A pointer to the attributes of the netlink message
 */
rtattr* NetlinkSocket::addRtaAttribute(nlmsghdr *nlh, const int maxlen, const int type, const void *data, const int len) {
    struct rtattr *rta = NLMSG_TAIL(nlh);
    int rtalen = RTA_LENGTH(len);

    if (NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(rtalen) > maxlen) {
        throw std::runtime_error("Message exceeds maximum length");
    }

    rta->rta_type = type;
    rta->rta_len = rtalen;

    if(data != nullptr) memcpy(RTA_DATA(rta), data, len);

    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(rtalen);
    return rta;
}

/**
 * @brief Sends the constructed Netlink Message to the kernel
 * @param nlh Pointer to the netlink message header
 * @param len length of the message
 * @return 
 */

void NetlinkSocket::sendMessage(nlmsghdr *nlh, const size_t len) {
    nlh->nlmsg_flags |= NLM_F_ACK;
    struct iovec iov = { nlh, len };
    struct sockaddr_nl kernel = { .nl_family = AF_NETLINK };
    struct msghdr msg = { &kernel, sizeof(kernel), &iov, 1, NULL, 0, 0 };

    if (sendmsg(socketFd, &msg, 0) < 0) {
        std::cerr << "Failed to send message to kernel" << std::endl;
        exit(-1);
    }

    this->saveResponse();
}

/**
 * @brief Saves the response provided by the kernel
 */
void NetlinkSocket::saveResponse() {
    char buffer[BUFFER_SIZE_REC];
    ssize_t len;

    this->clearResponse();

    while ((len = recv(this->socketFd, buffer, sizeof(buffer), MSG_DONTWAIT)) > 0) {
        struct nlmsghdr *nlh = (struct nlmsghdr *)buffer;

        for (; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE) {
                return;
            }

            if (nlh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr *error = (struct nlmsgerr *)NLMSG_DATA(nlh);
                if(error->error == 0) {
                    std::cout << "Netlink Message accepted" << std::endl;
                } else {
                    std::cerr << "Error in received message with code: " << error->error << std::endl;
                }
            }
            struct nlmsghdr *nlh_save = (nlmsghdr *)std::malloc(nlh->nlmsg_len);
            std::memcpy(nlh_save, nlh, nlh->nlmsg_len);
            this->response.emplace_back(nlh_save);
        }
    }

    if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("recv failed");
    }
}

/**
 * @brief Clears the response data structure
 */
void NetlinkSocket::clearResponse() {
    for(nlmsghdr* nlh : this->response) {
        std::free(nlh);
    }
    this->response.clear();
}

/**
 * @brief Getter for the saved response
 */
std::vector<nlmsghdr *> NetlinkSocket::getResponse() {
    return this->response;
}

