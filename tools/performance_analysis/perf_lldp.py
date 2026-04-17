import os
from datetime import datetime

# ==========================================
# KONFIGURATION
# ==========================================
INTERFACE = "enp2s0f1"
LOG_FILE = "lldp_trigger.log"

CMD_DOWN = f"sudo ip link set dev {INTERFACE} down"
CMD_UP = f"sudo ip link set dev {INTERFACE} up"
# ==========================================

def write_log(action):
    # Exakte Zeit auf Mikrosekunden generieren (6 Nachkommastellen)
    now = datetime.now()
    timestamp = now.strftime("%Y-%m-%d %H:%M:%S.%f")
    
    # Exakt das Format deines C++ Loggers! (Thread:0000 als Dummy)
    log_line = f"[{timestamp}] | Thread:0000 | [LLDP] [Python::Toggler] | {action}\n"
    
    with open(LOG_FILE, "a") as f:
        f.write(log_line)
    
    print(f"-> {log_line.strip()}")

def main():
    print("=" * 50)
    print(" LLDP Trigger & Log Generator")
    print("=" * 50)
    print(f"Target Interface: {INTERFACE}")
    print("  [d] : Link DOWN")
    print("  [u] : Link UP")
    print("  [q] : Beenden")
    print("-" * 50)

    with open(LOG_FILE, "w") as f:
        pass # Datei leeren beim Start

    while True:
        choice = input("\nAktion (d/u/q): ").strip().lower()
        
        if choice == 'd':
            write_log("Start Link DOWN")
            os.system(CMD_DOWN)
            write_log("End Link DOWN")
            
        elif choice == 'u':
            write_log("Start Link UP")
            os.system(CMD_UP)
            write_log("End Link UP")
            
        elif choice == 'q':
            print("Beendet.")
            break

if __name__ == '__main__':
    main()