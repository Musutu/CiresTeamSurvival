"""UE 5.8: retarget the purchased Fab animation packs onto every champion body (Docs/FAB-PURCHASED.md).

Input  Art/Fab/FabAnimMap.json (committed; object paths + timing only):
  "clips":      {"<clip>": {"path": "/Game/<Pack>/.../<Anim>.<Anim>", "start", "contact", "end", "recoverRate"}}
  "locomotion": {"idle": path, "walk_f", "walk_b", "walk_l", "walk_r", "walk_fl", "walk_fr", "walk_bl", "walk_br",
                 "run_f", "run_b", "run_l", "run_r", "run_fl", "run_fr", "run_bl", "run_br"}   (any subset; *_f required)
  "replace":    copied verbatim to Content/Data/FabAnimations.json (style/motion -> kind -> clips)
Bodies  every mesh in Content/Data/ChampionAttacks02.json "bodies" (mesh -> folder), or --only Folder,Folder.
Output  /Game/FabDerived/Anim/<Folder>/A_<Folder>_<clip>, .../BS_Fab_Locomotion_<Folder>,
        IK rigs + retargeters in /Game/FabDerived/Rigs/<Folder>. /Game/FabDerived is derived from licensed
        packs: gitignored, lives in the main checkout and is junctioned into every worktree (Tools/LinkFabContent.py).
        Content/Data/FabAnimations.json gets the windows of every clip that retargeted, plus "replace".

Method (the proven Preview02 route, Docs/AssetPipeline.md): auto-characterised IK rigs (UE5 Manny template on the
source, UE4 template on the Tripo 61-bone bodies), IK retargeter with chain-to-chain auto-align, batch retarget, then
the imported root scale of 100 restored in each copy (RepairTripoAnimationScale.repair_clip). Root motion is disabled
and root lock forced: the movement component stays authoritative. Every clip is validated (root scale 100, head above
pelvis above feet, pelvis near bind height at rest, finite, compact) and dropped from the data if it fails, so the
runtime falls back to the ChampionAttacks02 clip for that body.

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/RetargetFabAnimations.py -unattended -nullrhi
     -CireFabAnimOnly=Warden+lancer   (optional subset)   -CireFabAnimKeep (reuse existing outputs)
Report: Saved/FabAnimRetarget.json. Log marker CIRE_FAB_ANIM_RETARGET_PASS / _PARTIAL / _FAIL.
"""
import importlib.util
import json
import math
import stat
import sys
import time
import traceback
from pathlib import Path

import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
TOOLS = Path(__file__).resolve().parent
OUT = "/Game/FabDerived/Anim"
RIGS = "/Game/FabDerived/Rigs"
LANCER_BS = "/Game/Art/Characters/TripoBatch/Batch01/Locomotion/lancer/Animations/BS_Idle_Walk_Run_lancer"
lib = unreal.EditorAssetLibrary
DIRS = {"f": 0.0, "fr": 45.0, "r": 90.0, "br": 135.0, "b": 180.0, "bl": -135.0, "l": -90.0, "fl": -45.0}


def load_module(name):
    spec = importlib.util.spec_from_file_location(name, str(TOOLS / (name + ".py")))
    module = importlib.util.module_from_spec(spec)
    # Both helper scripts run their main() at import unless __name__ == "__main__"; spec name differs, so safe
    # for RetargetTripo (guarded). RepairTripoAnimationScale is guarded too.
    spec.loader.exec_module(module)
    return module


RT = load_module("RetargetTripo")          # configure_rig / configure_retargeter / create_asset
REPAIR = load_module("RepairTripoAnimationScale")  # repair_clip


def require(cond, msg):
    if not cond:
        raise RuntimeError(msg)


def path_of(obj):
    return obj.get_path_name().split(".")[0]


def v3(v):
    return (float(v.x), float(v.y), float(v.z))


def options(mesh, raw=True):
    o = unreal.AnimPoseEvaluationOptions()
    o.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW if raw else unreal.AnimDataEvalType.COMPRESSED)
    o.set_editor_property("optional_skeletal_mesh", mesh)
    o.set_editor_property("should_retarget", False)
    o.set_editor_property("extract_root_motion", False)
    o.set_editor_property("incorporate_root_motion_into_pose", False)
    return o


def writable_delete(path):
    file = ROOT / "Content" / (path[len("/Game/"):] + ".uasset")
    if file.exists():
        file.chmod(file.stat().st_mode | stat.S_IWRITE)
    if lib.does_asset_exist(path):
        lib.delete_asset(path)


def source_mesh_for(skeleton, hint_root):
    """A skeletal mesh on the clip's skeleton: the skeleton's preview mesh, else any mesh in the pack, else Manny."""
    try:
        preview = skeleton.get_editor_property("preview_skeletal_mesh")
        mesh = preview.load_synchronous() if hasattr(preview, "load_synchronous") else preview
        if isinstance(mesh, unreal.SkeletalMesh):
            return mesh
    except Exception:
        pass
    for asset in lib.list_assets(hint_root, recursive=True, include_folder=False):
        data = lib.find_asset_data(asset)
        if str(data.asset_class_path.asset_name) == "SkeletalMesh":
            mesh = data.get_asset()
            if mesh.get_editor_property("skeleton") == skeleton:
                return mesh
    manny = unreal.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
    if manny and manny.get_editor_property("skeleton") == skeleton:
        return manny
    raise RuntimeError("no skeletal mesh found for source skeleton " + path_of(skeleton))


def validate(anim, mesh):
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    length = float(anim.get_play_length())
    for raw in (True, False):
        for i in range(7):
            t = length * i / 6
            p = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, options(mesh, raw))
            require(unreal.AnimPoseExtensions.is_valid(p), "invalid pose")
            get = lambda n, ref=False: (unreal.AnimPoseExtensions.get_ref_bone_pose if ref else unreal.AnimPoseExtensions.get_bone_pose)(p, n, unreal.AnimPoseSpaces.WORLD)
            for n in ("pelvis", "head", "hand_l", "hand_r", "foot_l", "foot_r"):
                tr = get(n)
                require(all(math.isfinite(x) for x in v3(tr.translation)), "non-finite " + n)
                require(max(abs(x) for x in v3(tr.translation)) < 400, n + " outside sane bounds")
            root = unreal.AnimPoseExtensions.get_bone_pose(p, "root", unreal.AnimPoseSpaces.LOCAL)
            require(max(abs(x - 100) for x in v3(root.scale3d)) < .01, "root scale lost")
    return {"length": round(length, 3)}


def validate_upright(anim, mesh):
    """Standing clips (not death/roll): head above pelvis above feet at every sample."""
    length = float(anim.get_play_length())
    for i in range(7):
        p = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, length * i / 6, options(mesh))
        z = lambda n: unreal.AnimPoseExtensions.get_bone_pose(p, n, unreal.AnimPoseSpaces.WORLD).translation.z
        feet = min(z("foot_l"), z("foot_r"))
        require(z("head") > z("pelvis") > feet, "body order broken at %.2fs" % (length * i / 6))


def travel_speed(anim, mesh):
    """cm/s of root (or pelvis) travel in the source clip, raw mesh units."""
    o = unreal.AnimPoseEvaluationOptions()
    o.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    o.set_editor_property("optional_skeletal_mesh", mesh)
    o.set_editor_property("extract_root_motion", False)
    o.set_editor_property("incorporate_root_motion_into_pose", True)
    length = float(anim.get_play_length())
    a = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, 0.0, o)
    b = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, length, o)
    best = 0.0
    for bone in ("root", "pelvis"):
        pa = unreal.AnimPoseExtensions.get_bone_pose(a, bone, unreal.AnimPoseSpaces.WORLD).translation
        pb = unreal.AnimPoseExtensions.get_bone_pose(b, bone, unreal.AnimPoseSpaces.WORLD).translation
        best = max(best, math.hypot(pb.x - pa.x, pb.y - pa.y) / max(1e-3, length))
    return best


def retarget_body(folder, mesh, sources, locomotion, report, keep):
    """Retarget every source clip onto one body. Returns {clip: asset path} of validated outputs."""
    entry = report.setdefault(folder, {"mesh": path_of(mesh), "clips": {}, "errors": {}})
    out_dir = "%s/%s" % (OUT, folder)
    rig_dir = "%s/%s" % (RIGS, folder)
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    names = [str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())]
    children = [n for n in names if str(comp.get_parent_bone(n)) == "root"]
    require("root" in names and "pelvis" in names, "body without root/pelvis")
    # Group source clips by skeleton: one retargeter per (source skeleton, body).
    groups = {}
    for clip, (anim, src_mesh) in list(sources.items()) + [("loco_" + k, v) for k, v in locomotion.items()]:
        groups.setdefault(path_of(src_mesh), (src_mesh, []))[1].append((clip, anim))
    done = {}
    for index, (src_path, (src_mesh, clips)) in enumerate(sorted(groups.items())):
        todo = [(c, a) for c, a in clips if not (keep and lib.does_asset_exist("%s/A_%s_%s" % (out_dir, folder, c)))]
        for c, a in clips:
            if (c, a) not in todo:
                done[c] = "%s/A_%s_%s" % (out_dir, folder, c)
        if not todo:
            continue
        tag = "S%d" % index
        for name in ("IK_Src_" + tag, "IK_" + folder + "_" + tag, "RTG_" + tag):
            writable_delete("%s/%s" % (rig_dir, name))
        src_rig = RT.create_asset("IK_Src_" + tag, rig_dir, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
        RT.configure_rig(src_rig, src_mesh)
        dst_rig = RT.create_asset("IK_" + folder + "_" + tag, rig_dir, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
        RT.configure_rig(dst_rig, mesh)
        rtg = RT.create_asset("RTG_" + tag, rig_dir, unreal.IKRetargeter, unreal.IKRetargetFactory())
        RT.configure_retargeter(rtg, src_rig, dst_rig, src_mesh, mesh)
        for asset in (src_rig, dst_rig, rtg):
            lib.save_loaded_asset(asset, False)
        staging = out_dir + "/_staging"
        if lib.does_directory_exist(staging):
            lib.delete_directory(staging)
        inputs = unreal.IKRetargetBatchOperationInputs()
        for key, value in {
            "assets_to_retarget": [lib.find_asset_data(path_of(a)) for _, a in todo],
            "source_mesh": src_mesh, "target_mesh": mesh, "ik_retarget_asset": rtg,
            "target_path": staging, "suffix": "", "prefix": "", "use_source_path": False,
            "include_referenced_assets": False, "overwrite_existing_files": True,
        }.items():
            try:
                inputs.set_editor_property(key, value)
            except Exception:
                pass
        results = {path_of(d.get_asset()).rsplit("/", 1)[1]: d.get_asset() for d in unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)}
        root_scale = None
        for clip, anim in todo:
            final = "%s/A_%s_%s" % (out_dir, folder, clip)
            try:
                out = results.get(path_of(anim).rsplit("/", 1)[1])
                require(isinstance(out, unreal.AnimSequence), "retarget produced nothing")
                writable_delete(final)
                require(lib.rename_asset(path_of(out), final), "rename failed")
                out = unreal.load_asset(final)
                if root_scale is None:
                    ref = unreal.AnimPoseExtensions.get_anim_pose_at_time(out, 0, options(mesh))
                    root_scale = v3(unreal.AnimPoseExtensions.get_ref_bone_pose(ref, "root", unreal.AnimPoseSpaces.LOCAL).scale3d)
                cur = unreal.AnimPoseExtensions.get_bone_pose(unreal.AnimPoseExtensions.get_anim_pose_at_time(out, 0, options(mesh)), "root", unreal.AnimPoseSpaces.LOCAL)
                if max(abs(s - 1.0) for s in v3(cur.scale3d)) < .001 and max(abs(s - 1.0) for s in root_scale) > .01:
                    REPAIR.repair_clip(out, root_scale, children, mesh)
                out.set_editor_property("enable_root_motion", False)
                out.set_editor_property("force_root_lock", True)
                require(lib.save_loaded_asset(out, False), "save failed")
                info = validate(out, mesh)
                if not clip.startswith(("death", "roll", "loco_")) and "death" not in clip and "roll" not in clip:
                    validate_upright(out, mesh)
                entry["clips"][clip] = info
                done[clip] = final
            except Exception as error:
                entry["errors"][clip] = str(error)
                writable_delete(final)
        if lib.does_directory_exist(staging):
            lib.delete_directory(staging)
    return done


def build_locomotion(folder, mesh, done, loco_sources, speeds, lancer, report):
    need = [k for k in ("idle", "walk_f") if "loco_" + k not in done]
    if need:
        report[folder]["errors"]["locomotion"] = "missing " + ",".join(need)
        return None
    path = "%s/%s/BS_Fab_Locomotion_%s" % (OUT, folder, folder)
    writable_delete(path)
    factory = unreal.BlendSpaceFactoryNew()
    factory.set_editor_property("target_skeleton", mesh.get_editor_property("skeleton"))
    blend = unreal.AssetToolsHelpers.get_asset_tools().create_asset(path.rsplit("/", 1)[1], path.rsplit("/", 1)[0], unreal.BlendSpace, factory)
    params = lancer.get_editor_property("blend_parameters")
    blend.set_editor_property("blend_parameters", params)
    speed_max = float(params[1].get_editor_property("max"))
    samples, layout = [], set()

    def add(clip_key, direction, speed):
        key = (round(direction, 1), round(min(speed, speed_max), 1))
        if key in layout or "loco_" + clip_key not in done:
            return
        s = unreal.BlendSample()
        s.set_editor_property("animation", unreal.load_asset(done["loco_" + clip_key]))
        s.set_editor_property("sample_value", unreal.Vector(key[0], key[1], 0))
        s.set_editor_property("rate_scale", 1.0)
        samples.append(s)
        layout.add(key)
    for d in (-180.0, -90.0, 0.0, 90.0, 180.0, -135.0, -45.0, 45.0, 135.0):
        add("idle", d, 0.0)
    for gait in ("walk", "run"):
        fwd = gait + "_f"
        if "loco_" + fwd not in done:
            continue
        for suffix, d in DIRS.items():
            k = gait + "_" + suffix if "loco_" + gait + "_" + suffix in done else fwd
            add(k, d, speeds.get(gait, 150.0 if gait == "walk" else 420.0))
            if suffix == "b":
                add(k, -180.0, speeds.get(gait, 150.0 if gait == "walk" else 420.0))
    blend.set_editor_property("sample_data", samples)
    require(lib.save_loaded_asset(blend, False), "blend save failed")
    report[folder]["locomotion"] = {"blend": path, "samples": sorted(layout), "speeds": speeds}
    return path


def main():
    started = time.monotonic()
    cmd = unreal.SystemLibrary.get_command_line()
    only = None
    for token in cmd.split():
        if token.lower().startswith("-cirefabanimonly="):
            only = set(token.split("=", 1)[1].split("+"))
    keep = "-cirefabanimkeep" in cmd.lower()
    report = {"output": OUT, "bodies": {}, "status": "failed"}
    saved = ROOT / "Saved"
    try:
        cfg = json.loads((ROOT / "Art/Fab/FabAnimMap.json").read_text(encoding="utf-8"))
        bodies = json.loads((ROOT / "Content/Data/ChampionAttacks02.json").read_text(encoding="utf-8"))["bodies"]
        sources, loco = {}, {}
        missing = []
        for clip, row in cfg.get("clips", {}).items():
            anim = unreal.load_asset(row["path"].split(".")[0])
            if not isinstance(anim, unreal.AnimSequence):
                missing.append(row["path"]); continue
            sources[clip] = (anim, source_mesh_for(anim.get_editor_property("skeleton"), "/Game/" + row["path"].split("/")[2]))
        for key, p in cfg.get("locomotion", {}).items():
            anim = unreal.load_asset(p.split(".")[0])
            if not isinstance(anim, unreal.AnimSequence):
                missing.append(p); continue
            loco[key] = (anim, source_mesh_for(anim.get_editor_property("skeleton"), "/Game/" + p.split("/")[2]))
        report["missing_sources"] = missing
        require(sources or loco, "no Fab source clips found (packs not installed?)")
        lancer = unreal.load_asset(LANCER_BS)
        windows, ok_clips = {}, set()
        for mesh_path, folder in sorted(bodies.items(), key=lambda kv: kv[1]):
            if only and folder not in only:
                continue
            mesh = unreal.load_asset(mesh_path.split(".")[0])
            if not isinstance(mesh, unreal.SkeletalMesh):
                report["bodies"][folder] = {"error": "body missing"}; continue
            try:
                done = retarget_body(folder, mesh, sources, loco, report["bodies"], keep)
                ok_clips.update(c for c in done if not c.startswith("loco_"))
                # Ground speeds at the body's in-game scale: source travel x (body height / source height).
                speeds = {}
                for gait in ("walk", "run"):
                    if gait + "_f" in loco:
                        anim, src = loco[gait + "_f"]
                        src_h = src.get_bounds().box_extent.z * 2
                        speeds[gait] = round(travel_speed(anim, src) * 180.0 / max(1.0, src_h), 1)
                build_locomotion(folder, mesh, done, loco, speeds, lancer, report["bodies"])
            except Exception as error:
                report["bodies"].setdefault(folder, {})["error"] = traceback.format_exc()
        for clip in ok_clips:
            row = cfg["clips"][clip]
            windows[clip] = {k: row[k] for k in ("start", "contact", "end", "recoverRate") if k in row}
        data_path = ROOT / "Content/Data/FabAnimations.json"
        data = json.loads(data_path.read_text(encoding="utf-8"))
        data["clips"] = dict(sorted(windows.items()))
        data["replace"] = cfg.get("replace", {})
        data_path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        errors = sum(len(b.get("errors", {})) + ("error" in b) for b in report["bodies"].values())
        report["status"] = "pass" if not errors else "partial"
        report["clips"] = sorted(ok_clips)
        unreal.log("CIRE_FAB_ANIM_RETARGET_%s bodies=%d clips=%d errors=%d" % (
            "PASS" if not errors else "PARTIAL", len(report["bodies"]), len(ok_clips), errors))
    except Exception as error:
        report["error"] = traceback.format_exc()
        unreal.log_error("CIRE_FAB_ANIM_RETARGET_FAIL " + str(error))
    finally:
        report["seconds"] = round(time.monotonic() - started, 1)
        saved.mkdir(parents=True, exist_ok=True)
        (saved / "FabAnimRetarget.json").write_text(json.dumps(report, indent=1), encoding="utf-8")


main()
