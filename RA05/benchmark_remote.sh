#!/bin/bash
# ============================================================
# RA05 Remote Benchmark Runner
# 1 run per (n, t) configuration.
#
# Usage:
#   bash benchmark_remote.sh [ssh_user] [ssh_password]
#   bash benchmark_remote.sh quick [ssh_user] [ssh_password]
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
STRATEGY="tree"

MATRIX_SIZES=(4000 8000 16000)
SLAVE_COUNTS=(2 4 8 16)

# --- Parse args ---
QUICK=0
if [ "${1}" = "quick" ]; then
    QUICK=1
    MATRIX_SIZES=(100 200)
    SLAVE_COUNTS=(2 4)
    shift
    echo "*** QUICK MODE ***"; echo ""
fi

SSH_USER=${1:-$(whoami)}
SSH_PASS=${2:-"useruser"}

CSV_FILE="$SCRIPT_DIR/benchmark_remote_${TIMESTAMP}.csv"

echo "section,n,t,master_total_sec,slowest_slave_sec,fastest_slave_sec" > "$CSV_FILE"
echo "Output: $CSV_FILE"
echo ""

# ============================================================
# Helpers
# ============================================================

find_master_log() {
    ls "$SCRIPT_DIR/logs"/Node_0_Master_*.log 2>/dev/null | head -1
}

wait_for_completion() {
    while true; do
        local log_file
        log_file=$(find_master_log)
        if [ -n "$log_file" ] && grep -q "Shutting down" "$log_file" 2>/dev/null; then
            echo "$log_file"
            return 0
        fi
        sleep 1
    done
}

parse_master_log() {
    local log_file="$1"
    [ ! -f "$log_file" ] && echo ",," && return

    local master_time
    master_time=$(grep -A1 "MASTER TOTAL TIME" "$log_file" | tail -1 | grep -oE '[0-9]+\.[0-9]+')

    local slave_times
    slave_times=$(grep -oE 'Node [0-9]+ :  [0-9]+\.[0-9]+ sec' "$log_file" | grep -oE '[0-9]+\.[0-9]+ sec' | sed 's/ sec//')

    local slowest fastest
    if [ -n "$slave_times" ]; then
        slowest=$(echo "$slave_times" | sort -rn | head -1)
        fastest=$(echo "$slave_times" | sort -n  | head -1)
    fi

    echo "${master_time},${slowest},${fastest}"
}

cleanup_processes() {
    pkill -f "lab05" 2>/dev/null
    pkill -f "/tmp/ra05_" 2>/dev/null
    sleep 1

    if [[ "$(uname)" == "Darwin" ]]; then
        osascript <<'EOF' 2>/dev/null
tell application "Terminal"
    set wlist to every window
    repeat with w in wlist
        try
            set wname to custom title of first tab of w
            if wname contains "NODE" then
                close w saving no
            end if
        on error
            try
                close w saving no
            end try
        end try
    end repeat
end tell
EOF
    else
        pkill -f "gnome-terminal.*ra05_" 2>/dev/null
        pkill -f "bash /tmp/ra05_" 2>/dev/null
    fi

    sleep 1
}



# ============================================================
# Main loop
# ============================================================

echo "╔══════════════════════════════════════════════════════╗"
echo "║  RA05 REMOTE BENCHMARK                              ║"
echo "║  Sizes:  ${MATRIX_SIZES[*]}"
echo "║  Slaves: ${SLAVE_COUNTS[*]}"
echo "║  SSH:    $SSH_USER"
echo "╚══════════════════════════════════════════════════════╝"
echo ""

for n in "${MATRIX_SIZES[@]}"; do
    for t in "${SLAVE_COUNTS[@]}"; do


        echo -n "  n=$n  t=$t ... "

        cleanup_processes

        bash "$SCRIPT_DIR/run_remote.sh" "$n" "$t" "$STRATEGY" "$SSH_USER" "$SSH_PASS" > /dev/null 2>&1

        master_log=$(wait_for_completion)
        sleep 1
        result=$(parse_master_log "$master_log")
        echo "REMOTE,$n,$t,$result" >> "$CSV_FILE"

        master_t=$(echo "$result" | cut -d, -f1)
        slow_t=$(echo "$result"   | cut -d, -f2)
        fast_t=$(echo "$result"   | cut -d, -f3)
        echo "master=${master_t}s  slowest=${slow_t}s  fastest=${fast_t}s"

        cleanup_processes
    done
done

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║  DONE — $CSV_FILE"
echo "╚══════════════════════════════════════════════════════╝"
echo ""
column -t -s, "$CSV_FILE"
