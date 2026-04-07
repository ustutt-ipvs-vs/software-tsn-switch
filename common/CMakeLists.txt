project(common)

set(SOURCE_FILES
        PerformanceLogger.cpp
)

add_library(common_lib STATIC ${SOURCE_FILES})

target_include_directories(common_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(common_lib PUBLIC
        PkgConfig::SPDLOG
)

if (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_compile_definitions(common_lib PUBLIC ENABLE_PERF_LOGGING)
endif ()