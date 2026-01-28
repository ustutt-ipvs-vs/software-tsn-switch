#ifndef ENPRO_SWITCH_LINKMANAGER_H
#define ENPRO_SWITCH_LINKMANAGER_H

#include <NetlinkSocket.h>

#include <map>

#include "../../common/include/CncTypes.h"

class LinkManager {
   public:
    static void getAllInterfaces(NetlinkSocket& netlinkSocket);
    static void getInterface(NetlinkSocket& netlinkSocket, int ifindex);

    static void printLinkResponse(const NetlinkSocket& sock);
    static void getInterfacesInResponse(const NetlinkSocket& sock, std::map<int, ietfInterface_t>& interfacesMap, uint32_t currentReqId);
};

#endif  // ENPRO_SWITCH_LINKMANAGER_H
