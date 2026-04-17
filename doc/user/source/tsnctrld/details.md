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

## Other

By default, the `netopeer2` server has a timeout for ssh sessions of 180 seconds, meaning that if a NETCONF session is
idle for 180 seconds, it will be closed by the server.
Since this can often be too low, the timeout-interval can be increased by modifying the datastore of the
`ietf-netconf-server` module, which is part of the `netopeer2` dependency.
The relevant path is `/ietf-netconf-server:netconf-server/listen/idle-timeout`, and our `setup-on-debian.sh` script sets
it to 3600 seconds (1 hour) by default.