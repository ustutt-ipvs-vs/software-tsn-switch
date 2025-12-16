#include "../include/FipsImporter.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <vector>
#include <ctime>

//makes code more readable
using json = nlohmann::json;

struct InternalEvent {
    uint64_t timestamp; // when is there change
    int ququeIndex; // which queue
    bool isOpen; // open or close

    // For sorting events by timestamp
    bool operator<(const InternalEvent& other) const {
        return timestamp < other.timestamp;
    }
};

ietfInterface_t FipsImporter::importConfig(
    const std::string& jsonFilePath,
    const std::string& linkName,
    const std::string& targetIp,
    const std::string& targetInterface
) {
    // Read the JSON file
    std::ifstream file(jsonFilePath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + jsonFilePath);
    }

    //try to parse the file
    json data;
    try {
        data = json::parse(file);
    } catch (const json::parse_error& e) {
        throw std::runtime_error("JSON parse error: " + std::string(e.what()));
    }

    // Find the relevant link
    if (!data.contains("GCL") || !data["GCL"].contains(linkName)) {
        throw std::runtime_error("Link not found in JSON: " + linkName);
    }

    json linkJson = data["GCL"][linkName];

    std::vector<InternalEvent> events;
    uint64_t maxCycleTime = 0;

    // Process each queue's schedule
    for (int qIdx = 0; qIdx < 8; qIdx++) {
        std::string qKey = "Q" + std::to_string(qIdx);

        if (linkJson.contains(qKey)) {
            // Extract queue data
            auto qData = linkJson[qKey];

            // Initial state
            bool state = (qData["initial"].get<int>() == 1); // true = open, false = closed

            // Set initial event at time 0
            uint64_t currentTime = qData["offset"].get<uint64_t>();

            for (auto& durVal : qData["durations"]) {
                uint64_t durationNs = durVal.get<uint64_t>();
                
                if (durationNs > 0) {
                    currentTime += durationNs;
                    state = !state; // Toggle state

                    // New state starting from currentTime
                    events.push_back({currentTime, qIdx, state});
                }
            }

            // Update max cycle time
            if (currentTime > maxCycleTime) {
                maxCycleTime = currentTime;
            }
        }
    }
}

