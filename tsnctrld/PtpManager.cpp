#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG

#include "PtpManager.h"

#include <poll.h>
#include <spdlog/spdlog.h>

#include "../common/include/CncTypes.h"
#include "spdlog/fmt/bin_to_hex.h"

PtpManager::PtpManager(const std::string& ptp4l_socket, uint8_t transport_specific)
    : m_target_path(ptp4l_socket), m_sequence_id(1), m_transport_specific(transport_specific) {
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
    if (m_fd >= 0) close(m_fd);
    unlink(m_local_path.c_str());
}

uint32_t getTimestamp10ms() {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (uint32_t)(ts.tv_sec * 100 + ts.tv_nsec / 10000000);
}
void PtpManager::finalizePeriod(int type) {
    std::lock_guard<std::mutex> lock(m_socketMutex);
    uint32_t now = getTimestamp10ms();

    if (type == 15) {
        std::cout << "[STATS] 15-min Period Complete. Start: " << m_current15m.startTime10ms
                  << " Avg: " << m_current15m.mean << " StdDev: " << m_current15m.getStdDev() << std::endl;

        // TODO: Push m_current15m to your historical record list (indices 1-96)

        m_current15m.reset(now);
    } else if (type == 24) {
        std::cout << "[STATS] 24-hour Period Complete. Start: " << m_current24h.startTime10ms
                  << " Avg: " << m_current24h.mean << std::endl;

        // TODO: Move current 24h (index 97) to previous (index 98)

        m_current24h.reset(now);
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
            ptp::CurrentDs current_ds;
            getCurrentDataSet(current_ds);
            SPDLOG_DEBUG("");

            std::vector<ptp::PortDs> port_dses;
            getPortDataSets(port_dses);
            // 2. Check for Rollovers
            seconds_counter++;

            // 15 Minutes = 900 seconds
            if (seconds_counter % 900 == 0) {
                finalizePeriod(15);
            }

            // 24 Hours = 86400 seconds
            if (seconds_counter % 86400 == 0) {
                finalizePeriod(24);
                seconds_counter = 0;  // Reset main counter
            }

            std::this_thread::sleep_until(next_tick);
        }
        SPDLOG_DEBUG("[PTP] Monitor thread exiting.");
    });
}

bool PtpManager::getCurrentDataSet(double& offset_ns, double& mean_path_delay_ns) {
    int32_t seq = sendManagementGet(MID_CURRENT_DATA_SET);
    if (seq < 0) {
        return false;
    }

    ptp::ResponseWithSourceIdentity rx_data;
    if (!receiveManagementResponse(MID_CURRENT_DATA_SET, seq, rx_data)) {
        return false;
    }

    if (rx_data.bytes.size() < 18) {
        return false;
    }

    uint64_t offset_net;
    memcpy(&offset_net, &rx_data.bytes[2], 8);
    offset_ns = static_cast<int64_t>(be64toh(offset_net)) / 65536.0;

    uint64_t delay_net;
    memcpy(&delay_net, &rx_data.bytes[10], 8);
    mean_path_delay_ns = static_cast<int64_t>(be64toh(delay_net)) / 65536.0;

    return true;
}
bool PtpManager::getPortState(std::vector<std::string>& states) {
    // The Intel code relies on PORT_DATA_SET, which is standard IEEE 1588
    int32_t seq = sendManagementGet(MID_PORT_DATA_SET);
    if (seq < 0) {
        return false;
    }

    ptp::ResponseWithSourceIdentity rx_data;
    bool atLeastOne = false;
    for (uint16_t i = 0; i < m_assumedPortCount || m_assumedPortCount == 0; i++) {
        if (!receiveManagementResponse(MID_PORT_DATA_SET, seq, rx_data)) {
            break;
        }
        atLeastOne = true;
        if (rx_data.bytes.size() >= 11) {
            uint16_t portNum = be16toh(rx_data.source.portNumber);
            states.emplace_back(fmt::format(
                "{:02X}-{:02X}-{:02X}-{:02X}-{:02X}-{:02X}-{:02X}-{:02X} Port {}: State {}",
                rx_data.source.clockIdentity[0], rx_data.source.clockIdentity[1], rx_data.source.clockIdentity[2],
                rx_data.source.clockIdentity[3], rx_data.source.clockIdentity[4], rx_data.source.clockIdentity[5],
                rx_data.source.clockIdentity[6], rx_data.source.clockIdentity[7], portNum, rx_data.bytes[10]));
        }
    }
    return atLeastOne;
}

bool PtpManager::getDefaultDataSet(ptp::DefaultDs& default_ds) {
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
    int32_t seq = sendManagementGet(MID_PORT_DATA_SET);
    if (seq < 0) {
        return false;
    }

    ptp::ResponseWithSourceIdentity rx_data;

    for (uint16_t i = 0; i < m_assumedPortCount || m_assumedPortCount == 0; i++) {
        if (!receiveManagementResponse(MID_PORT_DATA_SET, seq, rx_data)) {
            break;
        }
        auto port = port_dses.emplace_back();
        std::memcpy(&port, rx_data.bytes.data(), sizeof(ptp::PortDs));
        // TODO: Correcting byteorder
    }
    // throw std::runtime_error("PtpManager::getPortDataSets not really implemented yet");
}
bool PtpManager::getClockDescriptions(std::vector<ptp::ClockDescription>& clock_descriptions) {
    int32_t seq = sendManagementGet(MID_CLOCK_DESCRIPTION);
    if (seq < 0) {
        return false;
    }

    ptp::ResponseWithSourceIdentity rx_data;

    for (uint16_t i = 0; i < m_assumedPortCount || m_assumedPortCount == 0; i++) {
        if (!receiveManagementResponse(MID_CLOCK_DESCRIPTION, seq, rx_data)) {
            break;
        }
        auto clock_description = clock_descriptions.emplace_back();
        std::memcpy(&clock_description.clockType, rx_data.bytes.data(), sizeof(uint16_t));

        clock_description.clockType = be16toh(clock_description.clockType);
    }
    spdlog::warn("[PTP] [CLOCK_DESC] Only clockType is implemented, rest is undef");
    return true;
}

void PtpManager::fillConfigData(PtpNode_t& node_to_fill) {
    ptp::DefaultDs default_ds;
    std::vector<ptp::ClockDescription> clock_descriptions;
    if (!getDefaultDataSet(default_ds)) {
        return;
    }
    if (!getClockDescriptions(clock_descriptions)) {
        return;
    }

    node_to_fill.defaultDs.numPorts = default_ds.numberPorts;
    node_to_fill.defaultDs.priority1 = default_ds.priority1;
    node_to_fill.defaultDs.clockIdentity =
        fmt::format("{:02X}-{:02X}-{:02X}-{:02X}-{:02X}-{:02X}-{:02X}-{:02X}", default_ds.clockIdentity[0],
                    default_ds.clockIdentity[1], default_ds.clockIdentity[2], default_ds.clockIdentity[3],
                    default_ds.clockIdentity[4], default_ds.clockIdentity[5], default_ds.clockIdentity[6],
                    default_ds.clockIdentity[7]);
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
