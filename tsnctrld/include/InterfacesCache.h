#ifndef ENPRO_SWITCH_INTERFACESCACHE_HPP
#define ENPRO_SWITCH_INTERFACESCACHE_HPP

#include <map>
#include <vector>

#include "../../common/include/CncTypes.h"
#include "NetlinkSocket.h"

class InterfacesCache {
   private:
    std::map<int, ietfInterface_t> m_interfaces;

    // Track what we have fetched for the CURRENT request
    uint32_t m_currentRequestId = -1;
    bool m_fullLinkDumpDone = false;
    bool m_fullQdiscDumpDone = false;

   public:
    void setCurrentRequestId(uint32_t reqId);
    ietfInterface_t* getInterface(int ifindex);
    ietfInterface_t* getInterface(const std::string& name);
    std::map<int, ietfInterface_t>& getAllInterfaces();

    // ietfInterface_t* ensureLinkData(NetlinkSocket& sock, int ifindex);
    // ietfInterface_t* ensureLinkData(NetlinkSocket& sock, const std::string& name);
    void ensureFullLinkData(NetlinkSocket& sock);

    // ietfInterface_t* ensureQdiscData(NetlinkSocket& sock, int ifindex);
    // ietfInterface_t* ensureQdiscData(NetlinkSocket& sock, const std::string& name);
    void ensureFullQdiscData(NetlinkSocket& sock);
};

#endif  // ENPRO_SWITCH_INTERFACESCACHE_HPP
