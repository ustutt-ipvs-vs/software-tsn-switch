#include "InterfacesCache.h"

#include <net/if.h>
#include <unistd.h>

#include <stdexcept>

#include "LinkManager.h"
#include "PerformanceLogger.h"
#include "QdiscManager.h"

/**
 * @brief Constructor for the InterfacesCache. Creates the required socket.
 */
InterfacesCache::InterfacesCache() {
    m_ethtool_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_ethtool_sock < 0) {
        throw std::runtime_error("Could not open ethtool socket");
    }
}

/**
 * @brief Destructor for the InterfacesCache, ensures the socket is closed.
 */
InterfacesCache::~InterfacesCache() {
    if (m_ethtool_sock >= 0) {
        close(m_ethtool_sock);
    }
}

/**
 * @brief Get a pointer to the @ref ietfInterface_t with a given ifindex present in the cache. nullptr if not found.
 *
 * @param ifindex The index of the interface which is to be retrieved from the cache.
 * @return A pointer to the interface with the given name. Returns a nullptr if the index was not found.
 */
ietfInterface_t* RequestContext::getInterface(int ifindex) {
    auto it = m_interfaces.find(ifindex);
    return (it != m_interfaces.end()) ? &it->second : nullptr;
}
/**
 * @brief Get a pointer to the @ref ietfInterface_t with a given name present in the cache. nullptr if not found.
 *
 * @param name The name of the interface which is to be retrieved from the cache.
 * @return A pointer to the interface with the given name. Returns a nullptr if the name was not found.
 */
ietfInterface_t* RequestContext::getInterface(const std::string& name) {
    for (auto& [idx, iface] : m_interfaces) {
        if (iface.name == name) {
            return &iface;
        }
    }
    return nullptr;
}
/**
 * @brief Returns all interfaces currently in the cache.
 * @return A reference to the entire cached map of interface-index to @ref ietfInterface_t struct
 */
std::map<int, ietfInterface_t>& RequestContext::getAllInterfaces() {
    SPDLOG_DEBUG("[IFCACHE] [GET_IF] cached_req={}", m_reqId);
    return m_interfaces;
}

/**
 * @brief Used to ensure an @ref IetfInterface_t for a given ifindex is present in the cache with fresh link data and
 * return a pointer to it.
 *
 * @param sock An instance of a @ref NetlinkSocket which is used to send the message and retrieve the response.
 * @param ifindex The index of the interface to return
 * @return Returns a pointer to the cached struct, or nullptr if the ifindex is unknown.
 */
ietfInterface_t* RequestContext::ensureLinkData(NetlinkSocket& sock, int ethtool_sock, int ifindex) {
    PERFORMANCE_LOGGING("[IFCACHE] [LINK_SINGLE_IDX]", "Start req={} ifindex={}", m_reqId, ifindex);
    std::lock_guard<std::mutex> lock(m_mutex);
    ietfInterface_t* iface = getInterface(ifindex);

    // 1. Freshness Check
    if ((iface != nullptr) && iface->lastLinkUpdateId == m_reqId) {
        PERFORMANCE_LOGGING("[IFCACHE] [LINK_SINGLE_IDX]", "End req={} ifindex={} Cached", m_reqId, ifindex);
        return iface;  // Cache Hit
    }

    // 2. Fetch Targeted
    // Note: RTM_GETLINK always returns full attributes.
    // Partial parsing isn't useful here as the kernel constructs the full message anyway.
    try {
        LinkManager::getInterface(sock, ifindex);
    } catch (...) {
        PERFORMANCE_LOGGING("[IFCACHE] [LINK_SINGLE_IDX]", "End req={} ifindex={} Error", m_reqId, ifindex);
        return nullptr;  // Interface likely doesn't exist
    }

    // 3. Parse & Upsert
    LinkManager::getInterfacesInResponse(sock, m_interfaces, m_reqId);
    ietfInterface_t* iface2 = getInterface(ifindex);
    if (iface2 != nullptr) {
        PERFORMANCE_LOGGING("[IFCACHE] [LINK_SINGLE_IDX] [QS+SPEED]", "Start req={} ifindex={}", m_reqId, ifindex);
        LinkManager::getActiveQueues(ethtool_sock, *iface2);
        LinkManager::getLinkSpeed(ethtool_sock, *iface2);
        PERFORMANCE_LOGGING("[IFCACHE] [LINK_SINGLE_IDX] [QS+SPEED]", "End req={} ifindex={}", m_reqId, ifindex);
    }
    PERFORMANCE_LOGGING("[IFCACHE] [LINK_SINGLE_IDX]", "End req={} ifindex={}", m_reqId, ifindex);
    return iface2;
}

/**
 * @brief Used to ensure an @ref IetfInterface_t for a given name is present in the cache with fresh link data and
 * return a pointer to it.
 *
 * @param sock An instance of a @ref NetlinkSocket which is used to send the message and retrieve the response.
 * @param name The name of the interface to return
 * @return Returns a pointer to the cached struct, or nullptr if the name is unknown.
 */
ietfInterface_t* RequestContext::ensureLinkData(NetlinkSocket& sock, int ethtool_sock, const std::string& name) {
    PERFORMANCE_LOGGING("[IFCACHE] [LINK_SINGLE_NAME]", "Start req={} ifname={}", m_reqId, name);
    unsigned int idx = if_nametoindex(name.c_str());
    if (idx == 0) {
        PERFORMANCE_LOGGING("[IFCACHE] [LINK_SINGLE_NAME]", "End req={} ifname={} Unknown ifname", m_reqId, name);
        return nullptr;  // OS doesn't know this name
    }
    PERFORMANCE_LOGGING("[IFCACHE] [LINK_SINGLE_NAME]", "End req={} ifname={}", m_reqId, name);
    return ensureLinkData(sock, ethtool_sock, static_cast<int>(idx));
}

/**
 * @brief Ensure Link Data is fresh for ALL interfaces.
 *
 * Performs a Full Dump and PRUNES interfaces that no longer exist.
 * Must only be called after @ref setCurrentRequestId().
 *
 * @param sock An instance of a @ref NetlinkSocket which is used to send the message and retrieve the response.
 */
void RequestContext::ensureFullLinkData(NetlinkSocket& sock, int ethtool_sock) {
    std::lock_guard<std::mutex> lock(m_mutex);
    PERFORMANCE_LOGGING("[IFCACHE] [LINK]", "Start req={}", m_reqId);
    if (m_fullLinkDumpDone) {
        PERFORMANCE_LOGGING("[IFCACHE] [LINK]", "End req={} Already fresh", m_reqId);
        return;
    }

    // 1. Mark all as stale
    // This is critical. If an interface was deleted in the kernel,
    // the dump won't return it, and we need to know that so we can delete it too.
    for (auto& [idx, iface] : m_interfaces) {
        iface.lastLinkUpdateId = 0;
    }

    // 2. Full Dump
    LinkManager::getAllInterfaces(sock);  // Sends RTM_GETLINK with NLM_F_DUMP

    // 3. Parse Response
    LinkManager::getInterfacesInResponse(sock, m_interfaces, m_reqId);

    // 4. Prune Dead Interfaces
    // If lastLinkUpdateId wasn't updated to currentReqId, the kernel didn't report it.
    for (auto it = m_interfaces.begin(); it != m_interfaces.end();) {
        if (it->second.lastLinkUpdateId != m_reqId) {
            it = m_interfaces.erase(it);  // Erase invalidates only this iterator
        } else {
            ++it;
        }
    }

    PERFORMANCE_LOGGING("[IFCACHE] [LINK] [QS+SPEED]", "Start req={}", m_reqId);
    for (auto& [id, iface] : m_interfaces) {
        PERFORMANCE_LOGGING("[IFCACHE] [LINK] [QS+SPEED]", "Start req={} ifname={}", m_reqId, iface.name);
        LinkManager::getActiveQueues(ethtool_sock, iface);
        LinkManager::getLinkSpeed(ethtool_sock, iface);
        PERFORMANCE_LOGGING("[IFCACHE] [LINK] [QS+SPEED]", "End req={} ifname={}", m_reqId, iface.name);
    }
    PERFORMANCE_LOGGING("[IFCACHE] [LINK] [QS+SPEED]", "End req={}", m_reqId);

    m_fullLinkDumpDone = true;
    PERFORMANCE_LOGGING("[IFCACHE] [LINK]", "End req={}", m_reqId);
}

/**
 * @brief Ensure QDisc Data is fresh for ALL interfaces.
 *
 * Must only be called after @ref setCurrentRequestId().
 *
 * @param sock An instance of a @ref NetlinkSocket which is used to send the message and retrieve the response.
 */
void RequestContext::ensureFullQdiscData(NetlinkSocket& sock) {
    std::lock_guard<std::mutex> lock(m_mutex);
    PERFORMANCE_LOGGING("[IFCACHE] [QDISC]", "Start req={}", m_reqId);
    if (m_fullQdiscDumpDone) {
        return;
    }

    // 1. Full Dump
    QdiscManager::getAllQdiscInfo(sock);

    // 2. Parse Response (No filter)
    QdiscManager::getInterfacesInResponse(sock, m_interfaces, m_reqId);

    // Unlike in ensureFullLinkData(), no entries from the list are deleted, only the GPTs for which no schedules are
    // set are invalidated.
    for (auto& [index, currentInterface] : m_interfaces) {
        if (currentInterface.lastQdiscUpdateId != m_reqId) {
            currentInterface.bridgePort.gateParameterTable.operDataSet = false;
            currentInterface.bridgePort.gateParameterTable.adminDataSet = false;
        }
    }

    m_fullQdiscDumpDone = true;
    PERFORMANCE_LOGGING("[IFCACHE] [QDISC]", "End req={}", m_reqId);
}

std::shared_ptr<RequestContext> InterfacesCache::getRequestContext(uint32_t reqId) {
    std::lock_guard<std::mutex> lock(m_cache_mutex);
    auto now = std::chrono::steady_clock::now();

    // 1. O(1) Garbage Collection: Clean up entries older than CACHE_TTL (5 seconds)
    // Because m_insertion_order tracks exactly when things were added,
    // we only ever need to check the front of the queue.
    while (!m_insertion_order.empty()) {
        uint32_t oldest_reqId = m_insertion_order.front();
        auto it = m_cache.find(oldest_reqId);

        if (it != m_cache.end() && (now - it->second.createdAt > CACHE_TTL)) {
            // It has been 5 seconds. It is 100% safe to delete.
            m_cache.erase(it);
            m_insertion_order.pop();
        } else {
            // If the oldest item in the queue isn't 5 seconds old yet,
            // nothing else behind it is either. We can stop checking instantly.
            break;
        }
    }

    // 2. Check if the current request is already in the cache
    auto it = m_cache.find(reqId);
    if (it != m_cache.end()) {
        return it->second.context;  // Cache Hit
    }

    // 3. Cache Miss: Create new context and record its creation time
    auto new_context = std::make_shared<RequestContext>(reqId);
    m_cache[reqId] = {.context = new_context, .createdAt = now};
    m_insertion_order.push(reqId);

    return new_context;
}