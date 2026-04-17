# CNC Command Line Arguments

The `cnc_cli` utility provides two primary subcommands: `get` for retrieving operational network state, and `set-schedule` for applying new configurations.

---

## Data Retrieval: `get`
The `get` command allows administrators to fetch various operational parameters and states from the TSN network.

**Syntax:**
```bash
cnc_cli get <category> [target-options] [format-options]
```

**Categories (`<category>`):**
* `schedule`: Retrieves the currently active Gate Control Lists (GCLs).
* `lldp`: Retrieves Link Layer Discovery Protocol topology data.
* `gptp`: Retrieves Generalized Precision Time Protocol synchronization state.
* `topology`: Retrieves the complete network topology layout. *(Note: Only supports the `--all` flag).*
* `graph`: Retrieves the raw topology graph data. *(Note: Only supports the `--all` flag).*

**Target Options (Mutually Exclusive):**
You must specify whether to fetch data for the entire network or for specific targets.
* `-a`, `--all`: Fetches information from all configured devices and interfaces.
* `-t`, `--targets <target1> <target2> ...`: Fetches information exclusively for the specified nodes or interfaces (e.g., `vstsn01.enp0f2s0`). 

**Format Options:**
* `-f`, `--format <format>`: Defines the output structure. Options are `indented` (default, human-readable) or `json` (ideal for programmatic parsing).

**Examples:**
```bash
# Get the full network topology
cnc_cli get topology --all

# Get LLDP data for specific interfaces formatted as JSON
cnc_cli get lldp --format json --targets vstsn01.enp0f2s0 vstsn02.enp0f2s2
```

---

## Configuration: `set-schedule`
The `set-schedule` command is used to push and deploy new Gate Control Lists (GCLs) to the network nodes.

**Syntax:**
```bash
cnc_cli set-schedule <input> [options]
```

**Input (`<input>`):**
* `<filepath>`: The absolute or relative path to a JSON file containing the schedule definitions (e.g., `~/schedule.json`).
* `-`: Instructs the CLI to read the JSON schedule data directly from standard input (stdin), enabling data piping from other scripts.

**Json Structure** 


**Options:**
* `-g`, `--gap <mode>`: Defines how the CNC should handle "gaps" or incomplete schedules (i.e., GCLs that exist on the switch but are not explicitly mentioned in your new JSON file). 
  * `deny` *(Default)*: Rejects the incomplete schedule deployment entirely.
  * `hold`: Deploys the new changes but leaves any unmentioned GCLs exactly as they currently are.
  * `zero`: Deploys the new changes and forcefully clears/zeroes all unmentioned GCLs.

**Examples:**
```bash
# Push a schedule from a file, leaving unmodified GCLs untouched
cnc_cli set-schedule ~/schedule.json --gap hold

# Pipe a schedule dynamically from a Python generator into the CLI
python3 super-scheduler.py | cnc_cli set-schedule -
```

## Schedule JSON Structure (Payload Format)

When pushing new configurations using the `set-schedule` command, the input JSON file must strictly adhere to the expected Gate Control List (GCL) schema. 

The payload is structured hierarchically per target node (`host_name`), followed by a list of physical interfaces, and finally the gate parameters.

**Example Payload:**

```json
{
  "host_name": "vstsn01",
  "interfaces": [
    {
      "name": "veth-red-host",
      "bridge_port": {
        "gate_parameter_table": {
          "admin_data_set": true,
          "gate_enabled": true,
          "config_change": true,
          "admin_gate_states": 255,
          "admin_cycle_time_extension_ns": 500,
          "admin_cycle_time": {
            "numerator": 1000000,
            "denominator": 1000000000
          },
          "admin_base_time": {
            "seconds": "0",
            "nanoseconds": 0
          },
          "admin_control_list": [
            {
              "index": 0,
              "operation_name": "sched:set-gate-states",
              "gate_states_value": 255,
              "time_interval_value": 300000
            },
            {
              "index": 1,
              "operation_name": "sched:set-gate-states",
              "gate_states_value": 255,
              "time_interval_value": 700000
            }
          ]
        }
      }
    }
  ]
}
```

**Key Parameter Breakdown:**
* **`host_name`**: The exact identifier of the target node (e.g., `vstsn01`).
* **`interfaces.name`**: The specific physical interface to configure (e.g., `veth-red-host`).
* **`admin_cycle_time`**: Defines the total duration of one repeating schedule cycle as a fraction of a second (`numerator` / `denominator`). For example, `1000000 / 1000000000` equates to a 1-millisecond cycle.
* **`admin_base_time`**: The precise gPTP network timestamp (in seconds and nanoseconds) defining when the schedule should start executing. Setting this to `"0"` initiates the schedule as soon as the configuration is processed.
* **`admin_control_list`**: The sequential list of gate operations that make up the cycle.
  * **`gate_states_value`**: An 8-bit integer mask (0-255) defining which of the 8 traffic queues are open (1) or closed (0). For example, `255` (binary `11111111`) opens all queues, while `0` closes all queues.
  * **`time_interval_value`**: The duration for which this specific gate state is held, defined in nanoseconds. The sum of all intervals in the list should logically match the total `admin_cycle_time`.