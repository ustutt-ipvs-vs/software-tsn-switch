#ifndef ENPRO_SWITCH_TSNCTRLD_HPP
#define ENPRO_SWITCH_TSNCTRLD_HPP

#include <libyang-cpp/Context.hpp>
#include <sysrepo-cpp/Changes.hpp>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/Session.hpp>
#include <sysrepo-cpp/Subscription.hpp>

#include "NetconfNetlinkMapper.h"
#include "NetlinkSocket.h"
#include "QdiscManager.h"

enum class GclFillOptions : uint32_t {
    OnlyDefault = 0b00,
    FillAdmin = 0b01,
    FillOper = 0b10,
    Both = FillAdmin | FillOper,
};

// Bitwise boilerplate
inline GclFillOptions operator|(GclFillOptions a, GclFillOptions b) {
    return static_cast<GclFillOptions>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline bool operator&(GclFillOptions a, GclFillOptions b) {
    return static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
}

class tsnctrld {
   private:
    NetlinkSocket m_sock;
    QdiscManager m_qm;
    NetconfNetlinkMapper m_mapper;
    sysrepo::Connection m_conn;
    sysrepo::Session m_sess;
    std::optional<sysrepo::Subscription> m_sub;

    std::vector<ietfInterface_t> m_interfaces;
    struct ifaddrs* m_ifa_cache = nullptr;
    uint32_t m_lastNetlinkId = -1;
    uint32_t m_lastIfAddrsId = -1;
    std::mutex m_cacheMtx;

    std::vector<std::string> m_pathsToReset;

    sysrepo::ErrorCode defaultOperCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                           const std::optional<std::string>& subXPath,
                                           const std::optional<std::string>& requestXPath, uint32_t requestId,
                                           std::optional<libyang::DataNode>& parent);
    sysrepo::ErrorCode operInterfaceCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                             const std::optional<std::string>& subXPath,
                                             const std::optional<std::string>& requestXPath, uint32_t requestId,
                                             std::optional<libyang::DataNode>& parent);
    sysrepo::ErrorCode operBridgeCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                          const std::optional<std::string>& subXPath,
                                          const std::optional<std::string>& requestXPath, uint32_t requestId,
                                          std::optional<libyang::DataNode>& parent);
    sysrepo::ErrorCode operLldpCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                        const std::optional<std::string>& subXPath,
                                        const std::optional<std::string>& requestXPath, uint32_t requestId,
                                        std::optional<libyang::DataNode>& parent);

    sysrepo::ErrorCode defaultChangeCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                             const std::optional<std::string>& subXPath, sysrepo::Event event,
                                             uint32_t requestId);
    sysrepo::ErrorCode changeInterfaceCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                               const std::optional<std::string>& subXPath, sysrepo::Event event,
                                               uint32_t requestId);
    sysrepo::ErrorCode changeBridgeCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                            const std::optional<std::string>& subXPath, sysrepo::Event event,
                                            uint32_t requestId);
    sysrepo::ErrorCode changeGptCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                         const std::optional<std::string>& subXPath, sysrepo::Event event,
                                         uint32_t requestId);
    sysrepo::ErrorCode changeLldpCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                          const std::optional<std::string>& subXPath, sysrepo::Event event,
                                          uint32_t requestId);

    void syncHardwareToRunning();
    void setupSubscriptions();
    ietfInterface_t& getExistingOrNewInterface(const std::string& ifname, std::vector<ietfInterface_t>& interfaces);

    void ensureCurrentNetlinkResponseInterfaces(uint32_t currentRequestId, const std::string& ifname);
    struct ifaddrs* ensureCurrentIfAddrsInterfaces(uint32_t currentRequestId);

    ietfInterface_t* syncInterfaceFromSysrepo(sysrepo::Session sess, const std::string& ifname);

    void resetTriggerLeaf(const std::string& xpath);

   public:
    tsnctrld();
    void initialize();
};

#endif  // ENPRO_SWITCH_TSNCTRLD_HPP
