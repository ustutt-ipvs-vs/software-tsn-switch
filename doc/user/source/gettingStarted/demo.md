# Project demo

- Terminals A1, A2, A3 are on vstsn01
- Terminals B1, B2, B3 are on vstsn02. 
- Both are on the project-demo branch with the repo's root as context
- You can switch to other branches after setup but need to switch back for teardown
- The order of executing commands is often important!

### Setup:

```sh
# A1:
sudo docker build -t tsnctrld-demo tools/project_demo/
sudo ./tools/virtual_wiring/vbridge_create.sh enp2s0f2
# A2:
sudo docker run --rm -it --network=none --name demo1 tsnctrld-demo
# A3:
sudo docker run --rm -it --network=none --name demo2 tsnctrld-demo
# A1:
sudo ./tools/virtual_wiring/container_link.sh demo1 172.29.253.1/24
sudo ./tools/virtual_wiring/container_link.sh demo2 172.29.253.2/24
# B1:
sudo docker build -t tsnctrld-demo tools/project_demo/
sudo ./tools/virtual_wiring/vbridge_create.sh enp2s0f2
# B2:
sudo docker run --rm -it --network=none --name demo1 tsnctrld-demo
# B3:
sudo docker run --rm -it --network=none --name demo2 tsnctrld-demo
# B1:
sudo ./tools/virtual_wiring/container_link.sh demo1 172.29.253.3/24
sudo ./tools/virtual_wiring/container_link.sh demo2 172.29.253.4/24
```

### Run test:

```sh
# B2:
./traffic_sink.sh
# B3:
./traffic_sink.sh
# A2:
./traffic_source 172.29.253.3 1400 50000 3
# A2:
./traffic_source 172.29.253.4 1400 50000 2
```

### Traffic shaping:

Now make sure that B2 gets all its 50.000 datagrams per sec!
Use the CNC's CLI or webinterface!
Manually you can do it this way:

```sh
# A1:
sudo tc qdisc replace dev enp2s0f2 parent root handle 100 taprio num_tc 8 map 1 0 2 3 4 5 6 7 0 0 0 0 0 0 0 0 queues 1@0 1@1 1@2 1@3 1@4 1@5 1@6 1@7 base-time 0 sched-entry S 82 100000 sched-entry S 08 1000000 sched-entry S 04 100000 clockid CLOCK_TAI
```

To remove:

```sh
# A1:
sudo tc qdisc delete dev enp2s0f2 root
```

### Teardown & Cleanup:
```sh
# A2: (stop container)
^C
exit
# A3: (stop container)
^C
exit
# B2: (stop container)
^C
exit
# B3: (stop container)
^C
exit
# A1:
sudo ./tools/virtual_wiring/container_cleanup.sh
sudo ./tools/virtual_wiring/vbridge_remove.sh
# B1:
sudo ./tools/virtual_wiring/container_cleanup.sh
sudo ./tools/virtual_wiring/vbridge_remove.sh
```
