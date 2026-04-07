#pragma once
#include <spdlog/spdlog.h>

#include <memory>

#ifdef ENABLE_PERF_LOGGING

void log_perf_internal(const char* context, const char* action);
#define PERFORMANCE_LOGGING(context, action) log_perf_internal(context, action)
#else
#define PERFORMANCE_LOGGING(context, action) ((void)0)
#endif
