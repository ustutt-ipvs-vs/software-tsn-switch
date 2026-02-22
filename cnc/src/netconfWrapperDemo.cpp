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
        std::cerr << "Could not find '" << filename << "'!" << '\n';
        std::cerr << "Please create the file in the execution directory." << '\n';
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
    std::cout << "Netconf Wrapper Demo Application" << '\n';

    // Load configuration
    auto config = loadConfig("../../config.txt");

    // 1. Load configuration for login
    std::string host = config["HOST"];
    // Fallback to port 830 if not specified not that good because if no target is specified connection will fail anyway
    int port = config["PORT"].empty() ? 830 : std::stoi(config["PORT"]);
    std::string user = config["USER"];
    std::string pass = config["PASS"];

    // Debug Print (without password)
    std::cout << "Target: " << host << ":" << port << '\n';
    std::cout << "User: " << user << '\n';

    common::NetconfSession session;

    std::cout << "Connecting..." << '\n';

    if (session.connect(host, port, user, pass)) {
        std::cout << "SUCCESS: Connected to server!" << '\n';

        // Read & Print current state
        std::cout << "\n--- 1. Reading Current State (Running) ---" << '\n';
        std::string xpath_filter = "/data:data";
        std::string initial_data = session.getData(xpath_filter);

        if (!initial_data.empty()) {
            std::cout << initial_data << '\n';
        } else {
            std::cout << "[Info] Filter returned no data (or empty). Check XPath." << '\n';
        }

        // Prepare for edit
        std::cout << "\n--- 2. Editing Data (Candidate) ---" << '\n';

        std::string changeXml = R"(<data xmlns="urn:examples:demo">
                <numbers>
                    <name>Test3</name>
                    <value>1234</value>
                </numbers>
            </data>)";

        if (session.editData(changeXml)) {
            std::cout << "-> Edit OK: Data is now in 'Candidate' datastore." << '\n';
        } else {
            std::cerr << "-> Edit FAILED. Stopping." << '\n';
            session.disconnect();
            return -1;
        }

        // Commit changes
        std::cout << "\n--- 3. Committing to 'Running' ---" << '\n';

        if (session.commit()) {
            std::cout << "-> Commit OK: Data is now live." << '\n';
        } else {
            std::cerr << "-> Commit FAILED." << '\n';
        }

        // Verify new state
        std::cout << "\n--- 4. Verifying New State ---" << '\n';
        std::cout << session.getData(xpath_filter) << '\n';

        session.disconnect();
    } else {
        std::cerr << "ERROR: Connection failed." << '\n';
    }

    return 0;
}