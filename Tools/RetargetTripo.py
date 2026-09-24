"""UE 5.8: inspect humanoid retargeting, or explicitly build new preview assets.

Default execution is a read-only asset preflight. It uses transient IK rigs and
writes Saved/TripoRetargetPlan.json. Pass --build to the Python script to create
new assets under /Game/Art/Characters/TripoRetarget/Preview01; --output-label can
select another fresh iteration. Existing output directories are never reused.
Commandlet-safe engine flags: -CireTripoRetargetBuild -CireTripoLocomotionSet
-CireTripoInspectAfter (avoid nested Windows -script argument quoting).
No original meshes/skeletons, animation blueprints, or levels are saved/assigned.

Grounded in the installed IKRigEditor IKRigController.h,
IKRetargeterController.h, IKRetargetBatchOperation.h and UE4/UE5 templates in
IKRigAutoCharacterizer.cpp. New UE 5.8 run_batch_retarget API is used.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import time

import unreal


SOURCE = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
TARGETS = {
    "Warden": "/Game/TripoModels/medieval_knight_armor_3d_model/medieval_knight_armor_3d_model",
    "Ranger": "/Game/TripoModels/armored_archer_3d_model/armored_archer_3d_model",
    "Scholar": "/Game/TripoModels/battlefield_healer_3d_model/battlefield_healer_3d_model",
}
ANIMATIONS = [
    "/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle",
    "/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd",
    "/Game/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd",
]
OUTPUT_ROOT = "/Game/Art/Characters/TripoRetarget"
REQUIRED_CHAINS = {
    "Spine", "Neck", "Head", "LeftArm", "RightArm", "LeftLeg", "RightLeg",
    "LeftFoot", "RightFoot", "LeftClavicle", "RightClavicle",
}


def path_of(obj):
    return str(obj.get_path_name()) if obj else None


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_checked(path, expected_type):
    result = unreal.load_asset(path)
    require(isinstance(result, expected_type), f"Expected {expected_type.__name__}: {path}")
    return result


def inspect_mesh(mesh):
    component = unreal.SkeletalMeshComponent()
    component.set_skeletal_mesh_asset(mesh)
    bones = [str(component.get_bone_name(i)) for i in range(component.get_num_bones())]
    require(0 < len(bones) <= 512, f"Unexpected bone count for {path_of(mesh)}")
    bounds = mesh.get_imported_bounds()
    height = float(bounds.box_extent.z * 2)
    return {
        "mesh": path_of(mesh), "skeleton": path_of(mesh.get_editor_property("skeleton")),
        "bone_count": len(bones), "height_cm": height,
        "suggested_component_scale_for_185cm": 185.0 / height if height > 0 else None,
        "spine_bones": [b for b in bones if b.startswith("spine_")],
        "parents": {b: str(component.get_parent_bone(b)) for b in bones},
    }


def configure_rig(rig, mesh):
    controller = unreal.IKRigController.get_controller(rig)
    require(controller.set_skeletal_mesh(mesh), f"Cannot set rig mesh {path_of(mesh)}")
    characterized = bool(controller.apply_auto_generated_retarget_definition())
    require(characterized, f"No humanoid template matched {path_of(mesh)}")
    fbik = bool(controller.apply_auto_fbik())
    require(fbik, f"Automatic FBIK setup failed for {path_of(mesh)}")
    chains = []
    for chain in controller.get_retarget_chains():
        name = str(chain.get_editor_property("chain_name"))
        chains.append({
            "name": name,
            "start": str(controller.get_retarget_chain_start_bone(name)),
            "end": str(controller.get_retarget_chain_end_bone(name)),
            "goal": str(controller.get_retarget_chain_goal(name)),
        })
    missing = REQUIRED_CHAINS - {c["name"] for c in chains}
    require(not missing, f"Missing humanoid chains: {sorted(missing)}")
    return {
        "auto_characterized": characterized, "auto_fbik": fbik,
        "pelvis": str(controller.get_retarget_root()),
        "root_motion_bone": str(controller.get_root_motion_bone()),
        "chains": chains, "solver_count": int(controller.get_num_solvers()),
    }


def configure_retargeter(retargeter, source_rig, target_rig, source, target):
    controller = unreal.IKRetargeterController.get_controller(retargeter)
    src = unreal.RetargetSourceOrTarget.SOURCE
    dst = unreal.RetargetSourceOrTarget.TARGET
    controller.set_ik_rig(src, source_rig)
    controller.set_ik_rig(dst, target_rig)
    controller.set_preview_mesh(src, source)
    controller.set_preview_mesh(dst, target)
    controller.add_default_ops()
    controller.assign_ik_rig_to_all_ops(src, source_rig)
    controller.assign_ik_rig_to_all_ops(dst, target_rig)
    controller.auto_map_chains(unreal.AutoMapChainType.EXACT, True)
    mappings = {}
    for name in sorted(REQUIRED_CHAINS):
        mappings[name] = str(controller.get_source_chain(name))
        require(mappings[name] == name, f"Unmapped required retarget chain {name}: {mappings[name]}")
    controller.auto_align_all_bones(dst, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
    return {
        "required_chain_mappings": mappings,
        "ops": [str(controller.get_op_name(i)) for i in range(controller.get_num_retarget_ops())],
        "target_pose_auto_alignment": "ChainToChain; visual validation still required",
    }


def create_asset(name, directory, asset_class, factory):
    require(not unreal.EditorAssetLibrary.does_asset_exist(directory + "/" + name),
            f"Refusing existing output {directory}/{name}")
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, directory, asset_class, factory)
    require(asset is not None, f"Asset creation failed: {directory}/{name}")
    return asset


def save_new_asset(asset, output):
    asset_path = path_of(asset)
    require(asset_path.startswith(output + "/"), f"Refusing to save outside output: {asset_path}")
    require(unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False),
            f"Could not save {asset_path}")


def run(build=False, output_label="Preview01", characters=None, inspect_after=False, locomotion_set=False):
    started = time.monotonic()
    require(re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{0,47}", output_label) is not None,
            "Output label must be a short alphanumeric asset-folder name")
    characters = list(characters or TARGETS)
    require(characters and len(set(characters)) == len(characters)
            and all(c in TARGETS for c in characters), "Unknown or duplicate character")
    output = OUTPUT_ROOT + "/" + output_label
    report = {
        "created_utc": datetime.now(timezone.utc).isoformat(), "build_requested": bool(build),
        "engine_version": unreal.SystemLibrary.get_engine_version(), "output": output,
        "original_assets_saved": False, "animation_blueprint_assigned": False,
        "visual_validation_complete": False, "created_assets": [], "targets": {},
        "directional_locomotion_set": bool(locomotion_set),
    }
    saved = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))
    report_path = saved / ("TripoRetargetBuild.json" if build else "TripoRetargetPlan.json")
    try:
        if build:
            require(not unreal.EditorAssetLibrary.does_directory_exist(output),
                    f"Output directory already exists; select a fresh iteration: {output}")
        source = load_checked(SOURCE, unreal.SkeletalMesh)
        source_skeleton = source.get_editor_property("skeleton")
        animations = [load_checked(p, unreal.AnimSequence) for p in ANIMATIONS]
        for animation in animations:
            require(animation.get_editor_property("skeleton") == source_skeleton,
                    f"Animation does not use Manny skeleton: {path_of(animation)}")
        report["source"] = inspect_mesh(source)
        report["source_animations"] = [path_of(a) for a in animations]
        locomotion = unreal.load_asset("/Game/Characters/Mannequins/Anims/Unarmed/BS_Idle_Walk_Run")
        if locomotion:
            report["source_blendspace"] = {
                "asset": path_of(locomotion), "class": str(locomotion.get_class().get_name()),
                "samples": [{"animation": path_of(s.get_editor_property("animation")),
                             "position": str(s.get_editor_property("sample_value"))}
                            for s in locomotion.get_editor_property("sample_data")],
                "axes": [{"name": p.get_editor_property("display_name"),
                          "min": p.get_editor_property("min"), "max": p.get_editor_property("max")}
                         for p in locomotion.get_editor_property("blend_parameters")],
            }
        input_paths = ANIMATIONS
        expected_sequence_count = len(animations)
        if locomotion_set:
            require(isinstance(locomotion, unreal.BlendSpace), "Expected source directional BlendSpace")
            require(locomotion.get_editor_property("skeleton") == source_skeleton,
                    "Source BlendSpace does not use Manny skeleton")
            referenced = sorted({s["animation"] for s in report["source_blendspace"]["samples"]})
            require(3 <= len(referenced) <= 20, "Unexpected source locomotion dependency count")
            for path in referenced:
                animation = load_checked(path, unreal.AnimSequence)
                require(animation.get_editor_property("skeleton") == source_skeleton,
                        f"Unexpected locomotion skeleton: {path}")
            expected_sequence_count = len(referenced)
            input_paths = [path_of(locomotion)]
        # These objects are transient and never saved; preflight doesn't create packages.
        source_rig = unreal.IKRigDefinition()
        report["source"]["rig"] = configure_rig(source_rig, source)
        targets = {}
        transient_keep_alive = [source_rig]
        for name in characters:
            target = load_checked(TARGETS[name], unreal.SkeletalMesh)
            targets[name] = target
            rig = unreal.IKRigDefinition()
            retargeter = unreal.IKRetargeter()
            transient_keep_alive.extend([rig, retargeter])
            info = inspect_mesh(target)
            info["rig"] = configure_rig(rig, target)
            info["retargeter"] = configure_retargeter(retargeter, source_rig, rig, source, target)
            report["targets"][name] = info
        report["preflight_passed"] = True
        if build:
            source_rig = create_asset("IK_Manny_Source", output, unreal.IKRigDefinition,
                                      unreal.IKRigDefinitionFactory())
            report["created_assets"].append(path_of(source_rig))
            configure_rig(source_rig, source)
            save_new_asset(source_rig, output)
            for name, target in targets.items():
                directory = output + "/" + name
                rig = create_asset("IK_" + name, directory, unreal.IKRigDefinition,
                                   unreal.IKRigDefinitionFactory())
                report["created_assets"].append(path_of(rig))
                configure_rig(rig, target)
                retargeter = create_asset("RTG_MannyTo" + name, directory, unreal.IKRetargeter,
                                          unreal.IKRetargetFactory())
                report["created_assets"].append(path_of(retargeter))
                configure_retargeter(retargeter, source_rig, rig, source, target)
                save_new_asset(rig, output)
                save_new_asset(retargeter, output)
                inputs = unreal.IKRetargetBatchOperationInputs()
                fields = {
                    "assets_to_retarget": [unreal.EditorAssetLibrary.find_asset_data(p) for p in input_paths],
                    "source_mesh": source, "target_mesh": target, "ik_retarget_asset": retargeter,
                    "target_path": directory + "/Animations", "suffix": "_" + name,
                    "use_source_path": False, "include_referenced_assets": bool(locomotion_set),
                    "overwrite_existing_files": False,
                }
                for key, value in fields.items():
                    inputs.set_editor_property(key, value)
                results = unreal.IKRetargetBatchOperation.run_batch_retarget(inputs)
                expected_count = expected_sequence_count + int(locomotion_set)
                require(len(results) == expected_count,
                        f"Expected {expected_count} retargeted animation assets for {name}, got {len(results)}")
                animation_info = []
                for data in results:
                    animation = data.get_asset()
                    report["created_assets"].append(path_of(animation))
                    require(isinstance(animation, (unreal.AnimSequence, unreal.BlendSpace)),
                            "Unexpected retarget output type")
                    actual_skeleton = animation.get_editor_property("skeleton")
                    require(actual_skeleton == target.get_editor_property("skeleton"),
                            f"Output uses incorrect skeleton: {path_of(animation)}")
                    if isinstance(animation, unreal.BlendSpace):
                        samples = animation.get_editor_property("sample_data")
                        for sample in samples:
                            clip = sample.get_editor_property("animation")
                            require(clip and path_of(clip).startswith(directory + "/Animations/"),
                                    "Retargeted BlendSpace retains a non-output animation reference")
                            require(clip.get_editor_property("skeleton") == actual_skeleton,
                                    "BlendSpace sample skeleton mismatch")
                        report["targets"][name]["output_blendspace"] = {
                            "asset": path_of(animation), "class": str(animation.get_class().get_name()),
                            "sample_count": len(samples), "all_samples_match_target_skeleton": True,
                            "input": "Vector(direction_degrees, speed_cm_per_second, 0)",
                            "walk_speed": 300, "jog_speed": 600,
                        }
                        save_new_asset(animation, output)
                        continue
                    require(float(animation.get_play_length()) > 0, "Empty animation duration")
                    save_new_asset(animation, output)
                    animation_info.append({"asset": path_of(animation), "skeleton": path_of(actual_skeleton),
                                           "skeleton_matches_target": True, "duration": float(animation.get_play_length())})
                report["targets"][name]["output_animations"] = animation_info
        report["status"] = "built_needs_visual_review" if build else "preflight_passed"
        if inspect_after:
            import runpy
            runpy.run_path(str(Path(__file__).with_name("InspectTripoAssets.py")), run_name="__main__")
        unreal.log("CIRE_TRIPO_RETARGET_BUILD_PASS" if build else "CIRE_TRIPO_RETARGET_PREFLIGHT_PASS")
        return report
    except Exception as error:
        report["status"] = "failed"
        report["error"] = str(error)
        raise
    finally:
        report["duration_seconds"] = round(time.monotonic() - started, 3)
        saved.mkdir(parents=True, exist_ok=True)
        temporary = report_path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(report, indent=2), encoding="utf-8")
        temporary.replace(report_path)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--output-label", default="Preview01")
    parser.add_argument("--character", action="append", choices=list(TARGETS))
    parser.add_argument("--inspect-after", action="store_true")
    parser.add_argument("--locomotion-set", action="store_true", help="Retarget the existing 2D BlendSpace and its 17 unique clips")
    options = parser.parse_args()
    flags = set(unreal.SystemLibrary.get_command_line().lower().split())
    options.build |= "-ciretriporetargetbuild" in flags
    options.locomotion_set |= "-ciretripolocomotionset" in flags
    options.inspect_after |= "-ciretripoinspectafter" in flags
    run(options.build, options.output_label, options.character, options.inspect_after, options.locomotion_set)
