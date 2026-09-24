"""Read-only UE AnimPose comparison of source and retargeted locomotion.

Writes only Saved/TripoAnimationInspection.json. Does not edit or save any asset.
Uses UE 5.8 AnimationBlueprintLibrary/Public/AnimPose.h APIs.
"""
import json
import math
from pathlib import Path
from datetime import datetime, timezone

import unreal


BONES = ["root", "pelvis", "head", "foot_l", "foot_r", "hand_l", "hand_r"]
BASE = "/Game/Art/Characters/TripoRetarget/Preview01"
MESHES = {
    "Manny": "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple",
    "Warden": "/Game/TripoModels/medieval_knight_armor_3d_model/medieval_knight_armor_3d_model",
    "Ranger": "/Game/TripoModels/armored_archer_3d_model/armored_archer_3d_model",
    "Scholar": "/Game/TripoModels/battlefield_healer_3d_model/battlefield_healer_3d_model",
}
CLIPS = {
    "idle": ("MM_Idle", "/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle"),
    "walk": ("MF_Unarmed_Walk_Fwd", "/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd"),
    "jog": ("MF_Unarmed_Jog_Fwd", "/Game/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd"),
}


def vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def transform(value):
    position, scale, quat = value.translation, value.scale3d, value.rotation
    values = vec(position) + vec(scale) + [float(quat.x), float(quat.y), float(quat.z), float(quat.w)]
    return {"position_cm": vec(position), "scale": vec(scale),
            "quat_xyzw": [float(quat.x), float(quat.y), float(quat.z), float(quat.w)],
            "finite": all(math.isfinite(v) for v in values)}


def inspect():
    report = {"created_utc": datetime.now(timezone.utc).isoformat(), "read_only_assets": True,
              "characters": {}, "warnings": []}
    saved = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))
    try:
        for name, mesh_path in MESHES.items():
            mesh = unreal.load_asset(mesh_path)
            if not isinstance(mesh, unreal.SkeletalMesh):
                raise RuntimeError("Missing mesh: " + mesh_path)
            character = {"mesh": mesh_path, "clips": {}}
            report["characters"][name] = character
            for state, (clip_name, source_path) in CLIPS.items():
                path = source_path if name == "Manny" else f"{BASE}/{name}/Animations/{clip_name}_{name}"
                animation = unreal.load_asset(path)
                if not isinstance(animation, unreal.AnimSequence):
                    raise RuntimeError("Missing sequence: " + path)
                duration = float(animation.get_play_length())
                clip = {"asset": path, "duration": duration, "evaluations": []}
                character["clips"][state] = clip
                for prop in ("enable_root_motion", "force_root_lock", "root_motion_root_lock", "additive_anim_type"):
                    clip[prop] = str(animation.get_editor_property(prop))
                for mode_name in ("RAW", "COMPRESSED"):
                    for retarget in (False, True):
                        options = unreal.AnimPoseEvaluationOptions()
                        options.set_editor_property("evaluation_type", getattr(unreal.AnimDataEvalType, mode_name))
                        options.set_editor_property("optional_skeletal_mesh", mesh)
                        options.set_editor_property("should_retarget", retarget)
                        options.set_editor_property("extract_root_motion", False)
                        for time in (0.0, 0.25, duration * 0.5):
                            pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(animation, time, options)
                            if not unreal.AnimPoseExtensions.is_valid(pose):
                                raise RuntimeError("Invalid pose: " + path)
                            sample = {"mode": mode_name, "retarget": retarget, "time": time, "bones": {}}
                            for bone in BONES:
                                sample["bones"][bone] = {}
                                for space_name in ("LOCAL", "WORLD"):
                                    space = getattr(unreal.AnimPoseSpaces, space_name)
                                    animated = transform(unreal.AnimPoseExtensions.get_bone_pose(pose, bone, space))
                                    reference = transform(unreal.AnimPoseExtensions.get_ref_bone_pose(pose, bone, space))
                                    sample["bones"][bone][space_name.lower()] = animated
                                    sample["bones"][bone]["reference_" + space_name.lower()] = reference
                                    if not animated["finite"] or min(abs(s) for s in animated["scale"]) < 0.01:
                                        report["warnings"].append({"character": name, "clip": state,
                                            "mode": mode_name, "retarget": retarget, "time": time,
                                            "bone": bone, "space": space_name, "transform": animated})
                            clip["evaluations"].append(sample)
        report["status"] = "inspected"
        unreal.log("CIRE_TRIPO_ANIMATION_INSPECTION_PASS warnings=" + str(len(report["warnings"])))
    except Exception as error:
        report["status"] = "failed"
        report["error"] = str(error)
        raise
    finally:
        path = saved / "TripoAnimationInspection.json"
        path.write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    inspect()
