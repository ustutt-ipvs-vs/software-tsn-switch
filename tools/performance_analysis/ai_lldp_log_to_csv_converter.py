import os
import csv
from datetime import datetime

# ==========================================
# KONFIGURATION
# ==========================================
LOG_FILES = [
    "cncd_lldp.log", 
    "tsnctrld_lldp.log", 
    "toggle_script_lldp.log"
]
CSV_FILE = "lldp_performance_metrics.csv"

def parse_time(time_str):
    # Schneidet die Klammern weg und parst die Zeit
    clean_str = time_str.strip("[]")
    return datetime.strptime(clean_str, "%Y-%m-%d %H:%M:%S.%f")

def main():
    all_events = []
    
    print("Lese Log-Dateien ein...")
    for filename in LOG_FILES:
        if not os.path.exists(filename):
            print(f"WARNUNG: Datei {filename} nicht gefunden, überspringe...")
            continue
            
        with open(filename, 'r') as f:
            for line in f:
                parts = line.strip().split(" | ")
                if len(parts) >= 4:
                    ts = parse_time(parts[0])
                    thread = parts[1].split(":")[1] if ":" in parts[1] else parts[1]
                    context = parts[2]
                    msg = parts[3]
                    all_events.append((ts, thread, context, msg))

    if not all_events:
        print("Keine Events gefunden! Überprüfe die Log-Dateien.")
        return

    # Alles chronologisch nach Uhrzeit sortieren!
    all_events.sort(key=lambda x: x[0])
    print(f"{len(all_events)} Events chronologisch sortiert. Starte Analyse...")

    results = []
    
    # State Machine Variablen
    t0 = None # Python Trigger
    t1 = None # Switch Detect
    t2 = None # CNC Start Fetch
    
    current_action = "" # "Link DOWN" oder "Link UP"

    for ts, thread, ctx, msg in all_events:
        
        # 1. TRIGGER (Kabel wird virtuell gezogen)
        if "Python::Toggler" in ctx and msg.startswith("Start Link"):
            t0 = ts
            # Wir speichern uns, ob es ein UP oder DOWN Event war
            current_action = "DOWN" if "DOWN" in msg else "UP"
            t1 = t2 = None # Reset für neuen Durchlauf
            
        # 2. HARDWARE DETECT (Switch merkt es)
        elif "CHANGE_EVENT" in ctx and msg == "Start" and t0 is not None:
            t1 = ts
            dur = round((t1 - t0).total_seconds() * 1000.0, 2)
            time_str = t0.strftime("%H:%M:%S.%f")[:-3]
            results.append((time_str, thread, "vstsn01", f"1_Hardware_Detection_{current_action}", dur))
            
        # 3. CNC NOTIFICATION (CNC reagiert und startet Fetch)
        elif "fetchLldpDataForNode" in ctx and "Start LLDP" in msg and t1 is not None:
            t2 = ts
            dur = round((t2 - t1).total_seconds() * 1000.0, 2)
            time_str = t0.strftime("%H:%M:%S.%f")[:-3]
            results.append((time_str, thread, "vstsn01", f"2_Notification_Propagation_{current_action}", dur))
            
        # 4. CNC FINISHED (Baum ist fertig geparst)
        elif "parseLldpData" in ctx and "Finished" in msg and t2 is not None:
            t3 = ts
            
            # Phase 3: Processing Time
            dur_processing = round((t3 - t2).total_seconds() * 1000.0, 2)
            time_str = t0.strftime("%H:%M:%S.%f")[:-3]
            results.append((time_str, thread, "vstsn01", f"3_CNC_Processing_{current_action}", dur_processing))
            
            # TOTAL END-TO-END TIME
            dur_total = round((t3 - t0).total_seconds() * 1000.0, 2)
            results.append((time_str, thread, "vstsn01", f"Total_E2E_{current_action}", dur_total))
            
            # Reset für das nächste Stecker-Ziehen
            t0 = t1 = t2 = None 

    # ==========================================
    # CSV SCHREIBEN
    # ==========================================
    if not results:
        print("Konnte keine zusammenhängenden Start/End Paare finden.")
        return

    print(f"Schreibe {len(results)} Metriken in '{CSV_FILE}'...")
    with open(CSV_FILE, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(["Timestamp_Start", "Thread", "Node", "Operation", "Duration_ms"])
        for row in results:
            writer.writerow(row)
            
    print("Fertig! CSV wurde erfolgreich erstellt.")

if __name__ == "__main__":
    main()