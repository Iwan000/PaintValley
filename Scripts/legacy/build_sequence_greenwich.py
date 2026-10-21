"""[legacy, v6: cameras for the Greenwich stage] Project Rain - builds LS_RainTrick: the camera coverage of the performance, cut to the film's beats.
The controller's timeline runs from BeginPlay at the render's fixed 23.976 fps step, so sequence
time == performance time. Run in the editor with L_RainStage open.
"""
import math
import unreal

ROOT = "/Game/RainMagic"
SEQ_DIR = ROOT + "/Cinematics"
SEQ_NAME = "LS_RainTrick"
FPS = unreal.FrameRate(24000, 1001)
LENGTH = 46.0

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def frame(t):
    return int(round(t * 24000.0 / 1001.0))


def look(src, dst):
    d = unreal.Vector(dst[0] - src[0], dst[1] - src[1], dst[2] - src[2])
    r = unreal.MathLibrary.make_rot_from_x(d)
    return (r.pitch, r.yaw, r.roll)


def camera(label, focal, fstop, focus):
    for a in eas.get_all_level_actors():
        if a.get_actor_label() == label:
            eas.destroy_actor(a)
    cam = eas.spawn_actor_from_class(unreal.CineCameraActor, unreal.Vector(0, 0, 200), unreal.Rotator(0, 0, 0))
    cam.set_actor_label(label)
    cam.set_folder_path("Cameras/Sequence")
    c = cam.get_cine_camera_component()
    fb = c.get_editor_property("filmback")
    fb.set_editor_property("sensor_width", 24.89)
    fb.set_editor_property("sensor_height", 10.41)
    c.set_editor_property("filmback", fb)
    c.set_editor_property("current_focal_length", focal)
    c.set_editor_property("current_aperture", fstop)
    fs = c.get_editor_property("focus_settings")
    fs.set_editor_property("focus_method", unreal.CameraFocusMethod.MANUAL)
    fs.set_editor_property("manual_focus_distance", focus)
    c.set_editor_property("focus_settings", fs)
    return cam


def key_path(seq, cam, keys):
    """keys: list of (time_s, (x, y, z), target_xyz)"""
    binding = seq.add_possessable(cam)
    track = binding.add_track(unreal.MovieScene3DTransformTrack)
    section = track.add_section()
    section.set_range(0, frame(LENGTH))
    ch = section.get_channels_by_type(unreal.MovieSceneScriptingDoubleChannel)
    # channel order: Location XYZ, Rotation X(roll) Y(pitch) Z(yaw), Scale XYZ
    prev_yaw = None
    for t, loc, target in keys:
        pitch, yaw, roll = look(loc, target)
        if prev_yaw is not None:  # keep yaw continuous for orbits
            while yaw - prev_yaw > 180: yaw -= 360
            while yaw - prev_yaw < -180: yaw += 360
        prev_yaw = yaw
        f = unreal.FrameNumber(frame(t))
        for i, v in enumerate((loc[0], loc[1], loc[2], roll, pitch, yaw)):
            ch[i].add_key(f, v)
        for i in (6, 7, 8):
            ch[i].add_key(f, 1.0)
    return binding


def build():
    path = f"{SEQ_DIR}/{SEQ_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)
    seq = asset_tools.create_asset(SEQ_NAME, SEQ_DIR, unreal.LevelSequence, unreal.LevelSequenceFactoryNew())
    seq.set_display_rate(FPS)
    seq.set_playback_start(0)
    seq.set_playback_end(frame(LENGTH))

    atlas = (0, 0, 150)
    hand = (-62, -12, 165)
    shots = []

    # A: talking -> claw -> stop; the same take pulls back and cranes up to reveal the frozen rain.
    a = camera("Cam_A_Stop", 35.0, 2.8, 600.0)
    shots.append((0.0, 9.5, key_path(seq, a, [
        (0.0, (-780, 40, 165), atlas), (5.9, (-560, 30, 162), atlas), (6.3, (-560, 30, 162), atlas),
        (9.5, (-1550, -40, 360), (0, 0, 120))])))
    # B: high wide down the colonnade, flood lamps backlighting the hanging drops.
    b = camera("Cam_B_Wide", 24.0, 5.6, 2000.0)
    shots.append((9.5, 12.5, key_path(seq, b, [
        (9.5, (-1900, 1100, 420), (0, -700, 260)), (12.5, (-1800, 700, 380), (0, -900, 240))])))
    # C: macro-ish close-up of the beads by the hand (shallow focus, ball-lens refractions).
    c = camera("Cam_C_Close", 50.0, 2.0, 175.0)
    shots.append((12.5, 16.0, key_path(seq, c, [
        (12.5, (-200, 90, 158), hand), (16.0, (-170, 20, 150), hand)])))
    # D: "go insane" - orbit around the conducting magician.
    d = camera("Cam_D_Orbit", 28.0, 4.0, 650.0)
    orbit = []
    for i in range(7):
        t = 16.0 + i * 1.5
        ang = math.radians(200 + i * 18)
        orbit.append((t, (650 * math.cos(ang), 650 * math.sin(ang), 210), atlas))
    shots.append((16.0, 25.0, key_path(seq, d, orbit)))
    # E: low angle on Atlas raising his arms, tilting up into the sky as the rain rises.
    e = camera("Cam_E_LowUp", 24.0, 4.0, 400.0)
    shots.append((25.0, 31.0, key_path(seq, e, [
        (25.0, (-380, 40, 40), (0, 0, 170)), (27.5, (-380, 40, 40), (0, 0, 260)), (31.0, (-380, 40, 40), (300, 0, 1500))])))
    # F: wide - the cloud of drops bubbling up out of the frame.
    f = camera("Cam_F_RiseWide", 24.0, 4.0, 1500.0)
    shots.append((31.0, 37.0, key_path(seq, f, [
        (31.0, (-900, 700, 120), (300, -900, 900)), (37.0, (-850, 650, 120), (300, -900, 1300))])))
    # G: the rain resumes - medium wide, then a slow push in.
    g = camera("Cam_G_Resume", 32.0, 2.8, 850.0)
    shots.append((37.0, 46.0, key_path(seq, g, [
        (37.0, (-950, -140, 175), (0, 0, 140)), (46.0, (-760, -90, 170), (0, 0, 145))])))

    cut_track = seq.add_track(unreal.MovieSceneCameraCutTrack)
    for start, end, binding in shots:
        sec = cut_track.add_section()
        sec.set_range(frame(start), frame(end))
        sec.set_camera_binding_id(seq.get_binding_id(binding))

    unreal.EditorAssetLibrary.save_loaded_asset(seq)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    print(f"{path}: {len(shots)} shots, {frame(LENGTH)} frames @ 23.976")


build()
