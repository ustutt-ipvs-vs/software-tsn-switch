import re
import csv
from datetime import datetime

# WARNING: This code is mostly AI generated!

# ==========================================
# KONFIGURATION
# ==========================================
LOG_FILE = "cncd2.log"               
CSV_FILE = "performance_metrics8.csv" 

log_pattern = re.compile(r"^\[(.*?)\]\s+\|\s+Thread:(\d+)\s+\|\s+\[(.*?)\]\s+\|\s+(.*)$")

def parse_time(time_str):
    return datetime.strptime(time_str, "%Y-%m-%d %H:%M:%S.%f")

def main():
    lines = []
    thread_to_nodes = {} # Speichert: Thread-ID -> Liste von (Uhrzeit, Node-Name)

    print(f"Lese Log-Datei '{LOG_FILE}'...")

    try:
        # PASS 1: Alle Zeilen einlesen und Thread -> Node Zuordnungen "lernen"
        with open(LOG_FILE, 'r') as f:
            for line in f:
                match = log_pattern.match(line.strip())
                if not match: 
                    continue 
                
                ts_str, thread, context, msg = match.groups()
                timestamp = parse_time(ts_str)
                lines.append((timestamp, thread, context, msg))

                # Wenn wir irgendwo den Switch-Namen finden, bringen wir ihn mit dem Thread in Verbindung!
                node_match = re.search(r"node:\s*(\w+)", msg)
                if node_match:
                    node_name = node_match.group(1)
                    if thread not in thread_to_nodes:
                        thread_to_nodes[thread] = []
                    thread_to_nodes[thread].append((timestamp, node_name))
    except FileNotFoundError:
        print(f"FEHLER: Die Datei '{LOG_FILE}' wurde nicht gefunden.")
        return

    # Hilfsfunktion: Sucht den passenden Switch für einen Thread anhand der Uhrzeit
    def get_node_for_thread(thread, t_time):
        if thread not in thread_to_nodes:
            return "Unknown"
        hints = thread_to_nodes[thread]
        # Finde den Hinweis, der zeitlich am nächsten an dieser Operation liegt
        closest_hint = min(hints, key=lambda x: abs((x[0] - t_time).total_seconds()))
        return closest_hint[1]

    active_ops = {}
    results = []

    # PASS 2: Start und Ende matchen und mit Node-Namen anreichern
    for timestamp, thread, context, msg in lines:
        if thread not in active_ops:
            active_ops[thread] = {}

        if msg.startswith("Start "):
            active_ops[thread][context] = {
                "start_time": timestamp,
                "msg": msg
            }
        
        elif msg.startswith("Finished ") or msg.startswith("Successfully") or msg.startswith("Got return") or msg.startswith("ERROR_COMMIT_FAILED"):
            if context in active_ops[thread]:
                start_data = active_ops[thread].pop(context)
                start_time = start_data["start_time"]
                
                # 1. Steht der Node direkt in der Start- oder End-Nachricht?
                node_match_start = re.search(r"node:\s*(\w+)", start_data["msg"])
                node_match_end = re.search(r"node:\s*(\w+)", msg)

                if node_match_start:
                    node_name = node_match_start.group(1)
                elif node_match_end:
                    node_name = node_match_end.group(1)
                else:
                    # 2. Magic Fallback: Wir nutzen das Wissen aus Pass 1!
                    node_name = get_node_for_thread(thread, start_time)
                
                # text für fehler
                if msg.startswith("ERROR_COMMIT_FAILED"):
                    final_duration = "COMMIT_FAILED"
                else:
                    duration_ms = (timestamp - start_time).total_seconds() * 1000.0
                    final_duration = round(duration_ms, 2)
                
                results.append({
                    "Timestamp_Start": start_time.strftime("%H:%M:%S.%f")[:-3],
                    "Thread": thread,
                    "Node": node_name,
                    "Operation": context,
                    "Duration_ms": final_duration
                })

    # ==========================================
    # CSV DATEI SCHREIBEN
    # ==========================================
    if not results:
        print("Es konnten keine passenden Start/Stop Paare gefunden werden.")
        return

    print(f"Schreibe {len(results)} Metriken in '{CSV_FILE}'...")
    with open(CSV_FILE, 'w', newline='') as f:
        fieldnames = ["Timestamp_Start", "Thread", "Node", "Operation", "Duration_ms"]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        
        writer.writeheader()
        writer.writerows(results)
        
    print("Fertig! N/A Fehler ist behoben. Du kannst die CSV jetzt öffnen.")

if __name__ == "__main__":
    main()