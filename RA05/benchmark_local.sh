#!/bin/bash
# ============================================================
# RA05 Local Benchmark Runner
# Runs all (n, t, affinity) combos × 3 trials through the
# REAL terminal pipeline (run_local.sh), then collects results
# into a CSV.
#
# Usage:
#   bash benchmark_local.sh              # full benchmark
#   bash benchmark_local.sh quick        # quick test (small n)
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
CSV_FILE="$SCRIPT_DIR/benchmark_${TIMESTAMP}.csv"
RUNS=3
STRATEGY="tree"

# Matrix sizes and slave counts matching the spreadsheet
MATRIX_SIZES=(4000 8000 16000)
SLAVE_COUNTS=(2 4 8 16)

# Quick mode for testing the script itself
if [ "${1}" = "quick" ]; then
    MATRIX_SIZES=(100 200)
    SLAVE_COUNTS=(2 4)
    RUNS=1
    echo "*** QUICK MODE: small matrices, fewer runs ***"
    echo ""
fi

# --- CSV header ---
echo "section,n,t,run,master_total_sec,slowest_slave_sec,fastest_slave_sec" > "$CSV_FILE"
echo "Results will be written to: $CSV_FILE"
echo ""

# --- Helper: wait for master log to have "Shutting down" ---
wait_for_completion() {
    local log_file="$1"
    local timeout="${2:-120}"   # default 120s timeout
    local elapsed=0

    while [ $elapsed -lt $timeout ]; do
        if [ -f "$log_file" ] && grep -q "Shutting down" "$log_file" 2>/dev/null; then
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done

    echo "  ⚠ TIMEOUT after ${timeout}s waiting for $log_file"
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

# --- Helper: kill leftover lab05 processes between runs ---
cleanup_processes() {
    pkill -f "lab05.*local" 2>/dev/null
    sleep 1
    # Close any Terminal windows opened by previous run
    # (only the ones running lab05 scripts)
    osascript -e '
    tell application "Terminal"
        set winList to every window
        repeat with w in winList
            try
                set tabProcs to processes of first tab of w
                repeat with p in tabProcs
                    if p contains "lab05" or p contains "ra05" then
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
    if [ $n -le 1000 ]; then echo 30
    elif [ $n -le 4000 ]; then echo 60
    elif [ $n -le 8000 ]; then echo 120
    else echo 240
    fi
}

# --- Main benchmark loop ---
run_benchmark() {
    local section="$1"
    local affinity="$2"

    echo "╔══════════════════════════════════════════════════════╗"
    echo "║  BENCHMARK: $section"
    echo "║  Strategy: $STRATEGY | Affinity: $affinity"
    echo "╚══════════════════════════════════════════════════════╝"
    echo ""

    for n in "${MATRIX_SIZES[@]}"; do
        for t in "${SLAVE_COUNTS[@]}"; do
            local timeout
            timeout=$(get_timeout $n)

            echo "┌─────────────────────────────────────────┐"
            echo "│  n=$n  t=$t  ($RUNS runs, timeout=${timeout}s)"
            echo "└─────────────────────────────────────────┘"

            for run in $(seq 1 $RUNS); do
                echo -n "  Run $run/$RUNS ... "

                # Clean slate
                cleanup_processes

                # Determine master log path
                BASE_PORT=$(awk '{print $2}' "$SCRIPT_DIR/config.local.txt")
                MASTER_LOG="$SCRIPT_DIR/logs/Node_0_Master_${BASE_PORT}.log"

                # Launch via the real run_local.sh (opens Terminal windows)
                bash "$SCRIPT_DIR/run_local.sh" "$n" "$t" "$STRATEGY" "$affinity" > /dev/null 2>&1

                # Wait for master to finish
                if wait_for_completion "$MASTER_LOG" "$timeout"; then
                    # Small grace period for log flush
                    sleep 1

                    # Parse results
                    local result
                    result=$(parse_master_log "$MASTER_LOG")
                    echo "$section,$n,$t,$run,$result" >> "$CSV_FILE"

                    # Show inline
                    local master_t=$(echo "$result" | cut -d, -f1)
                    local slow_t=$(echo "$result" | cut -d, -f2)
                    local fast_t=$(echo "$result" | cut -d, -f3)
                    echo "done  master=${master_t}s  slowest=${slow_t}s  fastest=${fast_t}s"
                else
                    echo "$section,$n,$t,$run,TIMEOUT,," >> "$CSV_FILE"
                    echo "TIMEOUT"
                fi

                # Clean up terminals before next run
                cleanup_processes
            done
            echo ""
        done
    done
}

# ============================================================
# Run benchmarks
# ============================================================

echo "╔══════════════════════════════════════════════════════╗"
echo "║  RA05 LOCAL BENCHMARK                               ║"
echo "║  Sizes: ${MATRIX_SIZES[*]}"
echo "║  Slaves: ${SLAVE_COUNTS[*]}"
echo "║  Runs per combo: $RUNS"
echo "║  Output: $CSV_FILE"
echo "╚══════════════════════════════════════════════════════╝"
echo ""

# Section 1: LOCAL RUN (NONCA) — no core affinity
run_benchmark "LOCAL_NONCA" "no_affine"

# Section 2: LOCAL RUN (CA) — with core affinity
run_benchmark "LOCAL_CA" "affine"

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║  BENCHMARK COMPLETE                                 ║"
echo "║  Results: $CSV_FILE"
echo "╚══════════════════════════════════════════════════════╝"
echo ""

# Print summary table
echo "--- CSV Preview ---"
column -t -s, "$CSV_FILE"
