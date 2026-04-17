# Developer Documentation — CNC Web Interface

This document outlines the frontend architecture, installation steps, and the build/bundling process for the CNC Web Interface.
The web interface is an additional building block on top of the CNC and the TSN control daemon. It provides a visual form of interaction for the underlying components and their data.

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Prerequisites](#prerequisites)
3. [Directory Structure](#directory-structure)
4. [Architecture Overview](#architecture-overview)
5. [Simple Startup (Development)](#simple-startup-development)
6. [Building & Bundling (Production)](#building--bundling-production)
7. [Deployment](#deployment)
8. [Runtime Behaviour](#runtime-behaviour)
9. [Common Issues](#common-issues)

---

## Project Overview

**Tech Stack**

| Layer | Technology |
|---|---|
| Framework | Angular (Standalone Components) |
| Language | TypeScript / JavaScript |
| Backend Communication | gRPC via grpc-web |
| Bundler | Vite |
| Styling | SCSS |

**Core Libraries**

| Library | Purpose |
|---|---|
| Angular Material | UI components (toolbar, sidenav, buttons, icons) |
| grpc-web | Backend communication |
| d3 | Topology visualization |
| RxJS | Reactive programming |

---

## Prerequisites

### Development (Simple Startup)

- Node.js ≥ 20.19
- npm ≥ 9.2
- `grpcwebproxy` 0.15.0 — see [grpcwebproxy on GitHub](https://github.com/improbable-eng/grpc-web/tree/master/go/grpcwebproxy)

### Production (Bundling)

- Node.js 18+
- npm 8+
- `grpcwebproxy` (deployed alongside the binary on the target machine)

---

## Directory Structure

```
cnc-web-interface/
├── frontend/
│   └── src/
│       └── app/
│           ├── grpc/                  # Generated gRPC client & models
│           ├── models/                # Internal frontend data types
│           ├── tabular-view/          # Tabular UI for node inspection
│           │   ├── gcl-sched/         # GCL schedule editor
│           │   ├── lldp/              # LLDP neighbor data
│           │   └── ptp/               # PTP timing data
│           ├── topological-view/      # Graph-based topology visualization
│           ├── app.html               # Root template
│           ├── app.scss               # Global styles
│           └── app.ts                 # Root component (data orchestration)
├── frontend/dist/frontend/browser/   # Built output (generated, not committed)
├── launcher.js                        # Entry point — serves frontend, spawns grpcwebproxy
└── package.json
```

---

## Architecture Overview

The web interface is built hierarchically around two main views.

The **tabular view** provides per-node inspection of interface information (schedules, port data), LLDP neighbor data, and PTP timing information including the current dataset, parent dataset, and performance metrics.

The **topological view** is a visual representation of the network topology, rendering each machine as a node and connections between interfaces based on available LLDP data.

### Data Flow

```
CncService (mock / grpc-web)
        ↓
     App Component
        ↓
 ┌───────────────┬────────────────┐
 │               │                │
Topological   Tabular View    Save Actions
 View            ↓                ↓
 (d3)       Editable Data    gRPC Requests
```

### Root Component (`App`)

The root component manages data fetching, view switching, state updates, and save operations.

**Global state:**

```ts
view: 'tabular' | 'topological'
nodeData: Topology | undefined
topologyGraph: TopologyGraph | undefined
```

**Data fetching strategy** — polls every 5 minutes using RxJS to maintain a consistent UI state while still allowing the user to edit data. If an update occurs during editing, the editing process will be interrupted.

```ts
interval(360_000)
  → switchMap(...)
  → forkJoin(...)
```

**Save logic:**
- Interface-level updates → `setInterfaceSchedule`
- Node-level updates → `setNodeSchedule`

### Topological View

Built with D3.js. Renders the network graph, supports radial, grid, and tree layouts, and emits node selection events.

**Data transformation:** uses the `getTopologyGraph` gRPC interface to fetch the topology graph, builds D3 nodes from graph nodes, and connects them via edges derived from LLDP neighbor data.

**Rendering pipeline:**

```
_buildModel()
    ↓
_applyLayout()
    ↓
_draw()
```

### Tabular View

Provides detailed per-node inspection of interfaces, GCL schedules, LLDP data, and PTP data, and supports edit/save workflows.

---

## Simple Startup (Development)

This approach runs the frontend dev server and the gRPC-Web proxy together without any bundling step. It is the recommended way to run the interface on a development machine or any machine where Node.js is available.

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

```
http://localhost:4200
```

The gRPC-Web proxy listens on port `8080` and forwards requests to the CNC daemon at `localhost:50051`.

---

## Building & Bundling

The deliverable is a single self-contained Linux binary produced by [`pkg`](https://github.com/vercel/pkg). It embeds the built Angular frontend as a static asset snapshot and spawns `grpcwebproxy` as a subprocess at runtime.

> **Important:** Always build the Angular frontend before running `pkg`. The bundler snapshots files at bundle time — if the `dist/` folder does not exist, the assets will be missing from the binary and it will not work.

### Step 1 — Install Dependencies

```bash
npm install
```

### Step 2 — Build the Angular Frontend

```bash
npm run build
```

Verify the output exists before proceeding:

```bash
ls frontend/dist/frontend/browser/index.html
```

If this file is missing, the build failed. Do not proceed to bundling.

### Step 3 — Bundle into a Binary

```bash
npx pkg . --target node18-linux-x64 --output cnc-interface --public
```

| Flag | Purpose |
|---|---|
| `.` | Use `package.json` in the current directory as entry config |
| `--target node18-linux-x64` | Target platform — Node 18, Linux, x64 |
| `--output cnc-interface` | Name of the output binary |
| `--public` | Includes source as plain JS (required for ESM packages like d3) |

> **Note:** You will see warnings like `Failed to make bytecode for d3-*.js` during bundling. These are harmless — d3 v7 ships as ESM which `pkg` cannot compile to V8 bytecode, but the source is still included and works correctly at runtime.

### Step 4 — Verify the Bundle

Confirm that `index.html` was snapshotted into the binary:

```bash
npx pkg . --target node18-linux-x64 --output cnc-interface --debug 2>&1 | grep "index.html"
```

Expected output:

```
Content of .../frontend/dist/frontend/browser/index.html is added to queue.
Stat info of .../frontend/dist/frontend/browser/index.html is added to queue.
```

If `index.html` does not appear, the Angular build is missing. Re-run Step 2.

---

## Deployment

Copy the following two files to the same directory on the target machine:

```
cnc-interface        ← the bundled binary
grpcwebproxy         ← the gRPC-Web proxy binary
```

`grpcwebproxy` is intentionally not bundled into the binary — it is a separate native binary that must be deployed alongside it.

Run:

```bash
chmod +x cnc-interface
./cnc-interface
```

Expected startup output:

```
📂 Serving Frontend from: /snapshot/cnc-web-interface/frontend/dist/frontend/browser
🚀 Starting gRPC Proxy at: /path/to/grpcwebproxy
🌐 Frontend available at http://localhost:4200
```

> **SSH Access:** If you are accessing the target machine via SSH, forward both ports to your local machine to reach the web interface from your local browser:
> ```bash
> ssh -L 4200:localhost:4200 -L 8080:localhost:8080 user@target
> ```

---

## Runtime Behaviour

| Component | Details |
|---|---|
| Frontend | Served statically from the pkg snapshot at `http://localhost:4200` |
| gRPC-Web Proxy | Spawned as subprocess, listens on port `8080`, forwards to `localhost:50051` |
| SPA fallback | All unmatched GET requests return `index.html` for Angular client-side routing |

### Path Resolution Inside the Binary

`pkg` embeds files into a virtual snapshot filesystem rooted at `/snapshot/`. At runtime:

- `__dirname` resolves inside the snapshot — used to read bundled frontend assets
- `process.execPath` resolves to the real binary path — used to locate `grpcwebproxy` on the real filesystem

The launcher checks the snapshot path first and falls back to the directory next to the binary if the snapshot path is unavailable.

---

## Common Issues

### `index.html not found` at runtime
The binary was built before `npm run build` was run. The correct order is: **build Angular first, then bundle with `pkg`**.

### `grpcwebproxy: no such file` at runtime
The `grpcwebproxy` binary is not present next to `cnc-interface` on the target filesystem. Place it in the same directory as the binary.

### `Cannot stat, ENOENT .../dist/frontend/browser` during `pkg`
The Angular build output does not exist at bundle time. Run `npm run build` and verify `frontend/dist/frontend/browser/index.html` exists before running `pkg`.

### `Failed to make bytecode` warnings for d3 modules
Harmless. d3 v7 uses ESM syntax that `pkg` cannot precompile to V8 bytecode. The modules are still included as plain JS source and function correctly at runtime.

### Port `4200` or `8080` already in use
Another process is occupying the port. Identify and stop it:
```bash
lsof -i :4200
lsof -i :8080
```