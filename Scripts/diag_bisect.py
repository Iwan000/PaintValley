import unreal
L = unreal.RainBuilderLibrary
ROOT = "/Users/guocheng/ProjectRain/"
def fill(path, extra=None):
    t = open(ROOT + path).read()
    rep = {"$(SEED)": "2654435769", "$(HALF_X)": "150.0", "$(HALF_Y)": "150.0", "$(HEIGHT)": "400.0", "$(FLOOR)": "0.0",
           "$(RAIN_RATE)": "15.0", "$(LAMBDA_SCALE)": "0.8", "$(D_MIN_MM)": "0.6", "$(D_MAX_MM)": "5.0",
           "$(EDGE_FADE)": "50.0", "$(EDGE_FADE_OFF)": "0.0"}
    for k, v in rep.items():
        t = t.replace(k, v)
    return t
W = "Particles.SpriteSize = float2(30.0, 30.0);\nParticles.Color = float4(1.0, 0.0, 1.0, 1.0);\nParticles.SpriteAlignment = float3(0.0, 0.0, 1.0);\n"
spawn = fill("Niagara/HLSL/RainSpawn.hlsl")
update = fill("Niagara/HLSL/RainUpdate.hlsl")
# name, spawnHlsl, updateHlsl, defaultModules, burst, count, gpu, declare, rainMat
variants = [
    ("D1_Default", "", W, True,  False, 50,     True, False, False),
    ("D2_Burst",   "", W, False, True,  2000,   True, False, False),
    ("D3_Attrs",   "", W, False, True,  2000,   True, True,  False),
    ("D4_RainHlsl", spawn, update, False, True, 2000, True, True, False),
    ("D5_RainMat", spawn, update, False, True,  2000, True, True, True),
    ("D6_Count",   spawn, update, False, True,  300000, True, True, True),
]
for v in variants:
    r = L.build_diag_system(*v)
    print(v[0], r.splitlines()[0], [l for l in r.splitlines() if "ERROR" in l or "!!" in l or "linked" in l])
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
path = "/Game/RainMagic/Diag/L_Diag"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    les.load_level(path)
    for a in eas.get_all_level_actors():
        if a.get_class().get_name() not in ("WorldSettings", "Brush"):
            eas.destroy_actor(a)
else:
    les.new_level(path, False)
for i, v in enumerate(variants):
    a = eas.spawn_actor_from_object(unreal.load_asset(f"/Game/RainMagic/Diag/NS_{v[0]}"), unreal.Vector(0, (i - 2.5) * 450, 0), unreal.Rotator(0, 0, 0))
    a.set_actor_label(v[0])
eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, 0, 0))
cam = eas.spawn_actor_from_class(unreal.CineCameraActor, unreal.Vector(-2600, 0, 250), unreal.Rotator(pitch=-2.0, yaw=0.0, roll=0.0))
cam.set_actor_label("CineCam_Hero")
cam.get_cine_camera_component().set_editor_property("current_focal_length", 18.0)
les.save_current_level()
print("diag level ready")
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
unreal.MaterialLibrary.set_vector_parameter_value(w, unreal.load_asset("/Game/RainMagic/Materials/MPC_Rain"), "Look", unreal.LinearColor(1, 1, 4, 100))
d = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 800), unreal.Rotator(pitch=-40.0, yaw=20.0, roll=0.0))
for a in eas.get_all_level_actors():
    if isinstance(a, unreal.NiagaraActor):
        a.niagara_component.reset_system()
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(unreal.Vector(-2600, 0, 250), unreal.Rotator(pitch=-2.0, yaw=0.0, roll=0.0))
les.editor_set_game_view(True)
