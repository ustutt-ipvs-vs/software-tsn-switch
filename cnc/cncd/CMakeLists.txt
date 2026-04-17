cmake_minimum_required(VERSION 3.20)
project(cncd_application)

# Helper-Skript aus dem cmake-Ordner (eine Ebene höher) einbinden
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/GenerateGrpc.cmake)

add_executable(cncd
    src/cncd.cpp
    src/CncServiceImpl.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_include_directories(cncd PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(cncd PRIVATE include)

# Hier verlinken wir cnc_lib (welche in der parent CMakeLists definiert wurde)
target_link_libraries(cncd
    PRIVATE
    cnc_lib       
    PkgConfig::GRPC
    protobuf::libprotobuf
    PkgConfig::SPDLOG
)