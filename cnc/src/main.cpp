#include <iostream>
#include <string>
#include <fstream>
#include <map>
#include <filesystem>

#include "CncTypes.h"
#include "Topology.h"
#include "JsonImporter.h"
#include "NetworkManager.h"

std::map<std::string, std::string> loadConfig(const std::string& filename) {
    std::map<std::string, std::string> config;
    std::ifstream file(filename);

    if (!file.is_open()) {
        std::cerr << "Could not find '" << filename << "'!" << std::endl;
        return config;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        auto delimiterPos = line.find("=");
        if(delimiterPos != std::string::npos) {
            std::string key = line.substr(0, delimiterPos);
            std::string value = line.substr(delimiterPos + 1);
            
            if (!value.empty() && value.back() == '\r') {
                value.pop_back();
            }

            config[key] = value;
        }
    }
    return config;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "      CNC NETWORK CONTROLLER v1.0       " << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. Load Credentials and Settings
    const std::string SETTINGS_FILE = "config.txt";
    auto settings = loadConfig(SETTINGS_FILE);

    if (settings.empty()) {
        std::cerr << "[FATAL] No configuration found or file is empty: " << SETTINGS_FILE << std::endl;
        std::cerr << "        Please create config.txt with USER, PASS and TOPOLOGY_FILE." << std::endl;
        std::cerr << "        CWD: " << std::filesystem::current_path() << std::endl;
        return 1;
    }

    std::string sshUser = settings["USER"];
    std::string sshPass = settings["PASS"];
    std::string jsonFile = settings.count("TOPOLOGY_FILE") ? settings["TOPOLOGY_FILE"] : "cnc/examples/simple_example_schedule_v2.json";

    // 2. Path check for Topology File
    if (sshUser.empty() || sshPass.empty()) {
        std::cerr << "[FATAL] USER or PASS not set in " << SETTINGS_FILE << std::endl;
        return 1;
    }

    // 3. Create Topology and Import from JSON
    Topology topology;
    std::cout << "[INFO] Importing topology from JSON: " << jsonFile << std::endl;

    if (!cnc::JsonImporter::importFromFile(jsonFile, topology)) {
        std::cerr << "[FATAL] Failed to import topology from JSON!" << std::endl;
        return 1;
    }

    if (topology.nodes.empty()) {
        std::cerr << "[FATAL] No nodes found in imported topology!" << std::endl;
        return 1;
    }

    // 4. Initialize Network Manager
    cnc::NetworkManager manager(topology);

    // 5. Connect to all nodes
    std::cout << "\n[INFO] --- Starting Connection Phase ---" << std::endl;
    std::cout << "[INFO] Login as user: " << sshUser << std::endl;

    bool connected = manager.connectAllNodes(sshUser, sshPass);

    if (!connected) {
        std::cerr << "[FATAL] Could not connect to all nodes!" << std::endl;
        return 1;
    }

    // 6. Deployment (Create XML and send via Netconf)
    std::cout << "\n[INFO] --- Starting Deployment Phase ---" << std::endl;
    manager.deployConfigToAll();

    // 7. Finish
    std::cout << "\n========================================" << std::endl;
    std::cout << "      CNC OPERATION FINISHED            " << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}