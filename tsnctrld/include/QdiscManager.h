#ifndef QDISCMANAGER_H
#define QDISCMANAGER_H
#include <linux/rtnetlink.h>

#include <map>
#include <string>

#include "../../common/include/CncTypes.h"
#include "NetlinkSocket.h"
#include "TaprioModel.h"

constexpr size_t BUFFER_SIZE = 8192;

/**
 * @brief This class is used to query, set/modify, or remove qdiscs of the interfaces present on the host
 *
 * The first use case is querying for data about the qdiscs. For that, use @ref getAllQdiscInfo() or @ref getQdiscInfo()
 * to query the host for the qdiscs of all interfaces or one specific interface respectively. Afterwards you can use
 * @ref getInterfacesInResponse() to populate a map from interface-index to @ref ietfInterface_t struct based on the
 * parsed response.
 *
 * The second usecase is setting/modifying a qdisc for a given interface. For that, you need to first create an instance
 * of th @ref TaprioConfig struct and fill it with appropriate values so that it can then be passed into the @ref
 * setQdisc() function.
 * You need to make sure that your configuration does not conflict with a potentially already existing qdisc on the
 * interface. Specifically, taprio does not support changing the priority->traffic-class mapping without stopping the
 * schedule (aka removing the qdisc).
 *
 * The third usecase is deleting a qdisc on a given interface. The only special consideration necessary, is that
 * attempting to remove a non-existant qdisc causes an error.
 *
 */
class QdiscManager {
   private:
    static void fillTaprioOptions(const rtattr* rta, int len, ietfInterface_t& ifToFill);
    static void fillTaprioAdminSched(const rtattr* rta, int len, ietfInterface_t& ifToFill);
    static void fillTaprioSchedEntry(const rtattr* rta, int len, std::vector<GclEntry_t>& gclEntriesToFill);
    static void parsePriomap(const rtattr* rta, ietfInterface_t& ifToFill);

   public:
    static void setQdisc(NetlinkSocket& netlinkSocket, const std::string& ifname, TaprioConfig& taprioConfig);
    static void removeQdisc(NetlinkSocket& netlinkSocket, const std::string& ifname);
    static void getQdiscInfo(NetlinkSocket& netlinkSocket, const std::string& ifname);
    static void getAllQdiscInfo(NetlinkSocket& netlinkSocket);
    static void getInterfacesInResponse(const NetlinkSocket& sock, std::map<int, ietfInterface_t>& interfacesMap,
                                        uint32_t currentReqId);
};

#endif
