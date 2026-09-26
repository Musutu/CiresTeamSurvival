"""UE 5.8 commandlet: measure the Medieval Kingdom (CastleTown) meshes the world-scale town and arenas may use.

For every candidate static mesh that exists locally it records the bounds (cm), Nanite, LOD0 triangles, the
material slots and whether every material's base material can be drawn on instanced static meshes (a Fab
master without that usage flag renders as the default grey material in uncooked -game runs). The result feeds
Tools/BuildFabWorldSlots.py, which writes the local-only overlay Content/Data/TownAssetSlots.fabworld.json.
Nothing is modified. Report: Saved/FabWorldMeasure.json. Marker CIRE_FAB_WORLD_MEASURE_DONE.

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/MeasureFabWorld.py -unattended -nullrhi
"""
import json
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
FOLDERS = ["/Game/CastleTown/Scanned_Foliage/Foliage", "/Game/CastleTown/Static_Mesh/Rock_Kit", "/Game/CastleTown/Static_Mesh/Hills",
           "/Game/CastleTown/Static_Mesh/Mountains", "/Game/CastleTown/Static_Mesh/Stone_Fence", "/Game/CastleTown/Static_Mesh/Cobblestone_Road",
           "/Game/CastleTown/Static_Mesh/Flags", "/Game/CastleTown/Static_Mesh/Foliage", "/Game/CastleTown/Static_Mesh/Castle_Kit/CurtainWall",
           "/Game/CastleTown/Static_Mesh/Props", "/Game/CastleTown/Static_Mesh/Building_Kit/WoodenFences"]


def usage_ok(material):
    try:
        base = material.get_base_material()
        return bool(base.get_editor_property("used_with_instanced_static_meshes"))
    except Exception:  # noqa: BLE001 - a material we cannot inspect counts as unknown
        return None


def main():
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    out = {}
    for folder in FOLDERS:
        for data in reg.get_assets_by_path(folder, recursive=True):
            if str(data.asset_class_path.asset_name) != "StaticMesh":
                continue
            path = f"{data.package_name}.{data.asset_name}"
            mesh = unreal.load_asset(path)
            if not mesh:
                continue
            box = mesh.get_bounding_box()
            size = [round(box.max.x - box.min.x, 1), round(box.max.y - box.min.y, 1), round(box.max.z - box.min.z, 1)]
            mats = []
            for slot in mesh.get_editor_property("static_materials"):
                m = slot.get_editor_property("material_interface")
                mats.append({"slot": str(slot.get_editor_property("material_slot_name")), "material": m.get_path_name() if m else None,
                             "instanced": usage_ok(m) if m else None})
            try:
                nanite = bool(mesh.get_editor_property("nanite_settings").get_editor_property("enabled"))
            except Exception:  # noqa: BLE001
                nanite = None
            try:
                tris = unreal.EditorStaticMeshLibrary.get_number_verts(mesh, 0)
            except Exception:  # noqa: BLE001
                tris = None
            out[path] = {"size": size, "min": [round(box.min.x, 1), round(box.min.y, 1), round(box.min.z, 1)], "nanite": nanite,
                         "lod0Verts": tris, "lods": mesh.get_num_lods(), "materials": mats}
    report = ROOT / "Saved/FabWorldMeasure.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    unreal.log(f"CIRE_FAB_WORLD_MEASURE_DONE meshes={len(out)} report={report}")


main()
