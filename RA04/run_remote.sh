#!/bin/bash
# RA04 Remote Mode: Binomial Tree Scatter — Dynamic SSH deployment
# Auto-detects OS: macOS (Terminal.app) or Linux (gnome-terminal)
#
# Config format (config.hosts.txt):
#   Line 0: master_ip master_base_port   (PC 0)
#   Line 1: slave_ip slave_base_port     (PC 1)
#   Line 2: slave_ip slave_base_port     (PC 2)
#
# Usage: bash run_remote.sh <matrix_size> <slaves> [strategy] [ssh_user] [ssh_password]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
HOSTS_FILE="$SCRIPT_DIR/config.hosts.txt"
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
N=${1:-1024}
SLAVES=${2:-12}
TOTAL_NODES=$((SLAVES + 1))
STRATEGY=${3:-tree}     # 'linear' or 'tree'
SSH_USER=${4:-$(whoami)}
SSH_PASS=${5:-"useruser"} # Defaults to lab password
OS_NAME=$(uname)

REMOTE_WORKSPACE="~/Documents/jcmagpantay-remote"

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
echo "  RA04 Remote Multiplexing Mode"
echo "  Matrix: ${N}x${N} | Slaves: $SLAVES (Total Nodes: $TOTAL_NODES)"
echo "  User: $SSH_USER | Strategy: $STRATEGY"
echo "  Workspace: $REMOTE_WORKSPACE"
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

# --- Read Hardware config ---
echo "=== Step 2: Preparing Hardware Topology ==="

# Auto-migrate config.remote.txt to config.hosts.txt to preserve physical IPs
if [ ! -f "$HOSTS_FILE" ]; then
    if [ -f "$CONFIG" ]; then
        echo "  Migrating config.remote.txt -> config.hosts.txt (Base HW IPs)"
        mv "$CONFIG" "$HOSTS_FILE"
    else
        echo "ERROR: $HOSTS_FILE not found!"
        exit 1
    fi
fi

HW_IPS=()
HW_PORTS=()
HW_COUNT=0

while IFS=' ' read -r ip port || [ -n "$ip" ]; do
    [ -z "$ip" ] && continue
    HW_IPS+=("$ip")
    HW_PORTS+=("$port")
    HW_COUNT=$((HW_COUNT + 1))
done < "$HOSTS_FILE"

if [ "$HW_COUNT" -lt 2 ]; then
    echo "ERROR: Need at least 2 physical PCs (1 master + 1 slave) in $HOSTS_FILE!"
    exit 1
fi

SLAVE_HW_COUNT=$((HW_COUNT - 1))
echo "  Detected $HW_COUNT Physical Hardware PCs ($SLAVE_HW_COUNT available for slaves)"
echo ""

# --- Generate logical mapping config.remote.txt ---
echo "=== Step 3: Multiplexing Nodes onto Hardware ==="
rm -f "$CONFIG"
ALL_IPS=()
ALL_PORTS=()

# Route Node 0 to PC 0 (Master)
echo "${HW_IPS[0]} ${HW_PORTS[0]}" >> "$CONFIG"
ALL_IPS[0]="${HW_IPS[0]}"
ALL_PORTS[0]="${HW_PORTS[0]}"
echo "  Node 0 -> HW Master (${HW_IPS[0]}:${HW_PORTS[0]})"

# Route Nodes 1 to SLAVES dynamically across Slave PCs
for i in $(seq 1 $SLAVES); do
    # Distribute sequentially using modulo (1-indexed offset)
    PC_INDEX=$(( ((i - 1) % SLAVE_HW_COUNT) + 1 ))
    target_ip="${HW_IPS[$PC_INDEX]}"
    
    # Increment port to avoid binding conflicts when many nodes run on 1 physical PC
    target_port=$((HW_PORTS[$PC_INDEX] + i))
    
    echo "$target_ip $target_port" >> "$CONFIG"
    ALL_IPS[$i]="$target_ip"
    ALL_PORTS[$i]="$target_port"
    
    echo "  Node $i -> PC $PC_INDEX ($target_ip:$target_port)"
done

echo "  Saved mapping to $CONFIG!"
echo ""

# --- Pre-flight: Test SSH to all UNIQUE physical slave PCs ---
echo "=== Step 4: Testing physical SSH connectivity ==="
ALL_OK=true
for i in $(seq 1 $SLAVE_HW_COUNT); do
    IP="${HW_IPS[$i]}"
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

# --- Deploy binary + generated config to hardware ---
echo "=== Step 5: Deploying to physical slave machines ==="
for i in $(seq 1 $SLAVE_HW_COUNT); do
    IP="${HW_IPS[$i]}"
    printf "  Deploying to Hardware PC $i ($SSH_USER@$IP:$REMOTE_WORKSPACE/)... "
    
    # Ensure remote directory exists
    eval "$SSH_PREFIX \"$SSH_USER@$IP\" \"mkdir -p $REMOTE_WORKSPACE\" >/dev/null 2>&1"
    
    # Notice we deploy the NEW config.remote.txt which contains the dynamic multiplex map
    eval "$SCP_PREFIX -q \"$BINARY\" \"$CONFIG\" \"$SSH_USER@$IP:$REMOTE_WORKSPACE/config.remote.txt\" 2>/dev/null"
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

# --- Launch slave processes via SSH ---
echo "=== Step 6: Launching $SLAVES slaves across $SLAVE_HW_COUNT machines ==="
for i in $(seq 1 $SLAVES); do
    IP="${ALL_IPS[$i]}"
    PORT="${ALL_PORTS[$i]}"
    LOG_FILE="Node_${i}_Slave_${IP}_${PORT}.log"

    echo "  Starting Node $i on $IP:$PORT... (Logging remotely to $LOG_FILE)"
    
    # Payload tells the remote PC to create its own logs folder, run lab04, and duplicate the stdout to a remote log file.
    REMOTE_CMD="mkdir -p $REMOTE_WORKSPACE/logs && cd $REMOTE_WORKSPACE && ./lab04 $N $i remote $TOTAL_NODES $STRATEGY | tee $REMOTE_WORKSPACE/logs/$LOG_FILE"
    open_terminal "NODE $i ($IP:$PORT via SSH)" "eval \"$SSH_PREFIX -t $SSH_USER@$IP '$REMOTE_CMD'\"" "$LOG_FILE"
done

echo "  Waiting for slaves to sync..."
sleep 3
echo ""

# --- Launch master locally ---
echo "=== Step 7: Launching master === "
MASTER_IP="${ALL_IPS[0]}"
MASTER_PORT="${ALL_PORTS[0]}"
LOG_FILE="Node_0_Master_${MASTER_IP}_${MASTER_PORT}.log"
open_terminal "NODE 0 (Master, $MASTER_IP:$MASTER_PORT)" "./lab04 $N 0 remote $TOTAL_NODES $STRATEGY" "$LOG_FILE"

echo ""
echo "============================================"
echo "  All $TOTAL_NODES process terminals launched!"
echo "  Strategy: $STRATEGY"
echo "  Master: $MASTER_IP:$MASTER_PORT"
echo "  Slaves: $SLAVES processes routed across $SLAVE_HW_COUNT hardware PCs"
echo "============================================"
