This project implements a software TSN switch for Linux.

The implementation integrates existing mechanisms from the mainstream Linux kernel to implement the switch data plane, in particular, TAPRIO QDiscs and vSwitches. 

The main contribution is a control plane implementation (`tsnctrld`) to configure the data plane (TAPRIO) using standard protocols (NETCONF) and YANG models as specified by IEEE, based on proven NETCONF/YANG libraries. 

A simple Centralized Network Controller (CNC) is also included, which can be used for simple configuration tasks and testing (for a comprehensive CNC implementation, have a look at other projects focusing on the CNC like OpenCNC).  

# Getting started

User and developer documentation is available [here](https://ustutt-ipvs-vs.github.io/software-tsn-switch/). 

# Project Structure

The repository is organized as follows:

* **`cnc/` (Centralized Network Controller):**
    The central management daemon. It manages the network topology, calculates schedules (GCLs), and distributes configurations via NETCONF to the switches.

* **`tsnctrld/` (TSN Control Daemon):**
    The local agent on the Linux host. It acts as a NETCONF server, applies GCL configurations via Netlink in the kernel (TAPRIO), and monitors LLDP neighborhoods.

* **`cnc-web-interface/`:**
    A visual layer on top of the CNC and the TSN Control Daemon. It allows you to inspect network topology, view per-node data, and edit GCL schedules through a browser-based UI.

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

