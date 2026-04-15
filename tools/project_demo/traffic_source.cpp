// Send datagrams to a given IP address' port 53660
// To compile: g++ -O2 -o traffic_source traffic_source.cpp

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

constexpr uint16_t PORT = 53660;
constexpr auto LOG_INTERVAL = std::chrono::seconds(1);

std::atomic<bool> running{true};
std::mutex mutex;

long recentDgrams = 0;
long droppedDgrams = 0;
long totalDgrams = 0;

void handleSignal(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        running = false;
    }
}

void reportStats() {
    auto nextTickTime = std::chrono::steady_clock::now() + LOG_INTERVAL;
    while (running) {
        std::this_thread::sleep_until(nextTickTime);
        std::unique_lock<std::mutex> lock(mutex);
        std::clog << std::to_string(recentDgrams) << " datagrams sent (" << std::to_string(totalDgrams)
                  << " total), though " << std::to_string(droppedDgrams)
                  << " datagrams were dropped due to send queue overload" << "\n";
        recentDgrams = 0;
        droppedDgrams = 0;
        lock.unlock();
        nextTickTime += LOG_INTERVAL;
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    if (argc != 5) {
        std::clog << "Usage: " << argv[0] << " <IP_ADDR> <PAYLOAD_SIZE> <DGRAMS_PER_SEC> <SKB_PRIO>" << "\n";
        std::clog << "\n";
        std::clog << "IP_ADDR: The IP address to send to, e.g. 172.29.253.225" << "\n";
        std::clog << "PAYLOAD_SIZE: How many bytes each datagram's payload should contain. Any value 36 <= x <= 1472 "
                     "is allowed."
                  << "\n";
        std::clog << "DGRAMS_PER_SEC: How many datagrams to send per second." << "\n";
        std::clog << "SKB_PRIO: The internal SKB priority the frames are handled with. Any value 0 <= x <= 7" << "\n";
        std::clog << std::flush;
        return 0;
    }

    char* ipAddr = argv[1];
    size_t payloadSize = std::stoi(argv[2]);
    int dgramsPerSec = std::stoi(argv[3]);
    auto dgramInterval = std::chrono::microseconds(1000000 / dgramsPerSec);
    int skbPriority = std::stoi(argv[4]);

    if (payloadSize < 36 || payloadSize > 1472) {
        std::cerr << "FRAME_SIZE is not in 60 <= x <= 1514 interval" << "\n";
        return 1;
    }
    if (skbPriority < 0 || skbPriority > 7) {
        std::cerr << "SKB_PRIO is not in 0 <= x <= 7 interval" << "\n";
        return 1;
    }

    // Setup socket
    int sendSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (sendSocket < 0) {
        perror("socket");
        return 1;
    }
    if (setsockopt(sendSocket, SOL_SOCKET, SO_PRIORITY, &skbPriority, sizeof(skbPriority)) < 0) {
        perror("setsockopt");
        return 1;
    }
    // Make sendto() calls non-blocking to simulate packet loss during high traffic
    int socketFlags = fcntl(sendSocket, F_GETFL, 0);
    fcntl(sendSocket, F_SETFL, socketFlags | O_NONBLOCK);

    // Socket address
    sockaddr_in destAddr;
    destAddr.sin_family = AF_INET;
    ;
    destAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, ipAddr, &destAddr.sin_addr);

    const std::vector<uint8_t> payload(payloadSize, 0xFD);

    // Launch stats reporter
    std::thread statsThread(reportStats);
    statsThread.detach();

    std::clog << "Sending datagrams..." << "\n";
    auto nextDgramTime = std::chrono::steady_clock::now();
    while (running) {
        ssize_t sendResult =
            sendto(sendSocket, payload.data(), payloadSize, 0, (struct sockaddr*)&destAddr, sizeof(destAddr));

        std::unique_lock<std::mutex> lock(mutex);
        ++recentDgrams;
        ++totalDgrams;
        if (sendResult < 0) {
            // perror("sendto");
            ++droppedDgrams;
        }
        lock.unlock();

        nextDgramTime += dgramInterval;
        std::this_thread::sleep_until(nextDgramTime);
    }
    std::clog << "Exiting, " << std::to_string(totalDgrams) << " datagrams were sent in total." << "\n";
    close(sendSocket);
    return 0;
}
