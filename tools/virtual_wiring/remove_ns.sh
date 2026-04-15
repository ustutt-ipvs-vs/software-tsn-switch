#!/bin/bash

# Remove the given network namespace, also severing veth connections

# Usage: sudo ./remove_ns.sh <ns-name>
# Example: sudo ./remove_ns.sh ns-docker1

set -e # Terminate on error

# Check for root privileges
if [ "$EUID" -ne 0 ]; then
    echo "Error: You must run this script with root privileges (sudo)" >&2
    exit 1
fi

echo "Removing namespace '$1'" >&2
ip netns del $1

echo "Done."
