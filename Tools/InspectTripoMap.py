"""Read-only actor inventory of saved Citadel Tripo Bridge staging instances.

Run as an Unreal Python commandlet after any active build finishes. It loads
Content/Maps/Citadel.umap and writes only Saved/TripoMapInspection.json. It never
moves/deletes actors or saves the map/assets. In an interactive editor it refuses
to switch maps if any map package has unsaved changes.
"""

from datetime import datetime, timezone
import json
from pathlib import Path

import unreal


MAP_PACKAGE = "/Game/Maps/Citadel"
TRIPO_ROOT = "/Game/TripoModels/"


def vector(value):
    return [float(value.x), float(value.y), float(value.z)]


def rotation(value):
    return {"pitch": float(value.pitch), "yaw": float(value.yaw), "roll": float(value.roll)}


def inspect():
    report = {
        "created_utc": datetime.now(timezone.utc).isoformat(), "map": MAP_PACKAGE,
        "read_only_map": True, "map_save_performed": False, "asset_save_performed": False,
        "tripo_actors": [],
    }
    saved = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))
    report_path = saved / "TripoMapInspection.json"
    try:
        subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        world = subsystem.get_editor_world()
        current_package = str(world.get_path_name()).split(".")[0] if world else None
        dirty_before = [str(p.get_path_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
        report["dirty_maps_before"] = dirty_before
        if current_package != MAP_PACKAGE:
            if dirty_before:
                raise RuntimeError("Refusing to switch maps while unsaved map packages exist: " + ", ".join(dirty_before))
            filename = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())) / "Maps" / "Citadel.umap"
            if not filename.is_file():
                raise RuntimeError("Saved Citadel map not found: " + str(filename))
            world = unreal.EditorLoadingAndSavingUtils.load_map(str(filename))
        if not world or str(world.get_path_name()).split(".")[0] != MAP_PACKAGE:
            raise RuntimeError("Citadel world did not load")
        report["world"] = str(world.get_path_name())
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        report["total_level_actors"] = len(actors)
        for actor in actors:
            components = []
            for component in actor.get_components_by_class(unreal.SkeletalMeshComponent):
                mesh = component.get_skeletal_mesh_asset()
                if not mesh or not str(mesh.get_path_name()).startswith(TRIPO_ROOT):
                    continue
                components.append({
                    "component": str(component.get_path_name()), "mesh": str(mesh.get_path_name()),
                    "skeleton": str(mesh.get_editor_property("skeleton").get_path_name()),
                    "relative_location_cm": vector(component.get_editor_property("relative_location")),
                    "relative_rotation_degrees": rotation(component.get_editor_property("relative_rotation")),
                    "relative_scale": vector(component.get_editor_property("relative_scale3d")),
                })
            if components:
                report["tripo_actors"].append({
                    "label": str(actor.get_actor_label()), "actor": str(actor.get_path_name()),
                    "class": str(actor.get_class().get_name()),
                    "location_cm": vector(actor.get_actor_location()),
                    "rotation_degrees": rotation(actor.get_actor_rotation()),
                    "scale": vector(actor.get_actor_scale3d()), "skeletal_components": components,
                })
        report["tripo_actors"].sort(key=lambda a: (a["label"], a["actor"]))
        report["tripo_actor_count"] = len(report["tripo_actors"])
        report["dirty_maps_after"] = [str(p.get_path_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
        report["status"] = "inspected"
        unreal.log("CIRE_TRIPO_MAP_INSPECTION_PASS actors=" + str(report["tripo_actor_count"]))
        return report
    except Exception as error:
        report["status"] = "failed"
        report["error"] = str(error)
        raise
    finally:
        saved.mkdir(parents=True, exist_ok=True)
        temporary = report_path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(report, indent=2), encoding="utf-8")
        temporary.replace(report_path)


if __name__ == "__main__":
    inspect()
