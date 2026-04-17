# Setting up a test network environment {#howto-test-network-env}

This document describes the setup for a lightweight testing environment consisting of two isolated network namespaces
connected via a native linux bridge. This architecture is designed to facilitate controlled traffic simulation.

## Architecture

The network topology looks like this:

```text
               +-----------------------------+
               |      HOST LINUX KERNEL      |
               |                             |
               |   +---------------------+   |
               |   |   NATIVE BRIDGE     |   |
               |   |    (br-purple)      |   |
               |   +--+---------------+--+   |
               |      |               |      |
               |  (Port 1)         (Port 2)  |
               |veth-red-host  veth-blue-host|
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

The environment is managed via the `test_vswitch_v3_native.sh` script. You can control the network state using the
following commands:

- **Start the network:**
```bash
./test_vswitch_v3_native.sh start
```

- **Stop the network:**
```bash
./test_vswitch_v3_native.sh stop
```

- **Restart the network:**
```bash
./test_vswitch_v3_native.sh restart
```

## TAPRIO QDisc Configuration

The following command can be used to set **TAPRIO QDiscs** (Time Aware Priority Shaper) on the interface:

```bash
sudo tc qdisc replace dev veth-red-host root handle 100: taprio \
    num_tc 3 \
    map 0 1 2 0 0 0 0 0 0 0 0 0 0 0 0 0 \
    queues 1@0 1@1 1@2 \
    base-time $(date +%s%N) \
    sched-entry S 01 300000 \
    sched-entry S 02 300000 \
    sched-entry S 04 300000 \
    clockid CLOCK_TAI
```

### Parameter Breakdown

* **`qdisc taprio 100:`**
  The Discipline is active.

* **`tc 3 map 0 1 2 ...`**
  You have **3 Traffic Classes**. The priorities are mapped as follows:
  * Priority 0 → TC0
  * Priority 1 → TC1
  * Priority 2 → TC2

* **`clockid TAI`**
  It is correctly using the system clock.

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

### 2. Interface / Hardware

**Check Namespaces existence:**
```bash
sudo ip netns list
```

**Count Hardware Queues (Set to 8 by the script):**
```bash
ls -d /sys/class/net/veth-red-host/queues/tx* | wc -l
```

**Check Hardware capabilities:**
```bash
ethtool -T veth-red-host
```
