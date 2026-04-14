# Project demo

It's a cheatsheet for now, TODO

Setup commands:
```
sudo docker build -t tsnctrld-demo tools/project_demo/
sudo docker run --rm -it --name demo1 tsnctrld-demo
sudo ip link add name dbridge type bridge vlan_filtering 1
sudo ip link set dbridge up
sudo ip link add dveth-a numtxqueues 4 numrxqueues 4 type veth peer name dveth-a-br numtxqueues 4 numrxqueues 4
sudo ip link add dveth-b numtxqueues 4 numrxqueues 4 type veth peer name dveth-b-br numtxqueues 4 numrxqueues 4
sudo ip link set dveth-a-br master dbridge
sudo ip link set dveth-b-br master dbridge
sudo ip link set dveth-a-br up
sudo ip link set dveth-b-br up
sudo ip link set enp2s0f2 master dbridge
sudo ip link set enp2s0f2 up
CPID=$(sudo docker inspect -f '{{.State.Pid}}' demo1)
sudo mkdir -p /var/run/netns
sudo ln -sf /proc/$CPID/ns/net /var/run/netns/$CPID
sudo ip link set dveth-a netns $CPID
sudo ip netns exec $CPID ip link set dveth-a up
sudo ip netns exec $CPID ip address
```

Teardown (terminating the container erases namespace and veth pair):
```
ip link del dbridge
```
