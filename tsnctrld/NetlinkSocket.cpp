/**
 * In part taken from @kupkabn implementation
 */
#include "./include/NetlinkSocket.h"

#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "PerformanceLogger.h"

/**
 * @brief Creates and returns a Netlink socket file descriptor.
 *
 * @return int The socket file descriptor, or <0 on failure
 */
int NetlinkSocket::connectSocket() {
    return socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
}

/**
 * @brief Initialization of the NetlinkSocket class with member initialization socket_fd with the return value of
 * connect_socket()
 */
NetlinkSocket::NetlinkSocket() : socketFd(connectSocket()) {
    if (this->socketFd < 0) {
        throw std::runtime_error("Error creating netlink socket.");
    }

    sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;

    if (bind(socketFd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(socketFd);
        throw std::runtime_error("Error binding Netlink socket");
    }

    int enable = 1;
    setsockopt(socketFd, SOL_NETLINK, NETLINK_EXT_ACK, &enable, sizeof(enable));
}

/**
 * @brief Destructor for NetlinkSocket.
 *
 * Clears any saved kernel responses and closes the socket.
 */
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
 * @param len The length of the added attribute
 * @return A pointer to the attributes of the netlink message
 */
rtattr *NetlinkSocket::addRtaAttribute(nlmsghdr *nlh, const int maxlen, const int type, const void *data,
                                       const unsigned long len) {
    rtattr *rta = NLMSG_TAIL(nlh);
    int rtalen = RTA_LENGTH(len);

    if (NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(rtalen) > maxlen) {
        throw std::runtime_error("Message exceeds maximum length");
    }

    rta->rta_type = type;
    rta->rta_len = rtalen;

    if (data != nullptr) {
        memcpy(RTA_DATA(rta), data, len);
    }

    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(rtalen);
    return rta;
}

/**
 * @brief Send a Netlink message to the kernel.
 *
 * Sends the constructed netlink message to the kernel and saves the response.
 * Exits the program if sending fails.
 *
 * @param nlh Pointer to the netlink message header
 * @param len Length of the message in bytes
 */
void NetlinkSocket::sendMessage(nlmsghdr *nlh, const size_t len) {
    PERFORMANCE_LOGGING("[NL] [SEND]", "Start");
    nlh->nlmsg_flags |= NLM_F_ACK;
    struct iovec iov = {.iov_base = nlh, .iov_len = len};
    struct sockaddr_nl kernel = {.nl_family = AF_NETLINK};
    struct msghdr msg = {.msg_name = &kernel,
                         .msg_namelen = sizeof(kernel),
                         .msg_iov = &iov,
                         .msg_iovlen = 1,
                         .msg_control = nullptr,
                         .msg_controllen = 0,
                         .msg_flags = 0};

    if (sendmsg(socketFd, &msg, 0) < 0) {
        throw std::runtime_error("Failed to send message to kernel");
    }
    PERFORMANCE_LOGGING("[NL] [SEND]", "Sent, receiving");

    this->saveResponse();
    PERFORMANCE_LOGGING("[NL] [SEND]", "End");
}

/**
 * @brief Receive and store responses from the kernel.
 *
 * Reads all available messages from the kernel and saves them internally.
 * Handles NLMSG_DONE and NLMSG_ERROR messages.
 */
void NetlinkSocket::saveResponse() {
    std::vector<char> buffer(BUFFER_SIZE_REC);
    ssize_t len;

    this->clearResponse();

    while ((len = recv(this->socketFd, buffer.data(), buffer.size(), MSG_DONTWAIT)) > 0) {
        auto *nlh = reinterpret_cast<struct nlmsghdr *>(buffer.data());

        for (; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE) {
                return;
            }

            if (nlh->nlmsg_type == NLMSG_ERROR) {
                auto *error = (struct nlmsgerr *)NLMSG_DATA(nlh);
                if (error->error == 0) {
                    SPDLOG_DEBUG("Netlink Message accepted");
                } else {
                    std::string extendedError;

                    // Determine how much of the original request the kernel sent back.
                    // If CAPPED is set, the kernel only sent back the 16-byte nlmsghdr.
                    // If NOT CAPPED, the kernel sent back the full nlh->nlmsg_len of the request.
                    uint32_t payload_to_skip = sizeof(int);  // The error code itself
                    if ((nlh->nlmsg_flags & NLM_F_CAPPED) != 0) {
                        payload_to_skip += sizeof(struct nlmsghdr);
                    } else {
                        // error->msg is the header of the original request.
                        // We skip the full length of the original request.
                        payload_to_skip += error->msg.nlmsg_len;
                    }

                    // The attributes start after the error code + original message portion
                    auto *rta = (struct rtattr *)((char *)error + payload_to_skip);
                    uint32_t rta_len = nlh->nlmsg_len - NLMSG_HDRLEN - payload_to_skip;

                    while (RTA_OK(rta, rta_len)) {
                        // NLMSGERR_ATTR_MSG is 1
                        if (rta->rta_type == 1) {
                            extendedError = ": " + std::string((char *)RTA_DATA(rta));
                            break;
                        }
                        rta = RTA_NEXT(rta, rta_len);
                    }

                    throw std::runtime_error("Error in received message with code: " + std::to_string(error->error) +
                                             extendedError);
                }
            }
            auto *nlh_save = (nlmsghdr *)std::malloc(nlh->nlmsg_len);
            std::memcpy(nlh_save, nlh, nlh->nlmsg_len);
            this->response.emplace_back(nlh_save);
        }
    }

    if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("recv failed");
    }
}

/**
 * @brief Clear all saved kernel responses.
 *
 * Frees allocated memory and clears the internal response vector.
 */
void NetlinkSocket::clearResponse() {
    for (nlmsghdr *nlh : this->response) {
        std::free(nlh);
    }
    this->response.clear();
}

/**
 * @brief Get the saved kernel responses.
 *
 * @return std::vector<nlmsghdr *> Vector of pointers to nlmsghdr structures
 */
std::vector<nlmsghdr *> NetlinkSocket::getResponse() const {
    return this->response;
}
