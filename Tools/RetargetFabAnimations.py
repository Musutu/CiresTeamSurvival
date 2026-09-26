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
     -CireFabAnimMonsters             (fab-coverage: shout + slam onto Tripo monster bodies -> MonsterFabClips.json)
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
    if not keep and lib.does_directory_exist(rig_dir):
        lib.delete_directory(rig_dir)  # rigs are rebuilt with the clips (a stale rig would block create_asset)
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
        existing = "%s/RTG_%s" % (rig_dir, tag)
        if keep and lib.does_asset_exist(existing):
            rtg = unreal.load_asset(existing)  # keep mode: reuse the rig pair built with the clips
        else:
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


def body_locomotion(mesh_path):
    """The body's own, proven locomotion BlendSpace (ChampionArtBindings / ChampionArt.tripo.json / Preview02)."""
    mesh_path = mesh_path.split(".")[0]
    for name, rel in (("ChampionArtBindings.json", "bindings"), ("ChampionArt.tripo.json", "champions")):
        data = json.loads((ROOT / "Content/Data" / name).read_text(encoding="utf-8"))
        for row in data.get(rel, []):
            if row.get("mesh", "").split(".")[0] == mesh_path and row.get("locomotion"):
                return row["locomotion"].split(".")[0]
    legacy = {"medieval_knight_armor_3d_model": "Warden", "armored_archer_3d_model": "Ranger", "battlefield_healer_3d_model": "Scholar"}
    for model, label in legacy.items():
        if model in mesh_path:
            return "/Game/Art/Characters/TripoRetarget/Preview02/%s/Animations/BS_Idle_Walk_Run_%s" % (label, label)
    return None


def build_locomotion(folder, mesh, done, loco_sources, speeds, lancer, report):
    """Duplicate the body's working BlendSpace and swap each sample's clip for the Fab one (same grid positions:
    speed 0 = idle, the middle speed = walk, the top speed = run; direction picks the 8-way clip). Building a
    BlendSpace from scratch in Python evaluated to the reference pose at runtime, so the proven asset is reused."""
    need = [k for k in ("idle", "walk_f") if "loco_" + k not in done]
    if need:
        report[folder]["errors"]["locomotion"] = "missing " + ",".join(need)
        return None
    template = body_locomotion(path_of(mesh))
    if not template or not lib.does_asset_exist(template):
        report[folder]["errors"]["locomotion"] = "no template BlendSpace for body"
        return None
    path = "%s/%s/BS_Fab_Locomotion_%s" % (OUT, folder, folder)
    writable_delete(path)
    blend = lib.duplicate_asset(template, path)
    require(isinstance(blend, unreal.BlendSpace), "template duplicate failed")
    require(blend.get_editor_property("skeleton") == mesh.get_editor_property("skeleton"), "template skeleton differs")
    samples = list(blend.get_editor_property("sample_data"))
    speeds_seen = sorted({round(s.get_editor_property("sample_value").y, 1) for s in samples})
    top = speeds_seen[-1]
    dirs = {0: "f", 45: "fr", 90: "r", 135: "br", 180: "b", -180: "b", -135: "bl", -90: "l", -45: "fl"}
    layout = []
    for s in samples:
        v = s.get_editor_property("sample_value")
        d = dirs.get(int(round(v.x)), "f")
        gait = "idle" if v.y <= 1 else ("run" if v.y >= top - 1 else "walk")
        key = "idle" if gait == "idle" else "%s_%s" % (gait, d)
        if "loco_" + key not in done:
            key = "idle" if gait == "idle" else "%s_f" % gait
        if "loco_" + key not in done:
            key = "walk_f"
        s.set_editor_property("animation", unreal.load_asset(done["loco_" + key]))
        layout.append([round(v.x, 1), round(v.y, 1), key])
    blend.set_editor_property("sample_data", samples)
    require(lib.save_loaded_asset(blend, False), "blend save failed")
    report[folder]["locomotion"] = {"blend": path, "template": template, "samples": layout}
    return path

def measure_window(anim, mesh, kind):
    """Timing for a clip without authored notifies: attacks contact at peak wrist speed (12%..85% of the clip)."""
    length = float(anim.get_play_length())
    if kind in ("death", "roll", "jump"):
        return {"start": 0.0, "contact": round(min(.1, length * .2), 3), "end": round(length, 3), "recoverRate": 1.0}
    if kind == "hit":
        return {"start": 0.0, "contact": round(min(.12, length * .25), 3), "end": round(min(length, .85), 3), "recoverRate": 1.0}
    o = unreal.AnimPoseEvaluationOptions()
    o.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    o.set_editor_property("optional_skeletal_mesh", mesh)
    steps = max(8, int(length * 60))
    prev, best, best_t = None, -1.0, length * .4
    for i in range(steps + 1):
        t = length * i / steps
        p = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, o)
        pts = [v3(unreal.AnimPoseExtensions.get_bone_pose(p, b, unreal.AnimPoseSpaces.WORLD).translation) for b in ("hand_r", "hand_l")]
        if prev is not None and length * .12 <= t <= length * .85:
            speed = sum(math.dist(a, b) for a, b in zip(pts, prev))
            if speed > best:
                best, best_t = speed, t
        prev = pts
    end = min(length, best_t + max(.35, (length - best_t) * .7))
    return {"start": 0.0, "contact": round(best_t, 3), "end": round(end, 3), "recoverRate": 1.2}


def foot_speed(anim, mesh):
    """Ground speed of an in-place loop from the planted foot's sweep: two steps per cycle."""
    o = unreal.AnimPoseEvaluationOptions()
    o.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    o.set_editor_property("optional_skeletal_mesh", mesh)
    length = float(anim.get_play_length())
    xs = []
    for i in range(33):
        p = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, length * i / 32, o)
        t = unreal.AnimPoseExtensions.get_bone_pose(p, "foot_l", unreal.AnimPoseSpaces.WORLD).translation
        xs.append(math.hypot(t.x, t.y) * (1 if (t.x + t.y) >= 0 else -1))
    return 2.0 * (max(xs) - min(xs)) / max(.1, length)


def main_monsters(cmd, started):
    """fab-coverage: -CireFabAnimMonsters retargets FabAnimMap "monsters.clips" (Fab shout + ground slam) onto the
    Tripo monster bodies in "monsters.bodies" (mesh -> variant) and writes Content/Data/MonsterFabClips.json
    (variant -> {war_cry, ground_slam} object paths, plus clip windows), read by CireMonsterArt."""
    only = None
    for token in cmd.split():
        if token.lower().startswith("-cirefabanimonly="):
            only = set(token.split("=", 1)[1].split("+"))
    report = {"output": OUT, "bodies": {}, "status": "failed"}
    try:
        cfg = json.loads((ROOT / "Art/Fab/FabAnimMap.json").read_text(encoding="utf-8"))["monsters"]
        sources, windows = {}, {}
        for clip, row in cfg["clips"].items():
            anim = unreal.load_asset(row["path"].split(".")[0])
            if isinstance(anim, unreal.AnimSequence):
                src_mesh = source_mesh_for(anim.get_editor_property("skeleton"), "/Game/" + row["path"].split("/")[2])
                sources[clip] = (anim, src_mesh)
                windows[clip] = measure_window(anim, src_mesh, row.get("kind", "attack"))
        data_path = ROOT / "Content/Data/MonsterFabClips.json"
        data = json.loads(data_path.read_text(encoding="utf-8")) if data_path.exists() else {}
        variants = data.get("variants", {})
        for mesh_path, variant in sorted(cfg["bodies"].items(), key=lambda kv: kv[1]):
            if only and variant not in only:
                continue
            mesh = unreal.load_asset(mesh_path.split(".")[0])
            if not isinstance(mesh, unreal.SkeletalMesh):
                report["bodies"][variant] = {"error": "body missing"}
                continue
            try:
                done = retarget_body(variant, mesh, sources, {}, report["bodies"], True)
                row = {clip.replace("monster_", ""): "%s.%s" % (path, path.rsplit("/", 1)[1]) for clip, path in done.items()}
                if row:
                    variants[variant] = row
                else:
                    variants.pop(variant, None)
            except Exception:
                report["bodies"].setdefault(variant, {})["error"] = traceback.format_exc()
        data = {"_comment": "Generated by Tools/RetargetFabAnimations.py -CireFabAnimMonsters (fab-coverage). Fab clips retargeted "
                            "onto Tripo monster bodies (derived from licensed packs: /Game/FabDerived, local only). "
                            "variants.<Variant>.<clip name> is added to that body's named clips when it has none of that "
                            "name; windows are keyed by the clip-name suffix like MonsterArt.json clips.",
                "variants": dict(sorted(variants.items())), "windows": windows}
        data_path.write_text(json.dumps(data, indent=1) + "\n", encoding="utf-8")
        errors = sum(len(b.get("errors", {})) + ("error" in b) for b in report["bodies"].values())
        report["status"] = "pass" if not errors else "partial"
        unreal.log("CIRE_FAB_ANIM_MONSTERS_%s bodies=%d variants=%d errors=%d" % (
            "PASS" if not errors else "PARTIAL", len(report["bodies"]), len(variants), errors))
    except Exception as error:
        report["error"] = traceback.format_exc()
        unreal.log_error("CIRE_FAB_ANIM_MONSTERS_FAIL " + str(error))
    finally:
        report["seconds"] = round(time.monotonic() - started, 1)
        (ROOT / "Saved").mkdir(parents=True, exist_ok=True)
        (ROOT / "Saved/FabAnimMonsters.json").write_text(json.dumps(report, indent=1), encoding="utf-8")


def main():
    started = time.monotonic()
    cmd = unreal.SystemLibrary.get_command_line()
    if "-cirefabanimmonsters" in cmd.lower():
        return main_monsters(cmd, started)
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
        loaded, windows, speeds_by_set, missing = {}, {}, {}, []

        def src(path):
            if path not in loaded:
                anim = unreal.load_asset(path.split(".")[0])
                loaded[path] = (anim, source_mesh_for(anim.get_editor_property("skeleton"), "/Game/" + path.split("/")[2])) \
                    if isinstance(anim, unreal.AnimSequence) else None
                if loaded[path] is None:
                    missing.append(path)
            return loaded[path]
        # Timing is measured once per source clip.
        for clip, row in cfg["clips"].items():
            s = src(row["path"])
            if s:
                windows[clip] = {k: row[k] for k in ("start", "contact", "end", "recoverRate") if k in row} or measure_window(s[0], s[1], row.get("kind", "attack"))
        # Samples sit at the game's own speeds (Content/Data/MovementTuning.json), so the blend space plays the
        # run cycle exactly when the champion runs; backward directions at the backpedal speed.
        tuning = json.loads((ROOT / "Content/Data/MovementTuning.json").read_text(encoding="utf-8"))
        game = {"walk": float(tuning.get("WalkSpeed", 240)), "run": float(tuning.get("RunSpeed", 520)), "back": float(tuning.get("BackpedalScale", .65))}
        for set_name in cfg["locomotion"]:
            speeds_by_set[set_name] = game
        report["windows"], report["speeds"], report["missing_sources"] = windows, speeds_by_set, missing
        lancer = unreal.load_asset(LANCER_BS)
        ok_clips = set()
        for mesh_path, folder in sorted(bodies.items(), key=lambda kv: kv[1]):
            if only and folder not in only:
                continue
            set_name = cfg.get("bodies", {}).get(folder)
            if not set_name:
                report["bodies"][folder] = {"skipped": "no weapon set"}; continue
            mesh = unreal.load_asset(mesh_path.split(".")[0])
            if not isinstance(mesh, unreal.SkeletalMesh):
                report["bodies"][folder] = {"error": "body missing"}; continue
            # The body's weapon set plus any extra sets (alternate loadouts, e.g. the Ranger's crossbow): clips only.
            sets = {set_name, *cfg.get("extraSets", {}).get(folder, [])}
            sources = {c: src(r["path"]) for c, r in cfg["clips"].items() if r.get("set") in sets and src(r["path"])}
            loco = {k: src(p) for k, p in cfg["locomotion"].get(set_name, {}).items() if src(p)}
            try:
                done = retarget_body(folder, mesh, sources, loco, report["bodies"], keep)
                ok_clips.update(c for c in done if not c.startswith("loco_"))
                report["bodies"][folder]["set"] = set_name
                # -CireFabAnimKeep adds clips (e.g. an extra set) without rebuilding a locomotion BlendSpace that exists.
                if not (keep and lib.does_asset_exist("%s/%s/BS_Fab_Locomotion_%s" % (OUT, folder, folder))):
                    build_locomotion(folder, mesh, done, loco, speeds_by_set.get(set_name, {}), lancer, report["bodies"])
            except Exception:
                report["bodies"].setdefault(folder, {})["error"] = traceback.format_exc()
        data_path = ROOT / "Content/Data/FabAnimations.json"
        data = json.loads(data_path.read_text(encoding="utf-8"))
        merged = dict(data.get("clips", {}))
        merged.update({c: windows[c] for c in ok_clips if c in windows})
        data["clips"] = dict(sorted(merged.items()))
        # Only clips that retargeted onto at least one body (and so have timing) may be named in a replacement.
        data["replace"] = {row: {kind: [c for c in clips if c in data["clips"]] for kind, clips in kinds.items()}
                           for row, kinds in cfg.get("replace", {}).items()}
        data["replace"] = {row: {k: v for k, v in kinds.items() if v} for row, kinds in data["replace"].items()}
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
        name = "FabAnimRetarget%s.json" % ("-" + "+".join(sorted(only)) if only else "")
        (saved / name).write_text(json.dumps(report, indent=1), encoding="utf-8")


main()
