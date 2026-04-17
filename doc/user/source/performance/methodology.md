# Methodology for Performance Analysis

## Throughput per Second Analysis
The performance evaluation was conducted using a custom suite of load generation and data processing scripts. To ensure reproducibility, the testing procedure followed a strict, five-step sequential workflow:

The scripts and log files used for the evaluation can be found [here](../../../../tools/performance_analysis/throughput_rps_analysis/)

1. **System Initialization**: The CNC daemon (cncd) was started. Upon initialization, the server automatically executes a network discovery phase to fetch LLDP and operational data from the connected nodes.

2. **Log Cleansing**: Because the initial network discovery phase generates baseline network traffic and log entries that are not relevant to the gRPC throughput test, the first ~50 lines of the cncd.log file were manually deleted. This ensured an isolated and clean dataset strictly containing the scheduling operations.

3. **Load Generation**: The stress test was initiated using the load_generator_rps.py script. This script acts as an asynchronous workload generator that rapidly escalates the arrival rate. It increments the number of fired requests by one each second (e.g., 5 requests in the 5th second, 6 requests in the 6th second) until the target maximum is reached, intentionally forcing a queue build-up to evaluate bottleneck behavior.

4. **Data Extraction and Parsing**: Once the load generator finished and the server processed the remaining queue, the ai_log_to_csv_converter.py script was executed. This parser scans the raw C++ logs, matches the start and end timestamps of individual threads, calculates the exact execution duration in milliseconds, and identifies hardware-level failures (e.g., ERROR_COMMIT_FAILED). The output is compiled into a structured CSV file.

5. **Visualization**: Finally, the ai_performance_visualizer.py script was run to generate the graphical plots. Before execution, the script was configured to ensure only the plot_requests_per_second(df) function was uncommented (with all other plotting variations disabled). This function directly generates the throughput-latency characteristic graphs based on the parsed CSV metrics.