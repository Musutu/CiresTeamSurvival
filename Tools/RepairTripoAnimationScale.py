"""Repair scale only in NEW Preview02 locomotion copies; preserve Preview01.

Imported reference roots have uniform scale100 and sub-centimeter child offsets.
Preview01 strips root scale to1 while retaining descendant offsets. Restore the
reference root scale and divide direct-root-child translations by that factor,
preserving their authored component trajectory and all rotations/root positions.
No original skeleton, mesh, Manny asset, Preview01 asset, or level is modified.

Run as a UE Python commandlet. -CireTripoVerifyRepair only verifies saved output.
Output: Saved/TripoAnimationScaleRepair.json (or ...Verification.json).
"""
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path

import unreal


SOURCE = "/Game/Art/Characters/TripoRetarget/Preview01"
DEST = "/Game/Art/Characters/TripoRetarget/Preview02"
MODELS = {
    "Warden": "medieval_knight_armor_3d_model",
    "Ranger": "armored_archer_3d_model",
    "Scholar": "battlefield_healer_3d_model",
}
BONES = ["root", "pelvis", "head", "foot_l", "foot_r", "hand_l", "hand_r"]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def path_of(obj):
    return str(obj.get_path_name())


def vector(v):
    return [float(v.x), float(v.y), float(v.z)]


def options(mesh, mode="RAW", incorporate=False, retarget=True):
    opts = unreal.AnimPoseEvaluationOptions()
    opts.set_editor_property("evaluation_type", getattr(unreal.AnimDataEvalType, mode))
    opts.set_editor_property("optional_skeletal_mesh", mesh)
    opts.set_editor_property("should_retarget", retarget)
    opts.set_editor_property("extract_root_motion", not incorporate)
    opts.set_editor_property("incorporate_root_motion_into_pose", incorporate)
    return opts


def get_pose(animation, time, mesh, mode="RAW", incorporate=False, retarget=True):
    pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(animation, time, options(mesh, mode, incorporate, retarget))
    require(unreal.AnimPoseExtensions.is_valid(pose), "Invalid animation pose: " + path_of(animation))
    return pose


def bone(pose, name, reference=False, local=False):
    fn = unreal.AnimPoseExtensions.get_ref_bone_pose if reference else unreal.AnimPoseExtensions.get_bone_pose
    return fn(pose, name, unreal.AnimPoseSpaces.LOCAL if local else unreal.AnimPoseSpaces.WORLD)


def hash_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def source_hashes(content):
    paths = list((content / "Art/Characters/TripoRetarget/Preview01").rglob("*.uasset"))
    for name in MODELS.values():
        directory = content / "TripoModels" / name
        paths += [directory / (name + ".uasset"), directory / (name + "_Skeleton.uasset")]
    return {str(p.relative_to(content)): hash_file(p) for p in paths}


def raw_tracks(animation, model, mesh, names):
    # FVector3f/FQuat4f key arrays are not exposed to UE Python. Sample the RAW
    # local pose at every exact key frame, with retargeting/root locking disabled.
    count = int(model.get_number_of_keys())
    opts = options(mesh, "RAW", True, False)
    tracks = {name:([],[],[]) for name in names}
    for index in range(count):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_frame(animation,index,opts)
        require(unreal.AnimPoseExtensions.is_valid(pose), "Invalid source key pose")
        for name in names:
            transform = bone(pose,name,local=True)
            tracks[name][0].append(transform.translation)
            tracks[name][1].append(transform.rotation)
            tracks[name][2].append(transform.scale3d)
    return tracks


def repair_clip(animation, root_scale, direct_children, mesh):
    model = animation.get_editor_property("data_model_interface")
    require(model is not None, "Missing animation data model")
    controller = animation.get_editor_property("controller")
    if controller is None:
        controller = unreal.AnimDataController()
        controller.set_model(model)
    tracks = raw_tracks(animation,model,mesh,["root"] + direct_children)
    for scale in tracks["root"][2]:
        require(max(abs(v - 1.0) for v in vector(scale)) < 0.001, "Root track is not uniform scale1")
    controller.open_bracket("Restore Tripo imported root scale in copied animation", False)
    try:
        for name, (positions, rotations, scales) in tracks.items():
            corrected_positions = []
            corrected_scales = []
            for i, value in enumerate(positions):
                if name == "root":
                    corrected_positions.append(unreal.Vector(value.x, value.y, value.z))
                    corrected_scales.append(unreal.Vector(*root_scale))
                else:
                    previous_root = vector(tracks["root"][2][i])
                    corrected_positions.append(unreal.Vector(*[v * old / new for v, old, new in zip(vector(value), previous_root, root_scale)]))
                    corrected_scales.append(unreal.Vector(*vector(scales[i])))
            quaternions = [unreal.Quat(q.x, q.y, q.z, q.w) for q in rotations]
            require(controller.set_bone_track_keys(name, corrected_positions, quaternions, corrected_scales, False),
                    "Could not update copied track " + name)
    finally:
        controller.close_bracket(False)


def validate_clip(animation, original, mesh, expected_scale):
    duration = float(animation.get_play_length())
    results = []
    # Test both raw and cooked-style data and both root-lock evaluation paths.
    for mode in ("RAW", "COMPRESSED"):
      for retarget in (False, True):
        for incorporate in (False, True):
            for time in (0.0, 0.25, duration * 0.5):
                pose = get_pose(animation, time, mesh, mode, incorporate, retarget)
                old_pose = get_pose(original, time, mesh, "RAW", True)
                transforms = {name: bone(pose, name) for name in BONES}
                for name, transform in transforms.items():
                    require(all(math.isfinite(v) for v in vector(transform.translation) + vector(transform.scale3d)),
                            f"Nonfinite repaired pose {name}")
                    require(max(abs(v) for v in vector(transform.translation)) < 500,
                            f"Repaired bone moved outside sane component bounds: {name}")
                root = transforms["root"]
                require(max(abs(a-b) for a,b in zip(vector(root.scale3d),expected_scale)) < 0.01,
                        "Repaired root scale differs from imported reference")
                head = transforms["head"].translation.z
                feet = min(transforms["foot_l"].translation.z, transforms["foot_r"].translation.z)
                span = float(head - feet)
                require(45 < span < 125, f"Repaired head/feet span is not humanoid: {span}")
                pelvis = vector(transforms["pelvis"].translation)
                old_pelvis = vector(bone(old_pose, "pelvis").translation)
                delta = math.sqrt(sum((a-b)**2 for a,b in zip(pelvis, old_pelvis)))
                # Runtime root locking deliberately removes the authored root
                # translation/rotation. Compare under that same evaluated root,
                # not to an unlocked root-motion trajectory far down the lane.
                old_local = bone(old_pose,"pelvis",local=True).translation
                old_root_scale = vector(bone(old_pose,"root",local=True).scale3d)
                expected_local = unreal.Vector(*[v*old/new for v,old,new in zip(vector(old_local),old_root_scale,expected_scale)])
                expected_world = vector(unreal.MathLibrary.transform_location(root,expected_local))
                runtime_delta = math.sqrt(sum((a-b)**2 for a,b in zip(pelvis,expected_world)))
                checked_delta = delta if incorporate else runtime_delta
                tolerance = 0.05 if mode == "RAW" else 2.0
                require(checked_delta < tolerance,
                        f"Pelvis delta {checked_delta}cm: {path_of(animation)} {mode} retarget={retarget} "
                        f"incorporate={incorporate} time={time} actual={pelvis} old_unlocked={old_pelvis} expected_runtime={expected_world}")
                results.append({"mode":mode, "retarget":retarget, "incorporate_root_motion":incorporate,
                                "extract_root_motion":not incorporate, "time":time,
                                "root_scale":vector(root.scale3d), "pelvis_cm":pelvis,
                                "unlocked_pelvis_delta_cm":delta,"runtime_pelvis_delta_cm":runtime_delta,
                                "expected_runtime_pelvis_cm":expected_world,"head_feet_span_cm":span,
                                "head_cm":vector(transforms["head"].translation),
                                "left_foot_cm":vector(transforms["foot_l"].translation),
                                "right_foot_cm":vector(transforms["foot_r"].translation)})
    return {"asset":path_of(animation), "force_root_lock":bool(animation.get_editor_property("force_root_lock")),
            "duration":duration, "samples":results}


def run(verify_only=False):
    saved = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))
    content = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()))
    report = {"created_utc":datetime.now(timezone.utc).isoformat(), "verify_only":verify_only,
              "source":SOURCE, "destination":DEST, "original_meshes_and_skeletons_saved":False,
              "preview01_saved":False, "visual_acceptance":False, "characters":{}, "created_assets":[]}
    hashes = source_hashes(content)
    try:
        if not verify_only and unreal.EditorAssetLibrary.does_directory_exist(DEST):
            previous=json.loads((saved/"TripoAnimationScaleRepair.json").read_text())
            require(previous.get("status")=="failed" and previous.get("destination")==DEST,
                    "Preview02 exists without an attributable failed build; refusing overwrite")
            existing=set(unreal.EditorAssetLibrary.list_assets(DEST,recursive=True,include_folder=False))
            require(existing.issubset(set(previous.get("created_assets",[]))),
                    "Preview02 contains assets not created by the previous failed build")
            report["resumed_failed_build"]=True
        for name, model_name in MODELS.items():
            mesh = unreal.load_asset(f"/Game/TripoModels/{model_name}/{model_name}")
            skeleton = mesh.get_editor_property("skeleton")
            component = unreal.SkeletalMeshComponent()
            component.set_skeletal_mesh_asset(mesh)
            names = [str(component.get_bone_name(i)) for i in range(component.get_num_bones())]
            children = [n for n in names if str(component.get_parent_bone(n)) == "root"]
            require(children == ["pelvis"], "Unexpected direct-root hierarchy; review before repair")
            original_blend = unreal.load_asset(f"{SOURCE}/{name}/Animations/BS_Idle_Walk_Run_{name}")
            source_clips = {path_of(s.get_editor_property("animation")):s.get_editor_property("animation")
                            for s in original_blend.get_editor_property("sample_data")}
            require(len(source_clips)==17, "Expected17 unique source clips")
            reference_pose = get_pose(next(iter(source_clips.values())), 0, mesh)
            root_scale = vector(bone(reference_pose, "root", True).scale3d)
            require(max(abs(s-100.0) for s in root_scale)<0.01, "Unexpected imported root scale")
            entry = {"mesh":path_of(mesh), "root_reference_scale":root_scale, "direct_root_children":children, "animations":[]}
            report["characters"][name]=entry
            mapping={}
            for source_path, original in sorted(source_clips.items()):
                package=source_path.split(".")[0].replace(SOURCE,DEST,1)
                if verify_only:
                    copy=unreal.load_asset(package)
                else:
                    copy=unreal.load_asset(package) if unreal.EditorAssetLibrary.does_asset_exist(package) else None
                    if copy is None:
                        copy=unreal.EditorAssetLibrary.duplicate_asset(source_path,package)
                        require(copy is not None, "Could not duplicate "+source_path)
                        repair_clip(copy,root_scale,children,mesh)
                        require(unreal.EditorAssetLibrary.save_loaded_asset(copy,only_if_is_dirty=False), "Could not save corrected clip")
                    report["created_assets"].append(path_of(copy))
                require(copy is not None and copy.get_editor_property("skeleton")==skeleton, "Copied skeleton mismatch")
                mapping[source_path]=copy
                entry["animations"].append(validate_clip(copy,original,mesh,root_scale))
            blend_path=f"{DEST}/{name}/Animations/BS_Idle_Walk_Run_{name}"
            if verify_only:
                blend=unreal.load_asset(blend_path)
            else:
                blend=unreal.EditorAssetLibrary.duplicate_asset(path_of(original_blend),blend_path)
                report["created_assets"].append(path_of(blend))
                samples=list(blend.get_editor_property("sample_data"))
                for sample in samples:
                    sample.set_editor_property("animation",mapping[path_of(sample.get_editor_property("animation"))])
                blend.set_editor_property("sample_data",samples)
                require(unreal.EditorAssetLibrary.save_loaded_asset(blend,only_if_is_dirty=False), "Could not save repaired BlendSpace")
            require(blend is not None and blend.get_editor_property("skeleton")==skeleton, "BlendSpace skeleton mismatch")
            for sample in blend.get_editor_property("sample_data"):
                clip=sample.get_editor_property("animation")
                require(path_of(clip).startswith(DEST+"/") and clip.get_editor_property("skeleton")==skeleton,
                        "BlendSpace references original/unrepaired data")
            entry["blendspace"]=path_of(blend)
            entry["sample_count"]=len(blend.get_editor_property("sample_data"))
        require(hashes == source_hashes(content), "Protected source asset bytes changed")
        report["protected_source_hashes_unchanged"]=True
        report["status"]="verified_needs_visual_review" if verify_only else "repaired_needs_reload_and_visual_review"
        unreal.log("CIRE_TRIPO_ANIMATION_SCALE_REPAIR_PASS verify_only="+str(verify_only))
    except Exception as error:
        report["status"]="failed"
        report["error"]=str(error)
        raise
    finally:
        report_path=saved/("TripoAnimationScaleVerification.json" if verify_only else "TripoAnimationScaleRepair.json")
        report_path.write_text(json.dumps(report,indent=2),encoding="utf-8")


if __name__ == "__main__":
    run("-ciretripoverifyrepair" in unreal.SystemLibrary.get_command_line().lower().split())
