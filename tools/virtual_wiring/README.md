# Virtual wiring scripts

To connect docker containers to the TSN network you need to

1. Create a virtual TSN bridge, example: `sudo ./create_vbridge.sh enp2s0f0 enp2s0f2`
2. Create a network namespace for each container, example: `sudo ./create_ns.sh ns-docker1 veth-docker1` for a single one
3. When the container is created, use one of the namespaces for it, example: `sudo docker ... --network ns:/run/netns/ns-docker1 ...`
4. In the container, use the appropriate veth end, which is `veth-docker1` in this example

After the containers are gone, use e.g. `sudo ./remove_ns.sh ns-docker1` to clean up the namespaces one by one and use `sudo ./remove_vbridge.sh` to remove the virtual TSN bridge.
