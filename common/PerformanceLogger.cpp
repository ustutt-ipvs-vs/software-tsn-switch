#include "PerformanceLogger.h"

#ifdef ENABLE_PERF_LOGGING

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <filesystem>
#include <mutex>

static std::shared_ptr<spdlog::logger> g_perf_logger = nullptr;
static std::once_flag g_init_flag;

static std::string get_executable_name() {
    char buffer[1024];
    // Read the symlink /proc/self/exe to get the full path to the binary
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        // Use std::filesystem to get just the filename (e.g., "tsnctrld_app")
        return std::filesystem::path(buffer).filename().string();
    }
    return "unknown_app";
}

static void init_logger() {
    // 1. Initialize the async background thread
    spdlog::init_thread_pool(1048576, 1);

    // 2. Create the file sink
    std::string app_name = get_executable_name();

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream filename_ss;
    filename_ss << "performance_trace_" << app_name << "_"
                << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d_%H-%M-%S") << "_pid" << getpid() << ".log";

    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename_ss.str(), true);

    // 3. Create the logger with the "overrun_oldest" policy so it NEVER blocks your sysrepo threads
    g_perf_logger = std::make_shared<spdlog::async_logger>("perf_logger", file_sink, spdlog::thread_pool(),
                                                           spdlog::async_overflow_policy::overrun_oldest);

    // 4. Set the high-precision timestamp format
    g_perf_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%f] | Thread:%t | %v");

    spdlog::flush_every(std::chrono::seconds(5));

    // 5. Register it globally
    spdlog::register_logger(g_perf_logger);

    g_perf_logger->info("Executable={}", app_name);
}

// Lazy initialization accessor
std::shared_ptr<spdlog::logger> get_perf_logger() {
    std::call_once(g_init_flag, init_logger);
    return g_perf_logger;
}

void log_perf_internal(const char* context, const std::string& message) {
    if (auto logger = get_perf_logger()) {
        logger->info("{} | {}", context, message);
    }
}
#endif  // ENABLE_PERF_LOGGING