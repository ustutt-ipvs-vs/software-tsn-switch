#ifndef ENPRO_SWITCH_TSNCTRLD_HPP
#define ENPRO_SWITCH_TSNCTRLD_HPP

#include <libyang-cpp/Context.hpp>
#include <sysrepo-cpp/Changes.hpp>
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/Session.hpp>
#include <sysrepo-cpp/Subscription.hpp>

#include "InterfacesCache.h"
#include "LinkManager.h"
#include "LldpDaemon.h"
#include "NetconfNetlinkMapper.h"
#include "NetlinkSocket.h"
#include "PtpManager.h"
#include "QdiscManager.h"

/**
 * @brief The main class of the "tsnctrld" control daemon for bridging the gap between sysrepo and kernel.
 *
 * Use this class by instantiating it and calling @ref initialize() on this instance. This reads the state of the system
 * and places the relevant data into the `RUNNING` datastore. Afterwards, all necessary callbacks are started. Make sure
 * the program keeps running, eg by starting an infinite while-loop.
 */
class tsnctrld {
   private:
    int m_ethtool_sock;
    NetlinkSocket m_sock;
    QdiscManager m_qm;
    LinkManager m_lm;
    PtpManager m_ptp;
    NetconfNetlinkMapper m_mapper;
    sysrepo::Connection m_conn;
    sysrepo::Session m_sess;
    sysrepo::Session m_operSess;
    std::vector<sysrepo::Subscription> m_subs;
    std::unique_ptr<LldpDaemon> m_lldpDaemon;

    InterfacesCache m_ifcache;

    std::vector<std::string> m_pathsToReset;

    sysrepo::ErrorCode defaultOperCallback(const sysrepo::Session& sess, uint32_t subId, const std::string& moduleName,
                                           const std::optional<std::string>& subXPath,
                                           const std::optional<std::string>& requestXPath, uint32_t requestId,
                                           std::optional<libyang::DataNode>& parent);
    sysrepo::ErrorCode operInterfaceCallback(const sysrepo::Session& sess, uint32_t subId,
                                             const std::string& moduleName, const std::optional<std::string>& subXPath,
                                             const std::optional<std::string>& requestXPath, uint32_t requestId,
                                             std::optional<libyang::DataNode>& parent);
    sysrepo::ErrorCode operBridgeCallback(const sysrepo::Session& sess, uint32_t subId, const std::string& moduleName,
                                          const std::optional<std::string>& subXPath,
                                          const std::optional<std::string>& requestXPath, uint32_t requestId,
                                          std::optional<libyang::DataNode>& parent);
    sysrepo::ErrorCode operBridgePortCallback(const sysrepo::Session& sess, uint32_t subId,
                                              const std::string& moduleName, const std::optional<std::string>& subXPath,
                                              const std::optional<std::string>& requestXPath, uint32_t requestId,
                                              std::optional<libyang::DataNode>& parent);
    sysrepo::ErrorCode operLldpLocalSystemCallback(const sysrepo::Session& sess, uint32_t subId,
                                                   const std::string& moduleName,
                                                   const std::optional<std::string>& subXPath,
                                                   const std::optional<std::string>& requestXPath, uint32_t requestId,
                                                   std::optional<libyang::DataNode>& parent);
    sysrepo::ErrorCode operPtpCallback(const sysrepo::Session& sess, uint32_t subId, const std::string& moduleName,
                                       const std::optional<std::string>& subXPath,
                                       const std::optional<std::string>& requestXPath, uint32_t requestId,
                                       std::optional<libyang::DataNode>& parent);
    sysrepo::ErrorCode operPtpPerformanceCallback(const sysrepo::Session& sess, uint32_t subId,
                                                  const std::string& moduleName,
                                                  const std::optional<std::string>& subXPath,
                                                  const std::optional<std::string>& requestXPath, uint32_t requestId,
                                                  std::optional<libyang::DataNode>& parent);
    sysrepo::ErrorCode operPtpPortPerformanceCallback(const sysrepo::Session& sess, uint32_t subId,
                                                      const std::string& moduleName,
                                                      const std::optional<std::string>& subXPath,
                                                      const std::optional<std::string>& requestXPath,
                                                      uint32_t requestId, std::optional<libyang::DataNode>& parent);

    sysrepo::ErrorCode defaultChangeCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                             const std::optional<std::string>& subXPath, sysrepo::Event event,
                                             uint32_t requestId);
    sysrepo::ErrorCode changeInterfaceCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                               const std::optional<std::string>& subXPath, sysrepo::Event event,
                                               uint32_t requestId);
    sysrepo::ErrorCode changeBridgeCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                            const std::optional<std::string>& subXPath, sysrepo::Event event,
                                            uint32_t requestId);
    sysrepo::ErrorCode changeBridgePortCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                                const std::optional<std::string>& subXPath, sysrepo::Event event,
                                                uint32_t requestId);
    sysrepo::ErrorCode changeGptCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                         const std::optional<std::string>& subXPath, sysrepo::Event event,
                                         uint32_t requestId);
    sysrepo::ErrorCode changeLldpCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                          const std::optional<std::string>& subXPath, sysrepo::Event event,
                                          uint32_t requestId);
    sysrepo::ErrorCode changePtpCallback(sysrepo::Session sess, uint32_t subId, const std::string& moduleName,
                                         const std::optional<std::string>& subXPath, sysrepo::Event event,
                                         uint32_t requestId);

    static int ensureRunningDaemons(const std::vector<std::string>& services);
    static void populateAsBridge(const ietfInterface_t& current, libyang::Context& ctx,
                                 std::optional<libyang::DataNode>& forest);
    static void populateAsTsnCapableInterface(ietfInterface_t& current, libyang::DataNode& if_node);
    static void populateAsInterface(ietfInterface_t& current, libyang::Context& ctx,
                                    std::optional<libyang::DataNode>& forest);

    static void popuplateAsLldpConfiguration(LldpNode_t& current, libyang::Context& ctx,
                                             std::optional<libyang::DataNode>& forest);
    void populatePtpConfig(libyang::Context& ctx, std::optional<libyang::DataNode>& forest);
    void syncHardwareToRunning();
    void setupSubscriptions();
    // ietfInterface_t& getExistingOrNewInterface(const std::string& ifname, std::vector<ietfInterface_t>& interfaces);
    //
    // void ensureCurrentNetlinkGetQdiscResponseInterfaces(uint32_t currentRequestId, const std::string& ifname);
    // void ensureCurrentNetlinkGetLinkResponseInterfaces(uint32_t currentRequestId, const std::string& ifname);
    // struct ifaddrs* ensureCurrentIfAddrsInterfaces(uint32_t currentRequestId);

    ietfInterface_t* syncInterfaceFromSysrepo(sysrepo::Session& sess, const std::string& ifname, uint32_t requestId);

    void resetTriggerLeaf(const std::string& xpath);

    template <typename T>
    T getLeaf(const std::optional<libyang::DataNode>& node, const std::string& path);

   public:
    tsnctrld();
    ~tsnctrld();
    void initialize();
};

#endif  // ENPRO_SWITCH_TSNCTRLD_HPP
