# protobuf package
find_package(Protobuf REQUIRED)

# gRPC search over pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(GRPC REQUIRED grpc++ IMPORTED_TARGET)

# Check for grpc_cpp_plugin
find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
    message(FATAL_ERROR "grpc_cpp_plugin nicht gefunden! Ist protobuf-compiler-grpc installiert?")
endif()

set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../proto/cnc.proto")

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILE})

set(GRPC_SRCS "${CMAKE_CURRENT_BINARY_DIR}/cnc.grpc.pb.cc")
set(GRPC_HDRS "${CMAKE_CURRENT_BINARY_DIR}/cnc.grpc.pb.h")
add_custom_command(
      OUTPUT "${GRPC_SRCS}" "${GRPC_HDRS}"
      COMMAND $<TARGET_FILE:protobuf::protoc>
      ARGS --grpc_out="${CMAKE_CURRENT_BINARY_DIR}"
           --cpp_out="${CMAKE_CURRENT_BINARY_DIR}"
           -I "${CMAKE_CURRENT_SOURCE_DIR}/../proto"
           --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
           "${PROTO_FILE}"
      DEPENDS "${PROTO_FILE}")