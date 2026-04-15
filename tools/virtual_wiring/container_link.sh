#!/bin/bash

# Link the virtual bridge 'tsn-bridge' (which must exist) to a container (which
# must already run) using a veth pair for TAPRIO. Keep in mind that the link is
# destroyed when the container stops.

# Usage: sudo ./container_link.sh <container-name> <container-cidr>
# Example: sudo ./create_ns.sh demo1 172.29.253.225/27

# After you created the link, view its namespace here: ip netns list
# And its contents here: sudo ip netns exec <NS-NAME> ip address show
# Use container_cleanup.sh to remove leftover namespace symlinks

BRIDGE="tsn-bridge"

set -e # Terminate on error

if [ "$#" -ne 2 ]; then
    echo "Error: Wrong number of arguments!" >&2
    echo "Usage: sudo ./container_link.sh <container-name> <container-cidr>" >&2
    exit 1
fi

# Check for root privileges
if [ "$EUID" -ne 0 ]; then
    echo "Error: You must run this script with root privileges (sudo)" >&2
    exit 1
fi

if ! ip link show $BRIDGE type bridge >/dev/null 2>&1 ; then
    echo "Error: Virtual bridge '$BRIDGE' does not exist (Did you run vbridge_create.sh already?)" >&2
    exit 1
fi

echo "Exposing namespace of container '$1' as identifier 'ns-$1'" >&2
PID="$(docker inspect -f '{{.State.Pid}}' $1)"
echo "Container '$1' has PID $PID" >&2
mkdir -p /var/run/netns
ln -sf /proc/$PID/ns/net /var/run/netns/ns-$1

echo "Setting up veth pair 'veth-$1' <-> 'veth-$1-b'" >&2
ip link add veth-$1 numtxqueues 4 numrxqueues 4 type veth peer name veth-$1-b numtxqueues 4 numrxqueues 4

echo "Connecting end 'veth-$1-b' to vbridge '$BRIDGE'" >&2
ip link set veth-$1-b master $BRIDGE
ip link set veth-$1-b up

echo "Moving end 'veth-$1' into namespace 'ns-$1'" >&2
ip link set veth-$1 netns ns-$1
ip netns exec ns-$1 ip link set lo up
ip netns exec ns-$1 ip link set veth-$1 up

echo "Assigning CIDR $2 to interface 'veth-$1'" >&2
ip netns exec ns-$1 ip address add $2 dev veth-$1

echo "Done."
