#ifndef QDISCMANAGER_H
#define QDISCMANAGER_H
#include <NetlinkSocket.h>
#include <TaprioModel.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <string>

#include "../../common/include/CncTypes.h"

#define BUFFER_SIZE 8192

class QdiscManager {
   private:
    static void fillTaprioOptions(const rtattr* rta, int len, GclConfig_t& toFill);
    static void fillTaprioAdminSched(const rtattr* rta, int len, GclConfig_t& toFill);
    static void fillTaprioSchedEntry(const rtattr* rta, int len, std::vector<GclEntry_t>& toFill);

   public:
    static void setQdisc(NetlinkSocket& netlink_socket, const std::string& ifname, TaprioConfig& gclConfig);
    static void removeQdisc(NetlinkSocket& netlink_socket, const std::string& ifname);
    void getQdiscInfo(NetlinkSocket& netlink_socket, const std::string& ifname);
    void getAllQdiscInfo(NetlinkSocket& netlink_socket);
    void printKernelResponse(const NetlinkSocket& sock);
    void printTaprioOptions(const rtattr* rta, int len);
    void printTaprioSchedEntry(const rtattr* rta, int len);
    void printSingleQdisc(const nlmsghdr* nlh);
    void parseAdminSchedule(const rtattr* rta, int len);
    void parseEntryList(const rtattr* rta, int len);
    void parsePriomap(const rtattr* rta);
    static void getInterfacesInResponse(const NetlinkSocket& sock, std::vector<ietfInterface_t>& interfacesOut);
};

#endif
