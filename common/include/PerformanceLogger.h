#pragma once
#include <spdlog/spdlog.h>

#include <memory>

#ifdef ENABLE_PERF_LOGGING

// Function that handles the thread-safe lazy-initialization
extern std::shared_ptr<spdlog::logger> get_perf_logger();

#define PERFORMANCE_LOGGING(context, action)          \
    do {                                              \
        if (auto logger = get_perf_logger()) {        \
            logger->info("{} | {}", context, action); \
        }                                             \
    } while (0)
#else
// Completely vanishes if not in RelWithDebInfo
#define PERFORMANCE_LOGGING(context, action) \
    do {                                     \
    } while (0)
#endif
