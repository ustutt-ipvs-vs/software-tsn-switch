#ifndef ENPRO_LLDPDAEMON_H
#define ENPRO_LLDPDAEMON_H

#include <lldpctl.h>

#include <atomic>
#include <mutex>
#include <sysrepo-cpp/Session.hpp>
#include <thread>

#include "CncTypes.h"

/**
 * @brief This class provides methods to collect and process information provided by the lldp protocol, based on the
 * IEEE 802.1AB lldp yang model.
 *
 * This class provides two main use cases. @ref syncInitialNeighbors() gathers the lldp neighbor information for all
 * known interfaces. This information is then processed into a IEEE 802.1AB yang model conform structure that is
 * subsequently written into the operational data store. With this, we produce an initial state that can then be updated
 * once the topology of the network changes. To keep the topology consistent, @ref startWatching() starts a background
 * watcher thread, using a callback method from lldp, to watch for changes of all interface neighbors. Once a change
 * occurs,
 * @ref processEvent() is called to process the event, either deleting or adding neighbor information inside the
 * operational data store.
 */
class LldpDaemon {
   public:
    explicit LldpDaemon(sysrepo::Session& operSession);
    ~LldpDaemon();

    void syncInitialNeighbors();
    void startWatching();
    void processEvent(lldpctl_change_t type, lldpctl_atom_t* iface, lldpctl_atom_t* neigh);

    void getConfigData(LldpNode_t& lldp_node);
    void getLocalInfo(LldpNode_t& lldp_node);

    LldpDaemon(const LldpDaemon&) = delete;
    LldpDaemon& operator=(const LldpDaemon&) = delete;

   private:
    sysrepo::Session& m_operSess;
    lldpctl_conn_t* m_queryConn{nullptr};
    lldpctl_conn_t* m_watchConn{nullptr};
    std::thread m_watchThread;
    std::mutex m_sessMutex;
    std::mutex m_queryMutex;
    std::atomic<bool> m_stop{false};

    static std::string getStr(lldpctl_atom_t* atom, lldpctl_key_t key);
    static uint32_t currentTimeMark();

    void refreshPortNeighbors(const std::string& ifName);
    void writeNeighborData(const std::string& base, lldpctl_atom_t* neigh, lldpctl_atom_t* chassis);
};

#endif  // ENPRO_LLDPDAEMON_H