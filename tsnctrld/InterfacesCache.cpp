#include "InterfacesCache.h"

#include <net/if.h>

#include "LinkManager.h"
#include "QdiscManager.h"

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
        if (iface.name == name) return &iface;
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
 * @param ethtool_sock A simple socket used to query the kernel for the number of active TX-queues of an interface.
 */
void InterfacesCache::ensureFullLinkData(NetlinkSocket& sock, int ethtool_sock) {
    if (m_fullLinkDumpDone) return;

    // 1. Mark all as stale
    // This is critical. If an interface was deleted in the kernel,
    // the dump won't return it, and we need to know that so we can delete it too.
    for (auto& [idx, iface] : m_interfaces) {
        iface.lastLinkUpdateId = 0;
    }

    // 2. Full Dump
    LinkManager::getAllInterfaces(sock);  // Sends RTM_GETLINK with NLM_F_DUMP

    // 3. Parse Response
    LinkManager::getInterfacesInResponse(sock, m_interfaces, m_currentRequestId);

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
        LinkManager::getActiveQueues(ethtool_sock, iface);
    }

    m_fullLinkDumpDone = true;
}

// ietfInterface_t* InterfacesCache::ensureQdiscData(NetlinkSocket& sock, int ifindex) {
//     ietfInterface_t* iface = ensureLinkData(sock, ifindex);
//     if (!iface) return nullptr;
//
//     // 1. Freshness Check
//     if (iface->lastQdiscUpdateId == m_currentRequestId) {
//         return iface;
//     }
//
//     // 2. Fetch Targeted
//     // Note: QdiscManager::sendGetQdiscTargeted must set tcm_ifindex
//     try {
//         QdiscManager::sendGetQdiscTargeted(sock, ifindex);
//     } catch (...) {
//         return iface;  // Return what we have, even if QDisc fetch failed
//     }
//
//     // 3. Parse & Upsert (Targeted Filter)
//     // Pass ifindex to parser to avoid processing unrelated noise if kernel dumps too much
//     QdiscManager::getInterfacesInResponse(sock, m_interfaces, m_currentRequestId, ifindex);
//
//     return findByIndex(ifindex);
// }
//
// ietfInterface_t* InterfacesCache::ensureQdiscData(NetlinkSocket& sock, const std::string& name) {
//     unsigned int idx = if_nametoindex(name.c_str());
//     if (idx == 0) return nullptr;  // OS doesn't know this name
//     return ensureQdiscData(sock, static_cast<int>(idx));
// }

/**
 * @brief Ensure QDisc Data is fresh for ALL interfaces.
 *
 * Must only be called after @ref setCurrentRequestId().
 *
 * @param sock An instance of a @ref NetlinkSocket which is used to send the message and retrieve the response.
 */
void InterfacesCache::ensureFullQdiscData(NetlinkSocket& sock) {
    if (m_fullQdiscDumpDone) return;

    // 1. Full Dump
    QdiscManager::getAllQdiscInfo(sock);

    // 2. Parse Response (No filter)
    QdiscManager::getInterfacesInResponse(sock, m_interfaces, m_currentRequestId);

    // Note: We do NOT prune here.
    // Absence of QDisc data just means "Default/FIFO", not "Interface Deleted".

    m_fullQdiscDumpDone = true;
}
