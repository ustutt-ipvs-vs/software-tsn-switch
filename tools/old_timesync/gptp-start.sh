#!/bin/bash

# Starts daemons for time syncinc using gPTP. Use gptp-stop.sh to stop them again.

# Usage: sudo ./gptp-start.sh <is_grandmaster> <nic> [<nic> ...]
# Example: sudo ./gptp-start.sh true enp2s0f2

set -e # Terminate on error
SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd) # Location of this script

# Check for root privileges
if [ "$EUID" -ne 0 ]; then
  echo "Error: You must run this script with root privileges (sudo)" >&2
  exit 1
fi


IS_GRANDMASTER="$1"
shift
if [ $IS_GRANDMASTER = "true" ]; then
    echo "This device will have the grandmaster clock"
else
    echo "This device will NOT have the grandmaster clock"
fi

# NTP one-time-sync
# -----------------
echo "Starting NTP one-time sync"
systemctl start systemd-timesyncd.service >/dev/null 2>&1
timedatectl set-ntp true

echo "Waiting for NTP synchronization..."
SYNC_WAITS=0
while true; do
    sleep 0.5
    SYNC_STATUS=$(timedatectl show -p NTPSynchronized --value)
    if [ "$SYNC_STATUS" = "yes" ]; then
        echo "System clock synchronized"
        break
    fi
    SYNC_WAITS=$((SYNC_WAITS + 1))
    if [ "$SYNC_WAITS" -ge 20 ]; then
        echo "ERROR: NTP synchronization timed out" >&2
        exit 1
    fi
done
timedatectl set-ntp false

for NIC in "$@"; do
    PHC=$(ls /sys/class/net/${NIC}/device/ptp*)
    phc_ctl /dev/${PHC} set >/dev/null 2>&1
    phc_ctl /dev/${PHC} adj 37 >/dev/null 2>&1 # UTC -> TAI offset
    echo "PHC '${PHC}' synchronized"
done
echo "NTP one-time sync successful"

echo "Starting linuxptp daemons..."

# Launch ptp4l
# ------------
NIC_ARGS=()
for NIC in "$@"; do
    NIC_ARGS+=("-i" "$NIC")
done
if [ $IS_GRANDMASTER = "true" ]; then
    nohup ptp4l ${NIC_ARGS[@]} -f ${SCRIPT_DIR}/gPTP.cfg --boundary_clock_jbod=1 --step_threshold=1 --priority1 10 >/dev/null 2>&1 &
else
    nohup ptp4l ${NIC_ARGS[@]} -f ${SCRIPT_DIR}/gPTP.cfg --boundary_clock_jbod=1 --step_threshold=1 >/dev/null 2>&1 &
fi
PTP_PID=$!
echo "ptp4l is now running with PID: $PTP_PID"
echo $PTP_PID > /tmp/ptp4l_process.pid

# Configure offset
# ----------------
pmc -u -b 0 -t 1 "SET GRANDMASTER_SETTINGS_NP clockClass 248 \
    clockAccuracy 0xfe offsetScaledLogVariance 0xffff \
    currentUtcOffset 37 leap61 0 leap59 0 currentUtcOffsetValid 1 \
    ptpTimescale 1 timeTraceable 1 frequencyTraceable 0 \
    timeSource 0xa0" >/dev/null 2>&1
echo "UTC -> TAI offset configured"

# Launch phc2sys
# --------------
nohup phc2sys -a -r --step_threshold=1 --transportSpecific=1 >/dev/null 2>&1 &
PHCSYNC_PID=$!
echo "phc2sys is now running with PID: $PHCSYNC_PID"
echo $PHCSYNC_PID > /tmp/phc2sys_process.pid

echo "gPTP synchronization is now running. Use the gptp-stop.sh script to switch back to regular NTP synchronization."
echo "Done."
