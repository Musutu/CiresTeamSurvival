"""Read-only inspection after a native Tripo Bridge transfer in Unreal Editor.

Run with Unreal's Python script runner or `py ".../Tools/InspectTripoAssets.py"`.
Only Saved/TripoBridgeInspection.json is written. Imported assets, skeletons,
materials, animations, and the current level are never changed or saved.
Unsaved assets already registered in the current editor session are included.
"""

from __future__ import annotations

from collections import Counter
from datetime import datetime, timezone
import json
from pathlib import Path
import time


ASSET_ROOT = "/Game/TripoModels"
MANNY_PATH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
MAX_ASSETS = 2048
MAX_BONES = 2048


def path_of(obj):
    return str(obj.get_path_name()) if obj else None


def vector_list(value):
    return [float(value.x), float(value.y), float(value.z)]


def optional(result, key, getter):
    """Keep partial inspection useful when an optional editor API is unavailable."""
    try:
        result[key] = getter()
    except Exception as error:
        result.setdefault("unavailable_fields", {})[key] = str(error)


def bounds_info(mesh, skeletal):
    bounds = mesh.get_imported_bounds() if skeletal else mesh.get_bounds()
    origin, extent = bounds.origin, bounds.box_extent
    size = [float(extent.x * 2), float(extent.y * 2), float(extent.z * 2)]
    return {
        "origin_cm": vector_list(origin),
        "extent_cm": vector_list(extent),
        "size_cm": size,
        "bottom_cm": float(origin.z - extent.z),
        "top_cm": float(origin.z + extent.z),
        "humanoid_height_check": "plausible" if 140 <= size[2] <= 240 else "review_scale_or_pose",
    }


def rig_info(unreal, mesh):
    # This component lives only in the transient package; it is never registered
    # in a world or saved. Assigning its mesh does not edit the mesh asset.
    component = unreal.SkeletalMeshComponent()
    component.set_skeletal_mesh_asset(mesh)
    count = int(component.get_num_bones())
    names = [str(component.get_bone_name(index)) for index in range(min(count, MAX_BONES))]
    parents = {}
    for name in names:
        parent = str(component.get_parent_bone(name))
        parents[name] = None if parent in {"", "None"} else parent
    return {
        "skeleton": path_of(mesh.get_editor_property("skeleton")),
        "bone_count": count,
        "bones": names,
        "parents": parents,
        "roots": [name for name in names if parents[name] is None],
        "bone_list_truncated": count > MAX_BONES,
    }


def compare_rigs(rig, reference):
    names, reference_names = set(rig["bones"]), set(reference["bones"])
    common = names & reference_names
    parent_differences = {
        name: {"tripo": rig["parents"][name], "manny": reference["parents"][name]}
        for name in sorted(common)
        if rig["parents"][name] != reference["parents"][name]
    }
    return {
        "manny_skeleton": reference["skeleton"],
        "manny_bone_count": reference["bone_count"],
        "same_skeleton_asset": bool(rig["skeleton"]) and rig["skeleton"] == reference["skeleton"],
        "same_bone_name_order": rig["bones"] == reference["bones"],
        "common_bones": sorted(common),
        "missing_manny_bones": sorted(reference_names - names),
        "extra_tripo_bones": sorted(names - reference_names),
        "parent_differences": parent_differences,
        "animation_compatibility_verified": False,
        "note": "Names and hierarchy do not verify bind pose, joint orientation, skin weights, or animation compatibility. Do not assign Manny's animation blueprint from this comparison alone.",
    }


def texture_info(texture):
    result = {"asset": path_of(texture), "class": texture.get_class().get_name()}
    optional(result, "width", lambda: int(texture.blueprint_get_size_x()))
    optional(result, "height", lambda: int(texture.blueprint_get_size_y()))
    optional(result, "srgb", lambda: bool(texture.get_editor_property("srgb")))
    optional(result, "compression_settings", lambda: str(texture.get_editor_property("compression_settings")))
    optional(result, "lod_group", lambda: str(texture.get_editor_property("lod_group")))
    return result


def material_info(unreal, material, texture_cache):
    result = {"asset": path_of(material), "class": material.get_class().get_name()}
    if isinstance(material, unreal.MaterialInstance):
        optional(result, "parent", lambda: path_of(material.get_editor_property("parent")))
        overrides = []
        for parameter in material.get_editor_property("texture_parameter_values"):
            texture = parameter.parameter_value
            overrides.append({"parameter": str(parameter.parameter_info.name), "texture": path_of(texture)})
            if texture and path_of(texture) not in texture_cache:
                texture_cache[path_of(texture)] = texture_info(texture)
        result["texture_overrides"] = overrides
    try:
        textures = unreal.MaterialEditingLibrary.get_material_used_textures(material)
        result["textures"] = sorted({path_of(texture) for texture in textures if texture})
        for texture in textures:
            if texture and path_of(texture) not in texture_cache:
                texture_cache[path_of(texture)] = texture_info(texture)
    except Exception as error:
        result["unavailable_fields"] = {"textures": str(error)}
    return result


def inspect_in_editor(unreal):
    started = time.perf_counter()
    report = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "engine_version": unreal.SystemLibrary.get_engine_version(),
        "asset_root": ASSET_ROOT,
        "read_only_assets": True,
        "asset_save_performed": False,
        "assets": [],
        "materials": [],
        "textures": [],
        "errors": [],
        "animation_compatibility_verified": False,
    }
    library = unreal.EditorAssetLibrary
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    # Do not scan/rescan the disk or save packages while a Bridge import is active.
    asset_data = registry.get_assets_by_path(ASSET_ROOT, recursive=True, include_only_on_disk_assets=False)
    assets = sorted(asset_data, key=lambda data: str(data.package_name) + "." + str(data.asset_name))
    report["registered_asset_count"] = len(assets)
    report["asset_list_truncated"] = len(assets) > MAX_ASSETS
    reference = None
    if library.does_asset_exist(MANNY_PATH):
        try:
            manny = library.load_asset(MANNY_PATH)
            reference = rig_info(unreal, manny)
            report["manny_reference"] = {"asset": path_of(manny), "rig": reference,
                                         "bounds": bounds_info(manny, True)}
        except Exception as error:
            report["errors"].append({"asset": MANNY_PATH, "error": str(error)})
    else:
        report["errors"].append({"asset": MANNY_PATH, "error": "Existing mannequin reference is unavailable."})

    material_cache, texture_cache = {}, {}
    skeletal_subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    classes = Counter()
    for data in assets[:MAX_ASSETS]:
        package = str(data.package_name)
        if not package.startswith(ASSET_ROOT + "/"):
            continue
        asset_path = package + "." + str(data.asset_name)
        result = {"asset": asset_path}
        try:
            asset = data.get_asset()
            if not asset:
                raise RuntimeError("Registered asset could not be loaded.")
            result["class"] = asset.get_class().get_name()
            classes[result["class"]] += 1
            skeletal = isinstance(asset, unreal.SkeletalMesh)
            if skeletal or isinstance(asset, unreal.StaticMesh):
                result["mesh_type"] = "skeletal" if skeletal else "static"
                optional(result, "bounds", lambda: bounds_info(asset, skeletal))
                slots = asset.get_editor_property("materials" if skeletal else "static_materials")
                result["material_slots"] = []
                for index, slot in enumerate(slots):
                    material = slot.material_interface
                    result["material_slots"].append({"index": index,
                        "name": str(slot.material_slot_name), "material": path_of(material)})
                    if material and path_of(material) not in material_cache:
                        material_cache[path_of(material)] = material_info(unreal, material, texture_cache)
                if skeletal:
                    result["rig"] = rig_info(unreal, asset)
                    if reference:
                        result["manny_comparison"] = compare_rigs(result["rig"], reference)
                    optional(result, "physics_asset", lambda: path_of(asset.get_editor_property("physics_asset")))
                    optional(result, "lod_count", lambda: int(skeletal_subsystem.get_lod_count(asset)))
                    optional(result, "lod0_vertices", lambda: int(skeletal_subsystem.get_num_verts(asset, 0)))
                    optional(result, "lod0_sections", lambda: int(skeletal_subsystem.get_num_sections(asset, 0)))
                else:
                    optional(result, "lod_count", lambda: int(asset.get_num_lods()))
                    optional(result, "lod0_vertices", lambda: int(asset.get_num_vertices(0)))
                    optional(result, "lod0_triangles", lambda: int(asset.get_num_triangles(0)))
            elif isinstance(asset, unreal.MaterialInterface):
                material_cache[asset_path] = material_info(unreal, asset, texture_cache)
            elif isinstance(asset, unreal.Texture):
                texture_cache[asset_path] = texture_info(asset)
            elif isinstance(asset, unreal.AnimationAsset):
                optional(result, "skeleton", lambda: path_of(asset.get_editor_property("skeleton")))
        except Exception as error:
            result["inspection_error"] = str(error)
            report["errors"].append({"asset": asset_path, "error": str(error)})
        report["assets"].append(result)

    report["class_counts"] = dict(sorted(classes.items()))
    report["materials"] = [material_cache[key] for key in sorted(material_cache)]
    report["textures"] = [texture_cache[key] for key in sorted(texture_cache)]
    optional(report, "unsaved_tripo_packages", lambda: sorted(
        str(package.get_path_name()) for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
        if str(package.get_path_name()).startswith(ASSET_ROOT + "/")))
    report["status"] = "no_assets_found" if not report["assets"] else "inspected_with_errors" if report["errors"] else "inspected"
    report["duration_seconds"] = round(time.perf_counter() - started, 3)
    destination = Path(unreal.Paths.project_saved_dir()).resolve() / "TripoBridgeInspection.json"
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    temporary.replace(destination)
    unreal.log(f"CIRE_TRIPO_INSPECTION {report['status']} assets={len(report['assets'])} report={destination}")
    return report


if __name__ == "__main__":
    try:
        import unreal
    except ImportError as error:
        raise SystemExit("Run this inspection inside Unreal Editor's Python environment; no external importer is invoked.") from error
    inspect_in_editor(unreal)
