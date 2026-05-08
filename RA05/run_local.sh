#!/bin/bash
# RA05 Local Mode: 1MPB Scatter + MA Compute + M1PR Gather
# Each node gets its own terminal window
# Auto-detects OS: macOS (Terminal.app) or Linux (gnome-terminal)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- Terminal launcher (OS-agnostic) ---
open_terminal() {
    local title="$1"
    local cmd="$2"
    local log_file="$3"

    local tmp_script
    tmp_script=$(mktemp /tmp/ra05_XXXXXX)
    cat > "$tmp_script" << TMPEOF
#!/bin/bash
echo -ne "\033]0;${title}\007"
echo "=== ${title} ==="
cd "$SCRIPT_DIR"
if [ -n "${log_file}" ]; then
    ${cmd} | tee "logs/${log_file}"
else
    ${cmd}
fi
echo ""
echo "Press Enter to close..."
read
rm -f "$tmp_script"
TMPEOF
    chmod +x "$tmp_script"

    if [[ "$(uname)" == "Darwin" ]]; then
        osascript -e "tell application \"Terminal\"
            activate
            do script \"bash '$tmp_script'\"
        end tell"
    else
        gnome-terminal --title="$title" -- bash "$tmp_script"
    fi
}

echo "=== Compiling lab05 ==="
gcc -o "$SCRIPT_DIR/lab05" "$SCRIPT_DIR/sockets.c" -lm
if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

# Quick test mode: bash run_local.sh test
if [ "${1}" = "test" ]; then
    echo ""
    echo "=== Running standalone MA test ==="
    "$SCRIPT_DIR/lab05" test
    exit $?
fi

N=${1:-1024}            # Matrix size
SLAVES=${2:-8}           # Number of slave processes
TOTAL_NODES=$((SLAVES + 1)) # Total participating nodes (1 master + slaves)
STRATEGY=${3:-tree}      # 'linear' (O(n)) or 'tree' (O(log n))
AFFINITY=${4:-no_affine} # 'affine' or 'no_affine'
TEST_FLAG=${5:-}         # 'test' to use known 5x5 matrix
OS_NAME=$(uname)

echo "Detected OS: $OS_NAME"
echo "Matrix: ${N}x${N} | Slaves: $SLAVES (Total Nodes: $TOTAL_NODES) | Strategy: $STRATEGY | Affinity: $AFFINITY"
echo ""

# Setup logs folder
rm -rf "$SCRIPT_DIR/logs"
mkdir -p "$SCRIPT_DIR/logs"
BASE_PORT=$(awk '{print $2}' "$SCRIPT_DIR/config.local.txt")

# Launch slave nodes first (nodes 1..TOTAL_NODES-1)
echo "=== Launching $((TOTAL_NODES - 1)) slave nodes ==="
for i in $(seq 1 $((TOTAL_NODES - 1))); do
    PORT=$((BASE_PORT + i))
    LOG_FILE="Node_${i}_Slave_${PORT}.log"
    open_terminal "NODE $i (Slave)" "./lab05 $N $i local $TOTAL_NODES $STRATEGY $AFFINITY $TEST_FLAG" "$LOG_FILE"
    echo "  Opened terminal for Node $i (Logging to $LOG_FILE)"
done

# Give slaves time to start listening (scale with slave count for slow Linux terminals)
SLAVE_WAIT=$(( 1 + SLAVES / 2 ))
[ $SLAVE_WAIT -gt 15 ] && SLAVE_WAIT=15
sleep $SLAVE_WAIT

# Launch master node (node 0)
echo ""
echo "=== Launching master node ==="
LOG_FILE="Node_0_Master_${BASE_PORT}.log"
open_terminal "NODE 0 (Master)" "./lab05 $N 0 local $TOTAL_NODES $STRATEGY $AFFINITY $TEST_FLAG" "$LOG_FILE"

echo ""
echo "=== All $TOTAL_NODES terminals launched! ==="
