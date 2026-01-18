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
    static void printTaprioOptions(const rtattr* rta, int len);
    static void printTaprioSchedEntry(const rtattr* rta, int len);

    static void fillTaprioOptions(const rtattr* rta, int len, GclConfig_t& toFill);
    static void fillTaprioAdminSched(const rtattr* rta, int len, GclConfig_t& toFill);
    static void fillTaprioSchedEntry(const rtattr* rta, int len, std::vector<GclEntry_t>& toFill);

   public:
    static void newQdisc(NetlinkSocket& netlink_socket, const std::string& ifname, TaprioConfig& taprioConfig);
    static void removeQdisc(NetlinkSocket& netlink_socket, const std::string& ifname);
    void getQdisc(NetlinkSocket& netlink_socket, const std::string& ifname);
    static void printKernelResponse(const NetlinkSocket& sock);
    static void getInterfacesInResponse(const NetlinkSocket& sock, std::vector<ietfInterface_t>& interfacesOut);
};

#endif
