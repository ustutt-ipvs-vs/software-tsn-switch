#include "SubprocessManager.h"

#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <iostream>
#include <thread>

/**
 * @brief File descriptor of the writing end of the watchdog pipe.
 * Only the parent process may hold this end open. When the parent dies and hence the end is closed, the watchdog will
 * terminate all children. */
int SubprocessManager::watchdogPipeWriter = -1;

/**
 * @brief Run the command. The command is terminated when the parent process terminates.
 *
 * @param processName The informal name of the command, used in the log.
 * @param command All components of the command to be run. Be mindful of command injection attacks.
 * @param async If @a true, detach from the running command. If @a false, block until the command terminates.
 * @param outputCallback Called every time the command writes to stderr or stdout. The first parameter is @p
 * processName, the second parameter is the command's output. The output might contain `/n`, but it won't end with one.
 * @param exitCallback Called when the command terminates. The first parameter is @p processName, the second parameter
 * is the exit code.
 */
void SubprocessManager::run(const std::string& processName, const std::vector<std::string>& command, bool async,
                            const std::function<void(const std::string&, const std::string&)>& outputCallback,
                            const std::function<void(const std::string&, int)>& exitCallback) {
    if (watchdogPipeWriter < 0) {
        runWatchdog();
    }

    int outputPipe[2];  // NOLINT(modernize-avoid-c-arrays)
    if (pipe(outputPipe) == -1) {
        spdlog::critical("Unable to create a pipe between processes (for subprocess)");
        exit(1);
    }

    pid_t subprocessPid = fork();

    if (subprocessPid == 0) {
        // Only the subprocess runs this code

        // Pipe terminal outputs back to parent process
        dup2(outputPipe[1], STDOUT_FILENO);
        dup2(outputPipe[1], STDERR_FILENO);
        close(outputPipe[0]);
        close(outputPipe[1]);

        // Run the command
        std::vector<char*> arguments;
        arguments.reserve(command.size());
        for (const auto& arg : command) {
            arguments.push_back(const_cast<char*>(arg.c_str()));
        }
        arguments.push_back(nullptr);
        execvp(arguments[0], arguments.data());

        // This code should be unreachable since execvp replaces the program
        _exit(1);
    } else {
        // Only the parent runs this code
        SPDLOG_DEBUG("[SUBPROCESS_MANAGER] Subprocess '{0}' has PID {1}", processName, subprocessPid);
        close(outputPipe[1]);
        write(watchdogPipeWriter, &subprocessPid, sizeof(pid_t));

        auto monitorThread =
            std::thread([subprocessPid, resultPipe = outputPipe[0], processName, outputCallback, exitCallback]() {
                std::array<char, 512> buffer;

                // Read until the stream ends
                while (true) {
                    ssize_t bytesRead = read(resultPipe, buffer.data(), buffer.size() - 1);
                    if (bytesRead <= 0) {
                        break;  // The pipe got closed or an error occured
                    }
                    // Cut off ending newline
                    if (buffer[bytesRead - 1] == '\n') {
                        --bytesRead;
                    }
                    buffer[bytesRead] = '\0';
                    outputCallback(processName, std::string(buffer.data()));
                }

                // React to the process ending
                int status;
                waitpid(subprocessPid, &status, 0);
                int exitStatus = WEXITSTATUS(status);
                exitCallback(processName, exitStatus);
            });
        if (async) {
            monitorThread.detach();
        } else {
            monitorThread.join();
        }
    }
}

/**
 * @brief Launch the watchdog that terminates all child subprocesses when the parent process dies.
 */
void SubprocessManager::runWatchdog() {
    SPDLOG_DEBUG("[SUBPROCESS_MANAGER] Parent PID: {}", getpid());
    SPDLOG_TRACE("[SUBPROCESS_MANAGER] Creating watchdog");
    int watchdogPipe[2];  // NOLINT(modernize-avoid-c-arrays)
    /* The watchdog depends on the fact that a pipe returns 0 when all write ends close.
     * But when a parent process creates new children with fork+exec, these children
     * inadvertently get additional write ends and can keep the pipe open after the
     * parent died, forever. O_CLOEXEC closes the pipe end when a process runs exec
     * after fork, making sure that the parent always holds the only write end. */
    if (pipe2(watchdogPipe, O_CLOEXEC) == -1) {
        spdlog::critical("Unable to create a pipe between processes (for watchdog)");
        exit(1);
    }
    pid_t watchdogPid = fork();
    if (watchdogPid == 0) {
        // Only the watchdog runs this code
        close(watchdogPipe[1]);
        std::vector<pid_t> children;

        while (true) {
            pid_t incomingMsg;
            // Block until the parent sends a subprocess PID or closes (which returns 0)
            ssize_t incomingMsgSize = read(watchdogPipe[0], &incomingMsg, sizeof(pid_t));
            if (incomingMsgSize <= 0) {
                // The parent died; terminate the children
                for (pid_t childPid : children) {
                    kill(childPid, SIGTERM);
                }
                sleep(3);
                for (pid_t childPid : children) {
                    kill(childPid, SIGKILL);
                }
                _exit(0);
            } else {
                children.push_back(incomingMsg);
                SPDLOG_TRACE("[SUBPROCESS_MANAGER] Added PID {} to watchdog", incomingMsg);
            }
        }
    } else {
        // Only the parent runs this code
        close(watchdogPipe[0]);
        watchdogPipeWriter = watchdogPipe[1];
        SPDLOG_DEBUG("[SUBPROCESS_MANAGER] Watchdog has PID {}", watchdogPid);
    }
}
