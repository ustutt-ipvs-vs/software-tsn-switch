#!/bin/bash

# Create a network namespace and connect it to the virtual bridge 'tsn-bridge'
# (which must exist already) using a veth pair for TAPRIO.

# Usage: sudo ./create_ns.sh <ns-name> <veth-name>
# Example: sudo ./create_ns.sh ns-docker1 veth-docker1

# After you created a namespace, view it here: ip netns list
# And its contents here: sudo ip netns exec <NS-NAME> ip address show
# And remove the namespace with: sudo ip netns del <NS-NAME>

BRIDGE="tsn-bridge"

set -e # Terminate on error

# Check for root privileges
if [ "$EUID" -ne 0 ]; then
    echo "Error: You must run this script with root privileges (sudo)" >&2
    exit 1
fi

if ! ip link show $BRIDGE type bridge >/dev/null 2>&1 ; then
    echo "Error: Virtual bridge '$BRIDGE' does not exist (Did you run create_vbridge.sh already?)" >&2
    exit 1
fi

echo "Creating namespace '$1'" >&2
ip netns add $1

echo "Setting up veth pair '$2' <-> '$2-b'" >&2
ip link add $2 numtxqueues 4 numrxqueues 4 type veth peer name $2-b numtxqueues 4 numrxqueues 4

echo "Connecting end '$2-b' to vbridge '$BRIDGE'" >&2
ip link set $2-b master $BRIDGE
ip link set $2-b up

echo "Moving end '$2' into namespace '$1'" >&2
ip link set $2 netns $1
ip netns exec $1 ip link set lo up
ip netns exec $1 ip link set $2 up

echo "Done."
