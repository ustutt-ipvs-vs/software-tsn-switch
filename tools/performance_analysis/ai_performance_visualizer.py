import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
from datetime import datetime

# WARNING: This code is mostly AI generated!

# ==========================================
# CONFIGURATION
# ==========================================
CSV_FILE = "performance_metrics5.csv"

# Set a beautiful, modern theme for the plots
sns.set_theme(style="whitegrid", palette="muted")
plt.rcParams.update({'figure.autolayout': True})

def load_data():
    try:
        df = pd.read_csv(CSV_FILE)
        # Convert timestamp to actual datetime objects for math
        df['Timestamp_Start'] = pd.to_datetime(df['Timestamp_Start'], format="%H:%M:%S.%f")
        return df
    except FileNotFoundError:
        print(f"ERROR: Could not find {CSV_FILE}. Run the parser first!")
        return None

def plot_end_to_end_latency(df):
    """Plot 1: Boxplot showing the overall latency spread per node."""
    plt.figure(figsize=(10, 6))
    
    # Filter only the high-level full request operation
    df_main = df[df['Operation'] == 'CncServiceImpl::SetNodeSchedule']
    
    sns.boxplot(data=df_main, x='Node', y='Duration_ms', hue='Node', palette='Set2', dodge=False)
    sns.stripplot(data=df_main, x='Node', y='Duration_ms', color=".25", alpha=0.5, jitter=True)
    
    plt.title("End-to-End Deployment Latency per Node\n(Lower is better)", fontsize=14, fontweight='bold')
    plt.ylabel("Latency (ms)", fontsize=12)
    plt.xlabel("Target Node", fontsize=12)
    
    plt.savefig("plot1_overall_latency.png", dpi=300)
    print("Saved -> plot1_overall_latency.png")

def plot_request_anatomy(df):
    """Plot 2: Stacked bar chart showing where the time is spent."""
    plt.figure(figsize=(10, 6))
    
    # Define the low-level sub-operations we want to analyze
    sub_ops = ['NetconfSession::editData', 'NetconfSession::commit', 
               'NetconfSession::getData', 'GclParser::parseOperationalGclData']
    
    df_sub = df[df['Operation'].isin(sub_ops)]
    
    # Calculate the mean duration for each operation per node
    df_grouped = df_sub.groupby(['Node', 'Operation'])['Duration_ms'].mean().unstack()
    
    # Reorder columns for a logical flow (Upload -> Apply -> Fetch -> Parse)
    ordered_cols = [col for col in sub_ops if col in df_grouped.columns]
    df_grouped = df_grouped[ordered_cols]
    
    # Plotting
    ax = df_grouped.plot(kind='bar', stacked=True, figsize=(10, 6), colormap='viridis')
    
    plt.title("Anatomy of a Request (Average Time per Phase)", fontsize=14, fontweight='bold')
    plt.ylabel("Average Duration (ms)", fontsize=12)
    plt.xlabel("Target Node", fontsize=12)
    plt.xticks(rotation=0)
    plt.legend(title="Operation Phase", bbox_to_anchor=(1.05, 1), loc='upper left')
    
    plt.savefig("plot2_request_anatomy.png", bbox_inches='tight', dpi=300)
    print("Saved -> plot2_request_anatomy.png")

def plot_degradation_over_time(df):
    """Plot 3: Line chart showing latency across the 50 runs."""
    plt.figure(figsize=(12, 6))
    
    df_main = df[df['Operation'] == 'CncServiceImpl::SetNodeSchedule'].copy()
    
    # Sort chronologically and assign a 'Run Number' (1 to 50) for each node
    df_main = df_main.sort_values('Timestamp_Start')
    df_main['Run_Number'] = df_main.groupby('Node').cumcount() + 1
    
    sns.lineplot(data=df_main, x='Run_Number', y='Duration_ms', hue='Node', marker='o', linewidth=2)
    
    plt.title("Stress Test Performance Over Time", fontsize=14, fontweight='bold')
    plt.ylabel("Latency (ms)", fontsize=12)
    plt.xlabel("Run Number (Sequential Spammer Iterations)", fontsize=12)
    
    plt.savefig("plot3_stress_test_timeline.png", dpi=300)
    print("Saved -> plot3_stress_test_timeline.png")

def plot_parallelism_gantt(df):
    """Plot 4: Gantt chart proving true parallelism for a single burst."""
    plt.figure(figsize=(12, 6))
    
    # We only take the very first burst (e.g., the first 1 second of the test)
    t0 = df['Timestamp_Start'].min()
    df_run = df[df['Timestamp_Start'] < t0 + pd.Timedelta(seconds=1)].copy()
    
    if df_run.empty:
        return

    # Calculate relative start time in milliseconds from the very first log entry
    df_run['Relative_Start_ms'] = (df_run['Timestamp_Start'] - t0).dt.total_seconds() * 1000.0
    
    # Map nodes to colors
    colors = {'vstsn01': '#1f77b4', 'vstsn02': '#ff7f0e', 'vstsn03': '#2ca02c', 'Unknown': '#7f7f7f'}
    
    fig, ax = plt.subplots(figsize=(12, 6))
    
    # We plot a horizontal bar for each operation
    # Y-axis will be the Thread ID to show separate parallel lanes
    threads = sorted(df_run['Thread'].unique())
    
    for idx, row in df_run.iterrows():
        y_pos = threads.index(row['Thread'])
        ax.barh(y_pos, width=row['Duration_ms'], left=row['Relative_Start_ms'], 
                color=colors.get(row['Node'], '#7f7f7f'), edgecolor='black', height=0.6)
        
        # Add tiny labels for the operations inside/above the bars if they are big enough
        if row['Duration_ms'] > 10:
            short_op = row['Operation'].split('::')[-1] # e.g. 'editData'
            ax.text(row['Relative_Start_ms'] + 2, y_pos, short_op, 
                    va='center', ha='left', color='white', fontsize=8, fontweight='bold')

    ax.set_yticks(range(len(threads)))
    ax.set_yticklabels([f"Thread {t}" for t in threads])
    
    # Custom legend for nodes
    import matplotlib.patches as mpatches
    legend_patches = [mpatches.Patch(color=color, label=node) for node, color in colors.items() if node != 'Unknown']
    plt.legend(handles=legend_patches, title="Target Node", loc='upper right')
    
    plt.title("Gantt Chart: Proof of True Thread Parallelism (First Burst)", fontsize=14, fontweight='bold')
    plt.xlabel("Relative Time since test start (ms)", fontsize=12)
    
    plt.savefig("plot4_parallelism_gantt.png", dpi=300)
    print("Saved -> plot4_parallelism_gantt.png")

# important works with the 3 second breaks in the ramp-up test to detect the waves of requests and analyze them separately
def plot_requests_per_second(df):
    """Plot 6: Analysiert den sekündlichen Durchsatz (Requests Per Second) und zeichnet die Kurve."""
    plt.figure(figsize=(12, 6))
    
    # 1. Filtern und nach Zeit sortieren
    df_main = df[df['Operation'] == 'CncServiceImpl::SetNodeSchedule'].copy()
    
    # Sicherstellen, dass Timestamp_Start ein echtes Datetime-Objekt ist, damit diff() funktioniert
    df_main['Timestamp_Start'] = pd.to_datetime(df_main['Timestamp_Start'], format='%H:%M:%S.%f', errors='coerce')
    df_main = df_main.sort_values('Timestamp_Start')
    
    if df_main.empty:
        return

    # ==========================================
    # EXPLIZITE FEHLERERKENNUNG
    # ==========================================
    # Wandelt "COMMIT_FAILED" in NaN um
    df_main['Latency_Numeric'] = pd.to_numeric(df_main['Duration_ms'], errors='coerce')
    df_main['Is_Error'] = df_main['Latency_Numeric'].isna()

    # ==========================================
    # DER NEUE MAGIC TRICK (Sekunden-Erkennung)
    # ==========================================
    # Da der Spammer jede Sekunde feuert und dazwischen 1s schläft, 
    # ist jede Lücke > 0.5 Sekunden der Start einer neuen Welle!
    df_main['Time_Diff'] = df_main['Timestamp_Start'].diff().dt.total_seconds().fillna(0)
    df_main['Wave_ID'] = (df_main['Time_Diff'] > 0.5).cumsum() + 1
    
    # 3. Metriken pro Sekunde (Welle) berechnen
    wave_stats = []
    for wave_id, group in df_main.groupby('Wave_ID'):
        total_reqs = len(group)
        
        avg_lat = group['Latency_Numeric'].mean()
        max_lat = group['Latency_Numeric'].max()
        error_count = group['Is_Error'].sum()
        
        wave_stats.append({
            'Wave': wave_id,              # Das entspricht jetzt der x-ten Testsekunde!
            'Total_Requests': total_reqs, # Das entspricht unseren angepeilten RPS!
            'Avg_Latency_ms': avg_lat,
            'Max_Latency_ms': max_lat,
            'Errors': error_count
        })
        
    df_waves = pd.DataFrame(wave_stats)
    
    df_waves['Avg_Latency_ms'] = df_waves['Avg_Latency_ms'].fillna(0)
    df_waves['Max_Latency_ms'] = df_waves['Max_Latency_ms'].fillna(0)
    
    # === PLOTTING ===
    fig, ax1 = plt.subplots(figsize=(12, 6))
    
    # Balkendiagramm für die Last (grau)
    # Wir nehmen Total_Requests als Label für die X-Achse, weil das genau deine RPS sind!
    bars = ax1.bar(df_waves['Total_Requests'].astype(str), df_waves['Total_Requests'], 
                   color='lightgray', alpha=0.6, label='Submitted Transactions')
                   
    ax1.set_xlabel('Arrival Rate (Requests/sec)', fontsize=12, fontweight='bold')
    ax1.set_ylabel('Cumulative Workload (Requests/window)', fontsize=12, color='dimgray')
    ax1.tick_params(axis='y', labelcolor='dimgray')
    
    # Liniendiagramm für die Latenzen im Vordergrund
    ax2 = ax1.twinx()
    sns.lineplot(data=df_waves, x=df_waves['Total_Requests'].astype(str), y='Avg_Latency_ms', 
                 ax=ax2, marker='o', color='blue', label='Average Response Time', linewidth=2.5)
    sns.lineplot(data=df_waves, x=df_waves['Total_Requests'].astype(str), y='Max_Latency_ms', 
                 ax=ax2, marker='s', color='red', label='Max Response Time', linewidth=2.5)
    
    ax2.set_ylabel('End-to-End Response Time (ms)', fontsize=12, color='black', fontweight='bold')
    
    # Echte Fehler als roten Warntext
    for i, row in df_waves.iterrows():
        if row['Errors'] > 0:
            y_pos = row['Max_Latency_ms'] if row['Max_Latency_ms'] > 0 else (ax2.get_ylim()[1] * 0.5)
            ax2.annotate(f"⚠️ {int(row['Errors'])} Failed Commits", 
                         (i, y_pos),
                         textcoords="offset points", xytext=(0,15), 
                         ha='center', color='red', fontweight='bold', fontsize=10)

    plt.title("Throughput-Latency Characteristics under Sustained Load", fontsize=14, fontweight='bold')
    
    # Legenden zusammenführen
    lines_1, labels_1 = ax1.get_legend_handles_labels()
    lines_2, labels_2 = ax2.get_legend_handles_labels()
    ax2.legend(lines_1 + lines_2, labels_1 + labels_2, loc='upper left')
    
    plt.savefig("plot6_requests_per_second.png", dpi=300)
    print("Saved -> plot6_requests_per_second.png")


def main():
    print("Loading data...")
    df = load_data()
    if df is None:
        return
        
    print(f"Data loaded successfully! Analyzing {len(df)} operations...")
    
    #plot_end_to_end_latency(df)
    #plot_request_anatomy(df)
    #plot_degradation_over_time(df)
    #plot_parallelism_gantt(df)
    #plot_ramp_up_performance(df)
    plot_requests_per_second(df)

    print("\n All visualizations generated successfully!")

if __name__ == "__main__":
    main()