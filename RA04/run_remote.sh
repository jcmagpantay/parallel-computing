#!/bin/bash
# RA04 Remote Mode: Binomial Tree Scatter — Dynamic SSH deployment
# Auto-detects OS: macOS (Terminal.app) or Linux (gnome-terminal)
#
# Config format (config.remote.txt):
#   Line 0: master_ip master_port   (node 0)
#   Line 1: slave_ip slave_port     (node 1)
#   Line 2: slave_ip slave_port     (node 2)
#   ...
#
# Usage: bash run_remote.sh <n> [ssh_user] [ssh_key]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG="$SCRIPT_DIR/config.remote.txt"
BINARY="$SCRIPT_DIR/lab04"

# --- Terminal launcher (OS-agnostic) ---
open_terminal() {
    local title="$1"
    local cmd="$2"

    local tmp_script
    tmp_script=$(mktemp /tmp/ra04_XXXXXX)
    cat > "$tmp_script" << TMPEOF
#!/bin/bash
echo "=== $title ==="
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

# --- Parse arguments ---
N=${1:-12}
SSH_USER=${2:-$(whoami)}
SSH_KEY=${3:-"$HOME/.ssh/id_ed25519_local"}
OS_NAME=$(uname)

echo "============================================"
echo "  RA04 Remote Mode (Binomial Tree Scatter)"
echo "  Matrix: ${N}x${N} | User: $SSH_USER"
echo "  Config: $CONFIG"
echo "  SSH Key: $SSH_KEY"
echo "  OS: $OS_NAME"
echo "============================================"
echo ""

# --- Compile ---
echo "=== Step 1: Compiling lab04 ==="
gcc -o "$BINARY" "$SCRIPT_DIR/sockets.c" -lm
if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi
echo "Compiled successfully."
echo ""

# --- Read config.remote.txt ---
echo "=== Step 2: Reading config.remote.txt ==="

if [ ! -f "$CONFIG" ]; then
    echo "ERROR: $CONFIG not found!"
    exit 1
fi

ALL_IPS=()
ALL_PORTS=()
NODE_COUNT=0

while IFS=' ' read -r ip port || [ -n "$ip" ]; do
    [ -z "$ip" ] && continue
    ALL_IPS+=("$ip")
    ALL_PORTS+=("$port")
    echo "  Node $NODE_COUNT: $ip:$port"
    NODE_COUNT=$((NODE_COUNT + 1))
done < "$CONFIG"

if [ "$NODE_COUNT" -lt 2 ]; then
    echo "ERROR: Need at least 2 nodes (1 master + 1 slave) in config!"
    exit 1
fi

MASTER_IP="${ALL_IPS[0]}"
MASTER_PORT="${ALL_PORTS[0]}"
SLAVE_COUNT=$((NODE_COUNT - 1))

echo "  Master: $MASTER_IP:$MASTER_PORT"
echo "  Total nodes: $NODE_COUNT ($SLAVE_COUNT slaves)"
echo ""

# --- Pre-flight: Test SSH to all slaves ---
echo "=== Step 3: Testing SSH connectivity ==="
ALL_OK=true
for i in $(seq 1 $((NODE_COUNT - 1))); do
    IP="${ALL_IPS[$i]}"
    printf "  Testing SSH to $IP (Node $i)... "
    ssh -i "$SSH_KEY" -o BatchMode=yes -o ConnectTimeout=5 "$SSH_USER@$IP" "echo OK" 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "FAILED"
        ALL_OK=false
    fi
done

if [ "$ALL_OK" = false ]; then
    echo ""
    echo "ERROR: Some SSH connections failed."
    exit 1
fi
echo "All SSH connections OK!"
echo ""

# --- Deploy binary + config to all slaves ---
echo "=== Step 4: Deploying to slaves ==="
for i in $(seq 1 $((NODE_COUNT - 1))); do
    IP="${ALL_IPS[$i]}"
    printf "  Copying to $SSH_USER@$IP:~/ (Node $i)... "
    scp -i "$SSH_KEY" -o BatchMode=yes -q "$BINARY" "$CONFIG" "$SSH_USER@$IP:~/" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "OK"
    else
        echo "FAILED"
        exit 1
    fi
done
echo "Deployment complete."
echo ""

# --- Launch slave nodes via SSH ---
echo "=== Step 5: Launching slaves ==="
for i in $(seq 1 $((NODE_COUNT - 1))); do
    IP="${ALL_IPS[$i]}"
    PORT="${ALL_PORTS[$i]}"

    echo "  Starting Node $i on $IP:$PORT..."
    open_terminal "NODE $i ($IP:$PORT via SSH)" "ssh -t -i $SSH_KEY $SSH_USER@$IP '~/lab04 $N $i remote $NODE_COUNT'"
done

echo "  Waiting for slaves to start..."
sleep 3
echo ""

# --- Launch master locally ---
echo "=== Step 6: Launching master ==="
open_terminal "NODE 0 (Master, $MASTER_IP:$MASTER_PORT)" "cd \"$SCRIPT_DIR\" && ./lab04 $N 0 remote $NODE_COUNT"

echo ""
echo "============================================"
echo "  All $NODE_COUNT terminals launched!"
echo "  Strategy: Binomial Tree Scatter O(log n)"
echo "  Master: $MASTER_IP:$MASTER_PORT"
echo "  Slaves: $SLAVE_COUNT instances"
echo "============================================"
