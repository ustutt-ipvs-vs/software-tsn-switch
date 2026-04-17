# Performance Metric Results

This section presents the empirical results derived from the performance and stress testing of the system.

The detailed methodology regarding the performance testing you can find here: [Methodology](methodology.md)

## Throughput per Second Analysis

The generated performance graphs visualize the end-to-end throughput and latency characteristics of the entire configuration pipeline under continuously escalating load. The primary objective of this test was to identify the maximum processing capacity of the system and observe the queuing behavior when this limit is exceeded.

### Target Infrastructure:
All test requests were directed at a single Time-Sensitive Networking (TSN) switch, identified as vstsn01.

### Payload and Configuration:
The workload consisted of gRPC requests instructing the CNC (Centralized Network Configuration) server to deploy an IEEE 802.1Qbv Gate Control List (GCL) schedule. Each request applied a standardized schedule configuration simultaneously across three network interfaces (enp2s0f1, enp2s0f2, and enp2s0f3). The deployed GCL template included parameters such as the administrative base time, cycle time, and an active control list defining specific gate states and time intervals.

### Varied Parameters:
The independent variable in this experiment was the Arrival Rate, measured in Requests Per Second (RPS). Instead of holding a constant load, the load generator systematically increased the arrival rate by 1 RPS every second. Specifically, the script fired 1 request during the first second, 2 requests during the second second, and continued this linear progression until a predefined ceiling (either 10 or 20 RPS) was reached.

### Meas ured Metrics:

Mean and Peak Response Time (Latency): The complete round-trip time. This measures the entire lifecycle from the client dispatching the gRPC request, the CNC server acquiring necessary locks, the deployment of the configuration down to the physical switch via NETCONF, and the propagation of the final result back to the client.

Failed Commits: The number of requests that were rejected or failed to apply on the hardware due to resource exhaustion or timeouts.

### Throughput 10 RPS no verification
![throughput_set_schedule_no_verify_max_10rps](../../../../tools/performance_analysis/throughput_rps_analysis/throughput_set_schedule_no_verify_max10rps.png)

### Throughput 20 RPS no verification
![throughput_set_schedule_no_verify_max_20rps](../../../../tools/performance_analysis/throughput_rps_analysis/throughput_set_schedule_no_verify_max20rps.png)

### Throughput 10 RPS with verification
![throughput_set_schedule_with_verify_max_10rps](../../../../tools/performance_analysis/throughput_rps_analysis/throughput_set_schedule_with_verify_max10rps.png)

### Throughput 20 RPS with verification
![throughput_set_schedule_no_verify_max_20rps](../../../../tools/performance_analysis/throughput_rps_analysis/throughput_set_schedule_with_verify_max20rps.png)