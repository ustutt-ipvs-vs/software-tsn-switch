#include <argparse/argparse.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <optional>
#include <google/protobuf/util/json_util.h>

#include "cnc.grpc.pb.h"
#include "../include/CncGrpcClient.h"

enum class GapMode : std::uint8_t { Hold, Zero, Deny };

/**
 * @brief Parses the command line arguments for the CNC ctrl and executes them
 * @param argc Number of arguments, same as `argc` passed to `main()`
 * @param argv Actual command line arguments, same as `argv` passed to `main()`
 * @return Return code, same as return codes for `main()`
 */
int parseAndExec(int argc, char* argv[]) {  // NOLINT(modernize-avoid-c-arrays)
    /* Example commands
     * ================
     *
     * These examples demonstrate how using cnc_app *should* work, but they are not yet
     * wired up with the actual program logic and hence only the --help commands work.
     * Every other command just prints the input parameters to console. This is not set
     * in stone, feel free to modify the commands as needed.
     *
     * Print overall help:
     * ./cnc_app --help
     *
     * Print help for the specific subcommands:
     * ./cnc_app set-schedule --help
     * ./cnc_app get --help
     *
     * Set schedule from file (prints error if incomplete):
     * ./cnc_app set-schedule ~/schedule.json
     *
     * Set incomplete schedule from file, leaving unmentioned GCLs as they are:
     * ./cnc_app set-schedule --gap=hold ~/schedule.json
     *
     * Set incomplete schedule from file, clearing all unmentioned GCLs:
     * ./cnc_app set-schedule --gap=zero ~/schedule.json
     *
     * Pipe schedule from another program into the CNC:
     * python3 super-scheduler.py | ./cnc_app set-schedule -
     *                  Don't forget the dash at the end! ^^^
     *
     * Get the full topology:
     * ./cnc_app get topology --all
     *
     * Get the full schedule in JSON format and save it to a file:
     * ./cnc_app get schedule --format=json --all > schedulefile.json
     *
     * Get LLDP information about vstsn01.enp0f2s0 and vstsn02.enp0f2s0 in JSON format:
     * ./cnc_app get lldp --format=json --targets vstsn01.enp0f2s0 vstsn02.enp0f2s2
     */

    argparse::ArgumentParser program("cnc_app", /* program version: */ "0.1");
    program.add_description("Change the schedules of your TSN network or get status information");

    argparse::ArgumentParser setSchedCommand("set-schedule");
    setSchedCommand.add_description("Change the schedules of your TSN network");
    setSchedCommand.add_argument("input")
        .help(
            "path to the file containing the TSN schedules; must be in JSON format; alternatively provide '-' to read "
            "from stdin")
        .required();
    setSchedCommand.add_argument("-g", "--gap")
        .help("how to deal with an incomplete schedule; can be 'hold', 'zero', or 'deny'")
        .default_value(std::string("deny"))
        .nargs(1);
    program.add_subparser(setSchedCommand);

    argparse::ArgumentParser getCommand("get");
    getCommand.add_description("Get status information about your TSN network");
    getCommand.add_argument("category")
        .help("what kind of information to retrieve; can be 'schedule', 'topology', 'lldp', or 'gptp'")
        .required();
    auto& getSelectionMutualEx = getCommand.add_mutually_exclusive_group(true);
    getSelectionMutualEx.add_argument("-a", "--all")
        .help(
            "retrieve information about all devices and all their interfaces; alternatively provide the --targets "
            "option")
        .flag();
    getSelectionMutualEx.add_argument("-t", "--targets")
        .help("which device(s)/interface(s) to retrieve information about; alternatively provide the --all flag")
        .nargs(argparse::nargs_pattern::at_least_one);
    getCommand.add_argument("-f", "--format")
        .help("the output format of the retrieved information; can be 'indented' or 'json'")
        .default_value(std::string("indented"))
        .nargs(1);
    program.add_subparser(getCommand);

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n";
        std::cerr << program;
        return 1;
    }

    // define gRPC client here so it can be used in both subcommands, but only gets initialized if one of the subcommands is actually used
    CncGrpcClient grpcClient("unix:///tmp/cnc_socket");

    if (program.is_subcommand_used(setSchedCommand)) {
        // "set-schedule" subcommand
        auto gapModeStr = setSchedCommand.get<std::string>("--gap");
        cnc::rpc::ScheduleUpdateMode protoGapMode;

        if (gapModeStr == "hold") {
            protoGapMode = cnc::rpc::ScheduleUpdateMode::HOLD;
        } else if (gapModeStr == "zero") {
            protoGapMode = cnc::rpc::ScheduleUpdateMode::ZERO;
        } else if (gapModeStr == "deny") {
            protoGapMode = cnc::rpc::ScheduleUpdateMode::DENY;
        } else {
            std::clog << "Warning: Unknown gap mode '" << gapModeStr << "', defaulting to HOLD.\n";
            protoGapMode = cnc::rpc::ScheduleUpdateMode::HOLD;
        }

        auto inputSource = setSchedCommand.get<std::string>("input");
        std::istream* inputPtr = &std::cin;  // Read from stdin by default
        std::ifstream fileStream;

        // If not reading from stdin, open the provided file path
        if (inputSource != "-") {
            fileStream.open(inputSource);
            if (!fileStream.is_open()) {
                std::cerr << "Error: Could not open file " << inputSource << "\n";
                return 1;
            }
            inputPtr = &fileStream;
        }

        // read content into a string
        std::string jsonContent((std::istreambuf_iterator<char>(*inputPtr)),
                                 std::istreambuf_iterator<char>());
        if (jsonContent.empty()) {
            std::cerr << "Error: Input is empty.\n";
            return 1;
        }

        // create empty protobuf message and let the library parse the JSON into it
        cnc::rpc::SetNodeScheduleRequest request;
        // automatically create json from protobuf message
        auto parseStatus = google::protobuf::util::JsonStringToMessage(jsonContent, &request);

        if (!parseStatus.ok()) {
            std::cerr << "Error: Failed to parse JSON!\n";
            std::cerr << parseStatus.message() << "\n";
            return 1;
        }

        request.set_mode(protoGapMode);

        // send request with gRPC client
        if (grpcClient.setNodeSchedule(request)) {
            std::cout << "Successfully deployed schedule!\n";
        } else {
            std::cerr << "Error deploying schedule.\n";
            return 1;
        }
        
        // TODO: grpc client call
    } else if (program.is_subcommand_used(getCommand)) {
        // "get" subcommand
        auto category = getCommand.get<std::string>("category");
        std::vector<std::string> validCategories = {"schedule", "topology", "lldp", "gptp", "graph"};
        if (std::find(validCategories.begin(), validCategories.end(), category) == validCategories.end()) {
            std::cerr << "Error: '" << category << "' is not a valid category (schedule|topology|lldp|gptp)" << "\n";
            return 1;
        }
        auto format = getCommand.get<std::string>("--format");
        std::vector<std::string> validFormats = {"indented", "json"};
        if (std::find(validFormats.begin(), validFormats.end(), format) == validFormats.end()) {
            std::cerr << "Error: '" << format << "' is not a valid format (indented|json)" << "\n";
            return 1;
        }
        auto selectAll = getCommand.get<bool>("--all");
        std::optional<std::vector<std::string>> selectedTargets = std::nullopt;
        if (!selectAll) {
            selectedTargets = getCommand.get<std::vector<std::string>>("--targets");
        }

        // TODO: Replace with actual querying process --> done
        bool asJson = (format == "json");
        std::vector<std::string> targetList;
        if (selectedTargets.has_value()) {
            targetList = selectedTargets.value();
        }

        // 1. unix socket client connection
        CncGrpcClient grpcClient("unix:///tmp/cnc_socket");

        // 2. forward to correct method and print result
        if (category == "topology") {
            if (!selectAll) {
                std::cerr << "Error: 'topology' currently only supports the --all flag.\n";
                return 1;
            }
            std::cout << grpcClient.getTopology(asJson) << "\n";
        } 
        else if (category == "lldp") {
            std::cout << grpcClient.getLldp(selectAll, targetList, asJson) << "\n";
        } 
        else if (category == "gptp") {
            std::cout << grpcClient.getPtp(selectAll, targetList, asJson) << "\n";
        } 
        else if (category == "schedule") {
            std::cout << grpcClient.getSchedule(selectAll, targetList, asJson) << "\n";
        }
        else if (category == "graph") {
            if (!selectAll) {
                std::cerr << "Error: 'graph' currently only supports the --all flag.\n";
                return 1;
            }
            std::cout << grpcClient.getTopologyGraph(asJson) << "\n";
        }
    } else {
        std::cerr << "Error: You have to provide one of these subcommands: 'set-schedule' or 'get'" << "\n";
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[]) {
    // TODO: Remove
    try {
        return parseAndExec(argc, argv);
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n";
        return 1;
    }

    
    return 0;
}