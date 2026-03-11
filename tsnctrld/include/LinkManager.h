#ifndef ENPRO_SWITCH_LINKMANAGER_H
#define ENPRO_SWITCH_LINKMANAGER_H

#include <NetlinkSocket.h>

#include <map>

#include "../../common/include/CncTypes.h"

/**
 * @brief This class is used to get information about the interfaces currently present on the host.
 *
 * First use @ref getAllInterfaces() or @ref getInterface() to query the host for all interfaces or one specific
 * interface respectively. Afterwards you can use @ref getInterfacesInResponse() to populate a map from interface-index
 * to @ref ietfInterface_t struct based on the parsed response. Use @ref getActiveQueues() to ensure the @ref
 * ietfInterface_t struct passed as parameter knows the correct number of currently active Transmission-Queues.
 */
class LinkManager {
   public:
    static void getAllInterfaces(NetlinkSocket& netlinkSocket);
    static void getInterface(NetlinkSocket& netlinkSocket, int ifindex);
    static void getInterfacesInResponse(const NetlinkSocket& sock, std::map<int, ietfInterface_t>& interfacesMap,
                                        uint32_t currentReqId);
    static void getActiveQueues(int sock, ietfInterface_t& iface);
    static void getLinkSpeed(int sock, ietfInterface_t& iface);
};

#endif  // ENPRO_SWITCH_LINKMANAGER_H
