# Virtual wiring scripts

See the [project demo](../../doc/user/source/gettingStarted/demo.md) for a concrete example scenario!

Don't forget that you need to `chmod +x <SCRIPT-PATH>` each script before you can run it.
To change them all at once: `find ./tools/virtual_wiring/ -type f -iname "*.sh" -exec chmod +x {} \;`

To connect docker containers to the TSN network you need to

1. Create a virtual TSN bridge, example: `sudo ./tools/virtual_wiring/vbridge_create.sh enp2s0f0 enp2s0f2`
2. Start a container with no default netwok setup and a name, example: `sudo docker run --rm -it --network=none --name demo1 tsnctrld-demo`
3. When the container is created, link it to the virtual TSN bridge and give it a CIDR, here: `sudo ./tools/virtual_wiring/container_link.sh demo1 172.29.253.1/24`

After the containers are gone, use e.g. `sudo ./tools/virtual_wiring/container_cleanup.sh` to clean up all expired namespaces and use `sudo ./tools/virtual_wiring/vbridge_remove.sh` to remove the virtual TSN bridge.
