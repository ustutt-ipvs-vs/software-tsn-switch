# Test Network Environment Documentation

This document describes the setup for a lightweight testing environment consisting of two isolated network namespaces connected via a virtual switch (vSwitch). This architecture is designed to facilitate controlled traffic simulation.

## Architecture

The network topology looks like this:

```text
               +-----------------------------+
               |      HOST LINUX KERNEL      |
               |                             |
               |   +---------------------+   |
               |   |     OVS BRIDGE      |   |
               |   |     (ovs-br0)       |   |
               |   +--+---------------+--+   |
               |      |               |      |
               |  (Port 1)         (Port 2)  |
               | veth-red-ovs    veth-blue-ovs
               |      ^               ^      |
               +------|---------------|------+
                      |               |
                [Virtual Cable] [Virtual Cable]
                      |               |
       +--------------v--+         +--v--------------+
       | NAMESPACE: RED  |         | NAMESPACE: BLUE |
       |                 |         |                 |
       |  (Interface)    |         |  (Interface)    |
       |   veth-red      |         |   veth-blue     |
       | IP: 10.0.0.1    |         | IP: 10.0.0.2    |
       +-----------------+         +-----------------+
```

## Management Script

The environment is managed via the `test_vswitch_v2.sh` script. You can control the network state using the following commands:

- **Start the network:**
```bash
./test_vswitch_v2.sh start
```

- **Stop the network:**
```bash
./test_vswitch_v2.sh stop
```

- **Restart the network:**
```bash
./test_vswitch_v2.sh restart
```

## TAPRIO QDisc Configuration

The following command can be used to set **TAPRIO QDiscs** (Time Aware Priority Shaper) on the interface:

```bash
sudo tc qdisc replace dev veth-red-ovs root handle 100: taprio \
    num_tc 3 \
    map 0 1 2 0 0 0 0 0 0 0 0 0 0 0 0 0 \
    queues 1@0 1@1 1@2 \
    base-time $(date +%s%N) \
    sched-entry S 01 300000 \
    sched-entry S 02 300000 \
    sched-entry S 04 300000 \
    flags 0x1 \
    txtime-delay 0 \
    clockid CLOCK_MONOTONIC
```

### Parameter Breakdown

* **`qdisc taprio 100:`**
  The Discipline is active.

* **`tc 3 map 0 1 2 ...`**
  You have **3 Traffic Classes**. The priorities are mapped as follows:
  * Priority 0 → TC0
  * Priority 1 → TC1
  * Priority 2 → TC2

* **`clockid MONOTONIC`**
  It is correctly using the software clock (no PTP hardware synchronization needed).

* **`flags 0x1`**
  It is correctly running in **Software Mode** (TxTime Assist).

* **`cycle-time 900000`**
  Your full cycle is **900µs** (calculated from the sum of schedule entries: 300+300+300).

* **`gatemask 0x1, 0x2, 0x4`**
  * `0x1` (Binary `001`): Gate 0 Open.
  * `0x2` (Binary `010`): Gate 1 Open.
  * `0x4` (Binary `100`): Gate 2 Open.

## Testing Commands

Use the following commands to verify connectivity, switch status, and hardware capabilities.

### 1. Connectivity (Ping)

**Ping from Red Namespace to Blue Namespace:**
```bash
sudo ip netns exec red ping 10.0.0.2
```

### 2. OVS Switch Status

**Show Full Topology:**
```bash
sudo ovs-vsctl show
```

**List only ports:**
```bash
sudo ovs-vsctl list-ports ovs-br0
```

**Show the MAC Address table:**
```bash
sudo ovs-appctl fdb/show ovs-br0
```

### 3. Interface / Hardware

**Check Namespaces existence:**
```bash
sudo ip netns list
```

**Count Hardware Queues (Must be 4 for TAPRIO):**
```bash
ls -d /sys/class/net/veth-red-ovs/queues/tx* | wc -l
```

**Check Hardware capabilities:**
```bash
ethtool -T veth-red-ovs
```