cmake_minimum_required(VERSION 3.20)
project(cnc_cli_application)

# helper script like in cncd
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/GenerateGrpc.cmake)

# cli executable
add_executable(cnc_cli
    src/main.cpp
    src/CncGrpcClient.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

# add include directories (gen. headers + own headers)
target_include_directories(cnc_cli PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(cnc_cli PRIVATE include)

# link libraries (same as cncd)
target_link_libraries(cnc_cli
    PRIVATE
    cnc_lib       
    PkgConfig::GRPC
    protobuf::libprotobuf
    PkgConfig::SPDLOG
)
install(TARGETS cnc_cli DESTINATION bin)