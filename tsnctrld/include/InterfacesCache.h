#ifndef ENPRO_SWITCH_INTERFACESCACHE_HPP
#define ENPRO_SWITCH_INTERFACESCACHE_HPP

#include <chrono>
#include <map>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <vector>

#include "CncTypes.h"
#include "NetlinkSocket.h"

/**
 * @brief A class to ensure a common and consistent view of the interfaces currently present as well as their associated
 * taprio qdiscs.
 *
 * This means that since one request to sysrepo can trigger multiple callbacks in this code, we only want to query
 * netlink once for this request, even if multiple callbacks need to access the data present in the ietfInterface_t
 * struct of the @ref m_interfaces map. Every function that wants to access the interface data current to its own
 * request needs to first request its context via @ref InterfacesCache::getRequestContext(). Afterwards this returned
 * context can use @ref ensureFullLinkData() and/or @ref ensureFullQdiscData() to ensure the cache is filled with the
 * data current to this request. If there already exists data for the current request within the cache, nothing more is
 * needed, but if it is missing, the appropriate netlink request is sent and its response parsed.
 *
 * Accessing the current interfaces from the cache can be done via any of the two @ref getInterface() or the @ref
 * getAllInterfaces() functions.
 */
class RequestContext {
   private:
    std::mutex m_mutex;
    std::map<int, ietfInterface_t> m_interfaces;

    uint32_t m_reqId;
    bool m_fullLinkDumpDone = false;
    bool m_fullQdiscDumpDone = false;

    const std::chrono::steady_clock::time_point m_createdAt;

   public:
    // Initialize m_createdAt in the constructor's initializer list
    explicit RequestContext(uint32_t reqId) : m_reqId(reqId), m_createdAt(std::chrono::steady_clock::now()) {
    }

    std::chrono::steady_clock::time_point getCreatedAt() const {
        return m_createdAt;
    }

    ietfInterface_t* getInterface(int ifindex);
    ietfInterface_t* getInterface(const std::string& name);
    std::map<int, ietfInterface_t>& getAllInterfaces();

    ietfInterface_t* ensureLinkData(NetlinkSocket& sock, int ethtool_sock, int ifindex);
    ietfInterface_t* ensureLinkData(NetlinkSocket& sock, int ethtool_sock, const std::string& name);

    void ensureFullLinkData(NetlinkSocket& sock, int ethtool_sock);
    void ensureFullQdiscData(NetlinkSocket& sock);
};

struct CacheEntry {
    std::shared_ptr<RequestContext> context;
    std::chrono::steady_clock::time_point createdAt;
};

/**
 * @brief A class to help ensure a common and consistent view of the interfaces currently present as well as their
 * associated taprio qdiscs.
 *
 * Accessing data about interfaces should only be done through a @ref RequestContext received via this class. This class
 * holds a cache to ensure all callbacks associated with a request can access the same context, in order to ensure that
 * all actions based on a single request to sysrepo act on the same state.
 */
class InterfacesCache {
   private:
    int m_ethtool_sock;

    std::mutex m_cache_mutex;

    std::queue<uint32_t> m_insertion_order;
    std::unordered_map<uint32_t, CacheEntry> m_cache;

    const std::chrono::seconds CACHE_TTL{5};

   public:
    InterfacesCache();

    ~InterfacesCache();
    std::shared_ptr<RequestContext> getRequestContext(uint32_t reqId);
    int getEthtoolSock() const {
        return m_ethtool_sock;
    }
};

#endif  // ENPRO_SWITCH_INTERFACESCACHE_HPP
