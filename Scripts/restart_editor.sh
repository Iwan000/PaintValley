#!/bin/zsh
# Rebuild the C++ modules (optional: pass "nobuild") and (re)start the editor on L_RainStage; waits until ready.
UE="/Users/Shared/Epic Games/UE_5.7"
PROJ="/Users/guocheng/ProjectRain/ProjectRain.uproject"
LOG="$HOME/Library/Logs/Unreal Engine/ProjectRainEditor/RainEditor.log"
pkill -9 -f "UnrealEditor.app/Contents/MacOS/UnrealEditor"; sleep 2
if [ "$1" != "nobuild" ]; then
  "$UE/Engine/Build/BatchFiles/Mac/Build.sh" ProjectRainEditor Mac Development -Project="$PROJ" -WaitMutex 2>&1 | grep -E "error|Result" | head -20
fi
rm -f "$LOG"
nohup "$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PROJ" /Game/RainMagic/Maps/L_RainStage -NoSplash -unattended -log=RainEditor.log > /dev/null 2>&1 &
sleep 5
until grep -q "Total Editor Startup Time" "$LOG" 2>/dev/null || ! pgrep -f "UnrealEditor.app/Contents/MacOS/UnrealEditor" >/dev/null; do sleep 3; done
grep "Total Editor Startup Time\|Critical error" "$LOG" | cut -c1-160
df -h /System/Volumes/Data | tail -1
