// Listen for experiment link layer traffic (Ethertype: 0x88B5) and display stats
// To compile: g++ -O2 -o traffic_sink traffic_sink.cpp

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>

auto LOG_INTERVAL = std::chrono::seconds(1);

int receiveSocket;
std::atomic<bool> running{true};
std::mutex mutex;

long recentFrames = 0;
long totalFrames = 0;
int recentFrameSize = 0;

void handleSignal(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        running = false;
    }
}

void countIncomingFrames() {
    uint8_t buffer[1518];

    std::clog << "Listening for frames..." << "\n";
    while (true) {
        ssize_t bytesRead = recvfrom(receiveSocket, buffer, sizeof(buffer), 0, nullptr, nullptr);
        if (bytesRead < 0) {
            if (running) {
                perror("recvfrom");
            }
            break;
        }

        std::unique_lock<std::mutex> lock(mutex);
        ++recentFrames;
        ++totalFrames;
        recentFrameSize = bytesRead;
        lock.unlock();
    }
}

int main() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    if (geteuid() != 0) {
        std::cerr << "No root access (did you forget sudo?)" << "\n";
        return 1;
    }

    // Setup socket
    receiveSocket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (receiveSocket < 0) {
        perror("socket");
        exit(1);
    }

    // Configure socket
    struct sockaddr_ll sockAddr{};
    sockAddr.sll_family = AF_PACKET;
    sockAddr.sll_protocol = htons(0x88B5);  // Local Experimental Ethertype 1
    if (bind(receiveSocket, (struct sockaddr*)&sockAddr, sizeof(sockAddr)) < 0) {
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
        nextTickTime += LOG_INTERVAL;
        std::unique_lock<std::mutex> lock(mutex);
        std::clog << std::to_string(recentFrames) << " frames received (" << std::to_string(totalFrames)
                  << " total), the last one was " << std::to_string(recentFrameSize) << " bytes long" << "\n";
        recentFrames = 0;
        lock.unlock();
    }

    close(receiveSocket);
    std::unique_lock<std::mutex> lock(mutex);
    std::clog << "Exiting, " << std::to_string(totalFrames) << " frames were received in total." << "\n";
    lock.unlock();
}
