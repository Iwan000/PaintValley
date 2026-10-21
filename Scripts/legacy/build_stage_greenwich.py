"""[legacy, v6] Project Rain - builds /Game/RainMagic/Maps/L_RainStage, a night stage inspired by the Old Royal
Naval College (Greenwich) where Atlas performs the rain trick in Now You See Me 2.

Run inside the editor (Python), e.g. via Scripts/ue_remote.py -f Scripts/build_stage.py.
Assumes URainBuilderLibrary.build_rain_assets() and build_stage_materials() have been run.
"""
import math
import random
import unreal

ROOT = "/Game/RainMagic"
MAP_PATH = ROOT + "/Maps/L_RainStage"
MI_PATH = ROOT + "/Materials/Instances"

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary

CUBE = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
CYL = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")
SPHERE = unreal.load_asset("/Engine/BasicShapes/Sphere.Sphere")
CONE = unreal.load_asset("/Engine/BasicShapes/Cone.Cone")
PLANE = unreal.load_asset("/Engine/BasicShapes/Plane.Plane")

random.seed(2016)


# ----------------------------------------------------------------------------- helpers
def R(rot):
    """Rotation from (pitch, yaw, roll). NB: unreal.Rotator's positional order is (roll, pitch, yaw)."""
    if isinstance(rot, unreal.Rotator):
        return rot
    return unreal.Rotator(pitch=rot[0], yaw=rot[1], roll=rot[2])


def mi(name, base=(0.2, 0.2, 0.2), rough=0.5, metal=0.0, emissive=(0, 0, 0)):
    path = f"{MI_PATH}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        inst = unreal.load_asset(path)
    else:
        inst = asset_tools.create_asset(name, MI_PATH, unreal.MaterialInstanceConstant,
                                        unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(inst, unreal.load_asset(ROOT + "/Materials/M_RainBasic"))
    mel.set_material_instance_vector_parameter_value(inst, "BaseColor", unreal.LinearColor(*base, 1))
    mel.set_material_instance_scalar_parameter_value(inst, "Roughness", rough)
    mel.set_material_instance_scalar_parameter_value(inst, "Metallic", metal)
    mel.set_material_instance_vector_parameter_value(inst, "Emissive", unreal.LinearColor(*emissive, 1))
    mel.update_material_instance(inst)
    unreal.EditorAssetLibrary.save_loaded_asset(inst)
    return inst


def mesh(m, loc, scale=(1, 1, 1), rot=(0, 0, 0), mat=None, label=None, folder="Stage", shadows=True):
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*loc), R(rot))
    c = a.static_mesh_component
    c.set_static_mesh(m)
    a.set_actor_scale3d(unreal.Vector(*scale))
    if mat:
        c.set_material(0, mat)
    c.set_editor_property("cast_shadow", shadows)
    if label:
        a.set_actor_label(label)
    a.set_folder_path(folder)
    return a


def spot(loc, rot, intensity, color, outer=30.0, inner=10.0, radius=6000.0, shadows=False,
         vol=1.0, source=5.0, label=None, folder="Lights"):
    a = eas.spawn_actor_from_class(unreal.SpotLight, unreal.Vector(*loc), R(rot))
    c = a.spot_light_component
    c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    c.set_editor_property("intensity", intensity)
    c.set_editor_property("light_color", unreal.Color(*color, 255))
    c.set_editor_property("outer_cone_angle", outer)
    c.set_editor_property("inner_cone_angle", inner)
    c.set_editor_property("attenuation_radius", radius)
    c.set_editor_property("cast_shadows", shadows)
    c.set_editor_property("volumetric_scattering_intensity", vol)
    c.set_editor_property("source_radius", source)
    c.set_mobility(unreal.ComponentMobility.MOVABLE)
    if label:
        a.set_actor_label(label)
    a.set_folder_path(folder)
    return a


def point(loc, intensity, color, radius=1500.0, vol=1.0, source=5.0, label=None, folder="Lights", shadows=False):
    a = eas.spawn_actor_from_class(unreal.PointLight, unreal.Vector(*loc), unreal.Rotator(0, 0, 0))
    c = a.point_light_component
    c.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
    c.set_editor_property("intensity", intensity)
    c.set_editor_property("light_color", unreal.Color(*color, 255))
    c.set_editor_property("attenuation_radius", radius)
    c.set_editor_property("cast_shadows", shadows)
    c.set_editor_property("volumetric_scattering_intensity", vol)
    c.set_editor_property("source_radius", source)
    c.set_mobility(unreal.ComponentMobility.MOVABLE)
    if label:
        a.set_actor_label(label)
    a.set_folder_path(folder)
    return a


def look_rot(src, dst):
    d = unreal.Vector(dst[0] - src[0], dst[1] - src[1], dst[2] - src[2])
    return unreal.MathLibrary.make_rot_from_x(d)


# ----------------------------------------------------------------------------- level
def open_fresh_level():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        les.load_level(MAP_PATH)
        for a in eas.get_all_level_actors():
            if isinstance(a, (unreal.WorldSettings,)) or a.get_class().get_name() in ("Brush", "WorldSettings"):
                continue
            eas.destroy_actor(a)
    else:
        les.new_level(MAP_PATH, False)


def build():
    open_fresh_level()

    stone_dark = mi("MI_StoneBuilding", (0.36, 0.31, 0.25), 0.55)
    stone_col = mi("MI_StoneColumn", (0.55, 0.49, 0.40), 0.45)
    lawn = mi("MI_LawnWet", (0.018, 0.03, 0.012), 0.42)
    coat = mi("MI_Coat", (0.012, 0.014, 0.02), 0.22)
    skin = mi("MI_Skin", (0.45, 0.3, 0.24), 0.45)
    cloth = [mi(f"MI_Cloth{i}", c, 0.6) for i, c in enumerate(
        [(0.02, 0.02, 0.025), (0.05, 0.04, 0.035), (0.03, 0.035, 0.05), (0.06, 0.06, 0.06), (0.025, 0.02, 0.02)])]
    umb_colors = {
        "Red": (0.55, 0.02, 0.02), "White": (0.75, 0.75, 0.73), "Blue": (0.02, 0.06, 0.35),
        "Teal": (0.0, 0.25, 0.22), "Black": (0.015, 0.015, 0.017), "Green": (0.03, 0.3, 0.06),
    }
    umbrellas = {k: mi(f"MI_Umbrella{k}", v, 0.18) for k, v in umb_colors.items()}
    umb_weights = ["Black"] * 5 + ["Red"] * 2 + ["White"] * 2 + ["Blue"] * 2 + ["Teal"] + ["Green"]
    lamp = mi("MI_LampHot", (1, 1, 1), 0.5, emissive=(60.0, 62.0, 66.0))
    led = mi("MI_LedBlue", (0.0, 0.0, 0.0), 0.3, emissive=(0.4, 3.0, 12.0))
    facade = mi("MI_FacadeLit", (0.62, 0.58, 0.5), 0.6, emissive=(0.05, 0.045, 0.035))
    bus_red = mi("MI_BusRed", (0.45, 0.01, 0.01), 0.25)
    bus_glass = mi("MI_BusWindow", (0.0, 0.0, 0.0), 0.05, emissive=(1.2, 1.4, 1.5))
    ground_mat = unreal.load_asset(ROOT + "/Materials/M_WetGround")

    # ---- ground: wet stone everywhere, wet lawns either side of the processional path
    g = mesh(PLANE, (5000, 0, 0), (260, 120, 1), mat=ground_mat, label="Ground_WetStone")
    for side in (-1, 1):
        mesh(CUBE, (2500, side * 1500, -1), (110, 22, 0.04), mat=lawn, label=f"Lawn_{side}")

    # ---- colonnades either side (Wren's King William / Queen Mary courts, stylised)
    lights_glint = []
    for side in (-1, 1):
        y_col = side * 2450
        mesh(CUBE, (3800, side * 3150, 700), (105, 12, 14), mat=stone_dark, label=f"Building_{side}")
        mesh(CUBE, (3800, side * 2450, 1180), (105, 3.6, 1.2), mat=stone_col, label=f"Entablature_{side}")
        mesh(CUBE, (3800, side * 2450, 40), (105, 3.6, 0.8), mat=stone_col, label=f"Stylobate_{side}")
        for i, x in enumerate(range(-1300, 9000, 450)):
            mesh(CYL, (x, y_col, 600), (1.1, 1.1, 11.0), mat=stone_col, folder="Stage/Columns")
            if i % 2 == 0:
                # warm amber up-light washing the column
                spot((x, y_col - side * 90, 30), (90, 0, 0), 900.0, (255, 150, 70), outer=22, inner=8,
                     radius=1600, vol=0.4, folder="Lights/Uplights")
        # floodlights on the roof line: the "strobe" lights, backlighting the rain
        for x in range(-900, 8200, 1300):
            src = (x, side * 2380, 1330)
            mesh(SPHERE, src, (0.35, 0.35, 0.35), mat=lamp, folder="Stage/Lamps", shadows=False)
            target = (x + 300, -side * 300, 0)
            s = spot(src, look_rot(src, target), 60000.0, (235, 242, 255), outer=38, inner=14,
                     radius=9000, vol=1.6, source=18, label=f"Flood_{side}_{x}", folder="Lights/Floods")
            if -1000 <= x <= 3000:
                lights_glint.append(s)

    # ---- the far lit white building (Queen's House) and the twin domes
    mesh(CUBE, (13500, 0, 650), (32, 9, 13), mat=facade, label="QueensHouse")
    for y in (-1200, 0, 1200):
        spot((12300, y, 20), (60, 0, 0), 25000.0, (255, 236, 205), outer=40, inner=20, radius=5000, vol=0.3,
             folder="Lights/Facade")
    for side in (-1, 1):
        mesh(CYL, (9300, side * 2700, 1500), (9, 9, 30), mat=stone_col, label=f"DomeDrum_{side}")
        mesh(SPHERE, (9300, side * 2700, 3000), (9.5, 9.5, 8), mat=stone_col, label=f"Dome_{side}")
        spot((8800, side * 2200, 100), look_rot((8800, side * 2200, 100), (9300, side * 2700, 2800)), 30000.0,
             (255, 200, 140), outer=25, inner=10, radius=5000, vol=0.3, folder="Lights/Facade")

    # ---- blue LED strips marking the stage path
    for side in (-1, 1):
        mesh(CUBE, (2000, side * 360, 2), (80, 0.05, 0.03), mat=led, label=f"LED_{side}", shadows=False)
        for x in range(-1500, 6000, 1500):
            point((x, side * 380, 15), 150.0, (60, 140, 255), radius=500, vol=0.2, folder="Lights/LED")

    # ---- crowd with umbrellas on both lawns (keep the camera corridor clear)
    n = 0
    for _ in range(420):
        x = random.uniform(-1500, 4200)
        y = random.choice((-1, 1)) * random.uniform(480, 2150)
        if x < -300 and abs(y) < 900:
            continue
        h = random.uniform(158, 190)
        mesh(CYL, (x, y, h * 0.42), (0.42, 0.36, h * 0.0084), mat=random.choice(cloth),
             folder="Crowd", shadows=False)
        mesh(SPHERE, (x, y, h * 0.92), (0.21, 0.2, 0.25), mat=skin, folder="Crowd", shadows=False)
        if random.random() < 0.7:
            r = random.uniform(0.95, 1.2)
            mesh(CONE, (x + random.uniform(-15, 15), y + random.uniform(-15, 15), h + 32),
                 (r, r, 0.32), rot=(random.uniform(-12, 12), 0, random.uniform(-12, 12)),
                 mat=umbrellas[random.choice(umb_weights)], folder="Crowd", shadows=False)
        n += 1
        if n >= 220:
            break

    # ---- red double-decker bus with lit windows (the kissing couple shot)
    mesh(CUBE, (2600, -2050, 230), (11, 2.5, 4.2), rot=(0, 8, 0), mat=bus_red, label="Bus")
    for z in (150, 330):
        mesh(CUBE, (2600, -1920, z), (10.2, 0.05, 0.75), rot=(0, 8, 0), mat=bus_glass, label=f"BusWindows_{z}",
             shadows=False)
    point((2600, -2050, 330), 300.0, (200, 225, 255), radius=900, vol=0.0, folder="Lights/Bus")
    point((2600, -2050, 150), 300.0, (200, 225, 255), radius=900, vol=0.0, folder="Lights/Bus")

    # ---- the magician: UE mannequin in a dark wet coat, idling, facing the hero camera (-X).
    manny = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
    idle = unreal.load_asset("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle")
    perf = eas.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0), R((0, 90, 0)))
    perf.set_actor_label("Atlas")
    perf.set_folder_path("Performer")
    sk = perf.skeletal_mesh_component
    sk.set_skeletal_mesh_asset(manny)
    for i in range(sk.get_num_materials()):
        sk.set_material(i, coat)
    sk.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
    sk.set_animation(idle)
    sk.play(True)
    key = spot((-500, -250, 420), look_rot((-500, -250, 420), (0, 0, 150)), 9000.0, (255, 246, 235),
               outer=18, inner=6, radius=2500, shadows=True, vol=0.6, source=20, label="Key_Atlas")
    lights_glint.insert(0, key)

    # ---- atmosphere: night, volumetric rain haze around the lamps
    fog = eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    fc = fog.component
    fc.set_editor_property("fog_density", 0.035)
    fc.set_editor_property("fog_height_falloff", 0.12)
    fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.004, 0.005, 0.007, 1))
    fc.set_editor_property("enable_volumetric_fog", True)
    fc.set_editor_property("volumetric_fog_scattering_distribution", 0.55)
    fc.set_editor_property("volumetric_fog_albedo", unreal.Color(200, 205, 210, 255))
    fc.set_editor_property("volumetric_fog_extinction_scale", 1.2)
    fc.set_editor_property("volumetric_fog_distance", 9000.0)
    fog.set_folder_path("Atmosphere")

    sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, 0, 0))
    sc = sky.light_component
    sc.set_editor_property("intensity", 0.6)
    sc.set_editor_property("lower_hemisphere_is_black", True)
    sc.set_editor_property("real_time_capture", False)
    sc.set_editor_property("light_color", unreal.Color(120, 140, 170, 255))
    sc.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.set_folder_path("Atmosphere")

    moon = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 800), unreal.Rotator(pitch=-35.0, yaw=40.0, roll=0.0))
    mc = moon.light_component
    mc.set_editor_property("intensity", 0.04)
    mc.set_editor_property("light_color", unreal.Color(150, 170, 255, 255))
    mc.set_editor_property("cast_shadows", False)
    mc.set_mobility(unreal.ComponentMobility.MOVABLE)
    moon.set_folder_path("Atmosphere")

    # ---- post: manual exposure (stable under the strobe), bloom, grain, 2.39 framing in the camera
    ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    ppv.set_actor_label("PPV_Strobe")
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    s.set_editor_property("override_auto_exposure_method", True)
    s.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    s.set_editor_property("override_auto_exposure_bias", True)
    s.set_editor_property("auto_exposure_bias", 2.5)
    s.set_editor_property("override_bloom_intensity", True)
    s.set_editor_property("bloom_intensity", 0.9)
    s.set_editor_property("override_vignette_intensity", True)
    s.set_editor_property("vignette_intensity", 0.45)
    s.set_editor_property("override_film_grain_intensity", True)
    s.set_editor_property("film_grain_intensity", 0.12)
    s.set_editor_property("override_motion_blur_amount", True)
    s.set_editor_property("motion_blur_amount", 0.35)
    s.set_editor_property("override_color_saturation", True)
    s.set_editor_property("color_saturation", unreal.Vector4(0.92, 0.96, 1.0, 1.0))
    s.set_editor_property("override_color_gain_shadows", True)
    s.set_editor_property("color_gain_shadows", unreal.Vector4(0.92, 1.0, 1.04, 1.0))
    ppv.set_editor_property("settings", s)
    ppv.set_folder_path("Atmosphere")

    # ---- the rain
    system = unreal.load_asset(ROOT + "/Niagara/NS_RainMagic")
    rain = eas.spawn_actor_from_object(system, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    rain.set_actor_label("RainMagic")
    rain.set_folder_path("Rain")

    cap = eas.spawn_actor_from_class(unreal.SceneCaptureCube, unreal.Vector(-250, 0, 200), unreal.Rotator(0, 0, 0))
    cap.set_actor_label("EnvCapture")
    cc = cap.get_editor_property("capture_component_cube")
    cc.set_editor_property("texture_target", unreal.load_asset(ROOT + "/Materials/RT_RainEnv"))
    cc.set_editor_property("capture_every_frame", False)
    cc.set_editor_property("capture_on_movement", False)
    cap.set_folder_path("Rain")

    ctrl = eas.spawn_actor_from_class(unreal.RainMagicController, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    ctrl.set_actor_label("RainMagicController")
    ctrl.set_editor_property("rain_actor", rain)
    ctrl.set_editor_property("env_capture", cap)
    ctrl.set_editor_property("collection", unreal.load_asset(ROOT + "/Materials/MPC_Rain"))
    ctrl.set_editor_property("glint_lights", lights_glint[:6])
    ctrl.set_editor_property("performer", perf)
    ctrl.set_editor_property("strobe_volume", ppv)
    ctrl.set_folder_path("Rain")

    # ---- hero camera: 35 mm, Super35 2.39:1, f/2.8 focused on Atlas
    cam_loc = (-650, 45, 160)
    cam = eas.spawn_actor_from_class(unreal.CineCameraActor, unreal.Vector(*cam_loc), look_rot(cam_loc, (0, 0, 150)))
    cam.set_actor_label("CineCam_Hero")
    ccc = cam.get_cine_camera_component()
    fb = ccc.get_editor_property("filmback")
    fb.set_editor_property("sensor_width", 24.89)
    fb.set_editor_property("sensor_height", 10.41)
    ccc.set_editor_property("filmback", fb)
    ccc.set_editor_property("current_focal_length", 35.0)
    ccc.set_editor_property("current_aperture", 2.8)
    focus = ccc.get_editor_property("focus_settings")
    focus.set_editor_property("focus_method", unreal.CameraFocusMethod.MANUAL)
    focus.set_editor_property("manual_focus_distance", 650.0)
    ccc.set_editor_property("focus_settings", focus)
    cam.set_folder_path("Cameras")
    ctrl.set_editor_property("view_camera", cam)
    ps = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-900, 0, 100), unreal.Rotator(0, 0, 0))
    ps.set_folder_path("Cameras")

    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        unreal.Vector(*cam_loc), look_rot(cam_loc, (0, 0, 150)))
    les.save_current_level()
    print(f"Stage built: {len(eas.get_all_level_actors())} actors, glint lights = {len(lights_glint[:6])}")
    return ctrl


build()
