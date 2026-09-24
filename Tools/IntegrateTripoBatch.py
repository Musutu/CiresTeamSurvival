"""Integrate explicitly bound Bridge imports without changing original assets.

Default: read-only preflight (ordinary Python supports manifest/file inspection;
UE Python also checks rigs/materials). --build writes only a fresh batch folder.
A separate --verify UE process validates saved assets and publishes ready runtime
bindings. This script never opens the Bridge, imports a download, or buys assets.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import sys
import uuid

try:
    import unreal
except ImportError:
    unreal = None

PROJECT = Path(__file__).resolve().parent.parent
OUTPUT_BASE = "/Game/Art/Characters/TripoBatch"
ORIGINAL_PROFILES = {"knight", "ranger", "scholar"}
ORIGINAL_FOLDERS = {"medieval_knight_armor_3d_model", "armored_archer_3d_model", "battlefield_healer_3d_model"}
CUSTOM_RIGS = {"bear", "whisp", "evergrove_centaur", "drakish_dragon"}
ROLE_PROFILES = {
    "Lancer": ["lancer"], "Paladin (Holy / Righteous shared body)": ["paladin_holy", "paladin_righteous"],
    "Dwarf Miner": ["dwarf_miner"], "Orc Chieftain": ["orc_chieftain"], "Wizard DPS": ["wizard"],
    "Keeper of the Light": ["keeper_of_light"], "Drakish Footman human form": ["drakish_footman"],
}
NAME_PROFILES = {"Iron Warden": ["knight"], "Ash Ranger": ["ranger"], "Veil Scholar": ["scholar"], "Rift Summoner": ["summoner"]}
POSES = {"Warden", "Ranger", "Scholar", "Lancer"}
REQUIRED_BONES = {"root", "pelvis", "spine_01", "spine_02", "spine_03", "head", "upperarm_l", "lowerarm_l", "hand_l",
                  "upperarm_r", "lowerarm_r", "hand_r", "foot_l", "foot_r", "ball_l", "ball_r"}


def require(value, message):
    if not value:
        raise RuntimeError(message)


def io_path(path):
    """Keep asset names intact while supporting non-long-path-aware UE Python."""
    path = Path(path).absolute()
    if os.name != "nt" or str(path).startswith("\\\\?\\"):
        return path
    value = str(path)
    return Path("\\\\?\\UNC\\" + value[2:] if value.startswith("\\\\") else "\\\\?\\" + value)


def read_json(path):
    path = io_path(path)
    require(path.is_file(), "Missing manifest: " + str(path))
    require(path.stat().st_size <= 2_000_000, "Manifest exceeds 2 MB: " + str(path))
    return json.loads(path.read_text(encoding="utf-8-sig"))


def canonical_uuid(value):
    require(isinstance(value, str), "UUID must be a string")
    parsed = str(uuid.UUID(value))
    require(parsed == value.lower(), "UUID must use canonical hyphenated form")
    return parsed


def package(value, folder=False):
    require(isinstance(value, str) and ".." not in value and "\\" not in value, "Invalid Unreal asset path")
    result = value.split(".")[0]
    require(re.fullmatch(r"/Game/[A-Za-z0-9_/-]+", result) is not None, "Expected an explicit /Game package path")
    require(not result.endswith("/"), "Unreal package paths cannot end with a slash")
    if folder:
        require("." not in value, "Import folder cannot be an object path")
    return result


def file_for(content, asset):
    return content / (package(asset)[len("/Game/"):] + ".uasset")


def path_of(obj):
    return str(obj.get_path_name()) if obj else None


def digest(path):
    result = hashlib.sha256()
    with io_path(path).open("rb") as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(data)
    return result.hexdigest()


def atomic_json(path, value):
    path = io_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def catalog(project):
    entries = {}
    files = [project / "Art/TripoHumanoidBatch.json", project / "Art/TripoCreatureBatch.json", project / "Art/TripoManifest.json"]
    for path in files:
        document = read_json(path)
        require(isinstance(document.get("assets"), list), "Manifest needs assets array: " + str(path))
        for asset in document["assets"]:
            raw = asset.get("uuid") or asset.get("asset_id") or asset.get("id")
            try:
                asset_id = canonical_uuid(raw)
            except (RuntimeError, ValueError, TypeError):
                continue
            if path.name == "TripoHumanoidBatch.json":
                profiles = ROLE_PROFILES.get(asset.get("role"), [])
            elif path.name == "TripoManifest.json":
                profiles = NAME_PROFILES.get(asset.get("name"), [])
            else:
                key = asset.get("id", "")
                profiles = ["troll_berserker_melee", "troll_berserker_ranged"] if key == "troll_berserker" else [key]
            if not profiles:
                continue
            require(asset_id not in entries, "UUID appears in more than one source manifest: " + asset_id)
            entries[asset_id] = {"uuid": asset_id, "profileIds": profiles, "sourceManifest": str(path.relative_to(project)),
                                 "rigKind": "custom" if set(profiles) & CUSTOM_RIGS else "humanoid",
                                 "protectedOriginal": bool(set(profiles) & ORIGINAL_PROFILES)}
    return entries


def load_bindings(path, entries):
    doc = read_json(path)
    require(doc.get("schemaVersion") == 1 and isinstance(doc.get("bindings"), list), "Expected binding schemaVersion 1 and bindings array")
    require(re.fullmatch(r"Batch[A-Za-z0-9_]{1,40}", doc.get("batch", "")) is not None, "Use a fresh batch name such as Batch01")
    result, folders, profiles, identifiers = [], set(), set(), set()
    for raw in doc["bindings"]:
        require(isinstance(raw, dict), "Import binding must be an object")
        identifier = canonical_uuid(raw.get("uuid"))
        require(identifier in entries, "UUID is not in the checked-in Tripo manifests: " + identifier)
        require(identifier not in identifiers, "Duplicate UUID binding: " + identifier)
        source = entries[identifier]
        require(not source["protectedOriginal"], "Original three champions must not be rebuilt by this batch")
        folder = package(raw.get("importFolder"), True)
        require(folder.startswith("/Game/TripoModels/") and len(folder.split("/")) == 4, "Bind one exact Bridge model folder under /Game/TripoModels")
        require(folder.rsplit("/", 1)[-1].lower() not in ORIGINAL_FOLDERS, "Refusing a protected original import folder")
        require(folder.lower() not in folders, "Different UUIDs cannot bind the same import folder")
        selected = raw.get("profileIds")
        require(isinstance(selected, list) and selected and all(isinstance(p, str) for p in selected), "profileIds must be a nonempty string array")
        require(len(selected) == len(set(selected)) and set(selected) <= set(source["profileIds"]), "Profiles do not match the source UUID's authored role")
        require(not profiles.intersection(selected), "A runtime profile cannot have two import bindings")
        mesh = package(raw["mesh"]) if raw.get("mesh") else None
        require(mesh is None or mesh.startswith(folder + "/"), "Explicit mesh must live inside its bound UUID import folder")
        pose = raw.get("attackPose", "Lancer" if "lancer" in selected else "Scholar" if any(p in selected for p in ("summoner", "wizard", "keeper_of_light", "ether_golem_support", "dryad")) else "Warden")
        require(pose in POSES, "attackPose must name a supported prototype articulation: " + ", ".join(sorted(POSES)))
        height = raw.get("heightCm", 182 if "lancer" in selected else 176)
        require(isinstance(height, (int, float)) and not isinstance(height, bool) and math.isfinite(height) and 80 <= height <= 400, "heightCm must be finite, 80..400")
        row = {**source, "importFolder": folder, "mesh": mesh, "profileIds": selected, "attackPose": pose,
               "heightCm": float(height), "key": selected[0], "rigKind": source["rigKind"]}
        result.append(row); folders.add(folder.lower()); profiles.update(selected); identifiers.add(identifier)
    return doc["batch"], result


def selection(entries, rows, requested, all_humanoids):
    if requested:
        desired = set(requested)
    elif all_humanoids:
        desired = {p for item in entries.values() if not item["protectedOriginal"] and item["rigKind"] == "humanoid" for p in item["profileIds"]}
    else:
        desired = {"lancer", "summoner"}
    known = {p for item in entries.values() for p in item["profileIds"]}
    require(desired and desired <= known and not desired.intersection(ORIGINAL_PROFILES), "Unknown or protected requested profile")
    found = {p for row in rows for p in row["profileIds"]}
    errors = ["Missing explicit UUID/importFolder binding for profile " + p for p in sorted(desired - found)]
    chosen = [row for row in rows if desired.intersection(row["profileIds"])]
    chosen.sort(key=lambda row: (0 if "lancer" in row["profileIds"] else 1 if "summoner" in row["profileIds"] else 2, row["key"]))
    return chosen, errors


def helpers():
    if str(PROJECT / "Tools") not in sys.path:
        sys.path.insert(0, str(PROJECT / "Tools"))
    return tuple(importlib.import_module(name) for name in ("RetargetTripo", "RepairTripoAnimationScale", "BuildAttackAnimations"))


def material_plan(mesh, folder, registry):
    candidates = []
    for data in registry.get_assets_by_path(folder + "/Textures", recursive=True):
        if str(data.asset_name).lower().endswith("_basecolor"):
            texture = data.get_asset()
            if isinstance(texture, unreal.Texture2D) and texture.get_editor_property("srgb"):
                candidates.append(texture)
    plans = []
    for index, slot in enumerate(mesh.get_editor_property("materials")):
        material = slot.get_editor_property("material_interface")
        require(material is not None, "Imported mesh has an empty material slot")
        row = {"slot": index, "material": path_of(material), "repair": False}
        if isinstance(material, unreal.MaterialInstanceConstant):
            names = {str(name) for name in unreal.MaterialEditingLibrary.get_texture_parameter_names(material)}
            if "BaseColorTex" in names:
                color = unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(material, "BaseColorTex")
                missing = color is None or "T_Tripo_Default_White" in path_of(color)
                if missing:
                    require(len(candidates) == 1, f"Missing BaseColorTex in {path_of(material)} requires exactly one sRGB *_BaseColor candidate; found {len(candidates)}")
                    row.update(repair=True, color=path_of(candidates[0]), previousColor=path_of(color))
        plans.append(row)
    require(plans, "Imported mesh has no PBR material slots")
    return plans


def engine_preflight(row, modules):
    rt, repair, attack = modules
    require(row["rigKind"] == "humanoid", "Custom creature rig cannot use the Manny humanoid retarget pipeline")
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    meshes = [data.get_asset() for data in registry.get_assets_by_path(row["importFolder"], recursive=True) if str(data.asset_class_path.asset_name) == "SkeletalMesh"]
    mesh = unreal.load_asset(row["mesh"]) if row["mesh"] else meshes[0] if len(meshes) == 1 else None
    require(isinstance(mesh, unreal.SkeletalMesh), f"Expected exactly one bound SkeletalMesh or explicit mesh; found {len(meshes)} in {row['importFolder']}")
    require(package(path_of(mesh)).startswith(row["importFolder"] + "/"), "Resolved mesh escaped its explicit UUID folder")
    info = rt.inspect_mesh(mesh)
    names = set(info["parents"])
    require(REQUIRED_BONES <= names, "Required articulated bones missing: " + ", ".join(sorted(REQUIRED_BONES - names)))
    require(20 < info["height_cm"] < 1000 and mesh.get_editor_property("skeleton"), "Missing skeleton or implausible imported bounds")
    require(info["parents"]["root"] in ("None", "", "none"), "Expected one root bone at the hierarchy origin")
    children = [name for name, parent in info["parents"].items() if parent == "root"]
    require(children == ["pelvis"], "Review nonstandard direct-root hierarchy before scale repair")
    source = rt.load_checked(rt.SOURCE, unreal.SkeletalMesh)
    source_rig, target_rig, retargeter = unreal.IKRigDefinition(), unreal.IKRigDefinition(), unreal.IKRetargeter()
    rt.configure_rig(source_rig, source); rt.configure_rig(target_rig, mesh)
    mapping = rt.configure_retargeter(retargeter, source_rig, target_rig, source, mesh)
    materials = material_plan(mesh, row["importFolder"], registry)
    return {**row, "mesh": path_of(mesh), "inspection": info, "directRootChildren": children, "materials": materials, "retarget": mapping}


def original_hashes(content, exclude):
    content = io_path(content)
    return {str(path.relative_to(content)): digest(path) for path in content.rglob("*")
            if path.suffix.lower() in (".uasset", ".uexp", ".ubulk") and not str(path.relative_to(content)).replace("\\", "/").startswith(exclude + "/")}


def validate_hashes(content, hashes):
    content = io_path(content)
    for relative, before in hashes.items():
        path = content / relative
        require(path.is_file() and digest(path) == before, "Protected preexisting asset changed: " + relative)


def duplicate(source, destination):
    require(not unreal.EditorAssetLibrary.does_asset_exist(destination), "Refusing to overwrite " + destination)
    value = unreal.EditorAssetLibrary.duplicate_asset(source, destination)
    require(value is not None, "Failed to create new asset " + destination)
    return value


def save(asset, output):
    require(path_of(asset).startswith(output + "/"), "Refusing to save outside fresh batch")
    require(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False), "Failed to save " + path_of(asset))


def copy_body(project, row, output):
    directory = output + "/Bodies/" + row["key"]
    mesh = duplicate(row["mesh"], directory + "/SK_" + row["key"])
    slots = list(mesh.get_editor_property("materials")); repaired = []
    for item in row["materials"]:
        if not item["repair"]:
            continue
        source_file = file_for(project / "Content", item["material"])
        require(io_path(source_file).is_file(), "Cannot retain source material backup: " + str(source_file))
        backup = project / "Art/Imports/TripoBatchMaterialBackups" / row["uuid"] / source_file.name
        io_path(backup.parent).mkdir(parents=True, exist_ok=True)
        if io_path(backup).exists():
            require(digest(backup) == digest(source_file), "An existing material backup differs; refusing to replace it")
        else:
            shutil.copy2(io_path(source_file), io_path(backup))
        material = duplicate(item["material"], directory + f"/MI_{row['key']}_{item['slot']}")
        texture = unreal.load_asset(item["color"])
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(material, "BaseColorTex", texture)
        unreal.MaterialEditingLibrary.update_material_instance(material)
        require(unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(material, "BaseColorTex") == texture, "Copied material color assignment failed")
        save(material, output)
        slots[item["slot"]].set_editor_property("material_interface", material)
        repaired.append({**item, "copy": path_of(material), "backup": str(backup)})
    mesh.set_editor_property("materials", slots); save(mesh, output)
    return mesh, repaired


def validate_clip(clip, mesh, repair, expected_scale, original=None):
    require(clip.get_editor_property("skeleton") == mesh.get_editor_property("skeleton"), "Animation skeleton mismatch")
    duration = float(clip.get_play_length()); require(math.isfinite(duration) and duration > 0, "Empty animation")
    model = clip.get_editor_property("data_model_interface")
    require(model is not None, "Saved animation data model missing")
    root_tracks = repair.raw_tracks(clip, model, mesh, ["root"])
    require(all(max(abs(a-b) for a,b in zip(repair.vector(scale), expected_scale)) < .01 for scale in root_tracks["root"][2]), "A saved root key lost its reference scale")
    reference = repair.get_pose(clip, 0, mesh, "RAW", True, False)
    ref_span = repair.bone(reference, "head", True).translation.z - min(repair.bone(reference, b, True).translation.z for b in ("foot_l", "foot_r"))
    require(ref_span > 10 and math.isfinite(ref_span), "Reference body has invalid vertical span")
    results = []
    for time in (0., min(.25, duration*.5), duration*.5):
        for incorporate in (False, True):
            poses = {}
            for mode in ("RAW", "COMPRESSED"):
                pose = repair.get_pose(clip, time, mesh, mode, incorporate, False)
                root = repair.bone(pose, "root")
                require(max(abs(a-b) for a,b in zip(repair.vector(root.scale3d), expected_scale)) < .01, "Animation lost the imported root scale")
                values = {b: repair.vector(repair.bone(pose, b).translation) for b in repair.BONES}
                limit = max(500., float(mesh.get_imported_bounds().box_extent.z)*8)
                require(all(math.isfinite(v) and abs(v) < limit for p in values.values() for v in p), "Animation has nonfinite or exploded component positions")
                span = values["head"][2]-min(values["foot_l"][2], values["foot_r"][2])
                require(.45*ref_span < span < 1.5*ref_span, "Animation body proportions diverge from its own reference rig")
                if original:
                    old_pose = repair.get_pose(original, time, mesh, "RAW", True, False)
                    old_local = repair.vector(repair.bone(old_pose, "pelvis", local=True).translation)
                    old_scale = repair.vector(repair.bone(old_pose, "root", local=True).scale3d)
                    expected_local = unreal.Vector(*[v*a/b for v,a,b in zip(old_local, old_scale, expected_scale)])
                    expected = repair.vector(unreal.MathLibrary.transform_location(root, expected_local))
                    require(math.dist(values["pelvis"], expected) < (.1 if mode == "RAW" else 2.5), "Scale repair changed the authored pelvis trajectory")
                poses[mode] = values
                results.append({"time": time, "mode": mode, "incorporateRootMotion": incorporate, "headFeetSpan": span})
            require(math.dist(poses["RAW"]["pelvis"], poses["COMPRESSED"]["pelvis"]) < 2.5, "Compressed pelvis differs from raw pose")
    return results


def repair_locomotion(row, mesh, raw_root, output, repair):
    name = row["key"]; original_blend = unreal.load_asset(f"{raw_root}/{name}/Animations/BS_Idle_Walk_Run_{name}")
    require(isinstance(original_blend, unreal.BlendSpace), "Missing retargeted directional BlendSpace")
    originals = {path_of(sample.get_editor_property("animation")): sample.get_editor_property("animation") for sample in original_blend.get_editor_property("sample_data")}
    require(len(originals) == 17, "Expected the existing 17-clip locomotion set")
    reference = repair.get_pose(next(iter(originals.values())), 0, mesh, "RAW", True, False)
    root_scale = repair.vector(repair.bone(reference, "root", True).scale3d)
    require(all(math.isfinite(s) and s > 0 for s in root_scale) and max(root_scale)-min(root_scale) < .001, "Imported root scale must be finite, positive, uniform")
    require(abs(root_scale[0]-100) < .01 or abs(root_scale[0]-1) < .001, "Unrecognized imported root scale; review required")
    mapping, validations = {}, []
    for source, original in sorted(originals.items()):
        destination = package(source).replace(raw_root, output + "/Locomotion", 1)
        clip = duplicate(source, destination)
        initial = repair.vector(repair.bone(repair.get_pose(clip, 0, mesh, "RAW", True, False), "root", local=True).scale3d)
        if max(abs(a-b) for a,b in zip(initial, root_scale)) >= .001:
            require(max(abs(s-1) for s in initial) < .001, "Retarget root differs from both unit scale and reference scale")
            repair.repair_clip(clip, root_scale, row["directRootChildren"], mesh)
        clip.set_editor_property("enable_root_motion", False); clip.set_editor_property("force_root_lock", True); save(clip, output)
        mapping[source] = clip
        validations.append({"asset": path_of(clip), "rawSource": source, "samples": validate_clip(clip, mesh, repair, root_scale, original)})
    blend = duplicate(path_of(original_blend), f"{output}/Locomotion/{name}/Animations/BS_Idle_Walk_Run_{name}")
    samples = list(blend.get_editor_property("sample_data"))
    for sample in samples:
        sample.set_editor_property("animation", mapping[path_of(sample.get_editor_property("animation"))])
    blend.set_editor_property("sample_data", samples); save(blend, output)
    idle = next((clip for clip in mapping.values() if clip.get_name() == "MM_Idle_" + name), None)
    require(idle is not None, "Repaired idle missing")
    return blend, idle, root_scale, validations


def make_attack(row, mesh, idle, output, attack):
    component = unreal.SkeletalMeshComponent(); component.set_skeletal_mesh_asset(mesh)
    names = [str(component.get_bone_name(i)) for i in range(component.get_num_bones())]
    parents = {name: str(component.get_parent_bone(name)) for name in names}
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(idle, 0, attack.options(mesh))
    base, reference = {}, {}
    for name in names:
        value = unreal.AnimPoseExtensions.get_bone_pose(pose, name, unreal.AnimPoseSpaces.LOCAL)
        base[name] = (attack.v(value.translation), attack.q(value.rotation), attack.v(value.scale3d))
        value = unreal.AnimPoseExtensions.get_ref_bone_pose(pose, name, unreal.AnimPoseSpaces.WORLD)
        reference[name] = (attack.v(value.translation), attack.q(value.rotation), attack.v(value.scale3d))
    height = float(mesh.get_imported_bounds().box_extent.z*2)
    poses = attack.authored_poses(row["attackPose"], base, parents, reference, height)
    clip = duplicate(path_of(idle), f"{output}/Attacks/{row['key']}/A_{row['key']}_Attack")
    controller = clip.get_editor_property("controller"); require(controller is not None, "Attack data controller missing")
    controller.open_bracket("Author new Tripo batch prototype attack", False)
    try:
        controller.set_frame_rate(unreal.FrameRate(attack.RATE, 1), False)
        controller.set_number_of_frames(unreal.FrameNumber(attack.FRAMES), False)
        keys = [attack.sample(poses, i/attack.FRAMES*.65) for i in range(attack.FRAMES+1)]
        for bone in names:
            require(controller.set_bone_track_keys(bone, [unreal.Vector(*frame[bone][0]) for frame in keys],
                    [unreal.Quat(*frame[bone][1]) for frame in keys], [unreal.Vector(*frame[bone][2]) for frame in keys], False), "Attack track failed: " + bone)
    finally:
        controller.close_bracket(False)
    clip.set_editor_property("enable_root_motion", False); clip.set_editor_property("force_root_lock", True); save(clip, output)
    return clip


def validate_attack(clip, mesh, repair, scale):
    samples = validate_clip(clip, mesh, repair, scale)
    require(abs(clip.get_play_length()-40/60) < .001, "Unexpected attack storage duration")
    for mode in ("RAW", "COMPRESSED"):
        hands = []
        for phase in (.18, .25):
            pose = repair.get_pose(clip, phase/.65*clip.get_play_length(), mesh, mode, True, False)
            hands.append(repair.vector(repair.bone(pose, "hand_r").translation))
        require(math.dist(*hands) > max(1., mesh.get_imported_bounds().box_extent.z*.025), "Attack lacks articulated windup/release")
    return samples


def publish(project, rows, report):
    path = project / "Content/Data/ChampionArtBindings.json"
    current = read_json(path) if path.exists() else {"schemaVersion": 1, "bindings": []}
    require(current.get("schemaVersion") == 1 and isinstance(current.get("bindings"), list), "Existing runtime bindings have an incompatible schema")
    replacing = {row["profileId"] for row in rows}
    require(not replacing.intersection(ORIGINAL_PROFILES), "Refusing to replace original champion bindings")
    merged = [row for row in current["bindings"] if row.get("profileId") not in replacing] + rows
    if current["bindings"] == merged:
        return  # Recover cleanly if publication succeeded before report saving.
    if path.exists():
        backup = project / "Saved/TripoBatchIntegration" / (report["batch"] + "_bindings_before.json")
        if backup.exists():
            require(backup.read_bytes() == path.read_bytes(), "Prior binding backup already exists with different contents")
        else:
            backup.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(path, backup)
    current["bindings"] = merged
    atomic_json(path, current)


def verify(project, batch, modules):
    rt, repair, attack = modules
    report_path = project / "Saved/TripoBatchIntegration" / (batch + ".json")
    report = read_json(report_path)
    require(report.get("status") in ("built_needs_reload_validation", "ready") and report.get("batch") == batch, "No successful batch build to verify")
    validate_hashes(project / "Content", report["protectedHashes"])
    bindings = []
    for row in report["built"]:
        mesh = rt.load_checked(row["mesh"], unreal.SkeletalMesh)
        blend = rt.load_checked(row["locomotion"], unreal.BlendSpace)
        clip = rt.load_checked(row["attack"], unreal.AnimSequence)
        require(blend.get_editor_property("skeleton") == mesh.get_editor_property("skeleton"), "Saved blend skeleton mismatch")
        samples = blend.get_editor_property("sample_data")
        paths = {path_of(sample.get_editor_property("animation")) for sample in samples}
        require(len(paths) == 17 and all(p.startswith(report["output"] + "/Locomotion/") for p in paths), "Saved BlendSpace lost repaired dependencies")
        for item in row["animations"]:
            animation = rt.load_checked(item["asset"], unreal.AnimSequence)
            raw = rt.load_checked(item["rawSource"], unreal.AnimSequence)
            validate_clip(animation, mesh, repair, row["rootScale"], raw)
        validate_attack(clip, mesh, repair, row["rootScale"])
        for material in row["materialRepairs"]:
            instance = rt.load_checked(material["copy"], unreal.MaterialInstanceConstant)
            require(path_of(unreal.MaterialEditingLibrary.get_material_instance_texture_parameter_value(instance, "BaseColorTex")) == material["color"], "Saved copied BaseColorTex did not persist")
        for profile in row["profileIds"]:
            bindings.append({"profileId": profile, "mesh": row["mesh"], "locomotion": row["locomotion"], "attack": row["attack"], "heightCm": row["heightCm"], "status": "ready"})
    validate_hashes(project / "Content", report["protectedHashes"])
    require(bindings, "Cannot publish an empty batch")
    # Ready denotes validated saved assets; final visual approval remains explicit.
    if report["status"] != "ready":
        publish(project, bindings, report)
    report.update(status="ready", publishedBindings=bindings, verifiedUtc=datetime.now(timezone.utc).isoformat(), visualReviewAccepted=False)
    atomic_json(report_path, report)
    unreal.log("CIRE_TRIPO_BATCH_VERIFY_PASS batch=" + batch + " profiles=" + str(len(bindings)))
    return {k: v for k, v in report.items() if k not in ("protectedHashes", "built")}


def run(args):
    project = args.project.resolve(); entries = catalog(project)
    binding_path = args.bindings or project / "Art/TripoImportBindings.json"
    report = {"mode": "verify" if args.verify else "build" if args.build else "preflight", "createdUtc": datetime.now(timezone.utc).isoformat(),
              "importBindings": str(binding_path), "assetMutation": False, "catalog": list(entries.values()), "selected": [], "errors": []}
    try:
        if args.verify:
            require(unreal is not None, "Saved asset verification requires UE Python")
            return verify(project, args.batch or "Batch01", helpers())
        batch, rows = load_bindings(binding_path, entries); batch = args.batch or batch
        require(re.fullmatch(r"Batch[A-Za-z0-9_]{1,40}", batch) is not None, "Invalid fresh batch label")
        report.update(batch=batch, output=OUTPUT_BASE + "/" + batch)
        chosen, errors = selection(entries, rows, args.profile, args.all_humanoids); report["errors"].extend(errors)
        modules = helpers() if unreal else None
        if unreal:
            unreal.AssetRegistryHelpers.get_asset_registry().search_all_assets(synchronous_search=True)
        for row in chosen:
            try:
                require(row["rigKind"] == "humanoid", "Custom-rig creature is explicitly deferred: " + row["key"])
                directory = project / "Content" / row["importFolder"][len("/Game/"):]
                require(directory.is_dir() and any(directory.rglob("*.uasset")), "Bridge import folder is not saved locally: " + row["importFolder"])
                if row["mesh"]:
                    require(file_for(project / "Content", row["mesh"]).is_file(), "Explicit mesh package missing: " + row["mesh"])
                checked = engine_preflight(row, modules) if unreal else {**row, "status": "files_present_engine_validation_pending"}
                report["selected"].append(checked)
            except Exception as error:
                report["errors"].append(row["key"] + ": " + str(error))
        output_dir = project / "Content" / report["output"][len("/Game/"):]
        if output_dir.exists():
            report["errors"].append("Fresh batch output already exists; choose a new batch label: " + report["output"])
        require(not report["errors"], "Preflight found " + str(len(report["errors"])) + " blocking issue(s)")
        report["status"] = "preflight_passed" if unreal else "files_present_engine_validation_pending"
        if not args.build:
            if unreal:
                unreal.log("CIRE_TRIPO_BATCH_PREFLIGHT_PASS bodies=" + str(len(report["selected"])))
            return report
        require(unreal is not None, "Asset build requires UE Python")
        rt, repair, attack = modules; output = report["output"]
        report["protectedHashes"] = original_hashes(project / "Content", output[len("/Game/"):])
        report["assetMutation"] = True; report["built"] = []; bodies = {}
        for row in report["selected"]:
            mesh, materials = copy_body(project, row, output); bodies[row["key"]] = (mesh, materials)
        old_targets, old_root = rt.TARGETS, rt.OUTPUT_ROOT
        try:
            rt.TARGETS = {name: path_of(value[0]) for name, value in bodies.items()}; rt.OUTPUT_ROOT = output
            rt.run(build=True, output_label="RetargetRaw", characters=list(bodies), locomotion_set=True)
        finally:
            rt.TARGETS, rt.OUTPUT_ROOT = old_targets, old_root
        for row in report["selected"]:
            mesh, materials = bodies[row["key"]]
            blend, idle, scale, animations = repair_locomotion(row, mesh, output + "/RetargetRaw", output, repair)
            clip = make_attack(row, mesh, idle, output, attack); validate_attack(clip, mesh, repair, scale)
            report["built"].append({"uuid": row["uuid"], "profileIds": row["profileIds"], "mesh": path_of(mesh), "locomotion": path_of(blend), "attack": path_of(clip),
                                   "heightCm": row["heightCm"], "rootScale": scale, "animations": animations, "materialRepairs": materials,
                                   "attackPose": row["attackPose"], "animationArtStatus": "local articulated prototype", "visualReviewAccepted": False})
        validate_hashes(project / "Content", report["protectedHashes"])
        report["status"] = "built_needs_reload_validation"
        unreal.log("CIRE_TRIPO_BATCH_BUILD_PASS batch=" + batch + " runtime_bindings_published=0")
        return report
    except Exception as error:
        report["status"] = "failed"; report["errors"].append(str(error))
        return report
    finally:
        if args.build and report.get("batch") and report["assetMutation"]:
            atomic_json(project / "Saved/TripoBatchIntegration" / (report["batch"] + ".json"), report)


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(); mode.add_argument("--build", action="store_true"); mode.add_argument("--verify", action="store_true")
    parser.add_argument("--project", type=Path, default=PROJECT)
    parser.add_argument("--bindings", type=Path)
    parser.add_argument("--batch")
    parser.add_argument("--profile", action="append", default=[])
    parser.add_argument("--all-humanoids", action="store_true")
    parser.add_argument("--report", type=Path, help="Optional diagnostic report; default preflight only prints JSON")
    args = parser.parse_args()
    if unreal:
        command = unreal.SystemLibrary.get_command_line()
        flags = set(command.lower().split())
        args.build |= "-ciretripobatchbuild" in flags; args.verify |= "-ciretripobatchverify" in flags
        args.all_humanoids |= "-ciretripobatchallhumanoids" in flags
        for flag, name in (("Bindings", "bindings"), ("Label", "batch"), ("Report", "report")):
            match = re.search(r"(?:^|\s)-CireTripoBatch" + flag + r'=(?:"([^"]+)"|(\S+))', command, re.I)
            if match:
                value = match.group(1) or match.group(2); setattr(args, name, Path(value) if name in ("bindings", "report") else value)
    require(not (args.build and args.verify), "Build and verify need separate UE processes")
    return args


if __name__ == "__main__":
    options = arguments(); result = run(options)
    if options.report:
        atomic_json(options.report, result)
    summary = {k: v for k, v in result.items() if k not in ("protectedHashes", "built")}
    print(json.dumps(summary, indent=2))
    if result.get("status") == "failed":
        if unreal:
            raise RuntimeError("CIRE_TRIPO_BATCH_FAIL: " + "; ".join(result.get("errors", [])))
        raise SystemExit(1)
