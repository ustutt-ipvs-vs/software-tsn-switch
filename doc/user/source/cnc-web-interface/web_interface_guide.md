# User Documentation — CNC Web Interface

The CNC Web Interface provides a visual layer on top of the CNC and TSN control daemon. It allows you to inspect network topology, view per-node data, and edit GCL schedules through a browser-based UI.

---

## Table of Contents

1. [Getting Started](#getting-started)
    - [Option A: Simple Startup](#option-a-simple-startup-recommended)
    - [Option B: Bundled Binary](#option-b-bundled-binary)
2. [Accessing the Interface](#accessing-the-interface)
3. [Using the Interface](#using-the-interface)
    - [Topological View](#topological-view)
    - [Tabular View](#tabular-view)
    - [Editing & Saving](#editing--saving)
4. [Troubleshooting](#troubleshooting)

---

## Getting Started

There are two ways to run the CNC Web Interface depending on your environment.

### Option A: Simple Startup (Recommended)

Best suited for machines where Node.js is available, such as a development machine or a controller node with a full toolchain installed.

**Requirements:**
- Node.js ≥ 20.19
- npm ≥ 9.2
- `grpcwebproxy` binary placed in the project root (`/cnc-web-interface`)directory and marked executable

### Step 1 — Install Dependencies

Navigate to the root of the project and run:

```bash
npm install
```

### Step 2 — Place `grpcwebproxy`

Download the appropriate `grpcwebproxy` binary (Version 0.15.0) for your platform from [GitHub](https://github.com/improbable-eng/grpc-web/tree/master/go/grpcwebproxy) and place it in the project root:

```
cnc-web-interface/
└── grpcwebproxy      ← place it here
```

Make it executable:

```bash
chmod +x grpcwebproxy
```

### Step 3 — Start

```bash
npm run start:together
```

This runs both the Angular dev server and `grpcwebproxy` concurrently. The frontend will be available at:

http://localhost:4200

> **Note:** `grpcwebproxy` must be located in the project root directory. It will not be found if it is only available on the system PATH or placed elsewhere.

---

### Option B: Bundled Binary

Best suited for deployment on target machines without a Node.js installation. The interface is distributed as a single self-contained binary that includes the frontend and manages the proxy automatically.

**Requirements:**
- `cnc-interface` binary
- `grpcwebproxy` binary placed in the **same directory** as `cnc-interface`


Please refer to the developer documentation for instructions on how to build the bundled binary.

---

## Accessing the Interface

Open a browser and navigate to:

```
http://localhost:4200
```

If you are running the interface on a **remote machine via SSH**, forward the required ports to your local machine first:

```bash
ssh -L 4200:localhost:4200 -L 8080:localhost:8080 user@target
```

Then open `http://localhost:4200` in your local browser as normal.

The interface requires the CNC daemon to be running and reachable on the target machine. If the daemon is not running, the interface will load but data will not be available.

---

## Using the Interface

The interface is divided into two main views, switchable via the toolbar.

### Topological View

The topological view renders the network as an interactive graph. Each node represents a device and each edge represents an LLDP-discovered connection between interfaces.

**Interactions:**
- **Click a node** to select it and load its data in the tabular view
- **Drag nodes** to reposition them
- **Scroll** to zoom in and out
- The layout can be switched between radial, grid, and tree arrangements via the toolbar

This view is useful for getting an at-a-glance understanding of the network structure and for navigating to a specific device.

---

### Tabular View

The tabular view shows detailed data for the currently selected node. It is organised into three sections:

**Interfaces**
Lists all network interfaces on the selected node, including port information and associated GCL schedules. Each interface can be expanded to inspect and edit its schedule entries.

**LLDP**
Displays LLDP neighbor data for the selected node — which interfaces are connected to which peers, and their reported identifiers. This data is read-only and reflects the last polled state.

**PTP**
Shows PTP timing information for the node, including the current dataset, parent dataset, and performance metrics. This data is read-only.

---

### Editing & Saving

GCL schedule editing is supported at both the interface level and the node level.
> **Important:** While saving at node level, the web interface will only set data of interfaces that where changed, data of other interfaces will not be manipulated. This corresponds to the `HOLD` command of th CNC CLI.

To edit a schedule:

1. Select a node in the topological view or navigate to it in the tabular view
2. Expand the relevant interface entry
3. Modify the schedule entries as needed
4. Click **Save** to write the changes back to the daemon via gRPC

> **Important:** The interface polls for fresh data every 5 minutes. If a data refresh occurs while you are editing, your unsaved changes will be discarded. Save your changes before the next poll cycle or be prepared to re-enter them.

Changes are submitted via two gRPC calls depending on scope:
- Per-interface changes use `setInterfaceSchedule`
- Node-wide changes use `setNodeSchedule`

---

## Troubleshooting

### The interface loads but shows no data
The tsn control daemon and/or the CNC is likely not running or having an error. Check if both components are running and see their outputs.


### `grpcwebproxy: no such file or directory`
The `grpcwebproxy` binary is missing or not in the expected location. It must be placed in the same directory as `cnc-interface` (for the bundled binary) or in the project root (for the simple startup). Also verify it is marked as executable:

```bash
chmod +x grpcwebproxy
```

### The page at `localhost:4200` does not load
Check that the interface is actually running. If using SSH, verify that port forwarding is active for both port `4200` and port `8080`. If a previous instance did not shut down cleanly, a port may still be occupied:
```bash
lsof -i :4200
lsof -i :8080
```

### My edits were lost
A data refresh occurred while you were editing. The interface polls for new data every 5 minutes and will overwrite local state on each refresh. Save your changes promptly after editing.

### The interface is slow or unresponsive
This is typically caused by a large network topology or a slow connection to the target machine. Try switching to the tabular view and selecting a specific node directly rather than loading the full topological graph.