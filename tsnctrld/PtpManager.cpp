#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG

#include "PtpManager.h"

#include <poll.h>
#include <spdlog/spdlog.h>

#include "../common/include/CncTypes.h"
#include "spdlog/fmt/bin_to_hex.h"

#define MONITORING_PERIOD 1000
#define COLLECTION_INTERVAL_15M 900    // 900 Seconds in 15m
#define COLLECTION_INTERVAL_24H 86400  // 86400 Seconds in 24h
#define NUMBER_RECORDS_15M 97 - 1      // One less in history, because this spot is used by the currently active record
#define NUMBER_RECORDS_24H 2 - 1       // One less in history, because this spot is used by the currently active record

std::string clockIdentityBytesToString(std::array<uint8_t, 8> clockIdentity) {
    return fmt::format("{:02X}-{:02X}-{:02X}-{:02X}-{:02X}-{:02X}-{:02X}-{:02X}", clockIdentity[0], clockIdentity[1],
                       clockIdentity[2], clockIdentity[3], clockIdentity[4], clockIdentity[5], clockIdentity[6],
                       clockIdentity[7]);
}

PtpManager::PtpManager(const std::string& ptp4l_socket, uint8_t transport_specific)
    : m_target_path(ptp4l_socket), m_sequence_id(1), m_transport_specific(transport_specific), m_assumedPortCount(0) {
    m_fd = socket(AF_LOCAL, SOCK_DGRAM, 0);
    if (m_fd < 0) throw std::runtime_error("Failed to create UDS socket");

    // CRITICAL FIX: Use /var/run to avoid Systemd PrivateTmp isolation!
    m_local_path = "/var/run/ptp_mgr_" + std::to_string(getpid());
    unlink(m_local_path.c_str());

    struct sockaddr_un local_addr{};
    local_addr.sun_family = AF_LOCAL;
    strncpy(local_addr.sun_path, m_local_path.c_str(), sizeof(local_addr.sun_path) - 1);

    if (bind(m_fd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        close(m_fd);
        throw std::runtime_error("Failed to bind local UDS socket (Are you running with sudo?): " + m_local_path);
    }
}

PtpManager::~PtpManager() {
    stopMonitoring();
    if (m_fd >= 0) close(m_fd);
    unlink(m_local_path.c_str());
}

uint32_t getTimestamp10ms() {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (uint32_t)(ts.tv_sec * 100 + ts.tv_nsec / 10000000);
}

template <typename T>
void pushSlidingWindow(std::deque<T>& dq, T&& item, size_t max_size) {
    dq.push_front(std::forward<T>(item));
    if (dq.size() > max_size) {
        dq.pop_back();
    }
}
void rolloverPeriod(ptp::PerformanceRecord& current, std::deque<PtpPerformanceRecord_t>& global_records,
                    std::map<uint16_t, std::deque<PtpPortPerformanceRecord_t>>& port_records, size_t max_size,
                    uint32_t now, int expectedEntries) {
    SPDLOG_DEBUG("[PTP] [ROLLOVER] current: start at {}, expected entries={}", current.startTime10ms, expectedEntries);
    SPDLOG_DEBUG("[PTP] [ROLLOVER] current: mpd: c={} avg={} min={} max={} stddev={}", current.meanPathDelay.count,
                 current.meanPathDelay.mean, current.meanPathDelay.min, current.meanPathDelay.max,
                 current.meanPathDelay.getStdDev());
    SPDLOG_DEBUG("[PTP] [ROLLOVER] current: off: c={} avg={} min={} max={} stddev={}", current.offsetFromMaster.count,
                 current.offsetFromMaster.mean, current.offsetFromMaster.min, current.offsetFromMaster.max,
                 current.offsetFromMaster.getStdDev());

    // 1. Snapshot the Instance-level record
    PtpPerformanceRecord_t globalRecord{
        true, current.startTime10ms,
        current.offsetFromMaster.count == expectedEntries && current.meanPathDelay.count == expectedEntries,
        current.offsetFromMaster.toParameters(), current.meanPathDelay.toParameters()};
    pushSlidingWindow(global_records, std::move(globalRecord), max_size);

    // 2. Snapshot the Port-level records
    for (const auto& [portId, stats] : current.portMeanLinkDelay) {
        SPDLOG_DEBUG("[PTP] [ROLLOVER] current: ports: id={} c={} avg={} min={} max={} stddev={}", portId, stats.count,
                     stats.mean, stats.min, stats.max, stats.getStdDev());

        PtpPortPerformanceRecord_t portRecord{current.startTime10ms, stats.toParameters()};
        pushSlidingWindow(port_records[portId], std::move(portRecord), max_size);
    }

    // 3. Reset the accumulator for the next period
    current.reset(now);
}

void PtpManager::finalizePeriod(int type) {
    uint32_t now = getTimestamp10ms();  // Your existing time fetcher

    if (type == 15) {
        rolloverPeriod(m_current15m, m_completedRecords15m, m_completedPortRecords15m, NUMBER_RECORDS_15M, now,
                       COLLECTION_INTERVAL_15M);
    } else if (type == 24) {
        rolloverPeriod(m_current24h, m_completedRecords24h, m_completedPortRecords24h, NUMBER_RECORDS_24H, now,
                       COLLECTION_INTERVAL_24H);
    }
}

void PtpManager::stopMonitoring() {
    m_running = false;
    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }
}
void PtpManager::startMonitoring() {
    if (m_pollThread.joinable()) {
        return;
    }
    m_running = true;
    m_pollThread = std::thread([this]() {
        SPDLOG_DEBUG("[PTP] Monitor thread starting.");
        auto next_tick = std::chrono::steady_clock::now();
        int seconds_counter = 0;
        uint32_t now = getTimestamp10ms();
        m_current15m.reset(now);
        m_current24h.reset(now);
        while (m_running) {
            next_tick += std::chrono::milliseconds(MONITORING_PERIOD);

            ptp::CurrentDs current_ds;
            getCurrentDataSet(current_ds);

            {
                std::lock_guard<std::mutex> lock(m_perfMutex);
                m_current15m.meanPathDelay.update(current_ds.meanDelay);
                m_current24h.meanPathDelay.update(current_ds.meanDelay);

                m_current15m.offsetFromMaster.update(current_ds.offsetFromTimeTransmitter);
                m_current24h.offsetFromMaster.update(current_ds.offsetFromTimeTransmitter);

                std::vector<ptp::PortDs> port_dses;
                getPortDataSets(port_dses);
                for (auto port_ds : port_dses) {
                    m_current15m.portMeanLinkDelay[port_ds.portIdentity.portNumber].update(port_ds.meanLinkDelay);
                    m_current24h.portMeanLinkDelay[port_ds.portIdentity.portNumber].update(port_ds.meanLinkDelay);
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_perfMutex);
                // 2. Check for Rollovers
                seconds_counter++;

                // 15 Minutes = 900 seconds
                if (seconds_counter % COLLECTION_INTERVAL_15M == 0) {
                    finalizePeriod(15);
                }

                // 24 Hours = 86400 seconds
                if (seconds_counter % COLLECTION_INTERVAL_24H == 0) {
                    finalizePeriod(24);
                    seconds_counter = 0;  // Reset main counter
                }
            }

            std::this_thread::sleep_until(next_tick);
        }
        SPDLOG_DEBUG("[PTP] Monitor thread exiting.");
    });
}

bool PtpManager::getDefaultDataSet(ptp::DefaultDs& default_ds) {
    std::lock_guard<std::mutex> lock(m_socketMutex);
    int32_t seq = sendManagementGet(MID_DEFAULT_DATA_SET);
    if (seq < 0) {
        return false;
    }

    ptp::ResponseWithSourceIdentity rx_data;
    if (!receiveManagementResponse(MID_DEFAULT_DATA_SET, seq, rx_data)) {
        return false;
    }

    if (rx_data.bytes.size() < sizeof(ptp::DefaultDs)) {
        return false;
    }
    std::memcpy(&default_ds, rx_data.bytes.data(), sizeof(ptp::DefaultDs));
    default_ds.numberPorts = be16toh(default_ds.numberPorts);
    default_ds.clockQuality.offsetScaledLogVariance = be16toh(default_ds.clockQuality.offsetScaledLogVariance);

    m_assumedPortCount = default_ds.numberPorts;
    return true;
}

bool PtpManager::getCurrentDataSet(ptp::CurrentDs& current_ds) {
    std::lock_guard<std::mutex> lock(m_socketMutex);
    int32_t seq = sendManagementGet(MID_CURRENT_DATA_SET);
    if (seq < 0) {
        return false;
    }

    ptp::ResponseWithSourceIdentity rx_data;
    if (!receiveManagementResponse(MID_CURRENT_DATA_SET, seq, rx_data)) {
        return false;
    }
    if (rx_data.bytes.size() < sizeof(ptp::CurrentDs)) {
        return false;
    }
    std::memcpy(&current_ds, rx_data.bytes.data(), sizeof(ptp::CurrentDs));
    current_ds.stepsRemoved = be16toh(current_ds.stepsRemoved);
    current_ds.offsetFromTimeTransmitter = be64toh(current_ds.offsetFromTimeTransmitter);
    current_ds.meanDelay = be64toh(current_ds.meanDelay);

    return true;
}
bool PtpManager::getParentDataSet(ptp::ParentDS& parent_ds) {
    std::lock_guard<std::mutex> lock(m_socketMutex);
    int32_t seq = sendManagementGet(MID_PARENT_DATA_SET);
    if (seq < 0) {
        return false;
    }

    ptp::ResponseWithSourceIdentity rx_data;
    if (!receiveManagementResponse(MID_PARENT_DATA_SET, seq, rx_data)) {
        return false;
    }
    if (rx_data.bytes.size() < sizeof(ptp::ParentDS)) {
        return false;
    }
    std::memcpy(&parent_ds, rx_data.bytes.data(), sizeof(ptp::ParentDS));
    parent_ds.parentPortIdentity.portNumber = be16toh(parent_ds.parentPortIdentity.portNumber);
    parent_ds.observedParentOffsetScaledLogVariance = be16toh(parent_ds.observedParentOffsetScaledLogVariance);
    parent_ds.observedParentClockPhaseChangeRate = be32toh(parent_ds.observedParentClockPhaseChangeRate);
    parent_ds.grandmasterClockQuality.offsetScaledLogVariance =
        be16toh(parent_ds.grandmasterClockQuality.offsetScaledLogVariance);

    return true;
}
bool PtpManager::getPortDataSets(std::vector<ptp::PortDs>& port_dses) {
    std::lock_guard<std::mutex> lock(m_socketMutex);
    int32_t seq = sendManagementGet(MID_PORT_DATA_SET);
    if (seq < 0) {
        return false;
    }

    ptp::ResponseWithSourceIdentity rx_data;

    for (uint16_t i = 0; i < m_assumedPortCount || m_assumedPortCount == 0; i++) {
        if (!receiveManagementResponse(MID_PORT_DATA_SET, seq, rx_data)) {
            break;
        }
        auto& port = port_dses.emplace_back();
        std::memcpy(&port, rx_data.bytes.data(), sizeof(ptp::PortDs));

        port.portIdentity.portNumber = be16toh(port.portIdentity.portNumber);
        port.meanLinkDelay = be64toh(port.meanLinkDelay);
    }
    return true;
}
bool PtpManager::getClockDescriptions(std::vector<ptp::ClockDescription>& clock_descriptions) {
    std::lock_guard<std::mutex> lock(m_socketMutex);
    int32_t seq = sendManagementGet(MID_CLOCK_DESCRIPTION);
    if (seq < 0) {
        return false;
    }

    ptp::ResponseWithSourceIdentity rx_data;

    for (uint16_t i = 0; i < m_assumedPortCount || m_assumedPortCount == 0; i++) {
        if (!receiveManagementResponse(MID_CLOCK_DESCRIPTION, seq, rx_data)) {
            break;
        }
        auto& clock_description = clock_descriptions.emplace_back();
        clock_description.source = rx_data.source;

        std::memcpy(&clock_description.clockType, rx_data.bytes.data(), sizeof(uint16_t));
        clock_description.clockType = be16toh(clock_description.clockType);
    }
    spdlog::warn("[PTP] [CLOCK_DESC] Only clockType and custom field \"source\" is implemented, rest is undef");
    return true;
}
void PtpManager::getPerformance15m(std::vector<PtpPerformanceRecord_t>& out) {
    std::lock_guard<std::mutex> lock(m_perfMutex);
    out.clear();
    out.reserve(NUMBER_RECORDS_15M + 1);
    out.emplace_back(false, m_current15m.startTime10ms, false, m_current15m.offsetFromMaster.toParameters(),
                     m_current15m.meanPathDelay.toParameters());
    std::copy(m_completedRecords15m.begin(), m_completedRecords15m.end(), std::back_inserter(out));
}
void PtpManager::getPerformance24h(std::vector<PtpPerformanceRecord_t>& out) {
    std::lock_guard<std::mutex> lock(m_perfMutex);
    out.clear();
    out.reserve(NUMBER_RECORDS_24H + 1);
    out.emplace_back(false, m_current24h.startTime10ms, false, m_current24h.offsetFromMaster.toParameters(),
                     m_current24h.meanPathDelay.toParameters());
    std::copy(m_completedRecords24h.begin(), m_completedRecords24h.end(), std::back_inserter(out));
}
void PtpManager::getPortPerformance15m(uint16_t portIndex, std::vector<PtpPortPerformanceRecord_t>& out) {
    std::lock_guard<std::mutex> lock(m_perfMutex);
    out.clear();
    out.reserve(NUMBER_RECORDS_15M + 1);
    out.emplace_back(m_current15m.startTime10ms, m_current15m.portMeanLinkDelay[portIndex].toParameters());
    std::copy(m_completedPortRecords15m[portIndex].begin(), m_completedPortRecords15m[portIndex].end(),
              std::back_inserter(out));
}
void PtpManager::getPortPerformance24h(uint16_t portIndex, std::vector<PtpPortPerformanceRecord_t>& out) {
    std::lock_guard<std::mutex> lock(m_perfMutex);
    out.clear();
    out.reserve(NUMBER_RECORDS_24H + 1);
    out.emplace_back(m_current24h.startTime10ms, m_current24h.portMeanLinkDelay[portIndex].toParameters());
    std::copy(m_completedPortRecords24h[portIndex].begin(), m_completedPortRecords24h[portIndex].end(),
              std::back_inserter(out));
}
void PtpManager::fillStateData(PtpNode_t& node_to_fill) {
    ptp::DefaultDs default_ds = {};
    if (!getDefaultDataSet(default_ds)) {
        return;
    }
    ptp::CurrentDs current_ds = {};
    if (!getCurrentDataSet(current_ds)) {
        return;
    }
    ptp::ParentDS parent_ds = {};
    if (!getParentDataSet(parent_ds)) {
        return;
    }
    std::vector<ptp::PortDs> port_dses = {};
    if (!getPortDataSets(port_dses)) {
        return;
    }
    if (default_ds.numberPorts != port_dses.size()) {
        uint16_t portsNum;
        std::memcpy(&portsNum, &default_ds.numberPorts, sizeof(uint16_t));

        SPDLOG_WARN(
            "[PTP] [FILL_STATE] Number of ports does not match between reported number {} and number of port datasets "
            "{}",
            portsNum, port_dses.size());
    }
    node_to_fill.defaultDs.numPorts = default_ds.numberPorts;
    node_to_fill.defaultDs.clockIdentity = clockIdentityBytesToString(default_ds.clockIdentity);

    node_to_fill.currentDs.stepsRemoved = current_ds.stepsRemoved;
    node_to_fill.currentDs.offsetFromTimeTransmitter = current_ds.offsetFromTimeTransmitter;

    node_to_fill.parentDs.parentClockIdentity = clockIdentityBytesToString(parent_ds.parentPortIdentity.clockIdentity);
    node_to_fill.parentDs.parentPortNumber = parent_ds.parentPortIdentity.portNumber;
    node_to_fill.parentDs.grandParentClockIdentity = clockIdentityBytesToString(parent_ds.grandmasterIdentity);

    for (auto port_ds : port_dses) {
        auto& port = node_to_fill.ports.emplace_back();
        port.portIndex = port_ds.portIdentity.portNumber;
        port.portDs.portClockIdentity = clockIdentityBytesToString(port_ds.portIdentity.clockIdentity);
        port.portDs.portPortNumber = port_ds.portIdentity.portNumber;
        port.portDs.portState = static_cast<PtpPortState_t>(port_ds.portState);
        port.portDs.meanLinkDelay = port_ds.meanLinkDelay;
    }
}

void PtpManager::fillConfigData(PtpNode_t& node_to_fill) {
    ptp::DefaultDs default_ds = {};
    std::vector<ptp::ClockDescription> clock_descriptions = {};
    if (!getDefaultDataSet(default_ds)) {
        return;
    }
    if (!getClockDescriptions(clock_descriptions)) {
        return;
    }

    PtpInstanceType_t type = PtpInstanceType_t::PTP_INSTANCE_INVALID;
    if ((clock_descriptions[0].clockType & (1 << 8 + 7)) != 0) {
        type = PtpInstanceType_t::PTP_INSTANCE_OC;
    } else if ((clock_descriptions[0].clockType & (1 << 8 + 6)) != 0) {
        type = PtpInstanceType_t::PTP_INSTANCE_BC;
    } else if ((clock_descriptions[0].clockType & (1 << 8 + 5)) != 0) {
        type = PtpInstanceType_t::PTP_INSTANCE_P2P_TC;
    } else if ((clock_descriptions[0].clockType & (1 << 8 + 4)) != 0) {
        type = PtpInstanceType_t::PTP_INSTANCE_E2E_TC;
    }
    node_to_fill.defaultDs.instanceType = type;

    node_to_fill.defaultDs.numPorts = default_ds.numberPorts;
    node_to_fill.defaultDs.priority1 = default_ds.priority1;
    node_to_fill.defaultDs.clockIdentity = clockIdentityBytesToString(default_ds.clockIdentity);
}

int32_t PtpManager::sendManagementGet(uint16_t managementId) {
    SPDLOG_TRACE("[PTP] [SEND_GET] Sending request with MID {}", managementId);
    // Lean payload size (exactly 54 bytes, mimicking Intel's get_req)
    size_t total_size = sizeof(ptp::PtpManagementMsg) + sizeof(ptp::PtpManagementTlv);

    ptp::PtpManagementMsg msg = {};
    ptp::PtpManagementTlv tlv = {};

    // 1. Build Header perfectly mimicking the working Intel C Code
    msg.header.tsmt = (m_transport_specific << 4) | 0x0D;   // e.g. 0x1D
    msg.header.ver = 0x02;                                  // Hardcoded PTP_VERSION
    msg.header.messageLength = htons(total_size);           // 54
    msg.header.sourcePortIdentity.portNumber = htons(0x1);  // specific portnum

    uint16_t current_seq = m_sequence_id++;
    msg.header.sequenceId = htons(current_seq);
    msg.header.controlField = 0x04;  // CTL_MANAGEMENT
    msg.header.logMessageInterval = 0x7F;

    // 2. Build Management Fields
    msg.targetPortIdentity.clockIdentity.fill(0xFF);
    msg.targetPortIdentity.portNumber = 0xFFFF;
    msg.actionField = 0;  // GET

    // 3. Build TLV (Type, Length, Value)
    tlv.tlvType = htons(0x0001);  // TLV_MANAGEMENT
    tlv.lengthField = htons(2);   // Intel uses exact size (2), ignoring padding
    tlv.managementId = htons(managementId);

    std::vector<uint8_t> buffer(total_size, 0);
    std::memcpy(buffer.data(), &msg, sizeof(ptp::PtpManagementMsg));
    std::memcpy(buffer.data() + sizeof(ptp::PtpManagementMsg), &tlv, sizeof(ptp::PtpManagementTlv));

    struct sockaddr_un target_addr{};
    target_addr.sun_family = AF_LOCAL;
    strncpy(target_addr.sun_path, m_target_path.c_str(), sizeof(target_addr.sun_path) - 1);

    if (sendto(m_fd, buffer.data(), buffer.size(), 0, (struct sockaddr*)&target_addr, sizeof(target_addr)) !=
        buffer.size()) {
        spdlog::warn("Sending of buffer failed for MID {}", managementId);
        return -1;
    }

    return current_seq;
}
bool PtpManager::receiveManagementResponse(uint16_t expectedId, uint16_t expectedSeq,
                                           ptp::ResponseWithSourceIdentity& out_data) {
    SPDLOG_TRACE("[PTP] [RECV_RESP] Receiving response with expected MID {}", expectedId);
    std::vector<uint8_t> rx_buffer(1024);
    struct pollfd pfd{};
    pfd.fd = m_fd;
    pfd.events = POLLIN;

    // Use poll() to mirror the Intel code's bulletproof receive logic
    int ret = poll(&pfd, 1, 50);
    if (ret <= 0) {  // Timeout or error
        spdlog::warn("[PTP] [RECV_RESP] Timeout (or Error) when receiving response with expected MID {}", expectedId);
        return false;
    }

    int rx_bytes = recv(m_fd, rx_buffer.data(), rx_buffer.size(), 0);
    if (rx_bytes < static_cast<int>(sizeof(ptp::PtpManagementMsg) + sizeof(ptp::PtpManagementTlv))) {
        spdlog::warn("[PTP] [RECV_RESP] Number of received bytes {} smaller than minimum valid length {}", rx_bytes,
                     sizeof(ptp::PtpManagementMsg) + sizeof(ptp::PtpManagementTlv));
        return false;
    }

    ptp::PtpManagementMsg msg;
    std::memcpy(&msg, rx_buffer.data(), sizeof(ptp::PtpManagementMsg));

    uint16_t tlvType;
    std::memcpy(&tlvType, rx_buffer.data() + sizeof(ptp::PtpManagementMsg), sizeof(uint16_t));
    tlvType = be16toh(tlvType);
    if (tlvType != 1) {
        spdlog::warn("[PTP] [RECV_RESP] [ERROR] Received response has unexpected tlvType {}", tlvType);
        if (tlvType == 2) {
            spdlog::error("[PTP] [RECV_RESP] [ERROR] Received response has unexpected tlvType MANAGEMENT_ERROR_STATUS");
            ptp::PtpManagementErrorTlv errorTlv;
            std::memcpy(&errorTlv, rx_buffer.data() + sizeof(ptp::PtpManagementMsg),
                        sizeof(ptp::PtpManagementErrorTlv));
            uint16_t length = be16toh(errorTlv.lengthField);
            spdlog::error("[PTP] [RECV_RESP] [ERROR] received bytes: {}", rx_bytes);
            spdlog::error("[PTP] [RECV_RESP] [ERROR] length: {}", length);
            spdlog::error("[PTP] [RECV_RESP] [ERROR] managementErrorID: {}", be16toh(errorTlv.managementErrorId));
            spdlog::error("[PTP] [RECV_RESP] [ERROR] managementID: {}", be16toh(errorTlv.managementId));
            if (length > 8) {
                uint8_t textLength =
                    *(rx_buffer.data() + sizeof(ptp::PtpManagementMsg) + sizeof(ptp::PtpManagementErrorTlv));
                spdlog::error("[PTP] [RECV_RESP] [ERROR] textLength: {}", textLength);
                spdlog::error(
                    "[PTP] [RECV_RESP] [ERROR] displayData: {}",
                    std::string_view(reinterpret_cast<const char*>(rx_buffer.data() + sizeof(ptp::PtpManagementMsg) +
                                                                   sizeof(ptp::PtpManagementErrorTlv)),
                                     textLength));
            } else {
                spdlog::error("[PTP] [RECV_RESP] [ERROR] No extra displayData");
            }
        }
        return false;
    }

    ptp::PtpManagementTlv tlv;
    std::memcpy(&tlv, rx_buffer.data() + sizeof(ptp::PtpManagementMsg), sizeof(ptp::PtpManagementTlv));

    if (ntohs(msg.header.sequenceId) != expectedSeq) {
        spdlog::warn("[PTP] [RECV_RESP] Received response has unexpected sequenceID {}", ntohs(msg.header.sequenceId));
        return false;
    }
    if (msg.actionField != 2) {
        spdlog::warn("[PTP] [RECV_RESP] Received response has unexpected actionField {} (should be 2 for response)",
                     msg.actionField);
        return false;  // 2 = RESPONSE
    }
    if (ntohs(tlv.managementId) != expectedId) {
        spdlog::warn("[PTP] [RECV_RESP] Received response has unexpected MID {}", ntohs(tlv.managementId));
        return false;
    }

    std::memcpy(&out_data.source, &msg.header.sourcePortIdentity, sizeof(ptp::PortIdentity));
    out_data.source.portNumber = be16toh(out_data.source.portNumber);

    int payload_len = ntohs(tlv.lengthField) - 2;
    if (payload_len == 0) {
        SPDLOG_TRACE("[PTP] [RECV_RESP] Empty response", expectedId);
        out_data.bytes.clear();
    } else if (payload_len > 0 &&
               rx_bytes >= sizeof(ptp::PtpManagementMsg) + sizeof(ptp::PtpManagementTlv) + payload_len) {
        SPDLOG_TRACE("[PTP] [RECV_RESP] Valid Response", expectedId);

        uint8_t* tlv_payload = rx_buffer.data() + sizeof(ptp::PtpManagementMsg) + sizeof(ptp::PtpManagementTlv);
        out_data.bytes.assign(tlv_payload, tlv_payload + payload_len);
    } else {
        spdlog::warn("[PTP] [RECV_RESP] Invalid payload length", expectedId);

        return false;
    }
    return true;
}
