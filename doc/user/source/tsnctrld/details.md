# Technical details

## YANG models

Most of the YANG models used are preinstalled by the `sysrepo` and `netopeer2` dependencies, but some models are not
included in those dependencies and need to be added manually.
These are the YANG models present in the `yang` directory of the project repository, and they are taken directly from
the https://github.com/YangModels/yang repository with the revisions listed below.

| Model                        | Revision   |
|------------------------------|------------|
| `ieee1588-ptp-tt`            | 2023-08-14 |
| `ieee802-dot1ab-lldp`        | 2022-03-15 |
| `ieee802-dot1ab-types`       | 2022-03-15 |
| `ieee802-dot1as-gptp`        | 2025-12-10 |
| `ieee802-dot1q-bridge`       | 2023-10-26 |
| `ieee802-dot1q-sched`        | 2023-10-22 |
| `ieee802-dot1q-sched-bridge` | 2023-10-26 |
| `iana-if-type`               | 2023-01-26 |
| `ietf-interfaces`            | 2018-02-20 |
| `ietf-routing`               | 2018-03-13 |

The `ieee1588-ptp-tt` module has the `performance-monitoring` feature enabled, which allows the `tsnctrld` to populate
the corresponding subtrees in the NETCONF datastore.

We provide operational data for the `ietf-interfaces`, `ieee802-dot1q-bridge`, `ieee802-dot1ab-lldp`, and
`ieee1588-ptp-tt` modules, and support configuration of the `ieee802-dot1q-sched:gate-parameter-table` subtree.
This subtree is added by the `ieee802-dot1q-sched-bridge` module by augmenting the
`/ietf-interfaces:interfaces/interface/ieee802-dot1q-bridge:bridge-port` path and adding
`ieee802-dot1q-sched-bridge:gate-parameter-table` as a child.
Operations attempting to modify any other subtree will be rejected with an error.

## Taprio flags

`taprio` supports two flags to indicate offloading: `0x1` for txtime-assist and `0x2` for full hardware offloading.
Since the NICs in our development machines do not support either, we use full software scheduling, which happens when no
flag is configured.
We must support virtual ethernet interfaces (veths) for the Docker containers, and `taprio` does not support offloading
on veths, so we would have to use software scheduling for those interfaces anyway.

If you have NICs that support offloading and want to use it, you need to let the `tsnctrld` know which interfaces
support offloading and which don't, so that it can set the correct flags when configuring `taprio` on each interface.
This is not implemented, but the information about NIC-capabilities could be passed to the daemon through command line
arguments, and then used in the `QDiscManager` when setting the schedule.

## Removing non-existing schedules

When disabling the schedule of an interface using the `gate-enabled=false` leaf, the `tsnctrld` will attempt to remove
the existing schedule on the hardware by deleting the `taprio` qdisc.
If there is no existing schedule on the hardware, for example because the interface was not previously configured with a
schedule, the `tsnctrld` will fail to remove the non-existing schedule and return an error.
This is a minor issue, but it can be fixed by making sure the user does not try to remove nonexistent things.
\
For a better method, the next point suggest a way of creating a backup of the current state, and checking that
backed-upped `gate-enabled` leaf before trying to remove the schedule.

## Updating multiple interfaces

When updating the datastore to trigger updates to the gate control list on multiple interfaces, the `tsnctrld` will
process the updates sequentially, this is a limitation of `sysrepo`.

Problems may occur when multiple interfaces are updated in one request, and one of the latter processed interfaces fails
at applying the configuration on the hardware, for example due to an invalid schedule.
In this case the remaining interfaces are not updated, but the already processed interfaces are not rolled back, which
can lead to an inconsistent state across the interfaces.
\
If you are a developer, we suggest the following to try and fix this:
In the `RequestContext` class, add a second member like the `m_interfaces` map and use it as a "backup" of the current
state without modifying.
Then, in the `changeGptCallback` callback with the `SR_EV_ABORT`/`sysrepo::Event::Abort` event, use that backup to
restore the interfaces to their previous state.

## Netopeer2 session timeout

By default, the `netopeer2` server has a timeout for ssh sessions of 180 seconds, meaning that if a NETCONF session is
idle for 180 seconds, it will be closed by the server.
Since this can often be too low, the timeout-interval can be increased by modifying the datastore of the
`ietf-netconf-server` module, which is part of the `netopeer2` dependency.
The relevant path is `/ietf-netconf-server:netconf-server/listen/idle-timeout`, and our `setup-on-debian.sh` script sets
it to 3600 seconds (1 hour) by default.

