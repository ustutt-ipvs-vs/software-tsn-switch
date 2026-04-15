#!/bin/bash

# Remove the virtual bridge called "tsn-bridge" and reset NICs that were
# connected to it

# Usage: sudo ./vbridge_remove.sh [<nic> ...]
# Example: sudo ./vbridge_remove.sh enp2s0f0 enp2s0f2

BRIDGE="tsn-bridge"

set -e # Terminate on error

# Check for root privileges
if [ "$EUID" -ne 0 ]; then
  echo "Error: You must run this script with root privileges (sudo)" >&2
  exit 1
fi

echo "Removing '$BRIDGE'" >&2
ip link del $BRIDGE

for NIC in "$@"; do
    echo "Removing '$NIC.100'" >&2
    ip link del $NIC.100
done

echo "Done."