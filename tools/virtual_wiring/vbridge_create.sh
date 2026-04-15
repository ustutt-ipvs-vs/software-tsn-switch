#!/bin/bash

# Create a virtual bridge called "tsn-bridge" and connect the provided network
# interfaces to it, creating a vlan interface in between.

# Usage: sudo ./vbridge_create.sh <nic> [<nic> ...]
# Example: sudo ./vbridge_create.sh enp2s0f0 enp2s0f2

BRIDGE="tsn-bridge"

set -e # Terminate on error

# Check for root privileges
if [ "$EUID" -ne 0 ]; then
    echo "Error: You must run this script with root privileges (sudo)" >&2
    exit 1
fi

if [ "$#" -lt 1 ]; then
    echo "Error: Wrong number of arguments!" >&2
    echo "Usage: sudo ./vbridge_create.sh <nic> [<nic> ...]" >&2
    exit 1
fi

echo "Creating bridge '$BRIDGE'" >&2
ip link add name $BRIDGE type bridge vlan_filtering 1

for NIC in "$@"; do
    echo "Connecting $NIC <-> $NIC.100 <-> $BRIDGE" >&2
    ip addr flush dev $NIC
    ip link add link $NIC name $NIC.100 type vlan id 100 \
        egress 0:0 1:1 2:2 3:3 4:4 5:5 6:6 7:7
    ip link set $NIC.100 master $BRIDGE
    ip link set $NIC up
    ip link set $NIC.100 up
done

echo "Setting '$BRIDGE' to UP" >&2
ip link set $BRIDGE up

echo "Done."
