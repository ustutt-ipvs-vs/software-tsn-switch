#!/bin/bash

# Stops daemons started by gptp-start.sh and activates NTP

# Usage: sudo ./gptp-stop.sh

set -e # Terminate on error
SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd) # Location of this script

# Check for root privileges
if [ "$EUID" -ne 0 ]; then
  echo "Error: You must run this script with root privileges (sudo)" >&2
  exit 1
fi

# Kill ptp4l daemon
# -----------------
PTP_PID_FILE="/tmp/ptp4l_process.pid"
if [ -f "$PTP_PID_FILE" ]; then
    PID=$(cat "$PTP_PID_FILE")
    rm "$PTP_PID_FILE"
    if kill -0 "$PID" 2>/dev/null; then
        if kill "$PID"; then
            echo "ptp4l daemon (PID: $PID) has been stopped"
        else
            echo "WARNING: Unable to stop ptp4l daemon (PID: $PID)"
        fi
    else
        echo "WARNING: ptp4l daemon (PID: $PID) no longer exists. Doing nothing."
    fi
else
    echo "WARNING: No ptp4l process has been registered for removal. Doing nothing."
fi

# Kill phc2sys daemon
# -------------------
PHCSYNC_PID_FILE="/tmp/phc2sys_process.pid"
if [ -f "$PHCSYNC_PID_FILE" ]; then
    PID=$(cat "$PHCSYNC_PID_FILE")
    rm "$PHCSYNC_PID_FILE"
    if kill -0 "$PID" 2>/dev/null; then
        if kill "$PID"; then
            echo "phc2sys daemon (PID: $PID) has been stopped"
        else
            echo "WARNING: Unable to stop phc2sys daemon (PID: $PID)"
        fi
    else
        echo "WARNING: phc2sys daemon (PID: $PID) no longer exists. Doing nothing."
    fi
else
    echo "WARNING: No phc2sys process has been registered for removal. Doing nothing."
fi

# Start NTP
# ---------
echo "Starting NTP"
systemctl start systemd-timesyncd.service >/dev/null 2>&1
timedatectl set-ntp true
echo "Done."
