"""Project Rain - helpers to drive the Paint Valley game in PIE from the file command queue (screenshots, tests).
Waiting happens in the shell (a sleep in editor Python stalls the editor).
    python3 Scripts/ue_cmd.py "import sys; sys.path.insert(0, '/Users/guocheng/ProjectRain/Scripts'); import paintlib; paintlib.play()"
"""
import os

import unreal

SHOTS = "/Users/guocheng/ProjectRain/Saved/Shots"


def play():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()


def stop():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()


def world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def game():
    w = world()
    if not w:
        raise RuntimeError("PIE is not running")
    return unreal.GameplayStatics.get_actor_of_class(w, unreal.RainPaintGame)


def console(cmd):
    unreal.SystemLibrary.execute_console_command(world(), cmd)


def status():
    print("STATUS", game().describe())


def shot(name, w=1920, h=1080):
    """Screenshot of the PIE viewport -> Saved/Shots/<name>.png (written a frame later)."""
    os.makedirs(SHOTS, exist_ok=True)
    console('HighResShot %dx%d filename="%s"' % (w, h, os.path.join(SHOTS, name + ".png")))
