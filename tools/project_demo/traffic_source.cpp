// Generate experiment link layer traffic (Ethertype: 0x88B5) directed at a MAC address
// To compile: g++ -O2 -o traffic_source traffic_source.cpp

#include <arpa/inet.h>
#include <fcntl.h>
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
#include <vector>

auto LOG_INTERVAL = std::chrono::seconds(1);

std::atomic<bool> running{true};
std::mutex mutex;

long recentFrames = 0;
long droppedFrames = 0;
long totalFrames = 0;

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
        std::clog << std::to_string(recentFrames) << " frames sent (" << std::to_string(totalFrames)
                  << " total), though " << std::to_string(droppedFrames)
                  << " frames were dropped due to send queue overload" << "\n";
        recentFrames = 0;
        droppedFrames = 0;
        lock.unlock();
        nextTickTime += LOG_INTERVAL;
    }
}

int main(int argc, char *argv[]) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    if (argc != 6) {
        std::clog << "Usage: " << argv[0] << " <NETW_IFACE> <DEST_MAC> <PAYLOAD_SIZE> <FRAMES_PER_SEC> <SKB_PRIO>"
                  << "\n";
        std::clog << "\n";
        std::clog << "NETW_IFACE: Name of the network interface to send from, e.g. 'eth0'" << "\n";
        std::clog << "DEST_MAC: Destination MAC address, e.g. '00:25:90:94:70:32'" << "\n";
        std::clog
            << "FRAME_SIZE: How many bytes each ethernet frame's payload should contain. Any value 60 <= x <= 1514"
               "1514 is allowed."
            << "\n";
        std::clog << "FRAMES_PER_SEC: How many frames to send per second." << "\n";
        std::clog << "SKB_PRIO: The internal SKB priority the frames are handled with. Any value 0 <= x <= 7" << "\n";
        std::clog << std::flush;
        return 0;
    }

    if (geteuid() != 0) {
        std::cerr << "No root access (did you forget sudo?)" << "\n";
        return 1;
    }

    int niIndex = if_nametoindex(argv[1]);
    size_t frameSizeNoCrc = std::stoi(argv[3]);
    int framesPerSec = std::stoi(argv[4]);
    auto frameInterval = std::chrono::microseconds(1000000 / framesPerSec);
    int skbPriority = std::stoi(argv[5]);

    if (frameSizeNoCrc < 60 || frameSizeNoCrc > 1514) {
        std::cerr << "FRAME_SIZE is not in 60 <= x <= 1514 interval" << "\n";
        return 1;
    }
    if (skbPriority < 0 || skbPriority > 7) {
        std::cerr << "SKB_PRIO is not in 0 <= x <= 7 interval" << "\n";
        return 1;
    }

    // Setup socket
    int sendSocket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
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

    size_t payloadSize = frameSizeNoCrc - 14;
    size_t frameSize = frameSizeNoCrc + 4;
    uint8_t frame[frameSizeNoCrc];

    // Destination MAC
    uint8_t destMac[6];
    sscanf(argv[2], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &destMac[0], &destMac[1], &destMac[2], &destMac[3], &destMac[4],
           &destMac[5]);

    // Ethernet header
    memcpy(frame, destMac, 6);
    uint16_t ethertype = htons(0x88B5);  // Local Experimental Ethertype 1
    memcpy(frame + 12, &ethertype, 2);

    // Payload
    std::vector<uint8_t> payload(payloadSize, 0xFD);
    memcpy(frame + 14, payload.data(), payloadSize);

    // Socket address
    struct sockaddr_ll sockAddr{};
    sockAddr.sll_family = AF_PACKET;
    sockAddr.sll_ifindex = niIndex;
    sockAddr.sll_halen = ETH_ALEN;
    memcpy(sockAddr.sll_addr, destMac, 6);

    // Launch stats reporter
    std::thread receiverThread(reportStats);
    receiverThread.detach();

    std::clog << "Sending frames..." << "\n";
    auto nextFrameTime = std::chrono::steady_clock::now();
    while (running) {
        ssize_t sendResult =
            sendto(sendSocket, frame, frameSizeNoCrc, 0, (struct sockaddr *)&sockAddr, sizeof(sockAddr));

        std::unique_lock<std::mutex> lock(mutex);
        ++recentFrames;
        ++totalFrames;
        if (sendResult < 0) {
            // perror("sendto");
            ++droppedFrames;
        }
        lock.unlock();

        nextFrameTime += frameInterval;
        std::this_thread::sleep_until(nextFrameTime);
    }
    std::clog << "Exiting, " << std::to_string(totalFrames) << " frames were sent in total." << "\n";
    close(sendSocket);
    return 0;
}
