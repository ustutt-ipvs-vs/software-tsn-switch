#!/bin/bash

# Remove the virtual bridge called "tsn-bridge", resetting NICs that were
# connected to it

# Usage: sudo ./vbridge_remove.sh

BRIDGE="tsn-bridge"

set -e # Terminate on error

# Check for root privileges
if [ "$EUID" -ne 0 ]; then
  echo "Error: You must run this script with root privileges (sudo)" >&2
  exit 1
fi

if [ "$#" -ne 0 ]; then
    echo "Error: Wrong number of arguments!" >&2
    echo "Usage: sudo ./vbridge_remove.sh" >&2
    exit 1
fi

echo "Removing '$BRIDGE'" >&2
ip link del $BRIDGE

echo "Done."