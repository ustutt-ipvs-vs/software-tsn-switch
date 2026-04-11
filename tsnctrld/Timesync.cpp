#include "Timesync.h"

#include <dirent.h>
#include <fmt/ranges.h>
#include <sys/stat.h>
#include <sys/timex.h>

#include <algorithm>
#include <fstream>
#include <iostream>

#include "SubprocessManager.h"

/**
 * @brief Start the daemons for synchronizing CLOCK_REALTIME using gPTP
 *
 * The daemons will keep running until the process that called this method terminates. Until then, do not call this
 * method a second time.
 *
 * @param nicVec The NIC names to run gPTP on, utilizing their PHCs. The vector must contain at least one element.
 * @param asGrandmaster Whether this device should act as a grandmaster. Sets this device's PTP "priority1" attribute to
 * 10 instead of 248.
 * @param disciplineWithNtp If @p asGrandmaster is @a false, this argument must be @a false and then has no effect. If
 * this parameter is @a true, CLOCK_REALTIME will be used as the grandmaster clock and chrony will be used to
 * continuously discipline it using NTP and a very light slew. If this parameter is @a false, chrony is used only once
 * at startup for a one-time NTP sync, synchronizing both CLOCK_REALTIME and the PHCs of all involved NICs.
 */
void Timesync::launch(const std::vector<std::string>& nicVec, bool asGrandmaster, bool disciplineWithNtp) {
    SPDLOG_TRACE("[TIMESYNC] Starting, parameters: NICs={0}, disciplineWithNtp={1}, asGrandmaster={2}", nicVec,
                 disciplineWithNtp, asGrandmaster);
    if (geteuid() != 0) {
        spdlog::critical("[TIMESYNC] No root access (did you forget sudo?)");
        exit(1);
    }
    if (nicVec.empty()) {
        spdlog::critical("[TIMESYNC] No NIC provided, you need to provide at least one");
        exit(1);
    }
    if (!asGrandmaster && disciplineWithNtp) {
        spdlog::critical("[TIMESYNC] Internal error: asGrandmaster is false and disciplineWithNtp is true");
        exit(1);
    }
    // Check the NIC names for non-alphanumeric characters to prevent command injection attacks
    for (const std::string& nic : nicVec) {
        if (!isAlphanumeric(nic)) {
            spdlog::critical("[TIMESYNC] A provided NIC contains illegal characters: {}", nic);
            exit(1);
        }
    }
    warnAboutActiveNtp();
    warnAboutInterferingProcesses({"chronyd", "ptp4l", "phc2sys"});
    if (asGrandmaster) {
        syncWithNtp(nicVec, disciplineWithNtp);
    }
    runPtp4l(nicVec, asGrandmaster);
    sleep(1);  // Wait for ptp4l to start before proceeding
    configurePtp4l();
    runPhc2sys(nicVec, disciplineWithNtp);
    setTaiUtcOffset(CURRENT_UTC_OFFSET);
    spdlog::info("[TIMESYNC] All clock synchronization daemons have been launched on this device!");
}

/**
 * @brief Use chrony to use NTP to sync CLOCK_REALTIME and the PHCs of provided NICs
 *
 * @param nicVec The NICs whose PHCs to one-time sync. Be mindful of command injection attacks. This parameter is not
 * used if @p disciplineWithNtp is @a false.
 * @param disciplineWithNtp If @a true, chrony will continuously discipline CLOCK_REALTIME via NTP with a very light
 * slew in a separate process. The PHCs won't be synchronized at all, that's phc2sys's job. If @a false, chrony will
 * synchronize CLOCK_REALTIME and the PHCs of provided NICs once, blocking until the process is complete.
 */
void Timesync::syncWithNtp(const std::vector<std::string>& nicVec, bool disciplineWithNtp) {
    executableExistsOrErr("chronyd");
    std::vector<std::string> chronyCmd = {
        "chronyd",
        // Do not detach from terminal
        "-n",
        // Look for config files with NTP sources here
        "sourcedir /run/chrony-dhcp", "sourcedir /etc/chrony/sources.d",
        // File containing ID/key pairs for NTP authentication
        "keyfile /etc/chrony/chrony.keys",
        // Rate information will be stored in this file
        "driftfile /var/lib/chrony/chrony.drift",
        // Where to store NTS keys and cookies
        "ntsdumpdir /var/lib/chrony",
        // Log files location
        "logdir /var/log/chrony",
        // Stop bad estimates upsetting machine clock
        "maxupdateskew 100.0",
        // Also sync real-time clock every 11 minutes
        "rtcsync",
        // Step instead of slew in the first 3 updates
        "makestep 1.0 3",
        // Get TAI-UTC offset and leap seconds from the system tz database.
        // This directive must be commented out when using time sources serving
        // leap-smeared time.
        "leapseclist /usr/share/zoneinfo/leap-seconds.list",
        // Slew the clock with 694ppm, correcting about 1 minute in a 24 hour period
        "maxslewrate 694",
        // When a leap second occurs, slew the clock instead of skipping/repeating a second
        "leapsecmode slew"};
    if (disciplineWithNtp) {
        // Keep chrony running in the background, continuously disciplining CLOCK_REALTIME
        spdlog::info("[TIMESYNC] Starting up chrony do discipline grandmaster (detached)");
        SPDLOG_DEBUG("[TIMESYNC] chrony is the following command: {}", chronyCmd);
        SubprocessManager::run(
            "chrony", chronyCmd, true,
            [](const std::string& commandName, const std::string& commandOutput) {
                logCommandOutput(commandName, commandOutput, spdlog::level::level_enum::err);
            },
            [chronyCmd](const std::string& commandName, int exitCode) {
                if (exitCode != 0) {
                    logCommandAndExit(commandName, chronyCmd);
                }
            });
    } else {
        // Synchronize once, then terminate chrony
        chronyCmd.emplace_back("-q");
        spdlog::info("[TIMESYNC] Starting up chrony for one-time sync (blocking)");
        SPDLOG_DEBUG("[TIMESYNC] chrony is the following command: {}", chronyCmd);
        SubprocessManager::run(
            "chrony", chronyCmd, false,
            [](const std::string& commandName, const std::string& commandOutput) {
                logCommandOutput(commandName, commandOutput, spdlog::level::level_enum::debug);
            },
            [chronyCmd](const std::string& commandName, int exitCode) {
                if (exitCode != 0) {
                    logCommandAndExit(commandName, chronyCmd);
                }
            });
        spdlog::info("[TIMESYNC] chrony completed a one-time sync");

        // One-time sync PHCs to the now up-to-date system clock
        executableExistsOrErr("phc_ctl");
        for (size_t i = 0; i < nicVec.size(); ++i) {
            std::string phcSetName = "phc_set_" + std::to_string(i);
            // Set PHC to CLOCK_REALTIME
            std::vector<std::string> phcSetCmd = {"phc_ctl", nicVec[i], "set"};
            SPDLOG_DEBUG("[TIMESYNC] Setting PHC with command: {}", phcSetCmd);
            SubprocessManager::run(
                phcSetName, phcSetCmd, false,
                [](const std::string& commandName, const std::string& commandOutput) {
                    logCommandOutput(commandName, commandOutput, spdlog::level::level_enum::debug);
                },
                [phcSetCmd](const std::string& commandName, int exitCode) {
                    if (exitCode != 0) {
                        logCommandAndExit(commandName, phcSetCmd);
                    }
                });
            std::string phcAdjName = "phc_adj_" + std::to_string(i);
            // Adjust PHC by TAI-UTC offset
            std::vector<std::string> phcAdjCmd = {"phc_ctl", nicVec[i], "adj", std::to_string(CURRENT_UTC_OFFSET)};
            SPDLOG_DEBUG("[TIMESYNC] Adjusting PHC with command: {}", phcAdjCmd);
            SubprocessManager::run(
                phcAdjName, phcAdjCmd, false,
                [](const std::string& commandName, const std::string& commandOutput) {
                    logCommandOutput(commandName, commandOutput, spdlog::level::level_enum::debug);
                },
                [phcAdjCmd](const std::string& commandName, int exitCode) {
                    if (exitCode != 0) {
                        logCommandAndExit(commandName, phcAdjCmd);
                    }
                });
        }
        spdlog::info("[TIMESYNC] PHCs have been set");
    }
}

/**
 * @brief Start the ptp4l daemon, synchronizing the PHCs via gPTP
 *
 * @param nicVec The NICs to run gPTP on, synchronizing their PHCs. Be mindful of command injection attacks. You must
 * provide at least one NIC.
 * @param asGrandmaster If true, use a higher PTP priority1 so that this device is more likely to host the grandmaster
 * clock.
 */
void Timesync::runPtp4l(const std::vector<std::string>& nicVec, bool asGrandmaster) {
    executableExistsOrErr("ptp4l");
    std::vector<std::string> ptpCmd = {
        "ptp4l",
        // gPTP configuration options
        "--gmCapable=1", "--priority2=248", "--logAnnounceInterval=0", "--logSyncInterval=-3", "--syncReceiptTimeout=3",
        "--neighborPropDelayThresh=800", "--min_neighbor_prop_delay=-20000000", "--assume_two_step=1",
        "--path_trace_enabled=1", "--follow_up_info=1", "--transportSpecific=0x1", "--ptp_dst_mac=01:80:C2:00:00:0E",
        "--network_transport=L2", "--delay_mechanism=P2P", "--boundary_clock_jbod=1", "--step_threshold=1",
        ("--utc_offset=" + std::to_string(CURRENT_UTC_OFFSET))};
    for (const std::string& nic : nicVec) {
        ptpCmd.emplace_back("-i");
        ptpCmd.emplace_back(nic);
    }
    if (asGrandmaster) {
        ptpCmd.emplace_back("--priority1=10");
    } else {
        ptpCmd.emplace_back("--priority1=248");
    }
    spdlog::info("[TIMESYNC] Starting up ptp4l daemon (detached)");
    SPDLOG_DEBUG("[TIMESYNC] ptp4l is the following command: {}", ptpCmd);
    SubprocessManager::run(
        "ptp4l", ptpCmd, true,
        [](const std::string& commandName, const std::string& commandOutput) {
            logCommandOutput(commandName, commandOutput, spdlog::level::level_enum::err);
        },
        [ptpCmd](const std::string& commandName, int exitCode) {
            if (exitCode != 0) {
                logCommandAndExit(commandName, ptpCmd);
            }
        });
}

/**
 * @brief Configure a running instance of ptp4l with a TAI-UTC offset
 *
 * This must happen after ptp4l has been launched and already started running and before phc2sys is launched.
 */
void Timesync::configurePtp4l() {
    executableExistsOrErr("pmc");
    std::vector<std::string> pmcCmd = {"pmc",
                                       "-u",
                                       "-b",
                                       "0",
                                       "-t",
                                       "1",
                                       "SET GRANDMASTER_SETTINGS_NP clockClass 248 clockAccuracy 0xfe "
                                       "offsetScaledLogVariance 0xffff currentUtcOffset " +
                                           std::to_string(CURRENT_UTC_OFFSET) +
                                           " leap61 0 leap59 0 currentUtcOffsetValid 1 ptpTimescale 1 timeTraceable 1 "
                                           "frequencyTraceable 0 timeSource 0xa0"};
    spdlog::info("[TIMESYNC] Using pmc to configure ptp4l");
    SPDLOG_DEBUG("[TIMESYNC] pmc is the following command: {}", pmcCmd);
    SubprocessManager::run(
        "pmc", pmcCmd, false,
        [](const std::string& commandName, const std::string& commandOutput) {
            logCommandOutput(commandName, commandOutput, spdlog::level::level_enum::debug);
        },
        [pmcCmd](const std::string& commandName, int exitCode) {
            if (exitCode != 0) {
                logCommandAndExit(commandName, pmcCmd);
            }
        });
}

/**
 * @brief Start the phc2sys daemon, synchronizing PHCs on this device and CLOCK_REALTIME
 *
 * @param nicVec This parameter is only used if @p disciplineWithNtp is @a true. If it is, CLOCK_REALTIME will
 * discipline the PHCs of these NICs. Be mindful of command injection attacks.
 * @param disciplineWithNtp If @a true, CLOCK_REALTIME will discipline the PHCs specified in @p nicVec. If @a false,
 * all NICs involved in the currently running ptp4l instance are synced with each other and together discipline
 * CLOCK_REALTIME.
 */
void Timesync::runPhc2sys(const std::vector<std::string>& nicVec, bool disciplineWithNtp) {
    executableExistsOrErr("phc2sys");
    std::vector<std::string> phcCmd = {"phc2sys", "--step_threshold=1", "--transportSpecific=1"};
    if (disciplineWithNtp) {
        phcCmd.emplace_back("-s");
        phcCmd.emplace_back("CLOCK_REALTIME");
        for (const std::string& nic : nicVec) {
            phcCmd.emplace_back("-c");
            phcCmd.emplace_back(nic);
        }
    } else {
        phcCmd.emplace_back("-a");
        phcCmd.emplace_back("-r");
    }
    spdlog::info("[TIMESYNC] Starting up phc2sys daemon (detached)");
    SPDLOG_DEBUG("[TIMESYNC] phc2sys is the following command: {}", phcCmd);
    SubprocessManager::run(
        "phc2sys", phcCmd, true,
        [](const std::string& commandName, const std::string& commandOutput) {
            logCommandOutput(commandName, commandOutput, spdlog::level::level_enum::err);
        },
        [phcCmd](const std::string& commandName, int exitCode) {
            if (exitCode != 0) {
                logCommandAndExit(commandName, phcCmd);
            }
        });
}

/**
 * @brief Set the device's TAI - UTC offset
 *
 * @param offset How many seconds TAI should be ahead of UTC (can also be negative)
 */
void Timesync::setTaiUtcOffset(int offset) {
    struct timex timexStruct;
    timexStruct.modes = ADJ_TAI;
    timexStruct.constant = offset;

    int result = adjtimex(&timexStruct);
    if (result < 0) {
        spdlog::critical("Failed to adjust UTC-TAI offset");
        exit(1);
    }
    SPDLOG_DEBUG("[TIMESYNC] TAI - UTC = {} seconds", timexStruct.tai);
}

/**
 * @brief Check whether Debian's default NTP service is running and write a warning to the log if it does
 */
void Timesync::warnAboutActiveNtp() {
    std::vector<std::string> ntpCmd = {"timedatectl", "show", "-p", "NTP", "--value"};
    SPDLOG_TRACE("[TIMESYNC] Now checking whether NTP is running");
    SubprocessManager::run(
        "ntp_check", ntpCmd, false,
        [](const std::string& commandName, const std::string& commandOutput) {
            if (commandOutput == "yes") {
                spdlog::warn(
                    "[TIMESYNC] NTP is enabled! It might interfere with time synchronization. Hint: 'sudo timedatectl "
                    "set-ntp false' disables NTP");
            }
        },
        [](const auto&...) {});  // Empty lambda function
}

/**
 * @brief Check whether processes are running that might interfere with timesyncing
 *
 * If processes are found, write a warning into the log. This is only a sporadic check and might miss processes.
 */
void Timesync::warnAboutInterferingProcesses(const std::unordered_set<std::string>& badProcessNames) {
    DIR* processesDir = opendir("/proc/");
    if (processesDir == nullptr) {
        spdlog::warn(
            "[TIMESYNC] Unable to check for potential interfering processes due to not being able to access /proc/");
        return;
    }
    dirent* singleProcessDir;
    while ((singleProcessDir = readdir(processesDir)) != nullptr) {
        char* processDirName = singleProcessDir->d_name;
        // Only directories consisting of digits interest us
        if (isdigit(processDirName[0]) == 0) {
            continue;
        }
        std::string commFilePath = std::string("/proc/") + processDirName + "/comm";
        std::ifstream commFile(commFilePath);
        std::string processName;

        if (std::getline(commFile, processName)) {
            if (badProcessNames.contains(processName)) {
                spdlog::warn(
                    "[TIMESYNC] Process '{0}' is already running, as PID {1}. It might interfere with tsnctrld's own "
                    "timesyncing daemons!",
                    processName, processDirName);
            }
        }
    }
}

/**
 * @brief Exit the program if the provided executable doesn't exist in PATH
 *
 * @param exec The executable's name. Alternatively, if the string contains a `/`, it is treated as a path instead.
 */
void Timesync::executableExistsOrErr(const std::string& exec) {
    // If the executable's path contains a slash, use access() directly to find it
    if (exec.find('/') != std::string::npos) {
        if (executableExistsThere(exec)) {
            return;  // File found!
        }
        spdlog::critical("[TIMESYNC] Required executable '{0}' doesn't exist or not accessible", exec);
        exit(1);
    }

    const char* pathEnv = std::getenv("PATH");
    if (pathEnv == nullptr) {
        spdlog::critical("[TIMESYNC] Unable to access PATH", exec);
        exit(1);
    }

    std::string pathStr(pathEnv);
    std::stringstream pathStringStream(pathStr);
    std::string dir;

    // Separate PATH into components
    while (std::getline(pathStringStream, dir, ':')) {
        std::string fullPath = std::string(dir).append("/").append(exec);
        if (executableExistsThere(fullPath)) {
            return;  // File found!
        }
    }
    // File not found
    spdlog::critical("[TIMESYNC] Required executable '{0}' doesn't exist or not accessible", exec);
    exit(1);
}

/**
 * @brief Whether the provided exectuable exists on the given path
 *
 * @param path An absolute path of a (potential) executable, starting with /
 * @return Whether the given path exists, is a file, and executeable
 */
bool Timesync::executableExistsThere(const std::string& path) {
    if (access(path.c_str(), X_OK) == 0) {
        // Make sure it's not a folder
        struct stat fileAttributes;
        if (stat(path.c_str(), &fileAttributes) == 0 && S_ISREG(fileAttributes.st_mode)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Print an output of a command to the log
 *
 * @param commandName The informal name of the command
 * @param commandOutput One or more lines (divided by \\n) of command output. Should not end with \\n
 * @param loggingLevel The spdlog logging level to create the log at
 */
void Timesync::logCommandOutput(const std::string& commandName, const std::string& commandOutput,
                                spdlog::level::level_enum loggingLevel) {
    spdlog::log(loggingLevel, "[{0}] {1}", commandName, commandOutput);
}

/**
 * @brief Print the command to the log and exit the program
 *
 * @param commandName The informal name of the command
 * @param command The full command, split into its components
 */
void Timesync::logCommandAndExit(const std::string& commandName, const std::vector<std::string>& command) {
    spdlog::critical("[TIMESYNC] Command '{0}' has encountered an error. Full command: {1}", commandName, command);
    exit(1);
}

/**
 * @brief Whether the string contains only alphanumeric characters
 *
 * @param str The string to test
 * @return Whether @p str contains only alphanumeric characters, the empty string returns @a true
 */
bool Timesync::isAlphanumeric(const std::string& str) {
    return std::ranges::all_of(str, [](unsigned char ch) { return std::isalnum(ch); });
}
