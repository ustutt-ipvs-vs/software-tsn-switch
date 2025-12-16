cmake_minimum_required(VERSION 3.20)
project(enpro-switch)

# ==============================================================================
# 1. ROBUST LIBRARY DISCOVERY
# We manually look for the headers and binaries.
# This fixes the "Missing .pc file" issue and the "IDE Indexing" issue.
# ==============================================================================

# --- Find LibYang ---
# Look for libyang.h in common paths + /usr/local/include
find_path(LIBYANG_INCLUDE_DIR
        NAMES libyang/libyang.h
        PATHS /usr/local/include /usr/include
        NO_DEFAULT_PATH
)
# Look for libyang.so in common paths + /usr/local/lib
find_library(LIBYANG_LIBRARY
        NAMES yang libyang
        PATHS /usr/local/lib /usr/lib
        NO_DEFAULT_PATH
)

if (NOT LIBYANG_INCLUDE_DIR OR NOT LIBYANG_LIBRARY)
    message(FATAL_ERROR "Could not find libyang! Checked /usr/local/include and /usr/local/lib")
else()
    message(STATUS "Found libyang headers: ${LIBYANG_INCLUDE_DIR}")
    message(STATUS "Found libyang library: ${LIBYANG_LIBRARY}")
endif()

# --- Find Sysrepo ---
find_path(SYSREPO_INCLUDE_DIR
        NAMES sysrepo.h
        PATHS /usr/local/include /usr/include
        NO_DEFAULT_PATH
)
find_library(SYSREPO_LIBRARY
        NAMES sysrepo libsysrepo
        PATHS /usr/local/lib /usr/lib
        NO_DEFAULT_PATH
)

if (NOT SYSREPO_INCLUDE_DIR OR NOT SYSREPO_LIBRARY)
    message(FATAL_ERROR "Could not find sysrepo!")
else()
    message(STATUS "Found sysrepo headers: ${SYSREPO_INCLUDE_DIR}")
    message(STATUS "Found sysrepo library: ${SYSREPO_LIBRARY}")
endif()

# ==============================================================================
# 2. GLOBAL SETTINGS
# ==============================================================================

# Crucial for CLion: This tells the IDE exactly where the headers are.
include_directories(${LIBYANG_INCLUDE_DIR} ${SYSREPO_INCLUDE_DIR})
add_subdirectory(tsnctrld)

# RPATH: Tells the executable to look in /usr/local/lib at runtime
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "/usr/local/lib")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# 3. EXECUTABLES
# ==============================================================================
# --- RUNNABLE FILES ---

# --- Yang ---
add_executable(yang_demo tsnctrld/sysrepo_connection/yang_demo.c)
target_link_libraries(yang_demo ${SYSREPO_LIBRARY} ${LIBYANG_LIBRARY})

# --- Netlink ---
add_executable(enpro-switch tsnctrld/QdiscManager.cpp)
target_link_libraries(enpro-switch PRIVATE tsnctrld_lib)
