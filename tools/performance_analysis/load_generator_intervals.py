import grpc
import time
import concurrent.futures
from google.protobuf import json_format

# Import the generated gRPC classes
import cnc_pb2
import cnc_pb2_grpc

# ==========================================
# CONFIGURATION
# ==========================================
CHANNEL_ADDRESS = 'unix:///tmp/cnc_socket' 

TARGET_NODES = ["vstsn01"] # , "vstsn02", "vstsn03"
INTERFACES = ["enp2s0f1", "enp2s0f2", "enp2s0f3"]

# How much requests do we fire per node in each step? (RAMP-UP)
RAMP_UP_STEPS = [1, 2, 4, 8, 12, 16, 24, 32, 48, 64]

# ==========================================
# PAYLOAD TEMPLATE
# ==========================================
GCL_TEMPLATE = {
    "admin_data_set": True,
    "gate_enabled": True,
    "config_change": True,
    "admin_gate_states": 255,
    "admin_cycle_time_extension_ns": 500,
    "admin_cycle_time": {"numerator": 1000000, "denominator": 1000000000},
    "admin_base_time": {"seconds": "0", "nanoseconds": 0},
    "admin_control_list": [
        {"index": 0, "operation_name": "sched:set-gate-states", "gate_states_value": 255, "time_interval_value": 500000},
        {"index": 1, "operation_name": "sched:set-gate-states", "gate_states_value": 0, "time_interval_value": 500000}
    ]
}

def build_grpc_request(node_name):
    request_dict = {"host_name": node_name, "interfaces": []}
    for iface_name in INTERFACES:
        request_dict["interfaces"].append({
            "name": iface_name,
            "bridge_port": {"gate_parameter_table": GCL_TEMPLATE}
        })
    request = cnc_pb2.SetNodeScheduleRequest()
    json_format.ParseDict(request_dict, request)
    return request

def fire_single_request(node_name):
    """Fires a single gRPC request."""
    request = build_grpc_request(node_name)
    try:
        with grpc.insecure_channel(CHANNEL_ADDRESS) as channel:
            stub = cnc_pb2_grpc.CncServiceStub(channel)
            # no timeout for real stress test
            response = stub.SetNodeSchedule(request)
            return "SUCCESS" if response.overall_success else "FAIL_LOGIC"
    except grpc.RpcError as e:
        return f"ERR_{e.code().name}"

def run_load_step(concurrency_per_node):
    """Fire the current load step and wait for all threads to complete."""
    errors = {}
    total_requests = concurrency_per_node * len(TARGET_NODES)
    
    print(f"\n[+] STEP: {concurrency_per_node} requess per node")
    print(f"    -> Fire now {total_requests} requests...")
    
    with concurrent.futures.ThreadPoolExecutor(max_workers=total_requests) as executor:
        futures = []
        for node in TARGET_NODES:
            for _ in range(concurrency_per_node):
                futures.append(executor.submit(fire_single_request, node))
        
        # Wait until all requests are done and collect results
        for future in concurrent.futures.as_completed(futures):
            status = future.result()
            if status != "SUCCESS":
                errors[status] = errors.get(status, 0) + 1
    
    if errors:
        print(f"    -> Finished! WARNING: there were errors: {errors}")
    else:
        print(f"    -> Finished! All {total_requests} requests successful.")

def main():
    print("=" * 60)
    print("Startet ramp up spammer")
    print(f"📍 Targets : {', '.join(TARGET_NODES)}")
    print("=" * 60)
    
    for current_load in RAMP_UP_STEPS:
        run_load_step(current_load)
        
        print("    -> pause for 3 seconds...")
        time.sleep(3)
        
    print("\n Finished!")

if __name__ == '__main__':
    main()