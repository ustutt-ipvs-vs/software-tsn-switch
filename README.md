# Software-based TSN Switch & Controller

This repository contains the source code for the development project **"Software-based TSN Switch for Linux"**.

The goal of this project is the realization of a deterministic real-time network connection (Time-Sensitive Networking) for Linux end devices, virtual machines, and containers. The system consists of a software switch component (tsnctrld) and a central controller (cnc).

## Documentation
For end-users: [User Documentation](http://enpro-switch-64df46.gitlab-pages-vs.informatik.uni-stuttgart.de/docs-user)  
For developers: [Developer Documentation](http://enpro-switch-64df46.gitlab-pages-vs.informatik.uni-stuttgart.de/docs-dev)

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
    Various scripts for setting up dependencies, setting up test environments, and to perform automated formatting.

* **`doc/`:**
    Project documentation files for end-users (`user/`) and developers (`dev/`).

* **`cmake/`:**
    Helper modules for the build system (e.g., to locate libraries like libnetconf2).

## Building & Checking
Please view the corresponding guide in the [developer documentation](http://enpro-switch-64df46.gitlab-pages-vs.informatik.uni-stuttgart.de/docs-dev/howto-build-check.html)!
