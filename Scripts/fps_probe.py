"""Wall-clock frame-rate probe for the running editor (works for the level viewport and PIE).
    python3 Scripts/fps_probe.py <label> [seconds]
Reads the engine frame counter twice through the file command queue and divides by wall time.
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CMD = os.path.join(HERE, "ue_cmd.py")


def frame_count():
    out = subprocess.run([sys.executable, CMD, "import time; print('FRAME', unreal.SystemLibrary.get_frame_count(), time.time())"],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.lstrip("﻿").startswith("FRAME"):
            _, f, t = line.split()
            return int(f), float(t)
    raise RuntimeError(out)


def main():
    label = sys.argv[1] if len(sys.argv) > 1 else "probe"
    seconds = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
    f0, t0 = frame_count()
    time.sleep(seconds)
    f1, t1 = frame_count()
    fps = (f1 - f0) / (t1 - t0)
    line = "%s: %.1f fps (%d frames / %.1f s)" % (label, fps, f1 - f0, t1 - t0)
    print(line)
    with open(os.path.join(os.path.dirname(HERE), "Saved", "fps.txt"), "a") as f:
        f.write(line + "\n")


if __name__ == "__main__":
    main()
