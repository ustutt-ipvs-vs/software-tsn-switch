#include "PtpManager.h"

#include <poll.h>
#include <spdlog/spdlog.h>

#include <utility>

#include "CncTypes.h"
#include "spdlog/fmt/bin_to_hex.h"

// Time constants
constexpr int MONITORING_PERIOD_MS = 1000;  // How many milliseconds between gathering PTP statistics
constexpr uint32_t COLLECTION_INTERVAL_15M_PERIOD_COUNT = 900;  // Periods to complete this interval, 900 Seconds in 15m
constexpr uint32_t COLLECTION_INTERVAL_24H_PERIOD_COUNT =
    86400;  // Periods to complete this interval, 86400 Seconds in 24h

// Sizing constants
constexpr size_t NUMBER_RECORDS_15M = 97 - 1;  // Count defined by the standard, leave space for current record
constexpr size_t NUMBER_RECORDS_24H = 2 - 1;   // Count defined by the standard, leave space for current record

/**
 * @brief Enum to hold the management IDs for communicating with the PTP daemon.
 */
enum : uint16_t {
    MID_CLOCK_DESCRIPTION = 0x0001,
    MID_DEFAULT_DATA_SET = 0x2000,
    MID_CURRENT_DATA_SET = 0x2001,
    MID_PARENT_DATA_SET = 0x2002,
    MID_TIME_PROPERTIES_DATA_SET = 0x2003,
    MID_PORT_DATA_SET = 0x2004,
    MID_CUSTOM_PORT_PROPERTIES = 0xC004,
};

/**
 * @brief Helper method to convert a bytearray to a string following the format of a `clock-identity` of the YANG model.
 * @param clockIdentity
 * @return
 */
std::string clockIdentityBytesToString(std::array<uint8_t, 8> clockIdentity) {
    return fmt::format("{:02X}-{:02X}-{:02X}-{:02X}-{:02X}-{:02X}-{:02X}-{:02X}", clockIdentity[0], clockIdentity[1],
                       clockIdentity[2], clockIdentity[3], clockIdentity[4], clockIdentity[5], clockIdentity[6],
                       clockIdentity[7]);
}

/**
 * @brief Constructor for the PtpManager. Creates and binds the used socket.
 * @param config A @ref PtpManagerConfig struct with the values used for the configuration of this instance.
 */
PtpManager::PtpManager(const PtpManagerConfig& config)
    : m_target_path(config.ptp4l_socket),
      m_transport_specific(config.transport_specific),
      m_assumedPortCount(config.assumedPortCount),
      enabled(config.enabled) {
    m_fd = socket(AF_LOCAL, SOCK_DGRAM, 0);
    if (m_fd < 0) {
        throw std::runtime_error("Failed to create UDS socket");
    }

    // CRITICAL FIX: Use /var/run to avoid Systemd PrivateTmp isolation!
    m_local_path = "/var/run/tsnctrld_ptp";
    unlink(m_local_path.c_str());

    struct sockaddr_un local_addr{};
    local_addr.sun_family = AF_LOCAL;
    strncpy(local_addr.sun_path, m_local_path.c_str(), sizeof(local_addr.sun_path) - 1);

    if (bind(m_fd, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        close(m_fd);
        throw std::runtime_error("Failed to bind local UDS socket (Are you running with sudo?): " + m_local_path);
    }
}

/**
 * @brief Destructor for the PtpManager. Ensures the thread monitoring the PTP statistics is stopped and closes the
 * socket.
 */
PtpManager::~PtpManager() {
    stopMonitoring();
    if (m_fd >= 0) {
        close(m_fd);
    }
    unlink(m_local_path.c_str());
    spdlog::info("Stopped PTP {}monitoring thread...", enabled ? "" : "disabled ");
}

/**
 * @brief Determines if PTP-related data may be stored in the datastore.
 *
 * @return Whether the subtree in the datastore for PTP is enabled. If this is false, the monitoring thread won't be
 * started and no data will be collected.
 */
bool PtpManager::isEnabled() const {
    return enabled;
}

/**
 * @brief Helper method to return the current timestamp according to the format desired in the YANG model.
 * @return The current time since boot, where one unit corresponds to 10 milliseconds.
 */
uint32_t getTimestamp10ms() {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (uint32_t)((ts.tv_sec * 100) + (ts.tv_nsec / 10000000));
}

/**
 * @brief Helper method to ensure the maximum number of performance-records allowed of a given type is not exceeded.
 */
template <typename T>
void pushSlidingWindow(std::deque<T>& dq, const T& item, size_t max_size) {
    dq.push_front(item);
    if (dq.size() > max_size) {
        dq.pop_back();
    }
}

/**
 * @brief Takes the current @ref ptp::PerformanceRecord and adds its data to the end of the deques for @ref
 * PtpPerformanceRecord_t and @ref PtpPortPerformanceRecord_t structs.
 * @param current The current @ref ptp::PerformanceRecord containing the statistics for the host and each port.
 * @param global_records A double-ended queue of @ref PtpPerformanceRecord_t structs to which the host-statistics are to
 * be appended.
 * @param port_records A map from port-index to a double-ended queue of @ref PtpPortPerformanceRecord_t structs. Each
 * port-statistic from the @current parameter is appended to its corresponding queue.
 * @param max_size The maximum number of periods in these queues.
 * @param now The current timestamp in the format required by the YANG model.
 * @param expectedEntries The number of times the statistic should have been collected within this period.
 */
void rolloverPeriod(ptp::PerformanceRecord& current, std::deque<PtpPerformanceRecord_t>& global_records,
                    std::map<uint16_t, std::deque<PtpPortPerformanceRecord_t>>& port_records, size_t max_size,
                    uint32_t now, uint32_t expectedEntries) {
    SPDLOG_DEBUG("[PTP] [ROLLOVER] current: start at {}, expected entries={}", current.startTime10ms, expectedEntries);
    SPDLOG_DEBUG("[PTP] [ROLLOVER] current: mpd: c={} avg={} min={} max={} stddev={}", current.meanPathDelay.count,
                 current.meanPathDelay.mean, current.meanPathDelay.min, current.meanPathDelay.max,
                 current.meanPathDelay.getStdDev());
    SPDLOG_DEBUG("[PTP] [ROLLOVER] current: off: c={} avg={} min={} max={} stddev={}", current.offsetFromMaster.count,
                 current.offsetFromMaster.mean, current.offsetFromMaster.min, current.offsetFromMaster.max,
                 current.offsetFromMaster.getStdDev());

    // 1. Snapshot the Instance-level record
    PtpPerformanceRecord_t globalRecord{.periodComplete = true,
                                        .pmTime = current.startTime10ms,
                                        .measurementValid = current.offsetFromMaster.isValid(expectedEntries) &&
                                                            current.meanPathDelay.isValid(expectedEntries),
                                        .offsetFromTimeTransmitter = current.offsetFromMaster.toParameters(),
                                        .meanPathDelay = current.meanPathDelay.toParameters()};
    pushSlidingWindow(global_records, globalRecord, max_size);

    // 2. Snapshot the Port-level records
    for (const auto& [portId, stats] : current.portMeanLinkDelay) {
        SPDLOG_DEBUG("[PTP] [ROLLOVER] current: ports: id={} c={} avg={} min={} max={} stddev={}", portId, stats.count,
                     stats.mean, stats.min, stats.max, stats.getStdDev());

        PtpPortPerformanceRecord_t portRecord{.pmTime = current.startTime10ms, .meanLinkDelay = stats.toParameters()};
        pushSlidingWindow(port_records[portId], portRecord, max_size);
    }

    // 3. Reset the accumulator for the next period
    current.reset(now);
}

/**
 * @brief Calls @ref PtpManager::rolloverPeriod with th appropriate arguments
 * @param type The type of period that was just completed. Implemented according to the standard are 15min and 24hours.
 */
void PtpManager::finalizePeriod(int type) {
    uint32_t now = getTimestamp10ms();

    if (type == 15) {
        rolloverPeriod(m_current15m, m_completedRecords15m, m_completedPortRecords15m, NUMBER_RECORDS_15M, now,
                       COLLECTION_INTERVAL_15M_PERIOD_COUNT);
    } else if (type == 24) {
        rolloverPeriod(m_current24h, m_completedRecords24h, m_completedPortRecords24h, NUMBER_RECORDS_24H, now,
                       COLLECTION_INTERVAL_24H_PERIOD_COUNT);
    }
}

/**
 * @brief Stops the monitoring thread after the current interval.
 */
void PtpManager::stopMonitoring() {
    m_running = false;
    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }
}

/**
 * @brief Starts the monitoring thread.
 *
 * The thread queries the PTP socket for relevant data, updates this class's internal statistics, and checks if any
 * statistics need to be consolidated. It then waits until the next querying-interval is supposed to start and loops
 * continues until the thread is stopped.
 */
void PtpManager::startMonitoring() {
    if (m_pollThread.joinable() || !enabled) {
        return;
    }
    m_running = true;
    m_pollThread = std::thread([this]() {
        SPDLOG_DEBUG("[PTP] Monitor thread starting.");
        auto next_tick = std::chrono::steady_clock::now();
        int periods_completed_counter = 0;
        uint32_t now = getTimestamp10ms();
        m_current15m.reset(now);
        m_current24h.reset(now);
        while (m_running) {
            next_tick += std::chrono::milliseconds(MONITORING_PERIOD_MS);

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
                periods_completed_counter++;

                // 15 Minutes = 900 seconds
                if (periods_completed_counter % COLLECTION_INTERVAL_15M_PERIOD_COUNT == 0) {
                    finalizePeriod(15);
                }

                // 24 Hours = 86400 seconds
                if (periods_completed_counter % COLLECTION_INTERVAL_24H_PERIOD_COUNT == 0) {
                    finalizePeriod(24);
                    periods_completed_counter = 0;  // Reset main counter
                }
            }

            std::this_thread::sleep_until(next_tick);
        }
        SPDLOG_DEBUG("[PTP] Monitor thread exiting.");
    });
}

/**
 * @brief Queries the PTP management socket for the DEFAULT_DATA_SET.
 *
 * @param default_ds The struct that should be filled with the response.
 * @return True if the struct was filled successfully, False otherwise.
 */
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

/**
 * @brief Queries the PTP management socket for the CURRENT_DATA_SET.
 *
 * @param current_ds The struct that should be filled with the response.
 * @return True if the struct was filled successfully, False otherwise.
 */
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

/**
 * @brief Queries the PTP management socket for the PARENT_DATA_SET.
 *
 * @param parent_ds The struct that should be filled with the response.
 * @return True if the struct was filled successfully, False otherwise.
 */
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

/**
 * @brief Queries the PTP management socket for the PORT_DATA_SET(s).
 *
 * @param port_dses A list that should be filled with the @ref ptp::PortDs structs parsed from the response.
 * @return True if the list was filled successfully, False otherwise.
 */
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

/**
 * @brief Queries the PTP management socket for the CLOCK_DESCRIPTION(s). Note that only the fields @ref
 * ptp::ClockDescription::source and @ref ptp::ClockDescription::clockType are implemented.
 *
 * @param clock_descriptions A list that should be filled with the @ref ptp::ClockDescription structs parsed from the
 * response.
 * @return True if the list was filled successfully, False otherwise.
 */
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
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_DEBUG
    spdlog::warn("[PTP] [CLOCK_DESC] Only clockType and custom field \"source\" is implemented, rest is undef");
#endif
    return true;
}

/**
 * @brief Queries the PTP management socket for the custom PORT_PROPERTIES_NP(s).
 *
 * @param port_dses A list that should be filled with the @ref ptp::PortProperties structs parsed from the response.
 * @return True if the list was filled successfully, False otherwise.
 */
bool PtpManager::getPortPropertiesNp(std::vector<ptp::PortProperties>& port_propertieses) {
    std::lock_guard<std::mutex> lock(m_socketMutex);
    int32_t seq = sendManagementGet(MID_CUSTOM_PORT_PROPERTIES);
    if (seq < 0) {
        return false;
    }

    ptp::ResponseWithSourceIdentity rx_data;

    for (uint16_t i = 0; i < m_assumedPortCount || m_assumedPortCount == 0; i++) {
        if (!receiveManagementResponse(MID_CUSTOM_PORT_PROPERTIES, seq, rx_data)) {
            break;
        }

        constexpr size_t kPrefixLen = sizeof(ptp::PortIdentity) + 3;
        if (rx_data.bytes.size() < kPrefixLen) {
            spdlog::warn("[PTP] [PORT_PROPERTIES_NP] Response too short: {} bytes", rx_data.bytes.size());
            continue;
        }

        auto& port = port_propertieses.emplace_back();
        std::memcpy(&port.source, rx_data.bytes.data(), sizeof(ptp::PortIdentity));
        port.source.portNumber = be16toh(port.source.portNumber);

        const uint8_t nameLength = rx_data.bytes[sizeof(ptp::PortIdentity) + 2];
        const size_t nameOffset = sizeof(ptp::PortIdentity) + 3;
        if (nameOffset + static_cast<size_t>(nameLength) > rx_data.bytes.size()) {
            spdlog::warn("[PTP] [PORT_PROPERTIES_NP] Invalid ifName length {} for payload {}", nameLength,
                         rx_data.bytes.size());
            continue;
        }

        const auto* begin = reinterpret_cast<const char*>(rx_data.bytes.data() + nameOffset);
        port.ifName.assign(begin, begin + nameLength);
    }
    return true;
}

/**
 * @brief Creates a list of @ref PtpPerformanceRecord_t structs that contains the record for the currently active
 * 15-minute period at the first index, followed by the records of the completed 15-minute periods.
 *
 * @param out The list to be filled.
 */
void PtpManager::getPerformance15m(std::vector<PtpPerformanceRecord_t>& out) {
    std::lock_guard<std::mutex> lock(m_perfMutex);
    out.clear();
    out.reserve(NUMBER_RECORDS_15M + 1);
    out.emplace_back(false, m_current15m.startTime10ms, false, m_current15m.offsetFromMaster.toParameters(),
                     m_current15m.meanPathDelay.toParameters());
    std::ranges::copy(m_completedRecords15m, std::back_inserter(out));
}

/**
 * @brief Creates a list of @ref PtpPerformanceRecord_t structs that contains the record for the currently active
 * 24-hour period at the first index, followed by the records of the completed 24-hour periods.
 *
 * @param out The list to be filled.
 */
void PtpManager::getPerformance24h(std::vector<PtpPerformanceRecord_t>& out) {
    std::lock_guard<std::mutex> lock(m_perfMutex);
    out.clear();
    out.reserve(NUMBER_RECORDS_24H + 1);
    out.emplace_back(false, m_current24h.startTime10ms, false, m_current24h.offsetFromMaster.toParameters(),
                     m_current24h.meanPathDelay.toParameters());
    std::ranges::copy(m_completedRecords24h, std::back_inserter(out));
}

/**
 * @brief For a given port-index, this function creates a list of @ref PtpPortPerformanceRecord_t structs that contains
 * the record for the currently active 15-minute period at the first index, followed by the records of the completed
 * 15-minute periods.
 *
 * @param portIndex The index of the port for which this list should contain its entries.
 * @param out The list to be filled.
 */
void PtpManager::getPortPerformance15m(uint16_t portIndex, std::vector<PtpPortPerformanceRecord_t>& out) {
    std::lock_guard<std::mutex> lock(m_perfMutex);
    out.clear();
    out.reserve(NUMBER_RECORDS_15M + 1);
    out.emplace_back(m_current15m.startTime10ms, m_current15m.portMeanLinkDelay[portIndex].toParameters());
    std::ranges::copy(m_completedPortRecords15m[portIndex], std::back_inserter(out));
}

/**
 * @brief For a given port-index, this function creates a list of @ref PtpPortPerformanceRecord_t structs that contains
 * the record for the currently active 24-hour period at the first index, followed by the records of the completed
 * 24-hour periods.
 *
 * @param portIndex The index of the port for which this list should contain its entries.
 * @param out The list to be filled.
 */
void PtpManager::getPortPerformance24h(uint16_t portIndex, std::vector<PtpPortPerformanceRecord_t>& out) {
    std::lock_guard<std::mutex> lock(m_perfMutex);
    out.clear();
    out.reserve(NUMBER_RECORDS_24H + 1);
    out.emplace_back(m_current24h.startTime10ms, m_current24h.portMeanLinkDelay[portIndex].toParameters());
    std::copy(m_completedPortRecords24h[portIndex].begin(), m_completedPortRecords24h[portIndex].end(),
              std::back_inserter(out));
}

/**
 * @brief This function queries all datasets and fills the provided @ref PtpNode_t with the relevant data defined as
 * `config false;` in the YANG model.
 *
 * @param node_to_fill The @ref PtpNode_t that should be filled
 */
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

/**
 * @brief This function queries the default dataset and clock description, and fills the provided @ref PtpNode_t with
 * the relevant configuration data as defined in the YANG model.
 *
 * @param node_to_fill The @ref PtpNode_t that should be filled
 */
void PtpManager::fillConfigData(PtpNode_t& node_to_fill) {
    ptp::DefaultDs default_ds = {};
    std::vector<ptp::ClockDescription> clock_descriptions = {};
    std::vector<ptp::PortProperties> port_propertieses = {};
    if (!getDefaultDataSet(default_ds)) {
        return;
    }
    if (!getClockDescriptions(clock_descriptions)) {
        return;
    }
    if (!getPortPropertiesNp(port_propertieses)) {
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

    for (auto& port_properties : port_propertieses) {
        auto& port = node_to_fill.ports.emplace_back();
        port.portIndex = port_properties.source.portNumber;
        port.underlyingInterface = port_properties.ifName;
    }
}

/**
 * @brief Internal helper to send messages to the PTP socket.
 *
 * @param managementId The management ID of the message to be sent.
 * @return The sequence ID of this message. Used to correlate a request with a response.
 */
int32_t PtpManager::sendManagementGet(uint16_t managementId) {
    if (!enabled) {
        spdlog::warn(
            "[PTP] [SEND_GET] Attempted to send management message with MID {} while PTP-integration is disabled",
            managementId);
        return -1;
    }
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
        spdlog::warn("Sending of buffer failed for MID {} with errno {}", managementId, errno);
        return -1;
    }

    return current_seq;
}

/**
 * @brief Internal helper method to receive responses from the PTP daemon. If a request can generate multiple responses,
 * this function must be called at least that many times, or until no more responses are received.
 *
 * @param expectedId The management ID of the request, must be the same as in the response.
 * @param expectedSeq The sequence ID of the request, must be the same as in the response.
 * @param out_data The @ref ptp::ResponseWithSourceIdentity struct into which the data should be written. This struct
 * contains the identity of the port about which the response is, as well as the list of raw bytes.
 * @return True if the data was received successfully, False otherwise.
 */
bool PtpManager::receiveManagementResponse(uint16_t expectedId, uint16_t expectedSeq,
                                           ptp::ResponseWithSourceIdentity& out_data) const {
    if (!enabled) {
        spdlog::warn("[PTP] [SEND_GET] Attempted to receive management message while PTP-integration is disabled");
        return false;
    }
    SPDLOG_TRACE("[PTP] [RECV_RESP] Receiving response with expected MID {}", expectedId);
    std::vector<uint8_t> rx_buffer(1024);
    struct pollfd pfd{};
    pfd.fd = m_fd;
    pfd.events = POLLIN;

    // Use poll() to mirror the Intel code's bulletproof receive logic
    int ret = poll(&pfd, 1, 50);
    if (ret <= 0) {  // Timeout or error
        spdlog::warn("[PTP] [RECV_RESP] Timeout (or Error) when receiving response with expected MID {} with code {}",
                     expectedId, errno);
        return false;
    }

    // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection): socket transaction must stay serialized.
    const ssize_t rx_bytes = recv(m_fd, rx_buffer.data(), rx_buffer.size(), 0);
    const auto min_msg_len = static_cast<ssize_t>(sizeof(ptp::PtpManagementMsg) + sizeof(ptp::PtpManagementTlv));
    if (rx_bytes < min_msg_len) {
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

    const uint16_t tlv_len = ntohs(tlv.lengthField);
    if (tlv_len < 2) {
        spdlog::warn("[PTP] [RECV_RESP] Invalid TLV length {}", tlv_len);
        return false;
    }

    const auto payload_len = static_cast<size_t>(tlv_len - 2);
    if (payload_len == 0) {
        SPDLOG_TRACE("[PTP] [RECV_RESP] Empty response", expectedId);
        out_data.bytes.clear();
    } else {
        const size_t required_bytes = sizeof(ptp::PtpManagementMsg) + sizeof(ptp::PtpManagementTlv) + payload_len;
        if (static_cast<size_t>(rx_bytes) >= required_bytes) {
            SPDLOG_TRACE("[PTP] [RECV_RESP] Valid Response", expectedId);

            uint8_t* tlv_payload = rx_buffer.data() + sizeof(ptp::PtpManagementMsg) + sizeof(ptp::PtpManagementTlv);
            out_data.bytes.assign(tlv_payload, tlv_payload + payload_len);
        } else {
            spdlog::warn("[PTP] [RECV_RESP] Invalid payload length", expectedId);
            return false;
        }
    }
    return true;
}
