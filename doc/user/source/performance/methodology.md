# Methodology for Performance Analysis

## Throughput per Second Analysis
The performance evaluation was conducted using a custom suite of load generation and data processing scripts. To ensure reproducibility, the testing procedure followed a strict, five-step sequential workflow:

The scripts and log files used for the evaluation can be found [here](../../../../tools/performance_analysis/throughput_rps_analysis/)

1. **System Initialization**: The CNC daemon (cncd) was started. Upon initialization, the server automatically executes a network discovery phase to fetch LLDP and operational data from the connected nodes.

2. **Log Cleansing**: Because the initial network discovery phase generates baseline network traffic and log entries that are not relevant to the gRPC throughput test, the first ~50 lines of the cncd.log file were manually deleted. This ensured an isolated and clean dataset strictly containing the scheduling operations.

3. **Load Generation**: The stress test was initiated using the load_generator_rps.py script. This script acts as an asynchronous workload generator that rapidly escalates the arrival rate. It increments the number of fired requests by one each second (e.g., 5 requests in the 5th second, 6 requests in the 6th second) until the target maximum is reached, intentionally forcing a queue build-up to evaluate bottleneck behavior.

4. **Data Extraction and Parsing**: Once the load generator finished and the server processed the remaining queue, the ai_log_to_csv_converter.py script was executed. This parser scans the raw C++ logs, matches the start and end timestamps of individual threads, calculates the exact execution duration in milliseconds, and identifies hardware-level failures (e.g., ERROR_COMMIT_FAILED). The output is compiled into a structured CSV file.

5. **Visualization**: Finally, the ai_performance_visualizer.py script was run to generate the graphical plots. Before execution, the script was configured to ensure only the plot_requests_per_second(df) function was uncommented (with all other plotting variations disabled). This function directly generates the throughput-latency characteristic graphs based on the parsed CSV metrics.

## LLDP End-to-End Latency Measurement

To evaluate the system's responsiveness to physical link changes (UP/DOWN), we conducted a distributed latency measurement using the following steps:

The scripts and log files used for the evaluation can be found [here](../../../../tools/performance_analysis/end_to_end_lldp_latency)

1. **System Synchronization**: The TSN switch and CNC server were clock-synchronized via gPTP to enable cross-device timestamp comparison.

2. **Event Generation**: A Python script (lldp_toggler.py) on the server toggled the physical link state (ip link set dev <interface> down/up) and logged the exact ground-truth timestamp ($T_0$).

3. **Distributed Tracing**: Custom C++ macros (PERFORMANCE_LOGGING) tracked the event's lifecycle across components, capturing timestamps for hardware detection (tsnctrld), NETCONF propagation, and data parsing (cncd).

4. **Data Aggregation**: A custom parsing script (ai_lldp_log_to_csv_converter.py) merged the distributed logs and calculated the latency for three distinct phases: Hardware Detection (representing the internal LLDP daemon), Notification Propagation (representing the tsnctrld component), and CNC Processing (repsenting the cncd component).

5. **Visualization**: Finally, the ai_performance_visualizer.py script was run to generate the graphical plots. Before execution, the script was configured to ensure only the plot_lldp_pipeline_anatomy(df) function was uncommented (with all other plotting variations disabled). This function directly generates the end-to-end lldp latency graphs based on the parsed CSV metrics.

## Function-Level Performance Profiling

In order to measure the execution time of individual functions and identify bottlenecks, we recorded their start and end
timestamps to a logfile.
We started the `tsnctrld` on two machines to ensure that the timestamps are synchronized via gPTP, and then we ran the
`performance_testing` executable on one of the machines to send requests that trigger the execution of the functions we
want to profile.
The `performance_testing` executable connects to the NETCONF server and sends three different messages to the candidate
datastore, commits it, and then gets the entire operational data-tree relevant to our project.
Each of these three-part operations is sent 100 times in order to get statistically significant results, and the
execution time of each function is recorded in the logs.
The results were analyzed with python scripts, more information about those can be
found [here](../../../../tools/performance_analysis/README.md).