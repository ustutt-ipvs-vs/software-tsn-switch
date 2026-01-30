#ifndef ENPRO_LLDPDAEMON_H
#define ENPRO_LLDPDAEMON_H

#include <lldpctl.h>

#include <atomic>
#include <mutex>
#include <sysrepo-cpp/Session.hpp>
#include <thread>

class LldpDaemon {
   public:
    explicit LldpDaemon(sysrepo::Session& operSession);
    ~LldpDaemon();

    void syncInitialNeighbors();
    void startWatching();
    void processEvent(lldpctl_change_t type, lldpctl_atom_t* iface, lldpctl_atom_t* neigh);

    LldpDaemon(const LldpDaemon&) = delete;
    LldpDaemon& operator=(const LldpDaemon&) = delete;

   private:
    sysrepo::Session& m_operSess;
    lldpctl_conn_t* m_queryConn{nullptr};
    lldpctl_conn_t* m_watchConn{nullptr};
    std::thread m_watchThread;
    std::mutex m_sessMutex;
    std::atomic<bool> m_stop{false};

    static std::string getStr(lldpctl_atom_t* atom, lldpctl_key_t key);
    static uint32_t currentTimeMark();

    void refreshPortNeighbors(const std::string& ifName);
};

#endif  // ENPRO_LLDPDAEMON_H