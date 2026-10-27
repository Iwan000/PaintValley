#!/bin/zsh
# Project Rain - records the scripted Rain Shield tour (-RainDemo, ~90 s) from a standalone 1600x900 window to
# Renders/RainShield_demo_raw.mp4 (1920x1080). The window is brought to the front and the screen recorded with
# ffmpeg (avfoundation; needs screen-recording permission), cropped to the game's viewport.
UE="/Users/Shared/Epic Games/UE_5.7"
PROJ="/Users/guocheng/ProjectRain/ProjectRain.uproject"
OUT="/Users/guocheng/ProjectRain/Renders/RainShield_demo_raw.mp4"
WINID="/Users/guocheng/ProjectRain/Saved/Tools/winid"
[ -x "$WINID" ] || { mkdir -p "$(dirname $WINID)"; swiftc -O /Users/guocheng/ProjectRain/Scripts/tools/winid.swift -o "$WINID"; }
pkill -9 -f "UnrealEditor.app/Contents/MacOS/UnrealEditor"; sleep 2
"$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PROJ" /Game/RainMagic/Maps/L_RainShield -game -windowed \
  -ResX=1600 -ResY=900 -ForceRes -RainDemo -RainDemoQuit -NoSplash -unattended > /dev/null 2>&1 &
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
