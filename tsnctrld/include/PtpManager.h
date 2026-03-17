#ifndef ENPRO_SWITCH_PTPMANAGER_H
#define ENPRO_SWITCH_PTPMANAGER_H
#include <arpa/inet.h>
#include <endian.h>  // For be64toh()
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cmath>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../../common/include/CncTypes.h"
#include "ptpStructs.h"

class PtpManager {
   public:
    PtpManager(const std::string& ptp4l_socket = "/var/run/ptp4l", uint8_t transport_specific = 1);

    ~PtpManager();

    void fillConfigData(PtpNode_t& node_to_fill);
    void fillStateData(PtpNode_t& node_to_fill);
    void getPerformance15m(std::vector<PtpPerformanceRecord_t>& out);
    void getPerformance24h(std::vector<PtpPerformanceRecord_t>& out);
    void getPortPerformance15m(uint16_t portIndex, std::vector<PtpPortPerformanceRecord_t>& out);
    void getPortPerformance24h(uint16_t portIndex, std::vector<PtpPortPerformanceRecord_t>& out);
    void startMonitoring();
    void stopMonitoring();

   private:
    int m_fd;
    uint16_t m_assumedPortCount;
    std::thread m_pollThread;
    std::mutex m_socketMutex;
    std::mutex m_perfMutex;
    std::atomic<bool> m_running{false};

    ptp::PerformanceRecord m_current15m;
    ptp::PerformanceRecord m_current24h;

    std::deque<PtpPerformanceRecord_t> m_completedRecords15m;
    std::deque<PtpPerformanceRecord_t> m_completedRecords24h;

    std::map<uint16_t, std::deque<PtpPortPerformanceRecord_t>> m_completedPortRecords15m;
    std::map<uint16_t, std::deque<PtpPortPerformanceRecord_t>> m_completedPortRecords24h;

    std::string m_local_path;
    std::string m_target_path;
    uint16_t m_sequence_id;
    uint8_t m_transport_specific;

    bool getDefaultDataSet(ptp::DefaultDs& default_ds);
    bool getCurrentDataSet(ptp::CurrentDs& current_ds);
    bool getParentDataSet(ptp::ParentDS& parent_ds);
    bool getPortDataSets(std::vector<ptp::PortDs>& port_dses);
    bool getClockDescriptions(std::vector<ptp::ClockDescription>& clock_descriptions);
    void finalizePeriod(int type);

    int32_t sendManagementGet(uint16_t managementId);
    bool receiveManagementResponse(uint16_t expectedId, uint16_t expectedSeq,
                                   ptp::ResponseWithSourceIdentity& out_data);
};

#endif  // ENPRO_SWITCH_PTPMANAGER_H
