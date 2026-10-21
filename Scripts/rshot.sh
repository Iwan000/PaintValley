#!/bin/zsh
# Usage: rshot.sh <name> [python-prelude]   e.g. rshot.sh frozen "rainlib.beat(10)"
# Runs the prelude in the editor, waits a moment for the sim, then screenshots through CineCam_Hero.
# SETTLE=<s>: first let it rain for s seconds (parked drops fall back in), then run the prelude. The wait
# happens out here: a sleep inside the editor's Python would stall the editor, so nothing would fall.
NAME=$1; PRE=${2:-pass}; WAIT=${WAIT:-3}
OUT="/Users/guocheng/ProjectRain/Saved/Shots/$NAME.png"; rm -f "$OUT"
if [ -n "$SETTLE" ]; then
  UE_TIMEOUT=120 python3 /Users/guocheng/ProjectRain/Scripts/ue_cmd.py "import sys; sys.path.insert(0, '/Users/guocheng/ProjectRain/Scripts'); import rainlib; rainlib.beat(1.0, auto=True)" > /dev/null
  sleep $SETTLE
fi
UE_TIMEOUT=120 python3 /Users/guocheng/ProjectRain/Scripts/ue_cmd.py "import sys, importlib; sys.path.insert(0, '/Users/guocheng/ProjectRain/Scripts'); import rainlib; importlib.reload(rainlib); $PRE" | grep -v "^\s*$" | grep -v "^RESULT: None" | grep -v "^SUCCESS"
sleep $WAIT
UE_TIMEOUT=120 python3 /Users/guocheng/ProjectRain/Scripts/ue_cmd.py "import rainlib; rainlib.shot('$NAME', 1920, 804, '${CAM:-CineCam_Hero}')" > /dev/null
for i in $(seq 1 60); do [ -s "$OUT" ] && { sleep 1; echo "$OUT"; exit 0; }; sleep 1; done
echo "NO_SCREENSHOT"; exit 1
