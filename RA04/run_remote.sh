#!/bin/bash
# RA04 Remote Mode: Fully dynamic — reads config.remote.txt, auto-deploys, auto-launches
# Auto-detects OS: macOS (Terminal.app) or Linux (gnome-terminal)
#
# Just edit config.remote.txt with your IPs and run this script!
# Format:
#   Line 1: master_ip
#   Line 2+: slave_ip port
#
# Usage: bash run_remote.sh <n> [ssh_user] [ssh_key]
#   n        = matrix size (default: 6)
#   ssh_user = SSH username on remote machines (default: current user)
#   ssh_key  = path to SSH private key (default: ~/.ssh/id_ed25519_local)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG="$SCRIPT_DIR/config.remote.txt"
BINARY="$SCRIPT_DIR/lab04"

# --- Detect OS and set terminal launcher ---
open_terminal() {
    local title="$1"
    local cmd="$2"

    # Write command to a temp script to avoid quoting issues with paths containing spaces
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
        # macOS: use osascript + Terminal.app
        osascript -e "tell application \"Terminal\"
            activate
            do script \"bash '$tmp_script'\"
        end tell"
    else
        # Linux: use gnome-terminal
        gnome-terminal --title="$title" -- bash "$tmp_script"
    fi
}

# --- Parse arguments ---
N=${1:-6}
SSH_USER=${2:-$(whoami)}
SSH_KEY=${3:-"$HOME/.ssh/id_ed25519_local"}
OS_NAME=$(uname)

echo "============================================"
echo "  RA04 Remote Mode (Dynamic)"
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

# Line 1 = master IP
MASTER_IP=$(sed -n '1p' "$CONFIG" | tr -d '[:space:]')
echo "  Master IP: $MASTER_IP"

# Lines 2+ = slave entries (ip port)
SLAVE_IPS=()
SLAVE_PORTS=()
SLAVE_COUNT=0

while IFS=' ' read -r ip port || [ -n "$ip" ]; do
    # Skip empty lines
    [ -z "$ip" ] && continue
    SLAVE_IPS+=("$ip")
    SLAVE_PORTS+=("$port")
    SLAVE_COUNT=$((SLAVE_COUNT + 1))
    echo "  Slave $SLAVE_COUNT: $ip:$port"
done < <(tail -n +2 "$CONFIG")

if [ "$SLAVE_COUNT" -eq 0 ]; then
    echo "ERROR: No slave entries found in config.remote.txt!"
    exit 1
fi
echo "  Total slaves: $SLAVE_COUNT"
echo ""

# --- Pre-flight: Test SSH to all slaves ---
echo "=== Step 3: Testing SSH connectivity ==="
ALL_OK=true
for i in $(seq 0 $((SLAVE_COUNT - 1))); do
    IP="${SLAVE_IPS[$i]}"
    printf "  Testing SSH to $IP... "
    ssh -i "$SSH_KEY" -o BatchMode=yes -o ConnectTimeout=5 "$SSH_USER@$IP" "echo OK" 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "FAILED"
        ALL_OK=false
    fi
done

if [ "$ALL_OK" = false ]; then
    echo ""
    echo "ERROR: Some SSH connections failed. Fix connectivity and try again."
    exit 1
fi
echo "All SSH connections OK!"
echo ""

# --- Deploy binary + config to all slaves ---
echo "=== Step 4: Deploying binary to slaves ==="
for i in $(seq 0 $((SLAVE_COUNT - 1))); do
    IP="${SLAVE_IPS[$i]}"
    printf "  Copying to $SSH_USER@$IP:~/ ... "
    scp -i "$SSH_KEY" -o BatchMode=yes -q "$BINARY" "$CONFIG" "$SSH_USER@$IP:~/" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo "OK"
    else
        echo "FAILED"
        echo "ERROR: Could not copy files to $IP"
        exit 1
    fi
done
echo "Deployment complete."
echo ""

# --- Launch slaves via SSH (each in its own terminal window) ---
echo "=== Step 5: Launching slaves ==="
for i in $(seq 0 $((SLAVE_COUNT - 1))); do
    IP="${SLAVE_IPS[$i]}"
    PORT="${SLAVE_PORTS[$i]}"
    SLAVE_NUM=$((i + 1))

    echo "  Starting Slave $SLAVE_NUM on $IP:$PORT..."
    open_terminal "SLAVE $SLAVE_NUM ($IP:$PORT via SSH)" "ssh -t -i $SSH_KEY $SSH_USER@$IP '~/lab04 $N $PORT 1 remote'"
done

# Give slaves time to start listening
echo "  Waiting for slaves to start..."
sleep 3
echo ""

# --- Launch master ---
echo "=== Step 6: Launching master ==="
MASTER_PORT=8000

open_terminal "MASTER ($MASTER_IP:$MASTER_PORT)" "cd \"$SCRIPT_DIR\" && ./lab04 $N $MASTER_PORT 0 remote $SLAVE_COUNT"

echo ""
echo "============================================"
echo "  All terminals launched!"
echo "  Master: $MASTER_IP:$MASTER_PORT"
echo "  Slaves: $SLAVE_COUNT instances"
echo "============================================"
