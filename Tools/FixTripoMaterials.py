"""Connect each imported hero's verified color texture to its PBR instance.

The Bridge placed base color under a part-prefixed name while assigning the
generic roughness/metallic/normal group to the sole material slot. Preserve all
other channels. Back up each original material package before the first edit.
"""
from pathlib import Path
import json
import shutil
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
backup = project / "Art" / "Imports" / "BridgeOriginalMaterials"
backup.mkdir(parents=True, exist_ok=True)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(synchronous_search=True)
changes = []
for model in ("medieval_knight_armor_3d_model", "armored_archer_3d_model", "battlefield_healer_3d_model"):
    folder = "/Game/TripoModels/" + model
    mat = unreal.load_asset(folder + "/" + model + "_Mat")
    if not isinstance(mat, unreal.MaterialInstanceConstant):
        raise RuntimeError("Missing imported PBR material: " + model)
    candidates = []
    for data in registry.get_assets_by_path(folder + "/Textures", recursive=True):
        if str(data.asset_name).lower().endswith("_basecolor"):
            texture = data.get_asset()
            if isinstance(texture, unreal.Texture2D) and texture.get_editor_property("srgb"):
                candidates.append(texture)
    if len(candidates) != 1:
        raise RuntimeError("Expected exactly one verified color texture for " + model)
    texture = candidates[0]
    source = project / "Content" / "TripoModels" / model / (model + "_Mat.uasset")
    saved_copy = backup / source.name
    if source.is_file() and not saved_copy.exists():
        shutil.copy2(source, saved_copy)
    before = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mat, "BaseColorTex")
    # UE 5.8's implementation always returns false even after performing the
    # update. Verify the actual value below instead of trusting that return bit.
    unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mat, "BaseColorTex", texture)
    unreal.MaterialEditingLibrary.update_material_instance(mat)
    if not unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False):
        raise RuntimeError("PBR material save failed for " + model)
    actual = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(mat, "BaseColorTex")
    if actual != texture:
        raise RuntimeError("PBR material verification failed for " + model)
    changes.append({"model": model, "material": mat.get_path_name(),
                    "previous_color": before.get_path_name() if before else None,
                    "color_texture": texture.get_path_name(), "backup": str(saved_copy)})
(project / "Saved" / "TripoMaterialRepair.json").write_text(json.dumps(changes, indent=2), encoding="utf-8")
unreal.log("CIRE_TRIPO_MATERIAL_REPAIR_PASS models=%d" % len(changes))
