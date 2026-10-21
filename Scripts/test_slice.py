"""Minimal end-to-end check: custom-HLSL module -> GPU emitter -> compiled system -> placed in a level."""
import unreal

print(unreal.RainBuilderLibrary.build_test_system())

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
path = "/Game/RainMagic/Test/L_Test"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    les.load_level(path)
    for a in eas.get_all_level_actors():
        if a.get_class().get_name() not in ("WorldSettings", "Brush"):
            eas.destroy_actor(a)
else:
    les.new_level(path, False)

system = unreal.load_asset("/Game/RainMagic/Test/NS_Test")
actor = eas.spawn_actor_from_object(system, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
light = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(pitch=-45.0, yaw=30.0, roll=0.0))
sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 300), unreal.Rotator(0, 0, 0))
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
    unreal.Vector(-700, 0, 150), unreal.Rotator(pitch=-5.0, yaw=0.0, roll=0.0))
les.save_current_level()
print("test level ready:", actor.get_name())
