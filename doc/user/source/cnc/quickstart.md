# CNC switch quickstart

## CNC Daemon (`cncd`)

### Execution & Network Discovery
After completing the installation, the `cncd` executable is available globally and can be started from any terminal. 
Once running, the application automatically connects to all configured TSN nodes (see [Installation](install.md)).

Upon connection, the daemon performs an initial network discovery. It connects to the `tsnctrld` on each target node ([tsnctrld docs](../tsnctrld/quickstart.md)) and fetches the baseline system state, including:
* Available network interfaces
* PTP (Precision Time Protocol) synchronization data
* LLDP (Link Layer Discovery Protocol) topology data

### gRPC API & Interfaces
To allow external applications to control the network, the `cncd` exposes a gRPC server. It listens on two endpoints simultaneously:
* **Unix Domain Socket:** `/tmp/cnc_socket` (Recommended for local, high-performance communication)
* **TCP Socket:** `0.0.0.0:50051` (Ideal for external API testing, e.g., using Postman)

### Endpoints
Via these gRPC endpoints, the daemon provides the following network management capabilities:
* Retrieve the current network topology
* Fetch operational LLDP and PTP data
* Read active schedule data (Gate Control Lists)
* Provision and set new network schedules

*Note: For a detailed technical description of the gRPC interfaces and protocol buffers, please refer to the [Developer Documentation](http://enpro-switch-64df46.gitlab-pages-vs.informatik.uni-stuttgart.de/docs-dev/classCncServiceImpl.html).*

## Command Line Arguments (`cnc_cli`)

The `cnc_cli` is a command-line utility engineered for direct interaction with the `cncd` background daemon. Designed for speed and automation, it allows administrators to seamlessly query network states, retrieve topologies, or push configurations straight from the terminal.

### Basic Usage
The utility operates on a strict, argument-driven execution model. By passing specific action arguments, the CLI processes the request, returns the formatted output to standard out (stdout), and immediately terminates. This stateless architecture makes the `cnc_cli` exceptionally well-suited for both manual administration and integration into automated shell scripts.

```bash
cnc_cli <argument>
```

For a comprehensive list of all available commands, expected parameters, and output structures, please refer to the [Command Line Arguments](clargs.md).
