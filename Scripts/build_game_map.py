"""Project Rain - builds L_RainShield, the Rain Shield game's level: a copy of L_RainStage (night backdrop, lamps,
wet ground, the film rain's controller for the drops' look) with an ARainShieldGame on the stage centre.
The copy is made on disk with the editor closed (duplicating a map and loading it in one editor session
crashes UE 5.7: the duplicated world is still referenced when the copy loads):
    cp Content/RainMagic/Maps/L_RainStage.umap Content/RainMagic/Maps/L_RainShield.umap
Then, in the editor (after unreal.RainBuilderLibrary.build_rain_game_assets()):
    python3 Scripts/ue_cmd.py -f Scripts/build_game_map.py
Re-running keeps the map and its game actor.
"""
import unreal

DST = "/Game/RainMagic/Maps/L_RainShield"

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
levels.load_level(DST)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
everything = actors.get_all_level_actors()

controller = next(a for a in everything if a.get_class().get_name() == "RainMagicController")
performer = controller.get_editor_property("performer")
centre = performer.get_actor_location() if performer else unreal.Vector(0.0, 0.0, 0.0)

games = [a for a in everything if a.get_class().get_name() == "RainShieldGame"]
game = games[0] if games else actors.spawn_actor_from_class(unreal.RainShieldGame, centre)
game.set_actor_location(centre, False, False)
game.set_actor_label("RainShieldGame")
game.set_editor_property("controller", controller)
levels.save_current_level()
print("L_RainShield: game at", centre)
