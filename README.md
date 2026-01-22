# Software-based TSN Switch & Controller

This repository contains the source code for the development project **"Software-based TSN Switch for Linux"**.

The goal of this project is the realization of a deterministic real-time network connection (Time-Sensitive Networking) for Linux end devices, virtual machines, and containers. The system consists of a software switch component (tsnctrld) and a central controller (cnc).

## Project Structure

The repository is organized as a monorepo containing the following components:

* **`cnc/` (Centralized Network Controller):**
    The central management daemon. It manages the network topology, calculates schedules (GCLs), and distributes configurations via NETCONF to the switches.

* **`tsnctrld/` (TSN Control Daemon):**
    The local agent on the Linux host. It acts as a NETCONF server, applies GCL configurations via Netlink in the kernel (TAPRIO), and monitors LLDP neighborhoods.

* **`common/`:**
    Shared C++ libraries, helper functions, and data structures used by both the CNC and the agent.

* **`yang/`:**
    The Single-Source-of-Truth for all used YANG models (IEEE 802.1Qbv, LLDP) serving as interface definitions.

* **`tools/`:**
    Scripts for setting up the test environment (Linux namespaces, veth pairs) and infrastructure helpers.

* **`doc/`:**
    Project documentation, requirements specifications (Lastenheft), and architecture diagrams.

* **`cmake/`:**
    Helper modules for the build system (e.g., to locate libraries like libnetconf2).

## Getting Started

In order to compile the code, various libraries are needed, the steps needed are written in [steps_for_installing_updating.txt] (TODO:Cleanup).

To build the project, run CMake (all commands assume you're in the repo's root):
```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && make --directory=build
```

To run all tests:
```bash
ctest --test-dir build
```

## Formatting
We use clang-format as our formatter.
Make sure you have it installed: `sudo apt install clang-format`.

Before you commit, please run our formatting script to keep the code tidy:

```bash
./tools/format-project.sh
```

If you forget to do this, the pipeline will complain.
If you would like an automated reminder, install our Git hook, which checks the code before each commit:

```bash
cp -rf tools/hooks/ .git; chmod --recursive +x .git/hooks/
```

## Linting
We use clang-tidy as our linter.
Make sure you have it installed: `sudo apt install clang-tidy`.
To lint a specific file (after building with the above commands!):

```bash
clang-tidy -p build path/to/file.cpp
```

To lint *everything*:

```bash
run-clang-tidy -p build -quiet '(cnc|common|tsnctrld)/.*'
```

Since linting takes a while to process and can sometimes be a pain to comply with,
there is no git hook for linting and the pipline will allow linting failures.
