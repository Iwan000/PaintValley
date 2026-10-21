#!/bin/zsh
# Usage: shot.sh <outname> [WxH]  - takes a HighResShot of the active editor viewport and copies it to Saved/Shots/<outname>.png
OUT=${1:-shot}; RES=${2:-1920x804}
DIR="/Users/guocheng/ProjectRain/Saved/Screenshots/MacEditor"
mkdir -p "$DIR" /Users/guocheng/ProjectRain/Saved/Shots
BEFORE=$(ls -t "$DIR" 2>/dev/null | head -1)
UE_TIMEOUT=60 python3 /Users/guocheng/ProjectRain/Scripts/ue_cmd.py "unreal.SystemLibrary.execute_console_command(None, 'HighResShot $RES')" > /dev/null
for i in $(seq 1 60); do
  NEW=$(ls -t "$DIR" 2>/dev/null | head -1)
  if [ -n "$NEW" ] && [ "$NEW" != "$BEFORE" ]; then sleep 1; cp "$DIR/$NEW" "/Users/guocheng/ProjectRain/Saved/Shots/$OUT.png"; echo "/Users/guocheng/ProjectRain/Saved/Shots/$OUT.png"; exit 0; fi
  sleep 1
done
echo "NO_SCREENSHOT"; exit 1
