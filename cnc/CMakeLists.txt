cmake_minimum_required(VERSION 3.14)
project(cnc_application)

# 1. Global Settings
# ---------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Fetch nlohmann/json library
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

# Only add common if not already added (by parent CMakeLists.txt)
if(NOT TARGET common_lib)
    add_subdirectory(../common common_build)
endif()

# 2. The Main Library (cnc_lib)
# ---------------------------------------------------------
# All cpp files that implement the core functionality
add_library(cnc_lib STATIC
    src/NetconfSession.cpp
    src/JsonImporter.cpp
    src/NetworkManager.cpp
    src/GclXmlBuilder.cpp
    src/LldpParser.cpp
    src/GclParser.cpp
    src/InterfaceParser.cpp
    src/PtpParser.cpp
    src/Topology.cpp
)

# Header paths for the library (and all who use it)
target_include_directories(cnc_lib PUBLIC
    /usr/local/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)

# Definitions and compile flags
target_compile_definitions(cnc_lib PUBLIC 
    NC_ENABLED_SSH_TLS
    CNC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Link libraries (Netconf, SSH, JSON, etc.)
target_link_libraries(cnc_lib PUBLIC
    netconf2
    yang
    ssh
    common_lib
    argparse
    nlohmann_json::nlohmann_json
    PkgConfig::SPDLOG
)

find_package(Protobuf REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(GRPC REQUIRED grpc++ IMPORTED_TARGET)

include_directories(${Protobuf_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR})

# 3. Executables (Apps & Tests)
# ---------------------------------------------------------
add_subdirectory(cnc_cli)
add_subdirectory(cncd)