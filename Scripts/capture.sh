#!/bin/zsh
# Project Rain - screenshots of the profiling overlays in a standalone 1920x1080 Rain Shield run.
#   Scripts/capture.sh <name> <-RainSweep value> "<console commands, comma separated>" [extra args...]
#   e.g. Scripts/capture.sh rain200k_statniagara 200000 "stat unit,stat Niagara"
# Holds the count for 30 s (RainSweepMeasure), grabs the game window 22 s in (even under other windows: screencapture -l
# with the id from Scripts/tools/winid.swift) into Docs/Perf/<name>.png, then quits.
UE="/Users/Shared/Epic Games/UE_5.7"
PROJ="/Users/guocheng/ProjectRain/ProjectRain.uproject"
NAME=$1; SWEEP=$2; CMDS=$3; shift 3
WINID="/Users/guocheng/ProjectRain/Saved/Tools/winid"
[ -x "$WINID" ] || { mkdir -p "$(dirname $WINID)"; swiftc -O /Users/guocheng/ProjectRain/Scripts/tools/winid.swift -o "$WINID"; }
pkill -9 -f "UnrealEditor.app/Contents/MacOS/UnrealEditor"; sleep 2
"$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PROJ" /Game/RainMagic/Maps/L_RainShield -game -windowed \
  -ResX=1920 -ResY=1080 -ForceRes -RainSweep=$SWEEP -RainSweepSettle=3 -RainSweepMeasure=30 -RainSweepQuit \
  "-ExecCmds=$CMDS" "$@" -NoSplash -unattended > /dev/null 2>&1 &
PID=$!
sleep 22
screencapture -x -o -l$("$WINID" UnrealEditor) "/Users/guocheng/ProjectRain/Docs/Perf/$NAME.png"
wait $PID
echo "/Users/guocheng/ProjectRain/Docs/Perf/$NAME.png"
