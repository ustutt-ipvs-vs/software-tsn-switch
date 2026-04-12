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
 * @brief Must be called before trying to ensure freshness of the cache or accessing any interface.
 *
 * Does nothing if the requestID is the same as the cached one, but invalidates the cache if the requestID is different.
 *
 * @param reqId The requestID as received from the sysrepo callback
 */
void InterfacesCache::setCurrentRequestId(uint32_t reqId) {
    if (reqId != m_currentRequestId) {
        m_currentRequestId = reqId;
        m_fullLinkDumpDone = false;
        m_fullQdiscDumpDone = false;
    }
}

/**
 * @brief Get a pointer to the @ref ietfInterface_t with a given ifindex present in the cache. nullptr if not found.
 *
 * @param ifindex The index of the interface which is to be retrieved from the cache.
 * @return A pointer to the interface with the given name. Returns a nullptr if the index was not found.
 */
ietfInterface_t* InterfacesCache::getInterface(int ifindex) {
    auto it = m_interfaces.find(ifindex);
    return (it != m_interfaces.end()) ? &it->second : nullptr;
}
/**
 * @brief Get a pointer to the @ref ietfInterface_t with a given name present in the cache. nullptr if not found.
 *
 * @param name The name of the interface which is to be retrieved from the cache.
 * @return A pointer to the interface with the given name. Returns a nullptr if the name was not found.
 */
ietfInterface_t* InterfacesCache::getInterface(const std::string& name) {
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
std::map<int, ietfInterface_t>& InterfacesCache::getAllInterfaces() {
    return m_interfaces;
}

// ietfInterface_t* InterfacesCache::ensureLinkData(NetlinkSocket& sock, int ifindex) {
//     ietfInterface_t* iface = getInterface(ifindex);
//
//     // 1. Freshness Check
//     if (iface && iface->lastLinkUpdateId == m_currentRequestId) {
//         return iface;  // Cache Hit
//     }
//
//     // 2. Fetch Targeted
//     // Note: RTM_GETLINK always returns full attributes.
//     // Partial parsing isn't useful here as the kernel constructs the full message anyway.
//     try {
//         LinkManager::getInterface(sock, ifindex);
//     } catch (...) {
//         return nullptr;  // Interface likely doesn't exist
//     }
//
//     // 3. Parse & Upsert
//     LinkManager::getInterfacesInResponse(sock, m_interfaces, m_currentRequestId);
//
//     return getInterface(ifindex);
// }
//
// ietfInterface_t* InterfacesCache::ensureLinkData(NetlinkSocket& sock, const std::string& name) {
//     unsigned int idx = if_nametoindex(name.c_str());
//     if (idx == 0) return nullptr;  // OS doesn't know this name
//     return ensureLinkData(sock, static_cast<int>(idx));
// }

/**
 * @brief Ensure Link Data is fresh for ALL interfaces.
 *
 * Performs a Full Dump and PRUNES interfaces that no longer exist.
 * Must only be called after @ref setCurrentRequestId().
 *
 * @param sock An instance of a @ref NetlinkSocket which is used to send the message and retrieve the response.
 */
void InterfacesCache::ensureFullLinkData(NetlinkSocket& sock) {
    PERFORMANCE_LOGGING("[IFCACHE] [LINK]", "req={} Start", m_currentRequestId);
    if (m_fullLinkDumpDone) {
        PERFORMANCE_LOGGING("[IFCACHE] [LINK]", "req={} Already fresh", m_currentRequestId);
        return;
    }

    // 1. Mark all as stale
    // This is critical. If an interface was deleted in the kernel,
    // the dump won't return it, and we need to know that so we can delete it too.
    for (auto& [idx, iface] : m_interfaces) {
        iface.lastLinkUpdateId = 0;
    }

    // 2. Full Dump
    PERFORMANCE_LOGGING("[IFCACHE] [LINK]", "req={} Sending query", m_currentRequestId);
    LinkManager::getAllInterfaces(sock);  // Sends RTM_GETLINK with NLM_F_DUMP
    PERFORMANCE_LOGGING("[IFCACHE] [LINK]", "req={} Receiving response", m_currentRequestId);

    // 3. Parse Response
    LinkManager::getInterfacesInResponse(sock, m_interfaces, m_currentRequestId);
    PERFORMANCE_LOGGING("[IFCACHE] [LINK]", "req={} Received, cleaning", m_currentRequestId);

    // 4. Prune Dead Interfaces
    // If lastLinkUpdateId wasn't updated to currentReqId, the kernel didn't report it.
    for (auto it = m_interfaces.begin(); it != m_interfaces.end();) {
        if (it->second.lastLinkUpdateId != m_currentRequestId) {
            it = m_interfaces.erase(it);  // Erase invalidates only this iterator
        } else {
            ++it;
        }
    }

    for (auto& [id, iface] : m_interfaces) {
        PERFORMANCE_LOGGING("[IFCACHE] [LINK]", "req={} Interface={} Getting TX-Queue count", m_currentRequestId,
                            iface.name);

        LinkManager::getActiveQueues(m_ethtool_sock, iface);
        PERFORMANCE_LOGGING("[IFCACHE] [LINK]", "req={} Interface={} Getting nominal speed", m_currentRequestId,
                            iface.name);
        LinkManager::getLinkSpeed(m_ethtool_sock, iface);
    }

    m_fullLinkDumpDone = true;
    PERFORMANCE_LOGGING("[IFCACHE] [LINK]", "req={} End", m_currentRequestId);
}

/**
 * @brief Ensure QDisc Data is fresh for ALL interfaces.
 *
 * Must only be called after @ref setCurrentRequestId().
 *
 * @param sock An instance of a @ref NetlinkSocket which is used to send the message and retrieve the response.
 */
void InterfacesCache::ensureFullQdiscData(NetlinkSocket& sock) {
    PERFORMANCE_LOGGING("[IFCACHE] [QDISC]", "req={} Start", m_currentRequestId);
    if (m_fullQdiscDumpDone) {
        return;
    }

    // 1. Full Dump
    PERFORMANCE_LOGGING("[IFCACHE] [QDISC]", "req={} Sending query", m_currentRequestId);
    QdiscManager::getAllQdiscInfo(sock);
    PERFORMANCE_LOGGING("[IFCACHE] [QDISC]", "req={} Receiving response", m_currentRequestId);

    // 2. Parse Response (No filter)
    QdiscManager::getInterfacesInResponse(sock, m_interfaces, m_currentRequestId);
    PERFORMANCE_LOGGING("[IFCACHE] [QDISC]", "req={} Received, cleaning", m_currentRequestId);

    // Unlike in ensureFullLinkData(), no entries from the list are deleted, only the GPTs for which no schedules are
    // set are invalidated.
    for (auto& [index, currentInterface] : m_interfaces) {
        if (currentInterface.lastQdiscUpdateId != m_currentRequestId) {
            currentInterface.bridgePort.gateParameterTable.operDataSet = false;
            currentInterface.bridgePort.gateParameterTable.adminDataSet = false;
        }
    }

    m_fullQdiscDumpDone = true;
    PERFORMANCE_LOGGING("[IFCACHE] [QDISC]", "req={} End", m_currentRequestId);
}
