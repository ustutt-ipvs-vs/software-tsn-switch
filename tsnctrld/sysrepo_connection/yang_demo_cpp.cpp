#include <iostream>
#include <csignal>
#include <unistd.h>
#include <atomic>
#include <random>
#include <thread>
#include <ctime>
#include <optional>
#include <vector> // Required to hold multiple subscriptions

// Sysrepo-cpp bindings
#include <sysrepo-cpp/Connection.hpp>
#include <sysrepo-cpp/Session.hpp>
#include <sysrepo-cpp/Subscription.hpp> // Required for ChangeCollection
#include <sysrepo-cpp/Changes.hpp> // Required for ChangeCollection
#include <sysrepo-cpp/Enum.hpp>

// Libyang-cpp bindings
#include <libyang-cpp/Context.hpp>
#include <libyang-cpp/DataNode.hpp>

// Use an atomic flag for thread safety
std::atomic<bool> exit_application{false};

static void sigint_handler(int) {
    exit_application = true;
}

/*
 * HELPER: Print values safely
 */
void print_change(const libyang::DataNode& node) {
    std::cout << node.path() << " = ";

    // Use the correct flag 'Siblings' as requested
    auto str_opt = node.printStr(libyang::DataFormat::JSON, libyang::PrintFlags::Siblings);

    if (str_opt.has_value()) {
        std::cout << str_opt.value() << std::endl;
    } else {
        std::cout << "(empty)" << std::endl;
    }
}

/*
 * CALLBACK 1: CANDIDATE LOGGER
 */
sysrepo::ErrorCode candidate_log_cb(sysrepo::Session session, uint32_t /*sub_id*/,
                                    const std::string& /*module_name*/,
                                    const std::optional<std::string>& /*sub_xpath*/,
                                    sysrepo::Event event, uint32_t /*request_id*/)
{
    if (event == sysrepo::Event::Done) {
        std::cout << "[CANDIDATE] User has modified the candidate store." << std::endl;
    }
    return sysrepo::ErrorCode::Ok;
}

/*
 * CALLBACK 2: RUNNING COMMIT HANDLER
 */
sysrepo::ErrorCode running_commit_cb(sysrepo::Session session, uint32_t /*sub_id*/,
                                     const std::string& module_name,
                                     const std::optional<std::string>& /*sub_xpath*/,
                                     sysrepo::Event event, uint32_t /*request_id*/)
{
    // PHASE 1: VALIDATION & STARTUP
    if (event == sysrepo::Event::Change || event == sysrepo::Event::Enabled) {

        std::string mode = (event == sysrepo::Event::Change) ? "COMMIT" : "STARTUP";
        std::cout << "[" << mode << "] Applying configuration to hardware..." << std::endl;

        // Iterate over changes using the ChangeCollection
        for (const auto& change : session.getChanges("//.")) {
            std::cout << " -> Change: ";
            // change.node is a libyang::DataNode
            print_change(change.node);
        }

        // Simulate Random Failure (Only during commit)
        if (event == sysrepo::Event::Change) {
            // 30% chance of failure
            if ((std::rand() % 100) < 30) {
                std::cout << "[COMMIT] [!!!] HARDWARE FAILURE SIMULATED! Aborting." << std::endl;

                // Set the error message for the client
                session.setErrorMessage("Random hardware failure occurred during apply.");
                return sysrepo::ErrorCode::OperationFailed;
            }
        }

        std::cout << "[" << mode << "] Apply successful." << std::endl;
    }

    // PHASE 2: FINALIZATION
    else if (event == sysrepo::Event::Done) {
        std::cout << "[DONE] DB sync complete." << std::endl;
    }

    return sysrepo::ErrorCode::Ok;
}

/*
 * CALLBACK 3: OPERATIONAL DATA PROVIDER
 */
sysrepo::ErrorCode oper_data_cb(sysrepo::Session session, uint32_t /*sub_id*/,
                                const std::string& /*module_name*/,
                                const std::optional<std::string>& /*sub_xpath*/,
                                const std::optional<std::string>& /*request_xpath*/,
                                uint32_t /*request_id*/,
                                std::optional<libyang::DataNode>& parent)
{
    std::cout << "[OPER-DATA] Request received." << std::endl;

    // Get current time string
    std::time_t now = std::time(nullptr);
    char* time_c_str = std::ctime(&now);
    if (time_c_str) {
        std::string time_str(time_c_str);
        if (!time_str.empty()) time_str.pop_back(); // Remove newline

        // Create the data node
        if (parent) {
            parent->newPath("/example-demo:data/current-time", time_str);
        } else {
            // If parent is null, we create a new standalone node
            parent = session.getContext().newPath("/example-demo:data/current-time", time_str);
        }
    }

    return sysrepo::ErrorCode::Ok;
}

int main() {
    std::srand(std::time(nullptr));

    try {
        std::cout << "Connecting to Sysrepo..." << std::endl;

        sysrepo::Connection conn;
        sysrepo::Session session = conn.sessionStart(sysrepo::Datastore::Running);

        // Vector to hold subscriptions (RAII)
        // When this vector is destroyed at end of main, all subscriptions stop.
        std::vector<sysrepo::Subscription> subs;

        std::cout << "Subscribing to 'example-demo'..." << std::endl;

        // 1. Subscribe to RUNNING
        subs.push_back(session.onModuleChange(
            "example-demo",
            running_commit_cb,
            std::nullopt, // sub_xpath
            0,            // priority
            sysrepo::SubscribeOptions::Enabled | sysrepo::SubscribeOptions::DoneOnly
            ));

        // 2. Subscribe to CANDIDATE
        // We create a temporary session for candidate access,
        // but the subscription is returned and stored in 'subs'.
        auto sess_cand = conn.sessionStart(sysrepo::Datastore::Candidate);

        subs.push_back(sess_cand.onModuleChange(
            "example-demo",
            candidate_log_cb,
            std::nullopt,
            0,
            sysrepo::SubscribeOptions::Default
            ));

        // 3. Subscribe to OPERATIONAL data
        subs.push_back(session.onOperGet(
            "example-demo",
            oper_data_cb,
            "/example-demo:data/current-time",
            sysrepo::SubscribeOptions::Default
            ));

        std::cout << "App running successfully. Ctrl+C to stop." << std::endl;

        signal(SIGINT, sigint_handler);
        while (!exit_application) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}