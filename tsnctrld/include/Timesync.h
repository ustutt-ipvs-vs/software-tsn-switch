#ifndef ENPRO_TIMESYNC
#define ENPRO_TIMESYNC
#include <spdlog/spdlog.h>

#include <string>
#include <unordered_set>
#include <vector>

/**
 * @brief Launches and manages daemons required for clock synchronization.
 */
class Timesync {
   public:
    /// @brief How many seconds TAI is ahead of UTC due to leap seconds
    static const int CURRENT_UTC_OFFSET = 37;
    static void launch(const std::vector<std::string>& nicVec, bool asGrandmaster, bool disciplineWithNtp);

   private:
    static void syncWithNtp(const std::vector<std::string>& nicVec, bool disciplineWithNtp);
    static void runPtp4l(const std::vector<std::string>& nicVec, bool asGrandmaster);
    static void configurePtp4l();
    static void runPhc2sys(const std::vector<std::string>& nicVec, bool disciplineWithNtp);
    static void setTaiUtcOffset(int offset);
    static void warnAboutActiveNtp();
    static void warnAboutInterferingProcesses(const std::unordered_set<std::string>& badProcessNames);
    static void executableExistsOrErr(const std::string& exec);
    static bool executableExistsThere(const std::string& path);
    static void logCommandOutput(const std::string& commandName, const std::string& commandOutput,
                                 spdlog::level::level_enum loggingLevel);
    static void logCommandAndExit(const std::string& commandName, const std::vector<std::string>& command);
    static bool isValidNicName(const std::string& s);
};

#endif  // ENPRO_TIMESYNC