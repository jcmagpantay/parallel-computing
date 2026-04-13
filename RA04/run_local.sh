#!/bin/bash
# RA04 Local Mode: Binomial Tree Scatter
# Each node gets its own terminal window
# Auto-detects OS: macOS (Terminal.app) or Linux (gnome-terminal)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- Terminal launcher (OS-agnostic) ---
open_terminal() {
    local title="$1"
    local cmd="$2"

    local tmp_script
    tmp_script=$(mktemp /tmp/ra04_XXXXXX)
    cat > "$tmp_script" << TMPEOF
#!/bin/bash
echo "=== $title ==="
cd "$SCRIPT_DIR"
$cmd
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

N=1024             # Matrix size (should be divisible by TOTAL_NODES)
TOTAL_NODES=9    # Total participating nodes (1 master + 3 slaves)
OS_NAME=$(uname)

echo "Detected OS: $OS_NAME"
echo "Matrix: ${N}x${N} | Nodes: $TOTAL_NODES"
echo ""

# Launch slave nodes first (nodes 1..TOTAL_NODES-1)
echo "=== Launching $((TOTAL_NODES - 1)) slave nodes ==="
for i in $(seq 1 $((TOTAL_NODES - 1))); do
    open_terminal "NODE $i (Slave)" "./lab04 $N $i local $TOTAL_NODES"
    echo "  Opened terminal for Node $i"
done

# Give slaves time to start listening
sleep 2

# Launch master node (node 0)
echo ""
echo "=== Launching master node ==="
open_terminal "NODE 0 (Master)" "./lab04 $N 0 local $TOTAL_NODES"

echo ""
echo "=== All $TOTAL_NODES terminals launched! ==="
