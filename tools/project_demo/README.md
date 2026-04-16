# Project demo

See the [project demo](../../doc/user/source/gettingStarted/demo.md) for a concrete example scenario that uses these scripts!

### traffic_source
To compile: `g++ -O2 -o traffic_source traffic_source.cpp`

It generates datagrams and sends them to the given IP address and port 53660.

Usage: `./traffic_source <IP_ADDR> <PAYLOAD_SIZE> <DGRAMS_PER_SEC> <SKB_PRIO>`  
**IP_ADDR:** The IP address to send to, e.g. 172.29.253.1  
**PAYLOAD_SIZE:** How many bytes each datagram's payload should contain. Any value 36 <= x <= 1472 is allowed.  
**DGRAMS_PER_SEC:** How many datagrams to send per second.  
**SKB_PRIO:** The internal SKB priority the frames are handled with. Any value 0 <= x <= 7


### traffic_sink
to compile: `g++ -O2 -o traffic_sink traffic_sink.cpp`

It accepts datagrams sent to its IP address on port 53660 and displays stats.
There are no command line arguments, just run it: `./traffic_sink`
