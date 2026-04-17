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

# Wie hoch wollen wir zählen? (Von 1 bis 10 Requests pro Sekunde)
MAX_RPS = 10 

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
            response = stub.SetNodeSchedule(request)
            return "SUCCESS" if response.overall_success else "FAIL_LOGIC"
    except grpc.RpcError as e:
        return f"ERR_{e.code().name}"

def main():
    print("=" * 60)
    print("Startet den schnellen Stack-Spammer (jede Sekunde +1)")
    print(f"📍 Targets : {', '.join(TARGET_NODES)}")
    print("=" * 60)
    
    # Wir brauchen einen großen Thread-Pool, der alle Requests aufnimmt,
    # auch wenn sie sich im Hintergrund extrem stauen.
    executor = concurrent.futures.ThreadPoolExecutor(max_workers=2000)
    futures = []
    
    for current_rps in range(1, MAX_RPS + 1):
        print(f"[+] Sekunde {current_rps}: Feuere {current_rps} Requests ab...")
        
        # Requests blind abfeuern (ohne hier auf eine Antwort zu warten!)
        for _ in range(current_rps):
            for node in TARGET_NODES:
                futures.append(executor.submit(fire_single_request, node))
                
        # Exakt 1 Sekunde Pause machen. 
        # Wenn der Server zu langsam ist, baut sich jetzt der Stau auf.
        time.sleep(1.0)
        
    print("\n[!] Alle Sekunden-Wellen abgefeuert.")
    print("    Warte jetzt darauf, dass der Server den restlichen Stau abarbeitet...")
    
    # Am Ende warten wir, bis der Server auch den allerletzten Request aus dem Stau verdaut hat
    errors = {}
    for future in concurrent.futures.as_completed(futures):
        status = future.result()
        if status != "SUCCESS":
            errors[status] = errors.get(status, 0) + 1
            
    if errors:
        print(f"\n[X] Finished! WARNING: there were errors: {errors}")
    else:
        total_requests = sum(range(1, MAX_RPS + 1)) * len(TARGET_NODES)
        print(f"\n[✓] Finished! All {total_requests} requests successful.")
        
    executor.shutdown()

if __name__ == '__main__':
    main()