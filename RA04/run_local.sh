#!/bin/bash
# RA04 Local Mode: Each slave and master gets its own terminal window
# Auto-detects OS: macOS (Terminal.app) or Linux (gnome-terminal)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

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
cd "$SCRIPT_DIR"
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

echo "=== Compiling lab04 ==="
gcc -o "$SCRIPT_DIR/lab04" "$SCRIPT_DIR/sockets.c" -lm
if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

N=50       # Matrix size
BASE=8000  # Base port from config.local.txt
T=10       # Number of slaves
OS_NAME=$(uname)

echo "Detected OS: $OS_NAME"
echo ""
echo "=== Launching $T slave terminals ==="

# Open each slave in its own terminal window
for i in $(seq 1 $T); do
    PORT=$((BASE + i))
    open_terminal "SLAVE $i (Port $PORT)" "./lab04 $N $PORT 1 local"
    echo "  Opened terminal for Slave $i (port $PORT)"
done

# Give slaves time to start listening
sleep 2

echo ""
echo "=== Launching Master terminal ==="

# Open the master in its own terminal window
open_terminal "MASTER (Port $BASE)" "./lab04 $N $BASE 0 local $T"

echo ""
echo "=== All terminals launched! ==="
