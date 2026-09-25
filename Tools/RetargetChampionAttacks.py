"""UE 5.8: transfer the Tripo batch-03 action clips onto every Tripo champion body.

Output: /Game/Art/Characters/ChampionAttacks02/<Target>/A_<Target>_<clip>
  slash (trimmed to its single swing, 0.80-3.30 s of the 6.58 s source), cast_a_spell, war_cry,
  plus attack_bow / attack_crossbow on the archer body (Ranger).

Sources are the per-monster copies of the Tripo motion library: slash from IronboundBruiserV2,
cast_a_spell from BlightCaster, war_cry from GravemawPackLeader, attack_bow from BarbedHunter,
attack_crossbow from BarbedHunterB.

Method. The first build used the RetargetTripo.py IK-retargeter pattern (auto IK rigs + retargeter,
run_batch_retarget). With the imported root scale of 100 on BOTH source and target, every output
collapsed: root scale written as 1 and the pelvis at ~9 of 53.6 units (legs folded under the body),
see Saved/ChampionAttacks02Build.json history in Docs/MonsterArt.md. Monster and champion rigs are
the same Tripo 61-bone hierarchy with identical bone names, so this build transfers the motion
directly: for every key, each target bone takes the source bone's component-space rotation change
from its bind pose (Qt = Qs * Qs_bind^-1 * Qt_bind), keeps its own bind-pose bone lengths (so the
target's proportions and root scale 100 are preserved exactly), and the pelvis translation delta is
scaled by the target/source pelvis height. That is the chain-to-chain FK mapping an IK retargeter
performs, without the root-scale failure; there is no foot IK, so validation checks the soles.

Each clip is written into a duplicate of a clip that already uses the target skeleton (tracks and
skeleton binding come with it); root motion is disabled and root lock forced. No original mesh,
skeleton, monster clip or Preview/Batch01 asset is saved. Validation (RAW and COMPRESSED): root
scale 100, pelvis height within 35% of bind at rest, head above feet, finite, compact.

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=Tools/RetargetChampionAttacks.py -unattended -nullrhi
     (-CireChampionAttacksRebuild deletes and rebuilds only /Game/Art/Characters/ChampionAttacks02)
     (-CireChampionAttacksAdd is additive: only the Tripo champion bodies in Content/Data/ChampionArt.tripo.json
      whose <attacksFolder> does not exist yet; the template is the body's own native idle. Existing clips are untouched.)
Report: Saved/ChampionAttacks02Build.json
"""
import json
import math
import stat
import time
from pathlib import Path

import unreal

OUT = "/Game/Art/Characters/ChampionAttacks02"
CLIPS = {
    "slash": ("/Game/Tripo/Monsters/IronboundBruiserV2/CTS_Monster_IronboundBruiserV2",
              "/Game/Tripo/Monsters/IronboundBruiserV2/Animations/CTS_Monster_IronboundBruiserV2_slash"),
    "cast_a_spell": ("/Game/Tripo/Monsters/BlightCaster/CTS_Monster_BlightCaster",
                     "/Game/Tripo/Monsters/BlightCaster/Animations/CTS_Monster_BlightCaster_cast_a_spell"),
    "war_cry": ("/Game/Tripo/Monsters/GravemawPackLeader/CTS_Boss_GravemawPackLeader",
                "/Game/Tripo/Monsters/GravemawPackLeader/Animations/CTS_Boss_GravemawPackLeader_war_cry"),
    "attack_bow": ("/Game/Tripo/Monsters/BarbedHunter/CTS_Monster_BarbedHunter",
                   "/Game/Tripo/Monsters/BarbedHunter/Animations/CTS_Monster_BarbedHunter_attack_bow"),
    "attack_crossbow": ("/Game/Tripo/Monsters/BarbedHunterB/CTS_Monster_BarbedHunterB",
                        "/Game/Tripo/Monsters/BarbedHunterB/Animations/CTS_Monster_BarbedHunterB_attack_crossbow"),
}
COMMON = ["slash", "cast_a_spell", "war_cry"]
TRIM = {"slash": (0.80, 3.30)}
LEGACY = {
    "Warden": ("/Game/TripoModels/medieval_knight_armor_3d_model/medieval_knight_armor_3d_model", "/Game/Art/Characters/CombatPrototype01/Warden/A_Warden_Attack"),
    "Ranger": ("/Game/TripoModels/armored_archer_3d_model/armored_archer_3d_model", "/Game/Art/Characters/CombatPrototype01/Ranger/A_Ranger_Attack"),
    "Scholar": ("/Game/TripoModels/battlefield_healer_3d_model/battlefield_healer_3d_model", "/Game/Art/Characters/CombatPrototype01/Scholar/A_Scholar_Attack"),
}
EXTRA = {"Ranger": ["attack_bow", "attack_crossbow"]}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def path_of(obj):
    return str(obj.get_path_name()).split(".")[0] if obj else None


# Quaternion helpers: Hamilton product, qm(a, b) applies b then a (same as FQuat a * b).
def v3(v): return (float(v.x), float(v.y), float(v.z))
def q4(q): return (float(q.x), float(q.y), float(q.z), float(q.w))
def add(a, b): return tuple(x + y for x, y in zip(a, b))
def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def mul(a, k): return tuple(x * k for x in a)
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def qinv(q): return (-q[0], -q[1], -q[2], q[3])


def qm(a, b):
    av, bv = a[:3], b[:3]
    xyz = add(add(mul(bv, a[3]), mul(av, b[3])), cross(av, bv))
    return (xyz[0], xyz[1], xyz[2], a[3] * b[3] - dot(av, bv))


def qnorm(q):
    n = math.sqrt(dot(q, q)) or 1.0
    return tuple(x / n for x in q)


def rot(q, v): return qm(qm(q, (v[0], v[1], v[2], 0.0)), qinv(q))[:3]


def targets():
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    bindings = json.loads((root / "Content/Data/ChampionArtBindings.json").read_text(encoding="utf-8"))
    out, seen = {}, set()
    for name, row in LEGACY.items():
        out[name] = row
        seen.add(row[0])
    for row in bindings["bindings"]:
        mesh = row["mesh"].split(".")[0]
        if row.get("status") != "ready" or mesh in seen:
            continue  # paladin_holy/righteous and troll melee/ranged share one body
        seen.add(mesh)
        out[mesh.rsplit("/", 1)[1].replace("SK_", "")] = (mesh, row["attack"].split(".")[0])
    return out


def added_targets():
    """tripo-races: new Tripo champion bodies (ChampionArt.tripo.json rows carrying attacksFolder)."""
    root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    rows = json.loads((root / "Content/Data/ChampionArt.tripo.json").read_text(encoding="utf-8"))["champions"]
    out = {}
    for row in rows:
        folder = row.get("attacksFolder")
        if folder and not unreal.EditorAssetLibrary.does_directory_exist("%s/%s" % (OUT, folder)):
            out[folder] = (row["mesh"].split(".")[0], row["animations"]["idle"].split(".")[0])
            if row.get("attacksExtra"):
                EXTRA[folder] = list(row["attacksExtra"])
    return out


def options(mesh, raw=True):
    o = unreal.AnimPoseEvaluationOptions()
    o.set_editor_property("evaluation_type", unreal.AnimDataEvalType.RAW if raw else unreal.AnimDataEvalType.COMPRESSED)
    o.set_editor_property("optional_skeletal_mesh", mesh)
    o.set_editor_property("should_retarget", False)
    o.set_editor_property("extract_root_motion", True)
    o.set_editor_property("incorporate_root_motion_into_pose", False)
    return o


def hierarchy(mesh):
    comp = unreal.SkeletalMeshComponent()
    comp.set_skeletal_mesh_asset(mesh)
    names = [str(comp.get_bone_name(i)) for i in range(comp.get_num_bones())]
    return names, {n: str(comp.get_parent_bone(n)) for n in names}


def reference(pose, names):
    world, local = {}, {}
    for n in names:
        w = unreal.AnimPoseExtensions.get_ref_bone_pose(pose, n, unreal.AnimPoseSpaces.WORLD)
        l = unreal.AnimPoseExtensions.get_ref_bone_pose(pose, n, unreal.AnimPoseSpaces.LOCAL)
        world[n] = (v3(w.translation), q4(w.rotation), v3(w.scale3d))
        local[n] = (v3(l.translation), q4(l.rotation), v3(l.scale3d))
    return world, local


def transfer(src_anim, src_mesh, target, template, final, window):
    lib = unreal.EditorAssetLibrary
    s_names, _ = hierarchy(src_mesh)
    t_names, t_parent = hierarchy(target)
    require("pelvis" in s_names and "root" in t_names, "rig without root/pelvis")
    s_ref, _ = reference(unreal.AnimPoseExtensions.get_anim_pose_at_time(src_anim, 0, options(src_mesh)), s_names)
    model = src_anim.get_editor_property("data_model_interface")
    rate = model.get_frame_rate()
    fps = float(rate.numerator) / float(rate.denominator)
    frames = int(model.get_number_of_frames())
    # Output at 30 fps (the template clips are 60 fps; 24 fps source keys are resampled in time).
    out_fps = 30
    start, end = (window if window else (0.0, frames / fps))
    end = min(end, frames / fps)
    count = max(1, int(round((end - start) * out_fps)))
    if lib.does_asset_exist(final):
        lib.delete_asset(final)
    clip = lib.duplicate_asset(template, final)
    require(isinstance(clip, unreal.AnimSequence), "template duplicate failed " + template)
    require(clip.get_editor_property("skeleton") == target.get_editor_property("skeleton"), "template skeleton differs from target")
    # tripo-races: native Tripo templates run at 24 fps, and the data controller only accepts a multiple or factor of it.
    t_rate = clip.get_editor_property("data_model_interface").get_frame_rate()
    t_fps = int(round(float(t_rate.numerator) / float(t_rate.denominator)))
    if t_fps and out_fps % t_fps and t_fps % out_fps:
        out_fps = t_fps
        count = max(1, int(round((end - start) * out_fps)))
    t_ref, t_local = reference(unreal.AnimPoseExtensions.get_anim_pose_at_time(clip, 0, options(target)), t_names)
    k = t_ref["pelvis"][0][2] / max(1e-3, s_ref["pelvis"][0][2])
    keys = {n: ([], [], []) for n in t_names}
    pelvis_world, lowest_toe, pelvis_parent = [], [], None
    bind_toe = min(t_ref["ball_l"][0][2], t_ref["ball_r"][0][2]) if "ball_l" in t_ref and "ball_r" in t_ref else None
    for index in range(count + 1):
        sp = unreal.AnimPoseExtensions.get_anim_pose_at_time(src_anim, min(end, start + index / out_fps), options(src_mesh))
        require(unreal.AnimPoseExtensions.is_valid(sp), "invalid source pose")
        world = {}
        for n in t_names:
            parent = t_parent.get(n, "None")
            bind_t, bind_q, bind_s = t_local[n]
            if parent not in world:
                # Root keeps its bind transform, including the imported scale of 100.
                world[n] = t_ref[n]
                keys[n][0].append(unreal.Vector(*bind_t))
                keys[n][1].append(unreal.Quat(*bind_q))
                keys[n][2].append(unreal.Vector(*bind_s))
                continue
            pp, pq, ps = world[parent]
            if n in s_names:
                w = unreal.AnimPoseExtensions.get_bone_pose(sp, n, unreal.AnimPoseSpaces.WORLD)
                q = qnorm(qm(qm(q4(w.rotation), qinv(s_ref[n][1])), t_ref[n][1]))
            else:
                q = qm(pq, bind_q)
            local_q = qnorm(qm(qinv(pq), q))
            if n == "pelvis":
                sw = unreal.AnimPoseExtensions.get_bone_pose(sp, "pelvis", unreal.AnimPoseSpaces.WORLD)
                pos = add(t_ref["pelvis"][0], mul(sub(v3(sw.translation), s_ref["pelvis"][0]), k))
                local_t = tuple(x / s for x, s in zip(rot(qinv(pq), sub(pos, pp)), ps))
            else:
                local_t = bind_t
                pos = add(pp, rot(pq, tuple(a * b for a, b in zip(bind_t, ps))))
            world[n] = (pos, q, tuple(a * b for a, b in zip(bind_s, ps)))
            if n == "pelvis":
                pelvis_world.append(pos)
                pelvis_parent = (pp, pq, ps)
            keys[n][0].append(unreal.Vector(*local_t))
            keys[n][1].append(unreal.Quat(*local_q))
            keys[n][2].append(unreal.Vector(*bind_s))
        if bind_toe is not None:
            lowest_toe.append(min(world["ball_l"][0][2], world["ball_r"][0][2]))
    # No foot IK: if the source stance presses the toes below the floor, lift the pelvis so the
    # typical (20th percentile) lowest toe joint sits at its bind height again.
    lift = 0.0
    if bind_toe is not None and lowest_toe:
        lift = max(0.0, bind_toe - sorted(lowest_toe)[len(lowest_toe) // 5])
        if lift > 1e-3 and pelvis_parent:
            pp, pq, ps = pelvis_parent
            lifted = []
            for pos in pelvis_world:
                local = rot(qinv(pq), sub(add(pos, (0.0, 0.0, lift)), pp))
                lifted.append(unreal.Vector(*[x / s for x, s in zip(local, ps)]))
            keys["pelvis"] = (lifted, keys["pelvis"][1], keys["pelvis"][2])
    controller = clip.get_editor_property("controller")
    controller.open_bracket("ChampionAttacks02 transfer", False)
    try:
        controller.set_frame_rate(unreal.FrameRate(out_fps, 1), False)
        controller.set_number_of_frames(unreal.FrameNumber(count), False)
        for n, (positions, rotations, scales) in keys.items():
            require(controller.set_bone_track_keys(n, positions, rotations, scales, False), "track " + n)
    finally:
        controller.close_bracket(False)
    clip.set_editor_property("enable_root_motion", False)
    clip.set_editor_property("force_root_lock", True)
    require(lib.save_loaded_asset(clip, only_if_is_dirty=False), "save failed " + final)
    return clip, {"frames": count, "fps": out_fps, "source_fps": fps, "window": [start, end], "pelvis_scale": round(k, 4), "toe_lift": round(lift, 3)}


def validate(anim, mesh):
    names, _ = hierarchy(mesh)
    length = float(anim.get_play_length())
    samples = []
    for raw in (True, False):
        for i in range(7):
            t = length * i / 6
            p = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, t, options(mesh, raw))
            require(unreal.AnimPoseExtensions.is_valid(p), "invalid pose")

            def get(n, ref=False):
                fn = unreal.AnimPoseExtensions.get_ref_bone_pose if ref else unreal.AnimPoseExtensions.get_bone_pose
                return fn(p, n, unreal.AnimPoseSpaces.WORLD)
            for n in names:
                tr = get(n)
                require(all(math.isfinite(x) for x in v3(tr.translation) + v3(tr.scale3d)), "non-finite " + n)
                require(max(abs(x) for x in v3(tr.translation)) < 250, n + " outside sane bounds")
            root = unreal.AnimPoseExtensions.get_bone_pose(p, "root", unreal.AnimPoseSpaces.LOCAL)
            require(max(abs(x - 100) for x in v3(root.scale3d)) < .01, "root scale lost")
            pelvis, ref_pelvis = get("pelvis").translation.z, get("pelvis", True).translation.z
            head = get("head").translation.z
            feet = min(get("foot_l").translation.z, get("foot_r").translation.z, get("ball_l").translation.z, get("ball_r").translation.z)
            require(head > pelvis > feet, "body order broken at %.2fs" % t)
            if i in (0, 6):
                require(abs(pelvis - ref_pelvis) < .35 * ref_pelvis, "pelvis %.1f vs bind %.1f at %.2fs" % (pelvis, ref_pelvis, t))
            samples.append({"raw": raw, "t": round(t, 3), "pelvis": round(pelvis, 2), "bind_pelvis": round(ref_pelvis, 2),
                            "head": round(head, 2), "lowest_foot": round(feet, 2)})
    return {"asset": path_of(anim), "length": round(length, 3), "samples": samples}


def main():
    started = time.monotonic()
    rebuild = "-cirechampionattacksrebuild" in unreal.SystemLibrary.get_command_line().lower()
    additive = "-cirechampionattacksadd" in unreal.SystemLibrary.get_command_line().lower()
    lib = unreal.EditorAssetLibrary
    report = {"output": OUT, "method": "bind-pose-offset FK transfer (the IK retargeter collapsed the pelvis with root scale 100)",
              "targets": {}, "created": [], "original_assets_saved": False}
    saved = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))
    content = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()))
    try:
        if lib.does_directory_exist(OUT) and not additive:
            require(rebuild, OUT + " exists; pass -CireChampionAttacksRebuild to replace it")
            for file in (content / "Art/Characters/ChampionAttacks02").rglob("*.uasset"):
                file.chmod(file.stat().st_mode | stat.S_IWRITE)
            require(lib.delete_directory(OUT), "could not clear " + OUT)
        sources = {c: (unreal.load_asset(m), unreal.load_asset(a)) for c, (m, a) in CLIPS.items()}
        for c, (m, a) in sources.items():
            require(isinstance(m, unreal.SkeletalMesh) and isinstance(a, unreal.AnimSequence), "missing source " + c)
        for name, (mesh_path, template) in (added_targets() if additive else targets()).items():
            target = unreal.load_asset(mesh_path)
            entry = {"mesh": mesh_path, "template": template, "clips": {}}
            report["targets"][name] = entry
            if not isinstance(target, unreal.SkeletalMesh) or not lib.does_asset_exist(template):
                entry["error"] = "missing target mesh or template clip"
                continue
            for clip in COMMON + EXTRA.get(name, []):
                src_mesh, src_anim = sources[clip]
                final = "%s/%s/A_%s_%s" % (OUT, name, name, clip)
                try:
                    anim, info = transfer(src_anim, src_mesh, target, template, final, TRIM.get(clip))
                    info.update(validate(anim, target))
                    info["source"] = path_of(src_anim)
                    entry["clips"][clip] = info
                    report["created"].append(final)
                except Exception as error:
                    entry["clips"][clip] = {"error": str(error)}
        failures = ["%s/%s" % (t, c) for t, e in report["targets"].items() for c, i in e.get("clips", {}).items() if "error" in i]
        failures += [t for t, e in report["targets"].items() if "error" in e]
        report["failures"] = failures
        report["status"] = "pass" if not failures else "partial"
        unreal.log("CIRE_CHAMPION_ATTACKS02_" + ("PASS" if not failures else "PARTIAL") + " clips=%d failures=%d" % (len(report["created"]), len(failures)))
    except Exception as error:
        report["status"] = "failed"
        report["error"] = str(error)
        unreal.log_error("CIRE_CHAMPION_ATTACKS02_FAIL " + str(error))
    finally:
        report["seconds"] = round(time.monotonic() - started, 1)
        (saved / ("ChampionAttacks02Build%s.json" % ("-Add" if additive else ""))).write_text(json.dumps(report, indent=1), encoding="utf-8")


main()
