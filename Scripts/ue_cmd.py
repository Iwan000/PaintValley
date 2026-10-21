"""Run Python inside the live Unreal Editor through the ProjectRainEditor file command queue.
Usage:  python3 ue_cmd.py "<python code>"      or      python3 ue_cmd.py -f script.py
Prints the editor's captured log output and result. Exit code 1 if the Python failed.
"""
import os
import sys
import time
import uuid

QUEUE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Saved", "RainCmd")


def main():
    os.makedirs(QUEUE, exist_ok=True)
    if len(sys.argv) >= 3 and sys.argv[1] == "-f":
        code = open(sys.argv[2]).read()
    else:
        code = "import unreal\n" + sys.argv[1]
    name = time.strftime("%Y%m%d_%H%M%S_") + uuid.uuid4().hex[:6]
    tmp = os.path.join(QUEUE, name + ".tmp")
    with open(tmp, "w") as f:
        f.write(code)
    os.rename(tmp, os.path.join(QUEUE, name + ".py"))
    out = os.path.join(QUEUE, name + ".out")
    timeout = float(os.environ.get("UE_TIMEOUT", "1800"))
    t0 = time.time()
    while not os.path.exists(out):
        if time.time() - t0 > timeout:
            print("TIMEOUT waiting for editor")
            sys.exit(2)
        time.sleep(0.3)
    time.sleep(0.2)
    text = open(out, encoding="utf-8", errors="replace").read()
    print(text)
    sys.exit(0 if text.rstrip().endswith("SUCCESS") else 1)


if __name__ == "__main__":
    main()
