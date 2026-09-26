"""UE 5.8: locomotion BlendSpaces for the Tripo champion bodies (tripo-races) + measurements for ChampionArt.tripo.json.

For each body in Content/Data/ChampionArt.tripo.json ("champions" rows) this builds
<folder>/Animations/BS_Idle_Walk_Run_<Name> on the body's own skeleton from its native Tripo idle/walk/run clips.
Axes and sample layout are copied from the lancer champion's BlendSpace (Direction x Speed), so the champion
runtime (CireChampionArt HasMatchingLocomotion / SetBlendSpacePosition) treats it exactly like the other bodies:
idle at speed 0, walk / run at the lancer's walk / run speeds (directional samples reuse the forward clip).
It also reports the native clips' lengths and pelvis drift, and each Tripo prop's cross-section profile along
its long axis (used to author the WeaponGrips handles). Report: Saved/TripoChampionMotion.json.

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=Tools/BuildTripoChampionMotion.py -unattended -nullrhi
"""
import json
import stat
from pathlib import Path

import unreal

LANCER_BS = "/Game/Art/Characters/TripoBatch/Batch01/Locomotion/lancer/Animations/BS_Idle_Walk_Run_lancer"
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
lib = unreal.EditorAssetLibrary


def opts(mesh):
    o = unreal.AnimPoseEvaluationOptions()
    o.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW)
    o.set_editor_property("optional_skeletal_mesh", mesh)
    o.set_editor_property("should_retarget", False)
    o.set_editor_property("extract_root_motion", False)
    o.set_editor_property("incorporate_root_motion_into_pose", False)
    return o


def pelvis_track(anim, mesh):
    out, length = [], float(anim.get_play_length())
    for i in range(9):
        p = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, length * i / 8, opts(mesh))
        t = unreal.AnimPoseExtensions.get_bone_pose(p, "pelvis", unreal.AnimPoseSpaces.WORLD).translation
        out.append((round(t.x, 2), round(t.y, 2), round(t.z, 2)))
    return {"length": round(length, 3), "pelvis": out}


def in_place(anim, mesh, path):
    """Copy of a native Tripo walk/run with the pelvis's forward travel removed (Tripo bakes locomotion into the
    pelvis; the champion runtime ignores root motion). The per-cycle drift is subtracted linearly, so the clip
    still loops. Returns (clip, travel cm per second at raw mesh scale)."""
    file = ROOT / "Content" / (path[len("/Game/"):] + ".uasset")
    if file.exists():
        file.chmod(file.stat().st_mode | stat.S_IWRITE)
        lib.delete_asset(path)
    clip = lib.duplicate_asset(anim.get_path_name().split(".")[0], path)
    model = clip.get_editor_property("data_model_interface")
    keys = int(model.get_number_of_keys())
    rate = model.get_frame_rate()
    fps = float(rate.numerator) / float(rate.denominator)
    poses = [unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, min(i / fps, float(anim.get_play_length())), opts(mesh)) for i in range(keys)]
    world = [unreal.AnimPoseExtensions.get_bone_pose(p, "pelvis", unreal.AnimPoseSpaces.WORLD).translation for p in poses]
    dx, dy = world[-1].x - world[0].x, world[-1].y - world[0].y
    # Some Tripo runs also start with the pelvis a stride ahead of the root: re-centre the cycle's mean over the bind pelvis.
    bind = unreal.AnimPoseExtensions.get_ref_bone_pose(poses[0], "pelvis", unreal.AnimPoseSpaces.WORLD).translation
    ox = sum(w.x - dx * i / max(1, keys - 1) for i, w in enumerate(world)) / keys - bind.x
    oy = sum(w.y - dy * i / max(1, keys - 1) for i, w in enumerate(world)) / keys - bind.y
    pos, rots, scales = [], [], []
    for i, p in enumerate(poses):
        f = i / max(1, keys - 1)
        root = unreal.AnimPoseExtensions.get_bone_pose(p, "root", unreal.AnimPoseSpaces.WORLD)
        local = unreal.AnimPoseExtensions.get_bone_pose(p, "pelvis", unreal.AnimPoseSpaces.LOCAL)
        w = world[i]
        fixed = unreal.Vector(w.x - dx * f - ox, w.y - dy * f - oy, w.z)
        rel = root.rotation.unrotate_vector(fixed - root.translation)
        pos.append(unreal.Vector(rel.x / root.scale3d.x, rel.y / root.scale3d.y, rel.z / root.scale3d.z))
        rots.append(local.rotation)
        scales.append(local.scale3d)
    controller = clip.get_editor_property("controller")
    controller.open_bracket("tripo-races in place", False)
    try:
        if not controller.set_bone_track_keys("pelvis", pos, rots, scales, False):
            raise RuntimeError("pelvis track write failed " + path)
    finally:
        controller.close_bracket(False)
    clip.set_editor_property("enable_root_motion", False)
    clip.set_editor_property("force_root_lock", True)
    lib.save_loaded_asset(clip, False)
    return clip, (dx * dx + dy * dy) ** .5 / max(1e-3, float(anim.get_play_length()))


def kind(anim_name):
    n = anim_name.lower()
    return "run" if ("run" in n or "jog" in n or "sprint" in n) else "walk" if "walk" in n else "idle"


def build_blend(row, lancer, report):
    mesh = unreal.load_asset(row["mesh"].split(".")[0])
    clips = {k: unreal.load_asset(row["animations"][k].split(".")[0]) for k in ("idle", "walk", "run")}
    skeleton = mesh.get_editor_property("skeleton")
    for k, c in clips.items():
        if not isinstance(c, unreal.AnimSequence) or c.get_editor_property("skeleton") != skeleton:
            raise RuntimeError("%s: %s clip missing or on another skeleton" % (row["profileId"], k))
    path = row["locomotion"].split(".")[0]
    folder, name = path.rsplit("/", 1)
    base = row["mesh"].split(".")[1]
    scale = row["heightCm"] / (mesh.get_bounds().box_extent.z * 2)
    speeds = {"idle": 0.0}
    native = {k: clips[k] for k in ("walk", "run")}
    for k in ("walk", "run"):
        clips[k], raw_speed = in_place(native[k], mesh, "%s/%s_%s_inplace" % (folder, base, k))
        speeds[k] = round(raw_speed * scale, 1)
        # champion-hq: newer Tripo exports bake walk/run in place (no pelvis travel). Use the tripo-races bodies'
        # measured ground speeds, scaled by height, so the BlendSpace keeps distinct idle / walk / run samples.
        if speeds[k] < 20.0:
            speeds[k] = round({"walk": 110.0, "run": 395.0}[k] * row["heightCm"] / 180.0, 1)
    file = ROOT / "Content" / (path[len("/Game/"):] + ".uasset")
    if file.exists():
        file.chmod(file.stat().st_mode | stat.S_IWRITE)
        lib.delete_asset(path)
    factory = unreal.BlendSpaceFactoryNew()
    factory.set_editor_property("target_skeleton", skeleton)
    blend = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.BlendSpace, factory)
    blend.set_editor_property("blend_parameters", lancer.get_editor_property("blend_parameters"))
    samples, layout = [], []
    for s in lancer.get_editor_property("sample_data"):
        k = kind(s.get_editor_property("animation").get_name())
        n = unreal.BlendSample()
        n.set_editor_property("animation", clips[k])
        v = s.get_editor_property("sample_value")
        # Samples sit at the clip's own ground speed (scaled to the champion height), so feet do not skate.
        v = unreal.Vector(v.x, speeds[k], v.z)
        if (round(v.x, 1), round(v.y, 1)) in {(a, b) for _, a, b in layout}:
            continue
        n.set_editor_property("sample_value", v)
        n.set_editor_property("rate_scale", 1.0)
        samples.append(n)
        layout.append([k, round(v.x, 1), round(v.y, 1)])
    blend.set_editor_property("sample_data", samples)
    # champion-hq: rebuild the runtime triangulation (set_editor_property alone leaves it stale, so the body stood in a
    # walk frame / T-pose arms at rest). UCireEditorAnimTools is the game module's editor helper.
    if hasattr(unreal, "CireEditorAnimTools") and not unreal.CireEditorAnimTools.resample_blend_space(blend):
        raise RuntimeError("BlendSpace triangulation failed " + path)
    lib.save_loaded_asset(blend, False)
    report[row["profileId"]] = {"blend": path, "samples": layout, "speeds": speeds, "scale": round(scale, 4),
                                "clips": {k: pelvis_track(c, mesh) for k, c in clips.items()},
                                "inPlace": {k: clips[k].get_path_name() for k in ("walk", "run")},
                                "native_attack_lengths": {k: round(float(unreal.load_asset(v.split(".")[0]).get_play_length()), 3)
                                                          for k, v in row["animations"].items() if k not in ("idle", "walk", "run")}}


def prop_profile(path):
    mesh = unreal.load_asset(path.split(".")[0])
    verts = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, 0, 0)[0]
    xs = [v.x for v in verts]
    lo, hi = min(xs), max(xs)
    bins = {}
    for v in verts:
        b = int((v.x - lo) // 4)
        e = bins.setdefault(b, [1e9, -1e9, 1e9, -1e9])
        e[0], e[1], e[2], e[3] = min(e[0], v.y), max(e[1], v.y), min(e[2], v.z), max(e[3], v.z)
    rows = [[round(lo + b * 4 + 2, 1), round(e[0], 1), round(e[1], 1), round(e[2], 1), round(e[3], 1)] for b, e in sorted(bins.items())]
    return {"verts": len(verts), "x": [round(lo, 1), round(hi, 1)], "bins_x_ymin_ymax_zmin_zmax": rows}


def main():
    report = {"bodies": {}, "props": {}}
    # champion-hq: CIRE_CHAMPION_ART_FILES=ChampionArt.hq.json (comma list) builds other row files; CIRE_CHAMPION_ART_ONLY
    # limits the rows to those profile ids. Default: the tripo-races file, as before.
    import os
    files = [f for f in os.environ.get("CIRE_CHAMPION_ART_FILES", "ChampionArt.tripo.json").split(",") if f]
    only = [s for s in os.environ.get("CIRE_CHAMPION_ART_ONLY", "").split(",") if s]
    data = {"champions": [], "props": {}}
    for f in files:
        part = json.loads((ROOT / "Content/Data" / f).read_text(encoding="utf-8"))
        data["champions"] += [r for r in part.get("champions", []) if not only or r["profileId"] in only]
        if not only:
            data["props"].update(part.get("props", {}))
    lancer = unreal.load_asset(LANCER_BS)
    report["lancer"] = {"params": [[str(p.get_editor_property("display_name")), p.get_editor_property("min"), p.get_editor_property("max")]
                                   for p in lancer.get_editor_property("blend_parameters")][:2]}
    try:
        for row in data["champions"]:
            if row.get("locomotion"):
                build_blend(row, lancer, report["bodies"])
        for prop in data["props"].values():
            report["props"][prop["asset"]] = prop_profile(prop["asset"])
        report["status"] = "pass"
        unreal.log("CIRE_TRIPO_CHAMPION_MOTION_PASS bodies=%d" % len(report["bodies"]))
    except Exception as error:
        report["status"] = "failed"
        import traceback; report["error"] = traceback.format_exc()
        unreal.log_error("CIRE_TRIPO_CHAMPION_MOTION_FAIL " + str(error))
    finally:
        (ROOT / "Saved" / "TripoChampionMotion.json").write_text(json.dumps(report, indent=1), encoding="utf-8")


main()
