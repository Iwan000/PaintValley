"""Project Rain - builds L_PaintValley, the paint game's level: an empty level (no sky, no lights: everything is
unlit) with an ARainPaintGame at the origin. Run in the editor after unreal.RainBuilderLibrary.build_paint_assets():
    python3 Scripts/ue_cmd.py -f Scripts/build_paint_map.py
Re-running keeps the map and its game actor.
"""
import unreal

DST = "/Game/RainMagic/Maps/L_PaintValley"

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist(DST):
    levels.load_level(DST)
else:
    levels.new_level(DST)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
games = [a for a in actors.get_all_level_actors() if a.get_class().get_name() == "RainPaintGame"]
game = games[0] if games else actors.spawn_actor_from_class(unreal.RainPaintGame, unreal.Vector(0.0, 0.0, 0.0))
game.set_actor_label("RainPaintGame")
levels.save_current_level()
print("L_PaintValley:", [a.get_actor_label() for a in actors.get_all_level_actors()])
