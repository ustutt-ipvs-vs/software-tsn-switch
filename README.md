# Software-based TSN Switch & Controller

A TSN network (IEEE 802.1Q) is a real-time network where you can configure each switch in the network to forward incoming Ethernet frames based on a precise schedule.
This causes the network to become deterministic:
With an appropriate schedule you can guarantee that your high-priority frames will always arrive on time and without being dropped.

This project allows you to use a Debian computer as a TSN switch, allowing you to connect Docker containers and virtual machines to have them take part in the TSN network.
Our two primary software components are:

- A daemon (`tsnctrld`) that turns your Debian computer into a TSN switch:
  It makes the device's forwarding schedules configurable from afar via NETCONF and ensures precise clock synchronization with neighboring devices.
- A Centralized Network Control (CNC) unit that accepts full-network schedules as an input and distributes & applies them to all the TSN switches in your network using NETCONF.

## Features
TODO


## Documentation (VPN required)
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

## Building & Checking
Please view the corresponding guide in the [developer documentation](http://enpro-switch-64df46.gitlab-pages-vs.informatik.uni-stuttgart.de/docs-dev/howto-build-check.html)! (VPN required!)
