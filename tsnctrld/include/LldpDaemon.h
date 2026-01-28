#ifndef ENPRO_LLDPDAEMON_H
#define ENPRO_LLDPDAEMON_H

#include <lldpctl.h>
#include <sysrepo-cpp/Callbacks.hpp>

class LldpDaemon {
public:
    explicit LldpDaemon(sysrepo::Session& session);
    ~LldpDaemon();

    void syncInitialNeighbors();

    LldpDaemon(const LldpDaemon&) = delete;
    LldpDaemon& operator=(const LldpDaemon&) = delete;

private:
    sysrepo::Session& member_session;
    lldpctl_conn_t* m_conn;

    static std::string getStr(lldpctl_atom_t* atom, lldpctl_key_t key);
    static uint32_t currentTimeMark();
};

#endif //ENPRO_LLDPDAEMON_H