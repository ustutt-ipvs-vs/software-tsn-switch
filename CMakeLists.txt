cmake_minimum_required(VERSION 3.20)
project(enpro-switch)

# Sysrepo-cpp requires C++17 or higher
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Find LibNetconf2 ---
find_path(LIBNETCONF2_INCLUDE_DIR
        NAMES libnetconf2/session_client.h
        PATHS /usr/local/include /usr/include
        # NO_DEFAULT_PATH # same as above
)
find_library(LIBNETCONF2_LIBRARY
        NAMES netconf2 libnetconf2
        PATHS /usr/local/lib /usr/lib
        # NO_DEFAULT_PATH # same as above
)

if (NOT LIBNETCONF2_INCLUDE_DIR OR NOT LIBNETCONF2_LIBRARY)
    message(FATAL_ERROR "Could not find libnetconf2!")
else()
    message(STATUS "Found libnetconf2 headers: ${LIBNETCONF2_INCLUDE_DIR}")
    message(STATUS "Found libnetconf2 library: ${LIBNETCONF2_LIBRARY}")
endif()

# ==============================================================================
# 1. FIX REMOTE ENVIRONMENT
# ==============================================================================
# Force CMake to find .pc files on the remote machine
# Should work now due to full reinstall, manually confirmed the files are present in that folder
set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:/usr/local/lib/pkgconfig:/usr/lib/pkgconfig:/usr/local/lib64/pkgconfig")

find_package(PkgConfig REQUIRED)

# Find C++ Libraries
pkg_check_modules(SPDLOG REQUIRED spdlog IMPORTED_TARGET)

# Find C++ Bindings
pkg_check_modules(SYSREPO_CPP REQUIRED sysrepo-cpp IMPORTED_TARGET)
pkg_check_modules(LIBYANG_CPP REQUIRED libyang-cpp IMPORTED_TARGET)
pkg_check_modules(LIBNETCONF2_CPP REQUIRED libnetconf2-cpp IMPORTED_TARGET)

# Find C Libraries
pkg_check_modules(SYSREPO REQUIRED sysrepo IMPORTED_TARGET)
pkg_check_modules(LIBYANG REQUIRED libyang IMPORTED_TARGET)
pkg_check_modules(LIBNETCONF2 REQUIRED libnetconf2 IMPORTED_TARGET)
pkg_check_modules(LLDPCTL REQUIRED lldpctl IMPORTED_TARGET)


# ==============================================================================
# 2. INTERNAL LIBRARIES
# ==============================================================================
# Build the internal helper library first
add_subdirectory(tsnctrld)
add_subdirectory(common) # maybe not needed?
add_subdirectory(cnc)



# ==============================================================================
# 3. GLOBAL SETTINGS
# ==============================================================================
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "/usr/local/lib")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)



# ==============================================================================
# 4. EXECUTABLES
# ==============================================================================
# --- RUNNABLE FILES ---

add_executable(tsnctrld_app tsnctrld/tsnctrld.cpp)
target_link_libraries(
    tsnctrld_app
    PUBLIC
    PkgConfig::SYSREPO_CPP
    PkgConfig::LIBYANG_CPP
    PkgConfig::SPDLOG
)
target_link_libraries(tsnctrld_app PRIVATE tsnctrld_lib)

# --- Netlink ---
#add_executable(enpro-switch tsnctrld/QdiscManager.cpp)
#target_link_libraries(enpro-switch PRIVATE tsnctrld_lib)



# ==============================================================================
# 5. TESTS
# ==============================================================================
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
# For Windows: prevent overriding parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()

add_subdirectory(tsnctrld/tests)
