"""Editor-side helpers for look development (import inside the editor's Python).

    import rainlib; rainlib.beat(10.0); rainlib.shot("frozen")
"""
import os
import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SHOTS = os.path.join(PROJECT, "Saved", "Shots")
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def actor(label):
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == label:
            return a
    return None


def controller():
    return actor("RainMagicController")


def beat(t, auto=False, reset=False):
    """Pin the performance at time t (seconds). auto=True lets it run on from there."""
    c = controller()
    c.set_editor_property("auto_advance", auto)
    if reset:
        c.restart_performance()
    c.set_editor_property("performance_time", float(t))
    return c


def game_view(on=True):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.editor_set_game_view(on)
    les.editor_set_viewport_realtime(True)


def pilot(label="CineCam_Hero"):
    cam = actor(label)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        cam.get_actor_location(), cam.get_actor_rotation())
    return cam


def shot(name, w=1920, h=804, camera="CineCam_Hero"):
    """High-res screenshot through a camera actor -> Saved/Screenshots/... (async, next frame)."""
    game_view(True)
    cam = actor(camera) if camera else None
    os.makedirs(SHOTS, exist_ok=True)
    unreal.AutomationLibrary.take_high_res_screenshot(w, h, os.path.join(SHOTS, name + ".png"), camera=cam)
    return os.path.join(SHOTS, name + ".png")


def cam(label, loc, target, focal=50.0, fstop=2.0, focus=None):
    """Create/move a look-dev cine camera (2.39:1) at loc looking at target."""
    a = actor(label)
    if a is None:
        a = eas.spawn_actor_from_class(unreal.CineCameraActor, unreal.Vector(*loc), unreal.Rotator(0, 0, 0))
        a.set_actor_label(label)
        a.set_folder_path("Cameras/LookDev")
    d = unreal.Vector(target[0] - loc[0], target[1] - loc[1], target[2] - loc[2])
    a.set_actor_location(unreal.Vector(*loc), False, False)
    a.set_actor_rotation(unreal.MathLibrary.make_rot_from_x(d), False)
    c = a.get_cine_camera_component()
    fb = c.get_editor_property("filmback")
    fb.set_editor_property("sensor_width", 24.89)
    fb.set_editor_property("sensor_height", 10.41)
    c.set_editor_property("filmback", fb)
    c.set_editor_property("current_focal_length", focal)
    c.set_editor_property("current_aperture", fstop)
    fs = c.get_editor_property("focus_settings")
    fs.set_editor_property("focus_method", unreal.CameraFocusMethod.MANUAL)
    fs.set_editor_property("manual_focus_distance", focus if focus else d.length())
    c.set_editor_property("focus_settings", fs)
    # Drop LOD needs this lens's pixels-per-radian at the screenshot width (1920).
    import math
    hfov = 2.0 * math.atan(24.89 * 0.5 / focal)
    ctrl = controller()
    if ctrl:
        ctrl.set_editor_property("pixel_scale_override", 960.0 / math.tan(hfov * 0.5))
        ctrl.set_editor_property("view_camera", a)
    # The rain LOD and near fade follow the "view" camera: point the editor viewport here too.
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        unreal.Vector(*loc), unreal.MathLibrary.make_rot_from_x(d))
    return a
