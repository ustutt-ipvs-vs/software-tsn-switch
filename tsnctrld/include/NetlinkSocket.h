#ifndef NETLINKSOCKET_H
#define NETLINKSOCKET_H

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <vector>
#include <cstdint>

#define NLMSG_TAIL(nmsg) ((struct rtattr *)(((char *)(nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))
enum : std::uint16_t {
BUFFER_SIZE_REC = 8192
};

class NetlinkSocket {
   private:
    const int socketFd;
    std::vector<nlmsghdr *> response;
    static int connectSocket();
    void saveResponse();
    void clearResponse();

   public:
    NetlinkSocket();
    static rtattr *addRtaAttribute(nlmsghdr *nlh, int maxlen, int type, const void *data, unsigned long len);
    void sendMessage(nlmsghdr *nlh, size_t len);
    [[nodiscard]] std::vector<nlmsghdr *> getResponse() const;
    ~NetlinkSocket();
};

#endif
