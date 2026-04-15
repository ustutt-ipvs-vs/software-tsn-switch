// Listen for datagrams at port 53660 and print stats
// To compile: g++ -O2 -o traffic_sink traffic_sink.cpp

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

constexpr uint16_t PORT = 53660;
constexpr auto LOG_INTERVAL = std::chrono::seconds(1);

int receiveSocket;
std::atomic<bool> running{true};
std::mutex mutex;

long recentDgrams = 0;
long totalDgrams = 0;
int recentDgramSize = 0;

void handleSignal(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        running = false;
    }
}

void countIncomingFrames() {
    uint8_t buffer[1500];
    sockaddr_in senderAddr;
    socklen_t senderAddrLen = sizeof(senderAddr);

    std::clog << "Listening for datagrams..." << "\n";
    while (true) {
        ssize_t bytesRead = recvfrom(receiveSocket, buffer, sizeof(buffer), 0, (sockaddr*)&senderAddr, &senderAddrLen);
        if (bytesRead < 0) {
            if (running) {
                perror("recvfrom");
            }
            break;
        }

        std::unique_lock<std::mutex> lock(mutex);
        ++recentDgrams;
        ++totalDgrams;
        recentDgramSize = bytesRead;
        lock.unlock();
    }
}

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    // Setup socket
    receiveSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (receiveSocket < 0) {
        perror("socket");
        exit(1);
    }

    // Configure socket
    sockaddr_in recvAddr;
    recvAddr.sin_family = AF_INET;
    recvAddr.sin_addr.s_addr = INADDR_ANY;  // Receive from any network interface
    recvAddr.sin_port = htons(PORT);
    if (bind(receiveSocket, (const sockaddr*)&recvAddr, sizeof(recvAddr)) < 0) {
        perror("bind");
        close(receiveSocket);
        exit(1);
    }

    // Launch listener
    std::thread receiverThread(countIncomingFrames);
    receiverThread.detach();

    // Print stats
    auto nextTickTime = std::chrono::steady_clock::now() + LOG_INTERVAL;
    while (running) {
        std::this_thread::sleep_until(nextTickTime);
        std::unique_lock<std::mutex> lock(mutex);
        std::clog << std::to_string(recentDgrams) << " datagrams received (" << std::to_string(totalDgrams)
                  << " total), the last one had " << std::to_string(recentDgramSize) << " bytes of payload" << "\n";
        recentDgrams = 0;
        lock.unlock();
        nextTickTime += LOG_INTERVAL;
    }

    close(receiveSocket);
    std::unique_lock<std::mutex> lock(mutex);
    std::clog << "Exiting, " << std::to_string(totalDgrams) << " datagrams were received in total." << "\n";
    lock.unlock();
}
