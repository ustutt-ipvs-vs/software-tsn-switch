#ifndef ENPRO_SUBPROCESS_MANAGER
#define ENPRO_SUBPROCESS_MANAGER
#include <functional>
#include <string>
#include <vector>

/**
 * @brief Runs shell commands and shuts them down when the parent process exits.
 */
class SubprocessManager {
   public:
    static void run(const std::string& processName, const std::vector<std::string>& command, bool async,
                    const std::function<void(const std::string&, const std::string&)>& outputCallback,
                    const std::function<void(const std::string&, int)>& exitCallback);

   private:
    static int watchdogPipeWriter;
    static void runWatchdog();
};

#endif  // ENPRO_SUBPROCESS_MANAGER