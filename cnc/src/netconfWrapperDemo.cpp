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
        std::cerr << "Could not find '" << filename << "'!" << std::endl;
        std::cerr << "Please create the file in the execution directory." << std::endl;
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
    std::cout << "Netconf Wrapper Demo Application" << std::endl;

    // Load configuration
    auto config = loadConfig("../../config.txt");

    // 1. Load configuration for login
    std::string host = config["HOST"];
    // Fallback to port 830 if not specified not that good because if no target is specified connection will fail anyway
    int port = config["PORT"].empty() ? 830 : std::stoi(config["PORT"]);
    std::string user = config["USER"];
    std::string pass = config["PASS"];

    // Debug Print (without password)
    std::cout << "Target: " << host << ":" << port << std::endl;
    std::cout << "User: " << user << std::endl;

    common::NetconfSession session;

    std::cout << "Connecting..." << std::endl;

    if (session.connect(host, port, user, pass)) {
        std::cout << "SUCCESS: Connected to server!" << std::endl;

        // Read & Print current state
        std::cout << "\n--- 1. Reading Current State (Running) ---" << std::endl;
        std::string xpath_filter = "/data:data";
        std::string initial_data = session.getData(xpath_filter);

        if (!initial_data.empty()) {
            std::cout << initial_data << std::endl;
        } else {
            std::cout << "[Info] Filter returned no data (or empty). Check XPath." << std::endl;
        }

        // Prepare for edit
        std::cout << "\n--- 2. Editing Data (Candidate) ---" << std::endl;

        std::string changeXml = R"(<data xmlns="urn:examples:demo">
                <numbers>
                    <name>Test3</name>
                    <value>1234</value>
                </numbers>
            </data>)";

        if (session.editData(changeXml)) {
            std::cout << "-> Edit OK: Data is now in 'Candidate' datastore." << std::endl;
        } else {
            std::cerr << "-> Edit FAILED. Stopping." << std::endl;
            session.disconnect();
            return -1;
        }

        // Commit changes
        std::cout << "\n--- 3. Committing to 'Running' ---" << std::endl;

        if (session.commit()) {
            std::cout << "-> Commit OK: Data is now live." << std::endl;
        } else {
            std::cerr << "-> Commit FAILED." << std::endl;
        }

        // Verify new state
        std::cout << "\n--- 4. Verifying New State ---" << std::endl;
        std::cout << session.getData(xpath_filter) << std::endl;

        session.disconnect();
    } else {
        std::cerr << "ERROR: Connection failed." << std::endl;
    }

    return 0;
}