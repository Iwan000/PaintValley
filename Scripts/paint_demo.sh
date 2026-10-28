#!/bin/zsh
# Project Rain - records the scripted Paint Valley tour (-PaintDemo, ~88 s) from a standalone 1600x900 window to
# Renders/PaintValley_demo_raw.mp4 (1920x1080). The window is brought to the front and the screen recorded with
# ffmpeg (avfoundation; needs screen-recording permission), cropped to the game's viewport. The game renders at full
# resolution (the desktop default would follow the display: 72.9 % on a Retina Mac).
UE="/Users/Shared/Epic Games/UE_5.7"
PROJ="/Users/guocheng/ProjectRain/ProjectRain.uproject"
OUT=${1:-/Users/guocheng/ProjectRain/Renders/PaintValley_demo_raw.mp4}
WINID="/Users/guocheng/ProjectRain/Saved/Tools/winid"
[ -x "$WINID" ] || { mkdir -p "$(dirname $WINID)"; swiftc -O /Users/guocheng/ProjectRain/Scripts/tools/winid.swift -o "$WINID"; }
pkill -9 -f "UnrealEditor.app/Contents/MacOS/UnrealEditor"; sleep 2
"$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PROJ" /Game/RainMagic/Maps/L_PaintValley -game -windowed \
  -ResX=1600 -ResY=900 -ForceRes -PaintDemo -PaintDemoQuit "-ExecCmds=r.ScreenPercentage.Default.Desktop.Mode 0,r.ScreenPercentage 100" \
  -NoSplash -unattended > /dev/null 2>&1 &
PID=$!
sleep 6
open -a "$UE/Engine/Binaries/Mac/UnrealEditor.app"
sleep 1
read ID X Y W H <<< "$("$WINID" UnrealEditor bounds)"
# Retina: 2 px per point; skip the 28 pt title bar.
CROP="crop=$((W*2)):$(((H-28)*2)):$((X*2)):$(((Y+28)*2))"
ffmpeg -hide_banner -loglevel error -y -f avfoundation -capture_cursor 0 -framerate 30 -i "3:none" -t 92 \
  -vf "$CROP,scale=1920:1080:flags=lanczos,fps=30" -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p "$OUT"
wait $PID
echo "$OUT"
