#!/bin/bash

# Configuration
BRIDGE="ovs-br0"
NS1="red"
NS2="blue"
IP1="10.0.0.1/24"
IP2="10.0.0.2/24"
VETH1="veth-red"
VETH1_BR="veth-red-ovs"
VETH2="veth-blue"
VETH2_BR="veth-blue-ovs"

# Check for root
if [[ $EUID -ne 0 ]]; then
   echo "Error: This script must be run as root (sudo)."
   exit 1
fi

# Check if OVS is installed
if ! command -v ovs-vsctl &> /dev/null; then
    echo "Error: Open vSwitch is not installed. (sudo apt install openvswitch-switch)"
    exit 1
fi

function setup_lab {
    echo "--- Setting up OVS Lab (Multi-Queue Enabled) ---"

    # 1. Create OVS Bridge
    echo "[+] Creating Bridge $BRIDGE"
    ovs-vsctl --may-exist add-br $BRIDGE
    ip link set $BRIDGE up

    # 2. Create Namespaces
    echo "[+] Creating Namespaces: $NS1, $NS2"
    ip netns add $NS1
    ip netns add $NS2

    # 3. Create Veth Pairs (MODIFIED FOR TAPRIO)
    # >>> CHANGE IS HERE <<<
    # We now specify 'numtxqueues 4' BEFORE and AFTER the 'peer' keyword.
    # This forces BOTH ends of the cable to have 4 queues.
    echo "[+] Creating Multi-Queue Virtual Cables (4 queues)"
    
    ip link add $VETH1 numtxqueues 4 numrxqueues 4 type veth peer name $VETH1_BR numtxqueues 4 numrxqueues 4
    
    ip link add $VETH2 numtxqueues 4 numrxqueues 4 type veth peer name $VETH2_BR numtxqueues 4 numrxqueues 4
    # >>> END OF CHANGE <<<

    # 4. Attach to OVS Bridge
    echo "[+] Plugging cables into OVS"
    ip link set $VETH1_BR up
    ip link set $VETH2_BR up
    ovs-vsctl --may-exist add-port $BRIDGE $VETH1_BR
    ovs-vsctl --may-exist add-port $BRIDGE $VETH2_BR

    # 5. Move interfaces to Namespaces
    echo "[+] Moving endpoints to namespaces"
    ip link set $VETH1 netns $NS1
    ip link set $VETH2 netns $NS2

    # 6. Configure IPs inside Namespaces
    echo "[+] Configuring IPs: $IP1, $IP2"
    ip netns exec $NS1 ip link set lo up
    ip netns exec $NS1 ip link set $VETH1 up
    ip netns exec $NS1 ip addr add $IP1 dev $VETH1

    ip netns exec $NS2 ip link set lo up
    ip netns exec $NS2 ip link set $VETH2 up
    ip netns exec $NS2 ip addr add $IP2 dev $VETH2

    echo "--- Setup Complete ---"
    echo "Test with: sudo ip netns exec $NS1 ping 10.0.0.2"
    echo "View OVS:  sudo ovs-vsctl show"
}

function teardown_lab {
    echo "--- Tearing down OVS Lab ---"
    
    # Remove OVS Bridge
    if ovs-vsctl br-exists $BRIDGE; then
        echo "[-] Deleting Bridge $BRIDGE"
        ovs-vsctl del-br $BRIDGE
    fi

    # Remove Namespaces (this automatically deletes the veth pairs)
    echo "[-] Deleting Namespaces"
    ip netns del $NS1 2>/dev/null
    ip netns del $NS2 2>/dev/null

    # Cleanup any stray links if namespaces were already gone
    ip link del $VETH1_BR 2>/dev/null
    ip link del $VETH2_BR 2>/dev/null

    echo "--- Cleanup Complete ---"
}

# Argument handling
case "$1" in
    start)
        setup_lab
        ;;
    stop)
        teardown_lab
        ;;
    restart)
        teardown_lab
        setup_lab
        ;;
    *)
        echo "Usage: sudo ./ovs-lab-v3.sh {start|stop|restart}"
        exit 1
        ;;
esac