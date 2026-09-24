"""Read-only UE 5.8 inspection of the Tripo batch-03 monster bodies and clips.

For every body listed in Content/Data/NPCMeshes.tripo.json (recommended body and
alternates) this records the skeleton, reference root scale, imported bounds and,
for every clip, component-space samples of root/pelvis/head/feet/hands under the
runtime evaluation (extract root motion, root not incorporated). It also reports the
right-hand speed profile of the multi-swing slash/chop clips so single swings can be
trimmed, and the lowest-foot ground speed of walk/run (for anti-slide play rates).

Writes only Saved/MonsterAnimInspection.json. Nothing is saved or modified.
Run: UnrealEditor-Cmd <project> -run=pythonscript -script=Tools/InspectMonsterAnimation.py
"""
import json
import math
from pathlib import Path

import unreal

BONES = ["root", "pelvis", "spine_03", "head", "foot_l", "foot_r", "ball_l", "ball_r", "hand_l", "hand_r"]


def vec(v):
    return [round(float(v.x), 3), round(float(v.y), 3), round(float(v.z), 3)]


def options(mesh):
    o = unreal.AnimPoseEvaluationOptions()
    o.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    o.set_editor_property("optional_skeletal_mesh", mesh)
    o.set_editor_property("should_retarget", False)
    o.set_editor_property("extract_root_motion", True)
    o.set_editor_property("incorporate_root_motion_into_pose", False)
    return o


def pose_at(anim, t, mesh):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, options(mesh))
    out = {}
    for b in BONES:
        tr = unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD)
        out[b] = {"p": vec(tr.translation), "s": vec(tr.scale3d)}
    ref = {}
    for b in BONES:
        tr = unreal.AnimPoseExtensions.get_ref_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD)
        ref[b] = {"p": vec(tr.translation), "s": vec(tr.scale3d)}
    return out, ref


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def main():
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    data = json.loads((root / "Content/Data/NPCMeshes.tripo.json").read_text(encoding="utf-8"))
    bodies = {}
    for arch, entry in data["archetypes"].items():
        for body in [entry] + list(entry.get("alternates", [])):
            bodies.setdefault(body["variant"] if "variant" in body else arch, body)
    report = {"bodies": {}}
    for name, body in sorted(bodies.items()):
        mesh = unreal.load_asset(body["mesh"].split(".")[0])
        info = {"mesh": body["mesh"], "loaded": isinstance(mesh, unreal.SkeletalMesh)}
        report["bodies"][name] = info
        if not info["loaded"]:
            continue
        b = mesh.get_imported_bounds()
        info["bounds_origin"] = vec(b.origin)
        info["bounds_extent"] = vec(b.box_extent)
        comp = unreal.SkeletalMeshComponent()
        comp.set_skeletal_mesh_asset(mesh)
        info["bone_count"] = comp.get_num_bones()
        clips = {}
        all_clips = body["animations"].get("all", {})
        for key, path in sorted(all_clips.items()):
            anim = unreal.load_asset(path.split(".")[0])
            if not isinstance(anim, unreal.AnimSequence):
                clips[key] = {"missing": path}
                continue
            length = float(anim.get_play_length())
            c = {"length": length, "frames": int(anim.get_editor_property("number_of_sampled_keys")) if hasattr(anim, "number_of_sampled_keys") else None,
                 "enable_root_motion": bool(anim.get_editor_property("enable_root_motion")),
                 "force_root_lock": bool(anim.get_editor_property("force_root_lock")),
                 "skeleton_match": anim.get_editor_property("skeleton") == mesh.get_editor_property("skeleton")}
            n = 60 if key in ("slash", "chop", "walk", "run", "ground_slam", "war_cry", "cast_a_spell", "attack_bow", "attack_crossbow", "fall", "hit_to_body_01", "shoot") else 6
            samples = []
            ref = None
            for i in range(n + 1):
                t = length * i / n
                p, r = pose_at(anim, t, mesh)
                ref = r
                samples.append({"t": round(t, 4), **{k: v["p"] for k, v in p.items()}, "root_scale": p["root"]["s"]})
            c["samples"] = samples
            clips[key] = c
            if ref and "ref" not in info:
                info["ref"] = ref
        info["clips"] = clips
    out = root / "Saved/MonsterAnimInspection.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, indent=1), encoding="utf-8")
    unreal.log("CIRE_MONSTER_ANIM_INSPECT_DONE " + str(out))


main()
