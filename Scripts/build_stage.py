"""Project Rain - builds /Game/RainMagic/Maps/L_RainStage: a minimal night stage for the rain.

Only what the drops need: a wet ground, the magician (UE mannequin), four high stage lamps (film lights
far out behind the magician backlight the rain: the drops' forward-scattering glints and the bright points
their ball lenses refract; a near side lamp lights the ground), a key light, a thin haze, a night backdrop
(an HDR panorama of a wet city square: the drops are water only through what they refract), the cameras
(V cycles them in PIE) and the rain/controller actors. The v6 Greenwich stage (colonnades, crowd, bus)
lives in Scripts/legacy/build_stage_greenwich.py.

Run inside the editor:  python3 Scripts/ue_cmd.py -f Scripts/build_stage.py
Assumes Scripts/import_backdrop.py, URainBuilderLibrary.build_rain_assets() and build_stage_materials()
have been run.
"""
import math
import unreal

ROOT = "/Game/RainMagic"
MAP_PATH = ROOT + "/Maps/L_RainStage"
MI_PATH = ROOT + "/Materials/Instances"

eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary

CYL = unreal.load_asset("/Engine/BasicShapes/Cylinder.Cylinder")
SPHERE = unreal.load_asset("/Engine/BasicShapes/Sphere.Sphere")
PLANE = unreal.load_asset("/Engine/BasicShapes/Plane.Plane")


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
         vol=1.0, source=5.0, label=None, folder="Lights", ground=True):
    """ground=False: the light skips lighting channel 0 (the ground) and only lights the magician."""
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
    if not ground:
        c.set_editor_property("lighting_channels", unreal.LightingChannels(channel0=False, channel1=True, channel2=False))
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


# Stage lamps around the magician, as (azimuth deg from +X, distance cm, height cm, candela, rgb). The
# cameras mostly look down +X. Rain is lit from behind: a drop lights up when a lamp sits in its forward-
# scattering lobe, i.e. just behind it as seen from the camera. So the rig is big lights far out (40 m,
# 10 m up: ~10 deg above the frame), the way film rain is backlit from cranes: behind Atlas for the main
# cameras (+8 deg), opposite the high three-quarter shot (-40 deg) and opposite the orbit (+70 deg). Being
# far away they light every drop in view about equally, from the lens to the background (a lamp inside
# the rain would light the drops around itself far more than those at the camera), and their ball lenses
# show them as a bright point inside every drop. One near side lamp lights the ground.
LAMPS = [
    (-100.0, 900.0, 600.0, 8000.0, (235, 242, 255)),
    (8.0, 4000.0, 1000.0, 160000.0, (235, 242, 255)),
    (-40.0, 4000.0, 1000.0, 160000.0, (230, 238, 255)),
    (70.0, 4000.0, 1000.0, 100000.0, (255, 225, 190)),
]
TARGET = (0.0, 0.0, 140.0)

# Night backdrop (M_NightSky): panorama brightness on the stage and its turn about the vertical (the lit
# town hall behind Atlas for the hero camera, which looks down +X). The dome is drawn at infinity; it is
# big (500 m) so the strip of it a camera sees below the ground's far edge is thin (~0.2 deg), and the
# ground plane reaches out to it.
SKY_EXPOSURE = 18.0
SKY_YAW = 0.0
SKY_RADIUS = 50000.0

# Framings for the rain, as (label, location, look-at, focal length mm, f-number, focus distance cm). Atlas
# stands at the origin facing -X; his hand is at about (-60, -10, 155).
CAMERAS = [
    # Medium shot from just below his eyeline, as in the film: Atlas waist-up, drops all around him.
    ("CineCam_Hero", (-230, -45, 140), (0, 10, 160), 40.0, 2.2, 205.0),
    # Close-up past his head down the square: beads at the lens, the city melted into lights.
    ("CineCam_Close", (-180, -30, 168), (100, -30, 164), 50.0, 2.0, 182.0),
    # From his side at hand height: the stopped rain in front of the lit shop fronts.
    ("CineCam_Side", (-70, -230, 150), (-60, 0, 152), 50.0, 2.8, 218.0),
    # Low, looking up past him into the street lamps and the glowing sky.
    ("CineCam_Low", (-250, -150, 110), (0, 0, 185), 35.0, 2.8, 290.0),
    # The whole figure from 6.5 m (the earlier hero view).
    ("CineCam_Wide", (-650, 45, 160), (0, 0, 150), 35.0, 2.8, 650.0),
]


def cine_cam(label, loc, target, focal, fstop, focus):
    a = eas.spawn_actor_from_class(unreal.CineCameraActor, unreal.Vector(*loc), look_rot(loc, target))
    a.set_actor_label(label)
    c = a.get_cine_camera_component()
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
    a.set_folder_path("Cameras")
    return a


def build():
    open_fresh_level()

    coat = mi("MI_Coat", (0.012, 0.014, 0.02), 0.22)
    lamp = mi("MI_LampHot", (1, 1, 1), 0.5, emissive=(60.0, 62.0, 66.0))
    pole_mat = mi("MI_Pole", (0.02, 0.02, 0.022), 0.4, metal=0.8)
    ground_mat = unreal.load_asset(ROOT + "/Materials/M_WetGround")

    # ---- ground: one wet paved plane with puddles and rain ripples (WetGround.hlsl), out to the dome,
    # where the backdrop's buildings stand on it
    mesh(PLANE, (0, 0, 0), (1200, 1200, 1), mat=ground_mat, label="Ground_Wet")

    # ---- night backdrop: an HDR panorama of a wet city square at night (NightSky.hlsl). It is the
    # background and, captured into the drops' environment cube, what every drop refracts: on black, a
    # water drop can only show up as a point of light.
    sky_path = f"{MI_PATH}/MI_NightSky"
    sky_mi = unreal.load_asset(sky_path) if unreal.EditorAssetLibrary.does_asset_exist(sky_path) else \
        asset_tools.create_asset("MI_NightSky", MI_PATH, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(sky_mi, unreal.load_asset(ROOT + "/Materials/M_NightSky"))
    mel.set_material_instance_scalar_parameter_value(sky_mi, "Exposure", SKY_EXPOSURE)
    mel.set_material_instance_scalar_parameter_value(sky_mi, "Yaw", SKY_YAW)
    mel.update_material_instance(sky_mi)
    unreal.EditorAssetLibrary.save_loaded_asset(sky_mi)
    s_sky = SKY_RADIUS / 50.0
    mesh(SPHERE, (0, 0, 0), (s_sky, s_sky, s_sky), mat=sky_mi, label="NightSky", folder="Atmosphere", shadows=False)

    # ---- lamps: spotlights with a small hot lamp head on a thin pole
    glints = []
    for i, (az, dist, h, cd, rgb) in enumerate(LAMPS):
        a = math.radians(az)
        src = (dist * math.cos(a), dist * math.sin(a), h)
        # The far lamps are film lights on cranes, out of the picture (the backdrop has its own street lamps):
        # only the near side lamp gets a visible head and pole.
        far = dist >= 2000
        if not far:
            mesh(SPHERE, src, (0.3, 0.3, 0.3), mat=lamp, label=f"LampHead_{i}", folder="Stage/Lamps", shadows=False)
            mesh(CYL, (src[0], src[1], (h - 15) * 0.5), (0.06, 0.06, (h - 15) / 100.0), mat=pole_mat,
                 label=f"LampPole_{i}", folder="Stage/Lamps", shadows=False)
        # The far lamps would glare in the wet ground behind Atlas: they light only him (and the drops).
        glints.append(spot(src, look_rot(src, TARGET), cd, rgb, outer=15 if far else 40, inner=6 if far else 15,
                           radius=6000, vol=1.2, source=18, label=f"Lamp_{i}", folder="Lights", ground=not far))

    # ---- sky lamp: a lamp high up behind the column (opposite the hero camera) whose light reaches only
    # 1 m, so it lights nothing in the scene. It only feeds the drops' glint/scatter lighting (as a point
    # lamp, no cone): the wide and low shots looking up at the rising rain see the drops backlit.
    sky_lamp = point((1500, -600, 2600), 150000.0, (225, 232, 255), radius=100, vol=0.0, source=40,
                     label="Lamp_Sky", folder="Lights")
    glints.append(sky_lamp)

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
    sk.set_editor_property("lighting_channels", unreal.LightingChannels(channel0=True, channel1=True, channel2=False))
    sk.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
    sk.set_animation(idle)
    sk.play(True)
    key = spot((-500, -250, 420), look_rot((-500, -250, 420), (0, 0, 150)), 9000.0, (255, 246, 235),
               outer=18, inner=6, radius=2500, shadows=True, vol=0.6, source=20, label="Key_Atlas")
    glints.insert(0, key)

    # ---- atmosphere: night haze. Volumetric fog (light beams through the rain) is off: it costs ~4 ms
    # a frame on an M2 Pro. Tick "Volumetric Fog" on the fog actor to get the beams back.
    fog = eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    fc = fog.component
    # Rain haze lit by the city: distant pavement and buildings fade into the same warm glow.
    # The panorama has its own air: the fog stops short of the dome (450 m).
    fc.set_editor_property("fog_density", 0.002)
    fc.set_editor_property("fog_cutoff_distance", 45000.0)
    fc.set_editor_property("fog_height_falloff", 0.15)
    fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(1.2, 1.05, 0.85, 1))
    fc.set_editor_property("enable_volumetric_fog", False)
    fc.set_editor_property("volumetric_fog_scattering_distribution", 0.55)
    fc.set_editor_property("volumetric_fog_albedo", unreal.Color(200, 205, 210, 255))
    fc.set_editor_property("volumetric_fog_extinction_scale", 1.2)
    fc.set_editor_property("volumetric_fog_distance", 3000.0)
    fog.set_folder_path("Atmosphere")

    sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, 0, 0))
    sc = sky.light_component
    sc.set_editor_property("intensity", 0.6)
    sc.set_editor_property("lower_hemisphere_is_black", True)
    # Everything beyond 50 m counts as sky: the backdrop dome (100 m) lights the stage and shows in the
    # wet ground's reflections; the stage itself (lamps out to 40 m) stays out of the capture.
    sc.set_editor_property("sky_distance_threshold", 5000.0)
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

    # ---- post: manual exposure, bloom, grain. No Lumen: at night the
    # stage is lit by a few direct lights, and Lumen GI/reflections cost a lot on a Mac GPU.
    ppv = eas.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    ppv.set_actor_label("PPV_Night")
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
    # No post motion blur: the drops blur analytically along their own motion, and the post pass would
    # smear them with the background's camera-motion vectors (radial streaks on every moving shot).
    s.set_editor_property("override_motion_blur_amount", True)
    s.set_editor_property("motion_blur_amount", 0.0)
    s.set_editor_property("override_color_saturation", True)
    s.set_editor_property("color_saturation", unreal.Vector4(0.92, 0.96, 1.0, 1.0))
    s.set_editor_property("override_color_gain_shadows", True)
    s.set_editor_property("color_gain_shadows", unreal.Vector4(0.92, 1.0, 1.04, 1.0))
    s.set_editor_property("override_dynamic_global_illumination_method", True)
    s.set_editor_property("dynamic_global_illumination_method", unreal.DynamicGlobalIlluminationMethod.NONE)
    s.set_editor_property("override_reflection_method", True)
    s.set_editor_property("reflection_method", unreal.ReflectionMethod.SCREEN_SPACE)
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
    ctrl.set_editor_property("glint_lights", glints[:6])
    ctrl.set_editor_property("performer", perf)
    ctrl.set_folder_path("Rain")

    # ---- cameras (Super35 2.39:1, focused on the drops around Atlas). The rain shows as water where drops are
    # both in focus and big on screen: close to the lens and the focus plane, so the framings are close, on
    # fast lenses, with the city behind blurred to lights. CineCam_Hero is the PIE / game view; V cycles them.
    cams = [cine_cam(*c) for c in CAMERAS]
    ctrl.set_editor_property("view_camera", cams[0])
    ctrl.set_editor_property("showcase_cameras", cams)
    cam_loc, cam_target = CAMERAS[0][1], CAMERAS[0][2]
    ps = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-900, 0, 100), unreal.Rotator(0, 0, 0))
    ps.set_folder_path("Cameras")

    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        unreal.Vector(*cam_loc), look_rot(cam_loc, cam_target))
    # The sky light lights the stage with what it captures: now the backdrop's overcast city sky.
    sc.recapture_sky()
    les.save_current_level()
    print(f"Stage built: {len(eas.get_all_level_actors())} actors, glint lights = {len(glints[:6])}")
    return ctrl


build()
