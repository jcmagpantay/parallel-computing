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
# Usage: bash run_remote.sh <n> [strategy] [ssh_user] [ssh_password]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG="$SCRIPT_DIR/config.remote.txt"
BINARY="$SCRIPT_DIR/lab04"

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

# --- Parse arguments ---
N=${1:-12}
STRATEGY=${2:-tree}     # 'linear' or 'tree'
SSH_USER=${3:-$(whoami)}
SSH_PASS=${4:-"useruser"} # Defaults to lab password
OS_NAME=$(uname)

# Check for sshpass utility
if ! command -v sshpass &> /dev/null; then
    echo "WARNING: 'sshpass' is not installed."
    echo "You will have to manually type the password for EVERY window."
    echo "To fix this, install it via: sudo apt-get install sshpass"
    echo ""
    SSH_PREFIX="ssh -o StrictHostKeyChecking=no"
    SCP_PREFIX="scp -o StrictHostKeyChecking=no"
else
    SSH_PREFIX="sshpass -p '$SSH_PASS' ssh -o StrictHostKeyChecking=no"
    SCP_PREFIX="sshpass -p '$SSH_PASS' scp -o StrictHostKeyChecking=no"
fi

echo "============================================"
echo "  RA04 Remote Mode"
echo "  Matrix: ${N}x${N} | User: $SSH_USER"
echo "  Strategy: $STRATEGY"
echo "  Config: $CONFIG"
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
    # We remove BatchMode because we're using passwords now
    eval "$SSH_PREFIX -o ConnectTimeout=5 \"$SSH_USER@$IP\" \"echo OK\" >/dev/null 2>&1"
    if [ $? -ne 0 ]; then
        echo "FAILED (Check password or IP)"
        ALL_OK=false
    else
        echo "OK"
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
    eval "$SCP_PREFIX -q \"$BINARY\" \"$CONFIG\" \"$SSH_USER@$IP:~/\" 2>/dev/null"
    if [ $? -eq 0 ]; then
        echo "OK"
    else
        echo "FAILED"
        exit 1
    fi
done
echo "Deployment complete."
echo ""

# Setup logs folder locally
rm -rf "$SCRIPT_DIR/logs"
mkdir -p "$SCRIPT_DIR/logs"

# --- Launch slave nodes via SSH ---
echo "=== Step 5: Launching slaves ==="
for i in $(seq 1 $((NODE_COUNT - 1))); do
    IP="${ALL_IPS[$i]}"
    PORT="${ALL_PORTS[$i]}"
    # Visual mode: We don't use 'open_terminal' locally. We force the remote PC to open a window.
    # Note: Local logging will be skipped for slaves since the buffer lives remotely.
    echo "  Spawning GUI window for Node $i on $IP:$PORT... (Check remote monitor!)"
    SSH_CMD="export DISPLAY=:0; export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/\$(id -u); gnome-terminal --title=\"NODE $i\" -- bash -c \"~/lab04 $N $i remote $NODE_COUNT $STRATEGY; echo \\\"\\\"; read -p \\\"Press Enter to close...\\\"\""
    eval "$SSH_PREFIX $SSH_USER@$IP '$SSH_CMD'" &
done

echo "  Waiting for slaves to start..."
sleep 3
echo ""

# --- Launch master locally ---
echo "=== Step 6: Launching master ==="
LOG_FILE="Node_0_Master_${MASTER_IP}_${MASTER_PORT}.log"
open_terminal "NODE 0 (Master, $MASTER_IP:$MASTER_PORT)" "./lab04 $N 0 remote $NODE_COUNT $STRATEGY" "$LOG_FILE"

echo ""
echo "============================================"
echo "  All $NODE_COUNT terminals launched!"
echo "  Strategy: $STRATEGY"
echo "  Master: $MASTER_IP:$MASTER_PORT"
echo "  Slaves: $SLAVE_COUNT instances"
echo "============================================"
