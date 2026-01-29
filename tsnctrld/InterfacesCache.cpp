#include "InterfacesCache.h"

#include <net/if.h>

#include "LinkManager.h"
#include "QdiscManager.h"

void InterfacesCache::setCurrentRequestId(uint32_t reqId) {
    if (reqId != m_currentRequestId) {
        m_currentRequestId = reqId;
        m_fullLinkDumpDone = false;
        m_fullQdiscDumpDone = false;
    }
}

ietfInterface_t* InterfacesCache::getInterface(int ifindex) {
    auto it = m_interfaces.find(ifindex);
    return (it != m_interfaces.end()) ? &it->second : nullptr;
}

ietfInterface_t* InterfacesCache::getInterface(const std::string& name) {
    for (auto& [idx, iface] : m_interfaces) {
        if (iface.name == name) return &iface;
    }
    return nullptr;
}

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
 * Performs a Full Dump and PRUNES interfaces that no longer exist.
 */
void InterfacesCache::ensureFullLinkData(NetlinkSocket& sock) {
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
