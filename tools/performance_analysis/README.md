# Performance Analysis Split Pipeline

This folder contains a number of AI generated scripts for parsing and correlating performance logs from the
`performance_testing` and `tsnctrld_app` executables.

Given the two performance traces, you can run the following scripts for parsing them and getting some "nice" plots:

```
python3 perf_pipeline.py
python3 perf_aggregate.py --hide-thread-labels
python3 perf_aggregate.py --hide-thread-labels --root-operation '[GET]' --output-dir tools/performance_analysis/out/aggregate_get_out
```

These scripts generate a number of CSV/JSON files with data parsed from the raw logs, such as how long each function
took, as well as their caller-stack-hierarchies.