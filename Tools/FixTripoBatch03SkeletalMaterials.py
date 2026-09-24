"""Assign each Tripo batch 03 monster its rebuilt PBR instance (the Bridge left the slot pointing at an unsaved MI).

UnrealEditor-Cmd.exe <project> -run=pythonscript -script=Tools/FixTripoBatch03SkeletalMaterials.py -nullrhi
"""
import unreal

EAL = unreal.EditorAssetLibrary
fixed = []
for folder in EAL.list_assets("/Game/Tripo/Monsters", recursive=False, include_folder=True):
    folder = folder.rstrip("/")
    for path in EAL.list_assets(folder, recursive=False):
        asset = EAL.load_asset(path.split(".")[0])
        if not isinstance(asset, unreal.SkeletalMesh):
            continue
        mi = EAL.load_asset("%s/%s_Mat" % (folder, asset.get_name()))
        if not isinstance(mi, unreal.MaterialInstanceConstant):
            raise RuntimeError("Missing instance for " + asset.get_path_name())
        new = list(asset.get_editor_property("materials"))
        for slot in new:
            slot.set_editor_property("material_interface", mi)
        asset.modify()
        asset.set_editor_property("materials", new)
        if not EAL.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError("Save failed " + asset.get_path_name())
        check = [s.get_editor_property("material_interface") for s in asset.get_editor_property("materials")]
        if any(c != mi for c in check):
            raise RuntimeError("Material verification failed " + asset.get_path_name())
        fixed.append(asset.get_name())
unreal.log("CIRE_TRIPO_BATCH03_SKELETAL_MATERIALS PASS meshes=%d %s" % (len(fixed), ",".join(fixed)))
