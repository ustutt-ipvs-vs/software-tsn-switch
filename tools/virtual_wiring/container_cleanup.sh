#!/bin/bash

# Cleans up any disconnected namespaces after its linked container has been
# stopped, so it no longer clogs up the list of namespaces (which can be viewed
# using `sudo ip netns list``)

# Usage: sudo ./container_cleanup.sh

set -e # Terminate on error

# Check for root privileges
if [ "$EUID" -ne 0 ]; then
    echo "Error: You must run this script with root privileges (sudo)" >&2
    exit 1
fi

if [ "$#" -ne 0 ]; then
    echo "Error: Wrong number of arguments!" >&2
    echo "Usage: sudo ./container_cleanup.sh" >&2
    exit 1
fi

echo "Removing broken namespace symlinks"
find /var/run/netns/ -xtype l -delete

echo "Done."
