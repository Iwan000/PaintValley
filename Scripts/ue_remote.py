"""Send Python to a running Unreal Editor (Python remote execution) and print the result.
Usage: python3 ue_remote.py "<python code>"   or   python3 ue_remote.py -f file.py
"""
import sys, time
sys.path.insert(0, "/Users/Shared/Epic Games/UE_5.7/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python")
import remote_execution as rex

def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "-f":
        code = open(sys.argv[2]).read()
        mode = rex.MODE_EXEC_FILE
        code = sys.argv[2]
    else:
        code = sys.argv[1]
        mode = rex.MODE_EXEC_STATEMENT
    timeout = float(__import__("os").environ.get("UE_TIMEOUT", "600"))
    cfg = rex.RemoteExecutionConfig()
    cfg.multicast_bind_address = "127.0.0.1"
    r = rex.RemoteExecution(cfg)
    r.start()
    try:
        t0 = time.time()
        while not r.remote_nodes and time.time() - t0 < 15:
            time.sleep(0.2)
        if not r.remote_nodes:
            print("NO_EDITOR_FOUND"); sys.exit(2)
        r.open_command_connection(r.remote_nodes[0]["node_id"])
        # remote_execution uses a 5s socket timeout by default; give long builds room.
        res = r.run_command(code, unattended=True, exec_mode=mode)
        for o in res.get("output", []):
            print(o.get("output", "").rstrip())
        print("RESULT:", res.get("result"))
        if not res.get("success"):
            print("PYTHON_FAILED"); sys.exit(1)
    finally:
        r.stop()

if __name__ == "__main__":
    main()
