#include <grpcpp/grpcpp.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

#include "CncServiceImpl.h"
#include "spdlog/spdlog.h"

std::unique_ptr<grpc::Server> g_server;
volatile std::sig_atomic_t g_shutdownRequested = 0;

void signalHandler(int signum) {
    (void)signum;
    g_shutdownRequested = 1;
}

int main(int argc, char** argv) {
    std::string config_path = "cnc/config/inventory.json";

    // check if user passed arg
    if (argc > 1) {
        config_path = argv[1];  // use the provided path
        spdlog::info("Using provided config path: {}", config_path);
    } else {
        spdlog::info("No path provided. Using default: {}", config_path);
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string socket_path = "/tmp/cnc_socket";
    std::string server_address = "unix://" + socket_path;
    std::string tcp_address = "0.0.0.0:50051";  // for testing in Postman

    unlink(socket_path.c_str());

    CncServiceImpl service(config_path);

    grpc::ServerBuilder builder;
    // Listen on both Unix socket and TCP for testing purposes
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Listen on TCP for testing with Postman or other gRPC clients that don't support Unix sockets
    builder.AddListeningPort(tcp_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    g_server = builder.BuildAndStart();

    if (g_server == nullptr) {
        spdlog::error("Failed to start CNC gRPC server on {}", server_address);
        return 1;
    }

    spdlog::info("CNC Server listening on {}", server_address);

    // Do gRPC shutdown in a normal thread context, not from the signal handler.
    std::thread shutdownThread([]() {
        while (!g_shutdownRequested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (g_server != nullptr) {
            g_server->Shutdown();
        }
    });

    g_server->Wait();
    g_shutdownRequested = 1;

    if (shutdownThread.joinable()) {
        shutdownThread.join();
    }

    unlink(socket_path.c_str());
    spdlog::info("CNC Server shut down gracefully.");
    return 0;
}