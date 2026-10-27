#!/bin/zsh
# Project Rain - Rain Shield scaling sweep in a standalone 1920x1080 game window (not PIE), then quits.
#   Scripts/sweep.sh rain               # System 1: 0 50k 100k 200k 400k 800k 1.6M drops
#   Scripts/sweep.sh hail               # System 2 on the CPU: 0 500 1k 2k 5k 10k 20k 40k stones
#   Scripts/sweep.sh hailgpu            # System 2 on the GPU (the CPU -> GPU port)
#   COUNTS=0,1000,2000 Scripts/sweep.sh hail    # other levels
# Closes a running editor first (it would share the GPU). Results: Saved/Sweeps/RainShield_<system>_<time>.csv
# (also printed). Each level: 3 s to settle (count checked against the live particle count), 5 s measured.
UE="/Users/Shared/Epic Games/UE_5.7"
PROJ="/Users/guocheng/ProjectRain/ProjectRain.uproject"
MODE=${1:-rain}
case $MODE in
  rain)    ARG="${COUNTS:-0,50000,100000,200000,400000,800000,1600000}"; EXTRA="" ;;
  hail)    ARG="hail:${COUNTS:-0,500,1000,2000,5000,10000,20000,40000}"; EXTRA="" ;;
  hailgpu) ARG="hail:${COUNTS:-0,500,1000,2000,5000,10000,20000,40000}"; EXTRA="-RainHailGPU" ;;
  *) echo "usage: sweep.sh rain|hail|hailgpu"; exit 1 ;;
esac
pkill -9 -f "UnrealEditor.app/Contents/MacOS/UnrealEditor"; sleep 2
"$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PROJ" /Game/RainMagic/Maps/L_RainShield -game -windowed \
  -ResX=1920 -ResY=1080 -ForceRes -RainSweep=$ARG -RainSweepQuit ${=EXTRA} -NoSplash -unattended -log=Sweep.log > /dev/null 2>&1
CSV=$(ls -t /Users/guocheng/ProjectRain/Saved/Sweeps/*.csv | head -1); echo "$CSV"; column -t -s, "$CSV"
