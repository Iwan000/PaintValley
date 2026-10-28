#!/bin/zsh
# Project Rain - screenshots of the profiling overlays in a standalone 1920x1080 Paint Valley run.
#   Scripts/paint_capture.sh <name> "<launch args>" "<console commands, comma separated>" [extra args...]
#   e.g. Scripts/paint_capture.sh paint_rain100k_stat_niagara "-PaintLevel=0" "stat Niagara"
# (-PaintMeasure already turns stat unit on; passing it again turns it off.)
# The round starts at once (-PaintMeasure, settling 30 s); the game window is grabbed 22 s in (even under other
# windows: screencapture -l with the id from Scripts/tools/winid.swift) into Docs/Perf/<name>.png, then it quits.
UE="/Users/Shared/Epic Games/UE_5.7"
PROJ="/Users/guocheng/ProjectRain/ProjectRain.uproject"
NAME=$1; ARGS=$2; CMDS=$3; shift 3
WINID="/Users/guocheng/ProjectRain/Saved/Tools/winid"
[ -x "$WINID" ] || { mkdir -p "$(dirname $WINID)"; swiftc -O /Users/guocheng/ProjectRain/Scripts/tools/winid.swift -o "$WINID"; }
pkill -9 -f "UnrealEditor.app/Contents/MacOS/UnrealEditor"; sleep 2
# Its measurement line (taken with the overlays on) is marked, and Scripts/paint_perf.py skips it.
echo "# capture $NAME $(date +%H:%M:%S)" >> /Users/guocheng/ProjectRain/Saved/Sweeps/PaintMeasure.txt
"$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PROJ" /Game/RainMagic/Maps/L_PaintValley -game -windowed \
  -ResX=1920 -ResY=1080 -ForceRes -PaintMeasure -PaintMeasureSettle=30 ${=ARGS} "-ExecCmds=$CMDS" "$@" \
  -abslog=/Users/guocheng/ProjectRain/Saved/Logs/Capture_$NAME.log -NoSplash -unattended > /dev/null 2>&1 &
PID=$!
sleep 22
screencapture -x -o -l$("$WINID" UnrealEditor) "/Users/guocheng/ProjectRain/Docs/Perf/$NAME.png"
wait $PID
echo "/Users/guocheng/ProjectRain/Docs/Perf/$NAME.png"
