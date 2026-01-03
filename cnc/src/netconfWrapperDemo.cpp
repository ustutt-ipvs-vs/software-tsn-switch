#include <iostream>
#include <string>
#include <fstream>
#include <map>

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
        if (line.empty() || line[0] == '#') continue;

        auto delimiterPos = line.find("=");
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
    std::cout<< "Netconf Wrapper Demo Application" << std::endl;

    // Load configuration
    auto config = loadConfig("test_config.txt");

    std::string host = config["HOST"];
    // Fallback to port 830 if not specified
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
        
        std::string xml_data = session.getData(); 

        if (!xml_data.empty()) {
            std::cout << "Received (" << xml_data.length() << " Bytes)." << std::endl;
            // std::cout << xml_data << std::endl; // Uncomment to print the full XML data
        } else {
            std::cout << "No data received." << std::endl;
        }

        session.disconnect();
    } else {
        std::cerr << "ERROR: Connection failed." << std::endl;
    }
    
    return 0;
}