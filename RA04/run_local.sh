#!/bin/bash
# RA04 Local Mode: Binomial Tree Scatter
# Each node gets its own terminal window
# Auto-detects OS: macOS (Terminal.app) or Linux (gnome-terminal)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- Terminal launcher (OS-agnostic) ---
open_terminal() {
    local title="$1"
    local cmd="$2"
    local log_file="$3"

    local tmp_script
    tmp_script=$(mktemp /tmp/ra04_XXXXXX)
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

echo "=== Compiling lab04 ==="
gcc -o "$SCRIPT_DIR/lab04" "$SCRIPT_DIR/sockets.c" -lm
if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

N=${1:-1024}            # Matrix size
TOTAL_NODES=${2:-9}      # Total participating nodes (1 master + N-1 slaves)
STRATEGY=${3:-tree}      # 'linear' (O(n)) or 'tree' (O(log n))
AFFINITY=${4:-no_affine} # 'affine' or 'no_affine'
OS_NAME=$(uname)

echo "Detected OS: $OS_NAME"
echo "Matrix: ${N}x${N} | Nodes: $TOTAL_NODES | Strategy: $STRATEGY | Affinity: $AFFINITY"
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
    open_terminal "NODE $i (Slave)" "./lab04 $N $i local $TOTAL_NODES $STRATEGY $AFFINITY" "$LOG_FILE"
    echo "  Opened terminal for Node $i (Logging to $LOG_FILE)"
done

# Give slaves time to start listening
sleep 2

# Launch master node (node 0)
echo ""
echo "=== Launching master node ==="
LOG_FILE="Node_0_Master_${BASE_PORT}.log"
open_terminal "NODE 0 (Master)" "./lab04 $N 0 local $TOTAL_NODES $STRATEGY $AFFINITY" "$LOG_FILE"

echo ""
echo "=== All $TOTAL_NODES terminals launched! ==="
