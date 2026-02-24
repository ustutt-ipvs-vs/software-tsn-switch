#include <spdlog/spdlog.h>

#include <chrono>  // For sleep (optional)
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>  // For sleep (optional)

#include "../include/NetconfSession.h"

std::map<std::string, std::string> loadConfig(const std::string& filename) {
    std::map<std::string, std::string> config;
    std::ifstream file(filename);

    if (!file.is_open()) {
        spdlog::error("Could not find '{}'! Please create the file in the execution directory.", filename);
        return config;
    }

    std::string line;
    while (std::getline(file, line)) {
        // skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }

        auto delimiterPos = line.find('=');
        if (delimiterPos != std::string::npos) {
            std::string key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);

            // Remove potential trailing carriage return
            if (!value.empty() && value.back() == '\r') {
                value.pop_back();
            }

            config[key] = value;
        }
    }
    return config;
}

int main() {
    spdlog::info("Netconf Wrapper Demo Application");

    // Load configuration
    auto config = loadConfig("../../config.txt");

    // 1. Load configuration for login
    std::string host = config["HOST"];
    // Fallback to port 830 if not specified not that good because if no target is specified connection will fail anyway
    int port = config["PORT"].empty() ? 830 : std::stoi(config["PORT"]);
    std::string user = config["USER"];
    std::string pass = config["PASS"];

    // Debug Print (without password)
    spdlog::info("Target: {}:{}", host, port);
    spdlog::info("User: {}", user);

    common::NetconfSession session;

    spdlog::info("Connecting...");

    if (session.connect(host, port, user, pass)) {
        spdlog::info("SUCCESS: Connected to server!");

        // Read & Print current state
        spdlog::info("\n--- 1. Reading Current State (Running) ---");
        std::string xpath_filter = "/data:data";
        std::string initial_data = session.getData(xpath_filter);

        if (!initial_data.empty()) {
            spdlog::info(initial_data);
        } else {
            spdlog::info("[Info] Filter returned no data (or empty). Check XPath.");
        }

        // Prepare for edit
        spdlog::info("\n--- 2. Editing Data (Candidate) ---");

        std::string changeXml = R"(<data xmlns="urn:examples:demo">
                <numbers>
                    <name>Test3</name>
                    <value>1234</value>
                </numbers>
            </data>)";

        if (session.editData(changeXml)) {
            spdlog::info("-> Edit OK: Data is now in 'Candidate' datastore.");
        } else {
            spdlog::error("-> Edit FAILED. Stopping.");
            session.disconnect();
            return -1;
        }

        // Commit changes
        spdlog::info("\n--- 3. Committing to 'Running' ---");

        if (session.commit()) {
            spdlog::info("-> Commit OK: Data is now live.");
        } else {
            spdlog::error("-> Commit FAILED.");
        }

        // Verify new state
        spdlog::info("\n--- 4. Verifying New State ---");
        spdlog::info(session.getData(xpath_filter));

        session.disconnect();
    } else {
        spdlog::error("ERROR: Connection failed.");
    }

    return 0;
}