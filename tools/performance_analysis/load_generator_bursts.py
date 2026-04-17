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
# Using the Unix Domain Socket
CHANNEL_ADDRESS = 'unix:///tmp/cnc_socket' 

# Target hardware
NODES = ["vstsn01", "vstsn02", "vstsn03"]
INTERFACES = ["enp2s0f1", "enp2s0f2", "enp2s0f3"]

# The GCL schedule template
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

# ==========================================
# HELPER FUNCTIONS
# ==========================================
def build_grpc_request(node_name):
    """Builds the dynamic gRPC request for a specific node."""
    request_dict = {
        "host_name": node_name,
        "interfaces": []
    }
    
    # Add all 3 physical interfaces with the same schedule
    for iface_name in INTERFACES:
        request_dict["interfaces"].append({
            "name": iface_name,
            "bridge_port": {
                "gate_parameter_table": GCL_TEMPLATE
            }
        })
        
    # Magic: Parse the Python Dictionary directly into the Protobuf Object
    request = cnc_pb2.SetNodeScheduleRequest()
    json_format.ParseDict(request_dict, request)
    return request

def fire_request(node_name, thread_id):
    """Executed by the worker threads. Fires the request and measures latency."""
    start_time = time.time()
    request = build_grpc_request(node_name)
    
    try:
        # Establish connection and fire
        with grpc.insecure_channel(CHANNEL_ADDRESS) as channel:
            stub = cnc_pb2_grpc.CncServiceStub(channel)
            
            # Timeout set to 10 seconds to catch thrashing deadlocks
            response = stub.SetNodeSchedule(request, timeout=10.0)
            
            end_time = time.time()
            latency = (end_time - start_time) * 1000 # in milliseconds
            
            status = "SUCCESS" if response.overall_success else "FAILED (Server returned false)"
            print(f"[Thread {thread_id:02d}] {status} on {node_name} - Latency: {latency:.2f} ms")
            
    except grpc.RpcError as e:
        end_time = time.time()
        latency = (end_time - start_time) * 1000
        print(f"[Thread {thread_id:02d}] GRPC ERROR on {node_name} - Time until failure: {latency:.2f} ms - Reason: {e.code().name}")

# ==========================================
# TEST SCENARIOS
# ==========================================
def test_multithreading_across_nodes():
    print(f"\n--- STARTING MULTI-NODE TEST ({len(NODES)} Threads) ---")
    print("Goal: Prove that the C++ server can configure different switches strictly in parallel.")
    
    # Fires exactly one request per node simultaneously
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(NODES)) as executor:
        for i, node in enumerate(NODES):
            executor.submit(fire_request, node, i)

def test_single_node_thrashing():
    num_spam_requests = 15
    target_node = "vstsn01"
    print(f"\n--- STARTING THRASHING TEST ({num_spam_requests} Threads on {target_node}) ---")
    print("Goal: Overload the Sysrepo-Daemon queue (provoke deadlocks or timeouts).")
    
    # Fires 15 requests simultaneously at the exact same node
    with concurrent.futures.ThreadPoolExecutor(max_workers=num_spam_requests) as executor:
        for i in range(num_spam_requests):
            executor.submit(fire_request, target_node, i)

if __name__ == '__main__':
    # Uncomment the scenario you want to test!
    
    for i in range(50):
        test_multithreading_across_nodes()
        time.sleep(1) # soll nicht komplett abschmieren
    
    # time.sleep(1) # Give the switch a short breather
    
    # test_single_node_thrashing()