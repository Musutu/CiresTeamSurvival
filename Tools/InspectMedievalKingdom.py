"""UE 5.8 commandlet: inventory the Medieval Kingdom (Hivemind, `Content/CastleTown`) building kits for the town.

Nothing is modified. For every static mesh in the kit folders it records bounds (cm, pivot = bounds min relative to
the origin), Nanite, LOD count, LOD0 vertices, collision (simple primitives / complex-as-simple) and, per material
slot, the material and whether its base material carries the instanced-static-mesh and Nanite usage flags (a
master without the instanced flag renders as the grey default material on the town's HISMs in uncooked -game runs).

It then opens every demo Level Instance map (`Levels/LevelInstances/...`: the pack's assembled houses, curtain-wall
towers, front gate, halls) and records each placed static-mesh piece (mesh, transform relative to the map origin,
override materials), point lights and Blueprint actors. Tools/BuildMedievalKingdomSlots.py turns these assemblies
into town slots.

Report: Saved/MedievalKingdom/Inventory.json. Marker CIRE_MK_INVENTORY_DONE.
Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/InspectMedievalKingdom.py -unattended -nullrhi
"""
import json
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
PACK = "/Game/CastleTown"
MESH_FOLDERS = ["Static_Mesh/Castle_Kit", "Static_Mesh/Village", "Static_Mesh/Building_Kit", "Static_Mesh/Brick_Kit",
                "Static_Mesh/Cobblestone_Road", "Static_Mesh/Stone_Fence", "Static_Mesh/DockWalls", "Static_Mesh/Flags",
                "Static_Mesh/Props", "Static_Mesh/Foliage", "Static_Mesh/Gangways", "Static_Mesh/Rock_Kit", "Candle", "SkySphere", "Water", "VFX"]
LEVEL_FOLDER = PACK + "/Levels/LevelInstances"


def r(v, n=1):
    return round(float(v), n)


def base_flags(material):
    try:
        base = material.get_base_material()
        return {"base": base.get_path_name(),
                "instanced": bool(base.get_editor_property("used_with_instanced_static_meshes")),
                "nanite": bool(base.get_editor_property("used_with_nanite"))}
    except Exception:  # noqa: BLE001
        return {"base": None, "instanced": None, "nanite": None}


def mesh_row(mesh):
    box = mesh.get_bounding_box()
    mats = []
    for slot in mesh.get_editor_property("static_materials"):
        m = slot.get_editor_property("material_interface")
        row = {"slot": str(slot.get_editor_property("material_slot_name")), "material": m.get_path_name() if m else None}
        if m:
            row.update(base_flags(m))
        mats.append(row)
    try:
        nanite = bool(mesh.get_editor_property("nanite_settings").get_editor_property("enabled"))
    except Exception:  # noqa: BLE001
        nanite = None
    try:
        verts = unreal.EditorStaticMeshLibrary.get_number_verts(mesh, 0)
    except Exception:  # noqa: BLE001
        verts = None
    collision = None
    try:
        body = mesh.get_editor_property("body_setup")
        if body:
            agg = body.get_editor_property("agg_geom")
            n = sum(len(agg.get_editor_property(k)) for k in ("box_elems", "sphyl_elems", "sphere_elems", "convex_elems"))
            collision = {"simple": n, "flag": str(body.get_editor_property("collision_trace_flag"))}
    except Exception:  # noqa: BLE001
        pass
    return {"size": [r(box.max.x - box.min.x), r(box.max.y - box.min.y), r(box.max.z - box.min.z)],
            "min": [r(box.min.x), r(box.min.y), r(box.min.z)], "nanite": nanite, "lods": mesh.get_num_lods(),
            "lod0Verts": verts, "collision": collision, "materials": mats}


def transform_row(t):
    loc, rot, scl = t.translation, t.rotation.rotator(), t.scale3d
    return {"t": [r(loc.x, 2), r(loc.y, 2), r(loc.z, 2)], "r": [r(rot.pitch, 3), r(rot.yaw, 3), r(rot.roll, 3)],
            "s": [r(scl.x, 4), r(scl.y, 4), r(scl.z, 4)]}


def overrides(comp, mesh):
    out = []
    try:
        count = comp.get_num_materials()
        for i in range(count):
            m = comp.get_material(i)
            base = mesh.get_material(i)
            out.append(m.get_path_name() if m and (not base or m.get_path_name() != base.get_path_name()) else None)
    except Exception:  # noqa: BLE001
        return []
    return out if any(out) else []


def inspect_level(path, subsystem):
    ok = unreal.EditorLoadingAndSavingUtils.load_map(path)
    if not ok:
        return {"error": "load failed"}
    pieces, lights, blueprints, others = [], [], [], {}
    for actor in subsystem.get_all_level_actors():
        cls = actor.get_class()
        cname = cls.get_name()
        if cname in ("WorldSettings", "Brush", "DefaultPhysicsVolume", "LevelBounds", "WorldDataLayers", "WorldPartitionMiniMap"):
            continue
        is_bp = cls.get_path_name().startswith("/Game/")
        if is_bp:
            blueprints.append({"class": cls.get_path_name(), **transform_row(actor.get_actor_transform())})
        for comp in actor.get_components_by_class(unreal.StaticMeshComponent):
            mesh = comp.get_editor_property("static_mesh")
            if not mesh:
                continue
            if not comp.is_visible() and not is_bp:
                continue
            ov = overrides(comp, mesh)
            if isinstance(comp, unreal.InstancedStaticMeshComponent):
                ct = comp.get_world_transform()
                for i in range(comp.get_instance_count()):
                    it = comp.get_instance_transform(i, True)
                    pieces.append({"mesh": mesh.get_path_name(), **transform_row(it), "mat": ov, "actor": actor.get_name(), "bp": is_bp})
            else:
                pieces.append({"mesh": mesh.get_path_name(), **transform_row(comp.get_world_transform()), "mat": ov,
                               "actor": actor.get_name(), "bp": is_bp})
        for comp in actor.get_components_by_class(unreal.PointLightComponent):
            loc = comp.get_world_location()
            c = comp.get_editor_property("light_color")
            lights.append({"t": [r(loc.x), r(loc.y), r(loc.z)], "intensity": r(comp.get_editor_property("intensity")),
                           "radius": r(comp.get_editor_property("attenuation_radius")), "color": [c.r, c.g, c.b]})
        if not is_bp and not actor.get_components_by_class(unreal.StaticMeshComponent):
            others[cname] = others.get(cname, 0) + 1
    return {"pieces": pieces, "lights": lights, "blueprints": blueprints, "otherActors": others}


def inspect_persistent(path, subsystem):
    """The pack's own town map: its sublevels, per-level bounds and actor classes, and the non-mesh actors (lights,
    sky, fog, water, volumes, Blueprints) with their locations."""
    if not unreal.EditorLoadingAndSavingUtils.load_map(path):
        return {"error": "load failed"}
    world = unreal.EditorLevelLibrary.get_editor_world()
    out = {"levels": {}, "special": []}
    try:
        out["streaming"] = [lv.get_outermost().get_name() for lv in unreal.EditorLevelUtils.get_levels(world)]
    except Exception as error:  # noqa: BLE001
        out["streaming"] = f"unavailable: {error}"
    try:
        out["worldPartition"] = bool(world.get_world_partition())
    except Exception:  # noqa: BLE001
        out["worldPartition"] = None
    for actor in subsystem.get_all_level_actors():
        level = actor.get_level()
        lname = level.get_outermost().get_name() if level else "?"
        row = out["levels"].setdefault(lname, {"actors": 0, "classes": {}, "min": [1e9] * 3, "max": [-1e9] * 3})
        cname = actor.get_class().get_name()
        row["actors"] += 1
        row["classes"][cname] = row["classes"].get(cname, 0) + 1
        try:
            origin, extent = actor.get_actor_bounds(False)
            if extent.x < 5e5 and (extent.x + extent.y + extent.z) > 0:
                for i, (o, e) in enumerate(((origin.x, extent.x), (origin.y, extent.y), (origin.z, extent.z))):
                    row["min"][i] = min(row["min"][i], r(o - e))
                    row["max"][i] = max(row["max"][i], r(o + e))
        except Exception:  # noqa: BLE001
            pass
        if cname not in ("StaticMeshActor",) and len(out["special"]) < 4000:
            loc = actor.get_actor_location()
            out["special"].append({"level": lname, "name": actor.get_name(), "class": actor.get_class().get_path_name(),
                                   "t": [r(loc.x), r(loc.y), r(loc.z)], "yaw": r(actor.get_actor_rotation().yaw)})
    return out


def main():
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    if "--persistent" in unreal.SystemLibrary.get_command_line():
        subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        out = {}
        for path in (PACK + "/Levels/Persistant/PL_CastleTown",):
            out[path] = inspect_persistent(path, subsystem)
        report = ROOT / "Saved/MedievalKingdom/Persistent.json"
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
        unreal.log(f"CIRE_MK_PERSISTENT_DONE report={report}")
        return
    meshes, other_assets = {}, {}
    for folder in MESH_FOLDERS:
        for data in reg.get_assets_by_path(f"{PACK}/{folder}", recursive=True):
            cls = str(data.asset_class_path.asset_name)
            path = f"{data.package_name}.{data.asset_name}"
            if cls == "StaticMesh":
                mesh = unreal.load_asset(path)
                if mesh:
                    meshes[path] = mesh_row(mesh)
            elif cls in ("Material", "MaterialInstanceConstant", "NiagaraSystem", "ParticleSystem", "Texture2D", "TextureCube", "Blueprint"):
                row = {"class": cls}
                if cls in ("Material", "MaterialInstanceConstant"):
                    m = unreal.load_asset(path)
                    if m:
                        row.update(base_flags(m))
                other_assets[path] = row
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    levels = {}
    for data in reg.get_assets_by_path(LEVEL_FOLDER, recursive=True):
        if str(data.asset_class_path.asset_name) != "World":
            continue
        path = str(data.package_name)
        try:
            levels[path] = inspect_level(path, subsystem)
        except Exception as error:  # noqa: BLE001
            levels[path] = {"error": str(error)}
        unreal.log(f"CIRE_MK_LEVEL {path} pieces={len(levels[path].get('pieces', []))}")
    out = {"meshes": meshes, "assets": other_assets, "levels": levels}
    report = ROOT / "Saved/MedievalKingdom/Inventory.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    unreal.log(f"CIRE_MK_INVENTORY_DONE meshes={len(meshes)} levels={len(levels)} report={report}")


main()
