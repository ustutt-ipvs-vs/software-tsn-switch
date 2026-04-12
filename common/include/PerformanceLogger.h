#pragma once
#include <spdlog/spdlog.h>

#include <memory>

#ifdef ENABLE_PERF_LOGGING

void log_perf_internal(const char* context, const std::string& message);

template <typename... Args>
void log_perf_external(const char* context, fmt::format_string<Args...> fmt, Args&&... args) {
    log_perf_internal(context, fmt::format(fmt, std::forward<Args>(args)...));
}

inline void log_perf_external(const char* context, const char* message) {
    log_perf_internal(context, message);
}
#define PERFORMANCE_LOGGING(context, fmt_or_msg, ...) log_perf_external(context, fmt_or_msg, ##__VA_ARGS__)
#else
#define PERFORMANCE_LOGGING(context, fmt_or_msg, ...) ((void)0)
#endif
