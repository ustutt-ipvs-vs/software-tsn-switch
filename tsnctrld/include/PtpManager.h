#ifndef ENPRO_SWITCH_PTPMANAGER_H
#define ENPRO_SWITCH_PTPMANAGER_H
#include <arpa/inet.h>
#include <endian.h>  // For be64toh()
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../../common/include/CncTypes.h"
#include "ptpStructs.h"

struct RunningStats {
    uint32_t count = 0;
    int64_t min = INT64_MAX;
    int64_t max = INT64_MIN;
    double mean = 0.0;
    double m2 = 0.0;  // Sum of squares of differences from the mean
    uint32_t startTime10ms = 0;

    void reset(uint32_t now10ms) {
        count = 0;
        min = INT64_MAX;
        max = INT64_MIN;
        mean = 0.0;
        m2 = 0.0;
        startTime10ms = now10ms;
    }

    void update(int64_t val) {
        count++;
        if (val < min) min = val;
        if (val > max) max = val;

        double delta = val - mean;
        mean += delta / count;
        double delta2 = val - mean;
        m2 += delta * delta2;
    }

    double getStdDev() const {
        return (count < 2) ? 0.0 : std::sqrt(m2 / (count - 1));
    }
};

class PtpManager {
   public:
    PtpManager(const std::string& ptp4l_socket = "/var/run/ptp4l", uint8_t transport_specific = 1);

    ~PtpManager();

    // -------------------------------------------------------------
    // COMMAND 1: GET CURRENT_DATA_SET (Standard IEEE 1588)
    // -------------------------------------------------------------
    bool getCurrentDataSet(double& offset_ns, double& mean_path_delay_ns);

    // -------------------------------------------------------------
    // COMMAND 2: GET PORT_PROPERTIES_NP (linuxptp specific)
    // -------------------------------------------------------------
    bool getPortState(std::vector<std::string>& states);
    void fillConfigData(PtpNode_t& node_to_fill);
    void startMonitoring();

   private:
    int m_fd;
    uint16_t m_assumedPortCount;
    std::thread m_pollThread;
    std::mutex m_socketMutex;
    std::atomic<bool> m_running{false};

    RunningStats m_current15m;
    RunningStats m_current24h;

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
