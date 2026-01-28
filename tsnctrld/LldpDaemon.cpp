#include "include/LldpDaemon.h"
#include <lldpctl.h>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <sysrepo-cpp/Session.hpp>

static std::string convertCString(const char* cstr) {
    return (cstr != nullptr) ? std::string(cstr) : std::string();
}

std::string LldpDaemon::getStr(lldpctl_atom_t* atom, lldpctl_key_t key) {
    return convertCString(lldpctl_atom_get_str(atom, key));
}

uint32_t LldpDaemon::currentTimeMark() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(
            steady_clock::now().time_since_epoch()).count()
    );
}
LldpDaemon::LldpDaemon(sysrepo::Session& session) : member_session(session), m_conn(nullptr) {
    m_conn = lldpctl_new(nullptr, nullptr, nullptr);
    if (m_conn == nullptr) {
        throw std::runtime_error("Failed to connect to lldpd (lldpctl_new returned nullptr). Is lldpd running?");
    }
}

LldpDaemon::~LldpDaemon() {
    if (m_conn != nullptr) {
        lldpctl_release(m_conn);
        m_conn = nullptr;
    }
}

void LldpDaemon::syncInitialNeighbors() {
    lldpctl_atom_t* iface = nullptr;
    lldpctl_atom_t* ifaces = lldpctl_get_interfaces(m_conn);

    std::cout << "[LLDP] Reading current neighbors from lldpd...\n";

    member_session.switchDatastore(sysrepo::Datastore::Operational);
    member_session.setOriginatorName("tsn-daemon-lldp");

    if (ifaces == nullptr) {
        std::cerr << "[LLDP] Failed to get interfaces\n";
        return;
    }

    lldpctl_atom_foreach(ifaces, iface) {
        const std::string ifName = getStr(iface, lldpctl_k_interface_name);
        lldpctl_atom_t* port = lldpctl_get_port(iface);
        uint32_t remoteIndex = 1;
        lldpctl_atom_t* neigh = nullptr;

        if (port == nullptr) { continue;}

        lldpctl_atom_t* neighbors = lldpctl_atom_get(port, lldpctl_k_port_neighbors);
        if (neighbors == nullptr) {
            lldpctl_atom_dec_ref(port);
            continue;
        }

        lldpctl_atom_foreach(neighbors, neigh) {
            lldpctl_atom_t* chassis = lldpctl_atom_get(neigh, lldpctl_k_port_chassis);
            const uint32_t timeMark = currentTimeMark();
            const std::string chassisId  = (chassis != nullptr) ? getStr(chassis, lldpctl_k_chassis_id)   : "";
            const std::string systemName = (chassis != nullptr) ? getStr(chassis, lldpctl_k_chassis_name) : "";
            const std::string portId     = getStr(neigh, lldpctl_k_port_id);

            const std::string base =
                "/ieee802-dot1ab-lldp:lldp"
                "/port[name='" + ifName + "']"
                "[dest-mac-address='01-80-c2-00-00-0e']"
                "/remote-systems-data"
                "[time-mark='" + std::to_string(timeMark) + "']"
                "[remote-index='" + std::to_string(remoteIndex) + "']";

            // setItem() will create list instances along the path in OPERATIONAL
            member_session.setItem(base + "/chassis-id", chassisId);
            member_session.setItem(base + "/port-id", portId);
            member_session.setItem(base + "/system-name", systemName);

            remoteIndex++;

            if (chassis != nullptr) {
                lldpctl_atom_dec_ref(chassis);
            }
        }

        lldpctl_atom_dec_ref(neighbors);
        lldpctl_atom_dec_ref(port);
    }

    member_session.applyChanges();
    std::cout << "[LLDP] Operational datastore updated\n";
}


//running data store --> configuration info, name, admin-status, dest-mac-address
//operational data store --> config=false, neighbour information
//sudo netopeer2-server -d -v3 --> connects to sysrepo and
//sudo ./tsncrld
//probleme mit pull vs push model
//pull --> onOperGet()
