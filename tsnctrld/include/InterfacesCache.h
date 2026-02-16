#ifndef ENPRO_SWITCH_INTERFACESCACHE_HPP
#define ENPRO_SWITCH_INTERFACESCACHE_HPP

#include <map>
#include <vector>

#include "../../common/include/CncTypes.h"
#include "NetlinkSocket.h"

/**
 * @brief A class to ensure a common and consistent view of the interfaces currently present as well as their associated
 * taprio qdiscs.
 *
 * Accessing data about interfaces should only be done through this class in order to ensure that all actions based on a
 * single request to sysrepo act on the same state. This means that since one request to sysrepo can trigger multiple
 * callbacks in this code, we only want to query netlink once for this request, even if multiple callbacks need to
 * access the data present in the ietfInterface_t struct of the @ref m_interfaces map.
 * Every function that wants to access the current interface data needs to first configure for which sysrepo request it
 * is responsible. This is done using @ref setCurrentRequestId(). Afterwards this function can use @ref
 * ensureFullLinkData() and/or @ref ensureFullQdiscData() to ensure the cache is filled with the data current to this
 * request. If there already exists data for the current request within the cache, nothing more is needed, but if it is
 * missing, the appropriate netlink request is sent and its response parsed.
 *
 * Accessing the current interfaces from the cache can be done via any of the two @ref getInterface() or the @ref
 * getAllInterfaces() functions.
 */
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
    void ensureFullLinkData(NetlinkSocket& sock, int ethtool_sock);

    // ietfInterface_t* ensureQdiscData(NetlinkSocket& sock, int ifindex);
    // ietfInterface_t* ensureQdiscData(NetlinkSocket& sock, const std::string& name);
    void ensureFullQdiscData(NetlinkSocket& sock);
};

#endif  // ENPRO_SWITCH_INTERFACESCACHE_HPP
