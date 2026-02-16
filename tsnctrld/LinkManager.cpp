#include "LinkManager.h"

#include <linux/ethtool_netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <spdlog/spdlog.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

/* RFC 2863 operational status
 * Taken from linux/if.h
 * */
enum {
    IF_OPER_UNKNOWN,
    IF_OPER_NOTPRESENT,
    IF_OPER_DOWN,
    IF_OPER_LOWERLAYERDOWN,
    IF_OPER_TESTING,
    IF_OPER_DORMANT,
    IF_OPER_UP,
};

// Mapping Linux Kernel State to IETF YANG OperStatus
static OperStatus mapOperState(uint8_t kernelState) {
    // Linux constants from <linux/if.h> (RFC 2863)
    // IF_OPER_UNKNOWN=0, NOTPRESENT=1, DOWN=2, LOWERLAYERDOWN=3,
    // TESTING=4, DORMANT=5, UP=6

    switch (kernelState) {
        case 6:
            return OperStatus::UP;  // IF_OPER_UP
        case 2:
            return OperStatus::DOWN;  // IF_OPER_DOWN
        case 4:
            return OperStatus::TESTING;  // IF_OPER_TESTING
        case 5:
            return OperStatus::DORMANT;  // IF_OPER_DORMANT
        case 1:
            return OperStatus::NOT_PRESENT;  // IF_OPER_NOTPRESENT
        case 3:
            return OperStatus::LOWER_LAYER_DOWN;  // IF_OPER_LOWERLAYERDOWN
        default:
            return OperStatus::UNKNOWN;
    }
}

// Helper to parse nested IFLA_LINKINFO to find the "Kind" (e.g., "bridge")
static const char* getLinkKindRaw(struct rtattr* rta, int len) {
    for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
        if (rta->rta_type == IFLA_INFO_KIND) {
            return (const char*)RTA_DATA(rta);
        }
    }
    return nullptr;
}

/**
 * @brief Query all network interfaces (links) on the system
 *
 * @param netlinkSocket The NetlinkSocket used to communicate with the kernel
 */
void LinkManager::getAllInterfaces(NetlinkSocket& netlinkSocket) {
    struct {
        struct nlmsghdr nlh;
        struct ifinfomsg ifm;
    } req;

    memset(&req, 0, sizeof(req));

    // 1. Setup Netlink Header
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    req.nlh.nlmsg_type = RTM_GETLINK;  // <--- The command to get interfaces
    // NLM_F_DUMP is crucial: it tells kernel to return ALL interfaces, not just one
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_DUMP;
    req.nlh.nlmsg_seq = 1;
    req.nlh.nlmsg_pid = getpid();

    // 2. Setup Payload (ifinfomsg)
    // AF_UNSPEC allows us to see all interfaces regardless of protocol state
    req.ifm.ifi_family = AF_UNSPEC;

    // For a dump request, other fields (ifi_index, flags) are usually left as 0

    // 3. Send and Handle Response
    netlinkSocket.sendMessage(&req.nlh, req.nlh.nlmsg_len);
}

/**
 * @brief Query the host for information about a single interface.
 * @param netlinkSocket The NetlinkSocket used to communicate with the kernel. The response must be read from this
 * instance.
 * @param ifindex The index of the desired interface.
 */
void LinkManager::getInterface(NetlinkSocket& netlinkSocket, int ifindex) {
    struct {
        struct nlmsghdr nlh;
        struct ifinfomsg ifm;
    } req;

    memset(&req, 0, sizeof(req));

    // 1. Setup Netlink Header
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
    req.nlh.nlmsg_type = RTM_GETLINK;  // <--- The command to get interfaces
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    req.nlh.nlmsg_seq = 1;
    req.nlh.nlmsg_pid = getpid();

    // 2. Setup Payload (ifinfomsg)
    // AF_UNSPEC allows us to see all interfaces regardless of protocol state
    req.ifm.ifi_family = AF_UNSPEC;
    req.ifm.ifi_index = ifindex;

    // 3. Send and Handle Response
    netlinkSocket.sendMessage(&req.nlh, req.nlh.nlmsg_len);
}

/**
 * @brief Takes a @ref NetlinkSocket with a stored response and fills the provided @ref interfacesMap with the parsed
 * interfaces.
 *
 * Only some of the attributes returned by netlink are currently parsed, but extendability should be self-explanatory
 * and relatively simple. Make sure to adjust the definition of the @ref ietfInterface_t struct as needed as well.
 * @param sock The NetlinkSocket that contains the response of a previous communication with the kernel.
 * @param interfacesMap A map from interface-index to an @ref ietfInterface_t struct. This map is filled with the
 * interfaces present in the response, creating new instances of the struct as needed.
 * @param currentReqId RequestId as received from sysrepo. Used to ensure that only one `RTM_GETLINK` dump request is
 * made per request to sysrepo, no matter how many callbacks activate.
 */
void LinkManager::getInterfacesInResponse(const NetlinkSocket& sock, std::map<int, ietfInterface_t>& interfacesMap,
                                          uint32_t currentReqId) {
    for (const nlmsghdr* nlh : sock.getResponse()) {
        // We only care about New/Get Link messages
        if (nlh->nlmsg_type != RTM_NEWLINK) {
            continue;
        }

        struct ifinfomsg* ifm = (struct ifinfomsg*)NLMSG_DATA(nlh);
        int ifindex = ifm->ifi_index;

        ietfInterface_t& current = interfacesMap[ifindex];

        current.lastLinkUpdateId = currentReqId;
        current.ifindex = ifindex;

        // 2. Parse Basic Flags
        current.adminEnabled = (ifm->ifi_flags & IFF_UP);
        bool isLoopback = (ifm->ifi_flags & IFF_LOOPBACK);

        // 3. Parse Attributes
        int len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifm));
        rtattr* rta = (struct rtattr*)(((char*)ifm) + NLMSG_ALIGN(sizeof(*ifm)));

        const char* kindRaw = nullptr;  // To store IFLA_INFO_KIND

        for (; RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
            switch (rta->rta_type) {
                case IFLA_IFNAME:
                    current.name = (char*)RTA_DATA(rta);
                    break;

                case IFLA_ADDRESS:
                    if (RTA_PAYLOAD(rta) == 6) {
                        std::memcpy(current.physAddress.data(), RTA_DATA(rta), 6);
                        current.hasPhysAddr = true;
                    }
                    break;

                case IFLA_MTU:
                    current.mtu = *reinterpret_cast<uint32_t*>(RTA_DATA(rta));
                    break;

                case IFLA_OPERSTATE:
                    current.operStatus = mapOperState(*reinterpret_cast<uint8_t*>(RTA_DATA(rta)));
                    break;

                case IFLA_NUM_TX_QUEUES:
                    current.numTxQueues = *reinterpret_cast<uint32_t*>(RTA_DATA(rta));
                    break;

                case IFLA_MASTER:
                    current.bridgePort.masterIndex = *reinterpret_cast<int*>(RTA_DATA(rta));
                    // Resolve Master Name immediately for convenience
                    char masterBuf[IF_NAMESIZE];
                    if (if_indextoname(current.bridgePort.masterIndex, masterBuf)) {
                        current.bridgePort.bridgeName = masterBuf;
                    }
                    break;

                case IFLA_LINKINFO:
                    kindRaw = getLinkKindRaw((rtattr*)RTA_DATA(rta), RTA_PAYLOAD(rta));
                    break;

                    // Handle other attributes if strictly necessary for debug
                    // case IFLA_STATS: ...
            }
        }

        // 4. Deduce Type (IANA Identity)
        if (isLoopback) {
            current.type = IfType::LOOPBACK;
        } else if (kindRaw != nullptr) {
            if (strcmp(kindRaw, "bridge") == 0) {
                current.type = IfType::BRIDGE;
            } else if (strcmp(kindRaw, "bond") == 0) {
                current.type = IfType::LAG;
            } else if (strcmp(kindRaw, "veth") == 0) {
                current.type = IfType::ETHERNET;
            } else {
                current.type = IfType::ETHERNET;  // Fallback for unknown kinds
            }
        } else {
            current.type = IfType::ETHERNET;  // Physical hardware
        }
    }
}

/**
 * @brief Uses a simple socket to query the kernel how many queues are active for the interface given by @ref iface.
 * @param sock A socket created via `socket(AF_INET, SOCK_DGRAM, 0);`
 * @param iface An @ref ietfInterface_t struct with the `name` set. This instance will be modified and
 * `numActiveTxQueues` will be set to the correct number.
 */
void LinkManager::getActiveQueues(int sock, ietfInterface_t& iface) {
    uint32_t active = 1;
    struct ethtool_channels channels = {};
    struct ifreq ifr = {};

    channels.cmd = ETHTOOL_GCHANNELS;
    strncpy(ifr.ifr_name, iface.name.c_str(), IFNAMSIZ - 1);
    ifr.ifr_data = (char*)&channels;

    if (ioctl(sock, SIOCETHTOOL, &ifr) < 0) {
        SPDLOG_DEBUG("[LM] [Active TX Qs] Interface {} does not support GCHANNELS, assuming 1 queue", iface.name);
        iface.numActiveTxQueues = active;
        return;
    }

    active = (channels.combined_count > 0) ? channels.combined_count : channels.tx_count;
    iface.numActiveTxQueues = active;
}