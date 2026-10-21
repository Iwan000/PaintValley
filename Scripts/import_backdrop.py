"""Imports the night backdrop panorama as /Game/RainMagic/Environment/T_NightSky (a TextureCube).

The panorama is Poly Haven's "Rathaus" by Greg Zaal (CC0, https://polyhaven.com/a/rathaus): a wet city square
at night under an overcast sky, close to the film's Greenwich night. Download the 4k .hdr to SourceArt/ first:
    curl -o SourceArt/rathaus_4k.hdr https://dl.polyhaven.org/file/ph-assets/HDRIs/hdr/4k/rathaus_4k.hdr
Then, in the running editor (before URainBuilderLibrary.build_stage_materials(), which builds M_NightSky):
    python3 Scripts/ue_cmd.py -f Scripts/import_backdrop.py
"""
import os
import unreal

PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
SOURCE = os.path.join(PROJECT, "SourceArt", "rathaus_4k.hdr")
DEST = "/Game/RainMagic/Environment"

task = unreal.AssetImportTask()
task.filename = SOURCE
task.destination_path = DEST
task.destination_name = "T_NightSky"
task.automated = True
task.replace_existing = True
task.save = True
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

tex = unreal.load_asset(DEST + "/T_NightSky")
print("imported:", tex.get_class().get_name() if tex else None)
