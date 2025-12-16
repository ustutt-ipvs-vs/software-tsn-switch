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