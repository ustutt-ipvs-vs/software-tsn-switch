// Generate experiment link layer traffic (Ethertype: 0x88B5) directed at a MAC address
// To compile: g++ -O2 -o traffic_source traffic_source.cpp

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
#include <thread>
#include <vector>

std::atomic<bool> running{true};

void handleSignal(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        running = false;
    }
}

int main(int argc, char *argv[]) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    if (argc != 5) {
        std::clog << "Usage: " << argv[0] << " <NETW_IFACE> <DEST_MAC> <PAYLOAD_SIZE> <FRAMES_PER_SEC>" << "\n";
        std::clog << "\n";
        std::clog << "NETW_IFACE: Name of the network interface to send from, e.g. 'eth0'" << "\n";
        std::clog << "DEST_MAC: Destination MAC address, e.g. '00:25:90:94:70:32'" << "\n";
        std::clog << "FRAME_SIZE: How many bytes each ethernet frame's payload should contain. Any value 60 <= x <= "
                     "1514 is allowed."
                  << "\n";
        std::clog << "FRAMES_PER_SEC: How many frames to send per second." << "\n";
        std::clog << std::flush;
        return 1;
    }

    if (geteuid() != 0) {
        std::cerr << "No root access (did you forget sudo?)" << "\n";
        return 1;
    }

    int niIndex = if_nametoindex(argv[1]);
    size_t frameSizeNoCrc = std::stoi(argv[3]);
    int framesPerSec = std::stoi(argv[4]);
    auto frameInterval = std::chrono::microseconds(1000000 / framesPerSec);

    if (frameSizeNoCrc < 60 || frameSizeNoCrc > 1514) {
        std::cerr << "FRAME_SIZE is not in 60 <= x <= 1514 interval" << "\n";
        return 1;
    }

    // Setup socket
    int sendSocket = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sendSocket < 0) {
        perror("socket");
        return 1;
    }

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

    std::clog << "Sending frames..." << "\n";
    uint32_t frameNumber = 0;
    auto nextFrameTime = std::chrono::steady_clock::now();
    while (running) {
        if (sendto(sendSocket, frame, frameSizeNoCrc, 0, (struct sockaddr *)&sockAddr, sizeof(sockAddr)) < 0) {
            perror("sendto");
        }
        nextFrameTime += frameInterval;
        frameNumber += 1;
        if (frameNumber % framesPerSec == 0) {
            std::clog << std::to_string(frameNumber) << " frames sent in total, each is "
                      << std::to_string(frameSizeNoCrc) << " bytes long." << "\n";
        }
        std::this_thread::sleep_until(nextFrameTime);
    }
    std::clog << "Exiting, " << std::to_string(frameNumber) << " frames were sent in total." << "\n";
    close(sendSocket);
    return 0;
}
