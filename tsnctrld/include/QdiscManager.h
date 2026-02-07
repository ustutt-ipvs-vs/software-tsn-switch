#ifndef QDISCMANAGER_H
#define QDISCMANAGER_H
#include <NetlinkSocket.h>
#include <TaprioModel.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <map>
#include <string>

#include "../../common/include/CncTypes.h"

#define BUFFER_SIZE 8192

class QdiscManager {
   private:
    static void fillTaprioOptions(const rtattr* rta, int len, ietfInterface_t& ifToFill);
    static void fillTaprioAdminSched(const rtattr* rta, int len, ietfInterface_t& ifToFill);
    static void fillTaprioSchedEntry(const rtattr* rta, int len, std::vector<GclEntry_t>& gclEntriesToFill);
    static void printTaprioOptions(const rtattr* rta, int len);
    static void printTaprioSchedEntry(const rtattr* rta, int len);
    static void printSingleQdisc(const nlmsghdr* nlh);
    static void parseAdminSchedule(const rtattr* rta, int len);
    static void parseEntryList(const rtattr* rta, int len);
    static void printPriomap(const rtattr* rta);
    static void parsePriomap(const rtattr* rta, ietfInterface_t& ifToFill);

   public:
    static void setQdisc(NetlinkSocket& netlink_socket, const std::string& ifname, TaprioConfig& taprioConfig);
    static void removeQdisc(NetlinkSocket& netlink_socket, const std::string& ifname);
    static void getQdiscInfo(NetlinkSocket& netlink_socket, const std::string& ifname);
    static void getAllQdiscInfo(NetlinkSocket& netlink_socket);
    static void printKernelResponse(const NetlinkSocket& sock);
    static void getInterfacesInResponse(const NetlinkSocket& sock, std::map<int, ietfInterface_t>& interfacesMap,
                                        uint32_t currentReqId);
};

#endif
