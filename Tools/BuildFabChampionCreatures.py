"""UE 5.8 commandlet: measure the purchased Fab creature bodies used by creature CHAMPIONS and write
Content/Data/ChampionArtBindings.fab.json.

The Bear champion takes the ROG Creatures bear, the Evergrove Centaur the Quadruped Fantasy Creatures centaur
(with its armour, shoulder pads, mane, beard and bow as leader-pose parts). Both run on the native monster path
(UCireCreatureArt::ApplyBinding, motion "monster_native", "reactions": true): gait phase locked to ground speed,
alternating strikes whose contact frame meets the server release, a clip per cast, a flinch and a held death pose.
The committed ChampionArtBindings.json rows (procedural bear, spatial-skin centaur) stay the fallback: the runtime
takes a Fab row only when its mesh and locomotion clips exist locally (the packs are licensed, never committed).

Measurements reuse Tools/BuildFabCreatures.py measure() (facing yaw, head height, in-place stride), plus:
  meshScale     wanted head height / reference head height (vendor skeletal bounds are loose)
  walk/runSpeedRaw  ground speed of the in-place cycles in mesh units (planted-paw stride, or root travel)
  contact       seconds into each strike where the striking limb is farthest forward (or, "speed", moves fastest)

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/BuildFabChampionCreatures.py -unattended -nullrhi
Log marker CIRE_FAB_CHAMPION_CREATURES_PASS / _FAIL; report Saved/FabChampionCreatures.json.
"""
import json
import math
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
_ns = {"CIRE_IMPORT_ONLY": True, "__file__": str(ROOT / "Tools/BuildFabCreatures.py")}
exec(compile((ROOT / "Tools/BuildFabCreatures.py").read_text(encoding="utf-8"), "BuildFabCreatures.py", "exec"), _ns)
measure, exists = _ns["measure"], _ns["exists"]
R, Q = "/Game/ROG_Creatures", "/Game/QuadrapedCreatures"
QC = Q + "/Centaur"

# profile: body, wanted head height (cm at actor scale 1), locomotion/reaction roles, casts (skill id or kind -> clip),
# contact rule per strike (limb socket, "reach" = farthest forward from the body, "speed" = fastest), leader-pose parts.
CHAMPIONS = {
    "bear": dict(
        mesh=R + "/Bear/Meshes/SK_Bear_Fur_Full", head=132, rig="quadruped", folder=R + "/Bear/Animations",
        sockets=dict(head="b_head", pelvis="b_spine_02", spine_03="b_spine_03", hand_l="b_L_F_toes", hand_r="b_R_F_toes",
                     foot_l="b_L_B_toes", foot_r="b_R_B_toes", ball_l="b_L_F_toes", ball_r="b_R_F_toes"),
        roles=dict(idle="A_Bear_idle", walk="A_Bear_walk", run="A_Bear_run", attack="A_Bear_attack_03", attackAlt="A_Bear_attack_04",
                   hit="A_Bear_Hit_Body_Front", death="A_Bear_death"),
        # Gravewood Maul / Rootbreaker Charge: the heavy two-paw strike; roars and guards: the aggressive stand.
        casts=dict(bear_maul="A_Bear_attack_01", bear_charge="A_Bear_attack_02", ability="A_Bear_attack_01",
                   shout="A_Bear_aggressive_1", war_cry="A_Bear_aggressive_1", bear_roar="A_Bear_aggressive_1",
                   iron_guard="A_Bear_aggressive_2", bear_hibernate="A_Bear_aggressive_2", spell="A_Bear_aggressive_2",
                   bear_colossus="A_Bear_aggressive_1"),  # fab-coverage: Elder of the Deepwood roars as it grows
        contact=dict(attack=("paws", "reach"), attackAlt=("paws", "reach"), bear_maul=("paws", "reach"), bear_charge=("paws", "reach"),
                     ability=("paws", "reach"), shout=("head", "speed"), war_cry=("head", "speed"), bear_roar=("head", "speed"),
                     iron_guard=("head", "speed"), bear_hibernate=("head", "speed"), spell=("head", "speed"), bear_colossus=("head", "speed")),
        parts=[],
        # The brown coat: the feral_ursoth boss (same pack) keeps the mesh's default black one under its race tint.
        swap={R + "/Bear/Materials/MI_Bear_Body_Black_ORM": R + "/Bear/Materials/MI_Bear_Body_Brown_ORM",
              R + "/Bear/Materials/MI_Fur_Bear_Black": R + "/Bear/Materials/MI_Fur_Bear_Brown"}),
    "evergrove_centaur": dict(
        mesh=QC + "/Meshes/SK_Centaur", head=222, rig="quadruped", folder=QC + "/Animations",
        sockets=dict(head="CENTAUR_-Head", pelvis="CENTAUR_-Spine", spine_03="CENTAUR_-Spine2", hand_l="CENTAUR_HAND_L", hand_r="CENTAUR_HAND_R",
                     foot_l="CENTAUR_-L-Foot", foot_r="CENTAUR_-R-Foot", ball_l="CENTAUR_-L-Foot", ball_r="CENTAUR_-R-Foot"),
        # A ranged warden: the bow set (bow in hand), the loose on every basic attack (Grove Javelin reads as a shot).
        roles=dict(idle="ANIM_Centaur_IdleBow", walk="ANIM_Centaur_WalkBow", run="ANIM_Centaur_GallopBow",
                   attack="ANIM_Centaur_ShootArrowToIdleAiming", hit="ANIM_Centaur_GetHitBow", death="ANIM_Centaur_DeathBow"),
        # Heals and blessings raise the bow to the sky; the rearing kick marks the trail / herd call / sanctuary.
        casts=dict(spell="ANIM_Centaur_IdleNormalToIdleAiming", restoring_light="ANIM_Centaur_IdleNormalToIdleAiming",
                   purify="ANIM_Centaur_IdleNormalToIdleAiming", centaur_grove_javelin="ANIM_Centaur_ShootArrowToIdleAiming",
                   ability="ANIM_Centaur_LegAttackBow", sanctuary="ANIM_Centaur_LegAttackBow", centaur_trailblaze="ANIM_Centaur_LegAttackBow",
                   centaur_herd_call="ANIM_Centaur_LegAttackBow", shout="ANIM_Centaur_LegAttackBow",
                   centaur_spring_march="ANIM_Centaur_LegAttackAiming"),  # fab-coverage: rears with the bow drawn
        contact=dict(attack=("hand_r", "speed"), centaur_grove_javelin=("hand_r", "speed"),
                     spell=("hand_l", "frac:.65"), restoring_light=("hand_l", "frac:.65"), purify=("hand_l", "frac:.65"),
                     ability=("front_hooves", "reach"), sanctuary=("front_hooves", "reach"), centaur_trailblaze=("front_hooves", "reach"),
                     centaur_herd_call=("front_hooves", "reach"), shout=("front_hooves", "reach"), centaur_spring_march=("front_hooves", "reach")),
        parts=["SK_Body_Armor", "SK_Shoulder_Pads", "SK_Mane", "SK_Beard", "SK_Bow_Action"],
        # A different coat and hair than the wild_outrider monsters (same pack) so the champion reads apart.
        swap={QC + "/Materials/M_Centaur_Body_1": QC + "/Materials/M_Centaur_Body_3",
              QC + "/Materials/M_Centaur_Hair_1": QC + "/Materials/M_Centaur_Hair_3"}),
}


def obj(path):
    return "%s.%s" % (path, path.rsplit("/", 1)[1])


def contact_time(anim, skeleton_root, root_ref, bones, mode, forward):
    """Seconds into the clip where the limb set is farthest forward from the root (reach) or moves fastest (speed)."""
    opts = u.AnimPoseEvaluationOptions()
    length = float(anim.get_play_length())
    if mode.startswith("frac:"):  # clips that end in a held pose (the bow raised): release before the end
        return round(length * float(mode[5:]), 3)
    steps = max(8, int(length * 60))
    best_t, best_v, last = 0.0, -1e9, None
    for k in range(steps + 1):
        t = length * k / steps
        pose = u.AnimPoseExtensions.get_anim_pose_at_time(anim, t, opts)
        root = u.AnimPoseExtensions.get_bone_pose(pose, skeleton_root, u.AnimPoseSpaces.WORLD)
        # component space with the root held at its reference transform (lockRoot), like BuildFabCreatures.measure
        pts = [u.MathLibrary.transform_location(root_ref, u.MathLibrary.inverse_transform_location(
            root, u.AnimPoseExtensions.get_bone_pose(pose, b, u.AnimPoseSpaces.WORLD).translation)) for b in bones]
        if mode == "reach":
            v = max(p.x * forward[0] + p.y * forward[1] for p in pts)
        else:
            v = 0.0 if last is None else max(math.dist((p.x, p.y, p.z), (q.x, q.y, q.z)) for p, q in zip(pts, last))
            last = pts
        if v > best_v:
            best_v, best_t = v, t
    # The action fades out over its last 0.25 s: the release must land while it still shows.
    return round(min(best_t, max(0.0, length - .3)), 3)


def main():
    report, rows = {}, []
    try:
        for profile, spec in CHAMPIONS.items():
            if not exists(spec["mesh"]):
                report[profile] = {"skipped": "pack not installed"}
                continue
            mspec = dict(spec, named=spec["casts"])
            m = measure(mspec)
            report[profile] = m
            clips = m["clips"]
            missing = [r for r in ("idle", "walk", "run", "attack", "hit", "death") if r not in clips]
            if missing:
                report[profile]["error"] = "missing roles " + ",".join(missing)
                continue
            scale = spec["head"] / max(1.0, m["headZ"])

            def raw_speed(c, duty):  # mesh units per second (BuildFabCreatures.gait_speed at scale 1)
                if c["travel"] > .25 * c["stride"]:
                    return c["travel"] / max(.1, c["length"])
                return c["stride"] / max(.1, 2 * duty * c["length"])
            walk_raw, run_raw = raw_speed(clips["walk"], .6), raw_speed(clips["run"], .45)
            mesh = u.load_asset(spec["mesh"])
            skeleton = mesh.get_editor_property("skeleton")
            ref = u.AnimPoseExtensions.get_reference_pose(skeleton)
            root_name = str(u.AnimPoseExtensions.get_bone_names(ref)[0])
            root_ref = u.AnimPoseExtensions.get_bone_pose(ref, root_name, u.AnimPoseSpaces.WORLD)
            yaw = math.radians(-m["yaw"])  # mesh forward in component space (inverse of the facing yaw)
            forward = (math.cos(yaw), math.sin(yaw))
            s = mspec["sockets"]
            limb = {"paws": [s["ball_l"], s["ball_r"]], "front_hooves": [s["ball_l"], s["ball_r"]], "head": [s["head"]],
                    "hand_r": [s["hand_r"]], "hand_l": [s["hand_l"]]}
            animations = {r: clips[r]["asset"] for r in spec["roles"] if r in clips}
            casts = {k: clips[k]["asset"] for k in spec["casts"] if k in clips}
            contact = {}
            for key, (bones, mode) in spec["contact"].items():
                a = clips.get(key)
                if a:
                    contact[key] = contact_time(u.load_asset(a["asset"]), root_name, root_ref, limb[bones], mode, forward)
            parts = []
            for p in spec["parts"]:
                path = "%s/%s" % (spec["mesh"].rsplit("/", 1)[0], p)
                part = u.load_asset(path) if exists(path) else None
                if part and part.get_editor_property("skeleton") == skeleton:
                    parts.append(obj(path))
            swap = {}
            for a, b in spec.get("swap", {}).items():
                if exists(a) and exists(b):
                    swap[obj(a)] = obj(b)
            animations["casts"] = casts
            animations["contact"] = contact
            rows.append({
                "profileId": profile, "status": "custom_ready", "motion": "monster_native", "reactions": True,
                "mesh": obj(spec["mesh"]), "heightCm": round(spec["head"] / .95, 1), "meshScale": round(scale, 4),
                "yaw": m["yaw"], "groundAtPivot": True, "lockRoot": True,
                "walkSpeedRaw": round(walk_raw, 1), "runSpeedRaw": round(max(walk_raw * 1.5, run_raw), 1),
                "animations": animations, "parts": parts, **({"materialSwap": swap} if swap else {}),
                "source": "Fab (licensed, local only): see Docs/FAB-PURCHASED.md"})
        path = ROOT / "Content/Data/ChampionArtBindings.fab.json"
        # paladin-hq: keep the humanoid plate-body rows written by Tools/BuildFabPaladins.py.
        rows += [r for r in json.loads(path.read_text(encoding="utf-8")).get("bindings", []) if r.get("motion") == "humanoid"] if path.exists() else []
        doc = {"schemaVersion": 1,
               "description": "Purchased Fab creature bodies for creature champions (Docs/FabIntegration.md). Generated by "
                              "Tools/BuildFabChampionCreatures.py. A row replaces the committed ChampionArtBindings.json row only "
                              "when its mesh and locomotion clips exist locally; -CireNoFab / -CireNoFabCreatures keep the committed art.",
               "bindings": rows}
        path.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
        u.log("CIRE_FAB_CHAMPION_CREATURES_PASS bindings=%d" % len(rows))
    except Exception as error:
        import traceback
        report["error"] = traceback.format_exc()
        u.log_error("CIRE_FAB_CHAMPION_CREATURES_FAIL " + str(error))
    finally:
        (ROOT / "Saved/FabChampionCreatures.json").write_text(json.dumps(report, indent=1), encoding="utf-8")


main()
