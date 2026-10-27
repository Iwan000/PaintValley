#!/bin/zsh
# Project Rain - Paint Valley screenshot: starts PIE (editor already on L_PaintValley), waits, takes a 1920x1080
# HighResShot to Saved/Shots/<name>.png, stops PIE. Optional: YAW (view yaw, default 45), HEIGHT (view height, cm).
cd /Users/guocheng/ProjectRain
NAME=${1:-paint}
PY="import sys; sys.path.insert(0, '/Users/guocheng/ProjectRain/Scripts'); import importlib, paintlib; importlib.reload(paintlib)"
python3 Scripts/ue_cmd.py "$PY; paintlib.play()" > /dev/null
sleep 6
python3 Scripts/ue_cmd.py "$PY; paintlib.console('r.ScreenPercentage 100'); paintlib.game().set_view(${YAW:-45}, ${HEIGHT:-0}); paintlib.status()" | grep STATUS
sleep 2
rm -f Saved/Shots/$NAME.png
python3 Scripts/ue_cmd.py "$PY; paintlib.shot('$NAME')" > /dev/null
for i in {1..20}; do [ -f Saved/Shots/$NAME.png ] && break; sleep 1; done
sleep 1
python3 Scripts/ue_cmd.py "$PY; paintlib.stop()" > /dev/null
ls Saved/Shots/$NAME.png
