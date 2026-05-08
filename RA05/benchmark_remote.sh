#!/bin/bash
# ============================================================
# RA05 Remote Benchmark Runner
# Runs all (n, t) combos × 3 trials through the REAL terminal
# pipeline (run_remote.sh), then collects results into a CSV.
#
# Usage:
#   bash benchmark_remote.sh [ssh_user] [ssh_password]
#   bash benchmark_remote.sh quick [ssh_user] [ssh_password]
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
CSV_FILE="$SCRIPT_DIR/benchmark_remote_${TIMESTAMP}.csv"
RUNS=3
STRATEGY="tree"

# Matrix sizes and slave counts matching the spreadsheet
MATRIX_SIZES=(4000 8000 16000)
SLAVE_COUNTS=(2 4 8 16)

# Quick mode for testing the script itself
QUICK=0
if [ "${1}" = "quick" ]; then
    QUICK=1
    MATRIX_SIZES=(100 200)
    SLAVE_COUNTS=(2 4)
    RUNS=1
    shift  # remove 'quick' so $1/$2 become ssh_user/ssh_password
    echo "*** QUICK MODE: small matrices, fewer runs ***"
    echo ""
fi

SSH_USER=${1:-$(whoami)}
SSH_PASS=${2:-"useruser"}

# --- CSV header ---
echo "section,n,t,run,master_total_sec,slowest_slave_sec,fastest_slave_sec" > "$CSV_FILE"
echo "Results will be written to: $CSV_FILE"
echo ""

# --- Helper: find the master log (name includes IP) ---
find_master_log() {
    local logdir="$SCRIPT_DIR/logs"
    # There should be exactly one Node_0_Master_*.log per run
    local found
    found=$(ls "$logdir"/Node_0_Master_*.log 2>/dev/null | head -1)
    echo "$found"
}

# --- Helper: wait for master log to have "Shutting down" ---
wait_for_completion() {
    local timeout="${1:-180}"
    local elapsed=0

    while [ $elapsed -lt $timeout ]; do
        local log_file
        log_file=$(find_master_log)
        if [ -n "$log_file" ] && grep -q "Shutting down" "$log_file" 2>/dev/null; then
            echo "$log_file"
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    echo ""
    return 1
}

# --- Helper: parse master log for timing data ---
parse_master_log() {
    local log_file="$1"

    if [ ! -f "$log_file" ]; then
        echo ",,,"
        return
    fi

    # Master total time: line after "MASTER TOTAL TIME:"
    local master_time
    master_time=$(grep -A1 "MASTER TOTAL TIME" "$log_file" | tail -1 | grep -oE '[0-9]+\.[0-9]+')

    # Slave times: "Node X :  TIME sec"
    local slave_times
    slave_times=$(grep -oE 'Node [0-9]+ :  [0-9]+\.[0-9]+ sec' "$log_file" | grep -oE '[0-9]+\.[0-9]+ sec' | sed 's/ sec//')

    local slowest fastest
    if [ -n "$slave_times" ]; then
        slowest=$(echo "$slave_times" | sort -rn | head -1)
        fastest=$(echo "$slave_times" | sort -n  | head -1)
    else
        slowest=""
        fastest=""
    fi

    echo "${master_time},${slowest},${fastest}"
}

# --- Helper: kill leftover processes between runs ---
cleanup_processes() {
    pkill -f "lab05" 2>/dev/null
    sleep 1
    # Close Terminal windows from previous run
    osascript -e '
    tell application "Terminal"
        set winList to every window
        repeat with w in winList
            try
                set tabProcs to processes of first tab of w
                repeat with p in tabProcs
                    if p contains "lab05" or p contains "ra05" or p contains "ssh" then
                        close w
                        exit repeat
                    end if
                end repeat
            end try
        end repeat
    end tell
    ' 2>/dev/null
    sleep 1
}

# --- Compute dynamic timeout based on matrix size ---
get_timeout() {
    local n=$1
    # Remote is slower — give more time
    if [ $n -le 1000 ]; then echo 60
    elif [ $n -le 4000 ]; then echo 120
    elif [ $n -le 8000 ]; then echo 240
    else echo 480
    fi
}

# --- Main benchmark loop ---
echo "╔══════════════════════════════════════════════════════╗"
echo "║  RA05 REMOTE BENCHMARK                              ║"
echo "║  Sizes: ${MATRIX_SIZES[*]}"
echo "║  Slaves: ${SLAVE_COUNTS[*]}"
echo "║  Runs per combo: $RUNS"
echo "║  SSH User: $SSH_USER"
echo "║  Output: $CSV_FILE"
echo "╚══════════════════════════════════════════════════════╝"
echo ""

for n in "${MATRIX_SIZES[@]}"; do
    for t in "${SLAVE_COUNTS[@]}"; do
        local_timeout=$(get_timeout $n)

        echo "┌─────────────────────────────────────────┐"
        echo "│  n=$n  t=$t  ($RUNS runs, timeout=${local_timeout}s)"
        echo "└─────────────────────────────────────────┘"

        for run in $(seq 1 $RUNS); do
            echo -n "  Run $run/$RUNS ... "

            # Clean slate
            cleanup_processes

            # Launch via the real run_remote.sh (opens Terminal windows + SSH)
            bash "$SCRIPT_DIR/run_remote.sh" "$n" "$t" "$STRATEGY" "$SSH_USER" "$SSH_PASS" > /dev/null 2>&1

            # Wait for master to finish
            master_log=$(wait_for_completion "$local_timeout")
            if [ -n "$master_log" ]; then
                # Small grace period for log flush
                sleep 1

                # Parse results
                result=$(parse_master_log "$master_log")
                echo "REMOTE,$n,$t,$run,$result" >> "$CSV_FILE"

                # Show inline
                master_t=$(echo "$result" | cut -d, -f1)
                slow_t=$(echo "$result" | cut -d, -f2)
                fast_t=$(echo "$result" | cut -d, -f3)
                echo "done  master=${master_t}s  slowest=${slow_t}s  fastest=${fast_t}s"
            else
                echo "REMOTE,$n,$t,$run,TIMEOUT,," >> "$CSV_FILE"
                echo "TIMEOUT"
            fi

            # Clean up terminals before next run
            cleanup_processes
        done
        echo ""
    done
done

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║  REMOTE BENCHMARK COMPLETE                         ║"
echo "║  Results: $CSV_FILE"
echo "╚══════════════════════════════════════════════════════╝"
echo ""

# Print summary table
echo "--- CSV Preview ---"
column -t -s, "$CSV_FILE"
