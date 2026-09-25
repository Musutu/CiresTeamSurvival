"""UE 5.8 commandlet: world-scale material instances (art direction "vibrant, fun and crisp").

Creates a world-aligned (triplanar) instance from the project's own CC0 arena masters, so it can be drawn on the
scaled ground cubes and on backdrop meshes of any size without stretching:
  /Game/Environment/Town/Materials/MI_TownW_Meadow     green meadow ground for the park, the farmsteads and the countryside
                                                       (Poly Haven withered_grass, re-tinted; child of MI_ArenaW_Stubble)
Nothing else is modified. Marker CIRE_WORLD_SCALE_MATERIALS_DONE.

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/BuildWorldScaleMaterials.py -unattended -nullrhi
"""
import unreal as u

lib = u.EditorAssetLibrary
tools = u.AssetToolsHelpers.get_asset_tools()
edit = u.MaterialEditingLibrary
PKG = "/Game/Environment/Town/Materials"
ARENA = "/Game/Arenas/Materials"


def instance(name, parent_path, scalars, vectors):
    path = f"{PKG}/{name}"
    mi = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, PKG, u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
    parent = lib.load_asset(parent_path)
    assert parent, parent_path
    edit.set_material_instance_parent(mi, parent)
    for k, v in scalars.items():
        edit.set_material_instance_scalar_parameter_value(mi, k, float(v))
    for k, v in vectors.items():
        edit.set_material_instance_vector_parameter_value(mi, k, u.LinearColor(*v, 1))
    lib.save_loaded_asset(mi, only_if_is_dirty=False)
    return path


made = [
    instance("MI_TownW_Meadow", f"{ARENA}/MI_ArenaW_Stubble", {"Saturation": 1.0, "MacroVariation": .6},
             {"Tint": (0.30, 0.47, 0.17)}),
]
u.log(f"CIRE_WORLD_SCALE_MATERIALS_DONE {made}")
