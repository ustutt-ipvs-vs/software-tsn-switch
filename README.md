This project implements a software TSN switch for Linux.

The implementation integrates existing mechanisms from the mainstream Linux kernel to implement the switch data plane, in particular, TAPRIO QDiscs and vSwitches. 

The main contribution is a control plane implementation (TSN Control Daemon `tsnctrld`) to configure the data plane (TAPRIO) using standard protocols (NETCONF) and YANG models as specified by IEEE, based on proven NETCONF/YANG libraries. 

A simple Centralized Network Controller (CNC) is also included, which can be used for simple configuration tasks and testing (for a comprehensive CNC implementation, have a look at other projects focusing on the CNC like OpenCNC).  

# Getting started

User and developer documentation is available [here](https://ustutt-ipvs-vs.github.io/software-tsn-switch/). 

# Project Structure

The repository is organized as follows:

* **`tsnctrld/` (TSN Control Daemon):**
    the local component on the Linux host acting as a NETCONF server, applying Gate Control List configurations via Netlink in the kernel (TAPRIO), and monitoring LLDP neighborhoods.

* **`cnc/` (Centralized Network Controller):**
    the central management daemon managing the network topology, calculating schedules (Gate Control Lists), and distributing configurations via NETCONF to the switches.

* **`cnc-web-interface/`:**
    a visual layer on top of the CNC and the TSN Control Daemon supporting inspection of network topology, viewing per-node data, and editing Gate Control List schedules through a browser-based UI.

* **`common/`:**
    shared C++ libraries, helper functions, and data structures used by both the switch and CNC.

* **`yang/`:**
    all used YANG models (IEEE 802.1Qbv, LLDP) serving as interface definitions.

* **`tools/`:**
    various scripts for setting up dependencies, test environments, and for automated formatting.

* **`doc/`:**
    Project documentation files for users (`user/`) and developers (`dev/`).

# Building

Please follow the corresponding guide in the [developer documentation](https://ustutt-ipvs-vs.github.io/software-tsn-switch/docs-dev/howto-build-check.html).

## Acknowledgements

Major contributions have been made by a student software project at University of Stuttgart with the following major contributors:

* Rico Haas
* Axel Körner
* Jannik Schoger
* Roman Vintonyak

