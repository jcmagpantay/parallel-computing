#!/bin/bash
# ============================================================
# RA05 Local Benchmark Runner
# 1 run per (n, t) configuration.
#
# Usage:
#   bash benchmark_local.sh nonca          # LOCAL_NONCA only
#   bash benchmark_local.sh ca             # LOCAL_CA only
#   bash benchmark_local.sh all            # both sections
#   bash benchmark_local.sh quick          # quick test (small n, nonca)
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
STRATEGY="tree"

# Matrix sizes and slave counts
MATRIX_SIZES=(4000 8000 16000)
SLAVE_COUNTS=(2 4 8 16)

# --- Parse mode ---
MODE="${1:-all}"

case "$MODE" in
    nonca)  SECTIONS=("LOCAL_NONCA"); AFFINITIES=("no_affine") ;;
    ca)     SECTIONS=("LOCAL_CA");    AFFINITIES=("affine")    ;;
    all)    SECTIONS=("LOCAL_NONCA" "LOCAL_CA"); AFFINITIES=("no_affine" "affine") ;;
    quick)
        SECTIONS=("LOCAL_NONCA"); AFFINITIES=("no_affine")
        MATRIX_SIZES=(100 200); SLAVE_COUNTS=(2 4)
        echo "*** QUICK MODE ***"; echo ""
        ;;
    *)
        echo "Usage: bash benchmark_local.sh [nonca|ca|all|quick]"
        exit 1
        ;;
esac

CSV_FILE="$SCRIPT_DIR/benchmark_local_${MODE}_${TIMESTAMP}.csv"

# --- CSV header ---
echo "section,n,t,master_total_sec,slowest_slave_sec,fastest_slave_sec" > "$CSV_FILE"
echo "Output: $CSV_FILE"
echo ""

# ============================================================
# Helpers
# ============================================================

wait_for_completion() {
    local log_file="$1"
    while true; do
        if [ -f "$log_file" ] && grep -q "Shutting down" "$log_file" 2>/dev/null; then
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

    # Wait for ports 12000-12020 to drain (important on Linux where TIME_WAIT lingers)
    local drain_wait=0
    while [ $drain_wait -lt 10 ]; do
        if command -v ss &>/dev/null; then
            active=$(ss -tnp 2>/dev/null | grep -E '120[0-9]{2}' | grep -c 'lab05' || true)
        elif command -v netstat &>/dev/null; then
            active=$(netstat -tnp 2>/dev/null | grep -E '120[0-9]{2}' | grep -c 'lab05' || true)
        else
            active=0
        fi
        [ "$active" -eq 0 ] && break
        sleep 1
        drain_wait=$((drain_wait + 1))
    done
    sleep 1
}



# ============================================================
# Main loop
# ============================================================

echo "╔══════════════════════════════════════════════════════╗"
echo "║  RA05 LOCAL BENCHMARK                               ║"
echo "║  Mode:    $MODE"
echo "║  Sizes:   ${MATRIX_SIZES[*]}"
echo "║  Slaves:  ${SLAVE_COUNTS[*]}"
echo "╚══════════════════════════════════════════════════════╝"
echo ""

for idx in "${!SECTIONS[@]}"; do
    section="${SECTIONS[$idx]}"
    affinity="${AFFINITIES[$idx]}"

    echo "╔══════════════════════════════════════════════════════╗"
    echo "║  $section  (affinity=$affinity)"
    echo "╚══════════════════════════════════════════════════════╝"
    echo ""

    for n in "${MATRIX_SIZES[@]}"; do
        for t in "${SLAVE_COUNTS[@]}"; do


            echo -n "  n=$n  t=$t ... "

            cleanup_processes

            BASE_PORT=$(awk '{print $2}' "$SCRIPT_DIR/config.local.txt")
            MASTER_LOG="$SCRIPT_DIR/logs/Node_0_Master_${BASE_PORT}.log"

            bash "$SCRIPT_DIR/run_local.sh" "$n" "$t" "$STRATEGY" "$affinity" > /dev/null 2>&1

            wait_for_completion "$MASTER_LOG"
            sleep 1
            result=$(parse_master_log "$MASTER_LOG")
            echo "$section,$n,$t,$result" >> "$CSV_FILE"

            master_t=$(echo "$result" | cut -d, -f1)
            slow_t=$(echo "$result"   | cut -d, -f2)
            fast_t=$(echo "$result"   | cut -d, -f3)
            echo "master=${master_t}s  slowest=${slow_t}s  fastest=${fast_t}s"

            cleanup_processes
        done
    done

    echo ""
done

echo "╔══════════════════════════════════════════════════════╗"
echo "║  DONE — $CSV_FILE"
echo "╚══════════════════════════════════════════════════════╝"
echo ""
column -t -s, "$CSV_FILE"
