#!/bin/zsh
# Project Rain - Paint Valley scaling sweep: one standalone 1920x1080 game run per level (-PaintMeasure: the round
# starts, settles, then 5 s of stat unit raw times are averaged and appended to Saved/Sweeps/PaintMeasure.txt).
#   Scripts/paint_sweep.sh rain          # System 1 (GPU drops) on Blocks: 25k 50k 100k 200k 400k
#   Scripts/paint_sweep.sh balls         # System 2 (CPU paint balls) on Nook: 500 1k 2k 4k 8k
#   Scripts/paint_sweep.sh pause         # System 2 at 2k with the idle rain (800k drops) paused (1) or simulating (0)
#   COUNTS=1000,2000 Scripts/paint_sweep.sh balls
# Closes a running editor first (it would share the GPU).
UE="/Users/Shared/Epic Games/UE_5.7"
PROJ="/Users/guocheng/ProjectRain/ProjectRain.uproject"
OUT=/Users/guocheng/ProjectRain/Saved/Sweeps/PaintMeasure.txt
MODE=${1:-rain}
pkill -9 -f "UnrealEditor.app/Contents/MacOS/UnrealEditor"; sleep 2
run() {
  echo "# $MODE $1 $(date +%H:%M:%S)" >> $OUT
  "$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$PROJ" /Game/RainMagic/Maps/L_PaintValley -game -windowed \
    -ResX=1920 -ResY=1080 -ForceRes -PaintMeasure ${=2} -ExecCmds="$3" -NoSplash -unattended -log=PaintSweep.log > /dev/null 2>&1
  tail -1 $OUT
}
case $MODE in
  rain)  for N in ${(s:,:)${COUNTS:-25000,50000,100000,200000,400000}}; do run "drops=$N" "-PaintLevel=0 -PaintMeasureSettle=6" "rain.paint.Count $N"; done ;;
  balls) for N in ${(s:,:)${COUNTS:-500,1000,2000,4000,8000}}; do run "balls=$N" "-PaintLevel=3 -PaintBalls -PaintMeasureSettle=8" "rain.paint.Balls $N,rain.paint.BallWait 8"; done ;;
  pause) for P in 1 0; do run "pause=$P" "-PaintLevel=3 -PaintBalls -PaintMeasureSettle=8" "rain.paint.Count 800000,rain.paint.Balls 2000,rain.paint.BallWait 8,rain.paint.PauseIdle $P"; done ;;
  *) echo "usage: paint_sweep.sh rain|balls|pause"; exit 1 ;;
esac
