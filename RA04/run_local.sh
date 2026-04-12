#!/bin/bash
# RA04 Local Mode: Each slave and master gets its own Terminal.app window
# Uses macOS 'osascript' (AppleScript) — equivalent to gnome-terminal on Linux

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== Compiling lab04 ==="
gcc -o "$SCRIPT_DIR/lab04" "$SCRIPT_DIR/sockets.c" -lm
if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

N=50       # Matrix size (6x6)
BASE=8000 # Base port from config.local.txt
T=10       # Number of slaves

echo "=== Launching $T slave terminals ==="

# Open each slave in its own Terminal.app window
for i in $(seq 1 $T); do
    PORT=$((BASE + i))
    osascript -e "
        tell application \"Terminal\"
            activate
            do script \"echo '=== SLAVE $i (Port $PORT) ===' && cd \\\"$SCRIPT_DIR\\\" && ./lab04 $N $PORT 1 local; echo ''; echo 'Press Enter to close...'; read\"
        end tell
    "
    echo "  Opened Terminal for Slave $i (port $PORT)"
done

# Give slaves time to start listening
sleep 2

echo "=== Launching Master terminal ==="

# Open the master in its own Terminal.app window
osascript -e "
    tell application \"Terminal\"
        activate
        do script \"echo '=== MASTER (Port $BASE) ===' && cd \\\"$SCRIPT_DIR\\\" && ./lab04 $N $BASE 0 local $T; echo ''; echo 'Press Enter to close...'; read\"
    end tell
"

echo ""
echo "=== All terminals launched! Check Terminal.app windows. ==="
