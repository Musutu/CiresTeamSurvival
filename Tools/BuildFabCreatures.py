"""UE 5.8 commandlet: measure the purchased Fab creature bodies and write Content/Data/RaceMeshes.fab.json.

Creatures are driven by the native monster path (UCireMonsterAnimInstance), not the vendor AnimBPs
(Docs/FabIntegration.md): gait phase follows ground speed, casts/hits/deaths/corpses work like every unit.
For each unit below it measures, on the pack's own reference pose and clips:
  facing yaw (head vs pelvis for animals, foot->ball for humanoids), reference head height -> meshScale for the
  wanted head height, reach, walk/run ground speed from the planted-paw stride of the in-place cycles.
Writes only units whose mesh and clips exist (the packs are local-only); the runtime also re-checks.

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/BuildFabCreatures.py -unattended -nullrhi
Log marker CIRE_FAB_CREATURES_PASS / _FAIL; report Saved/FabCreatures.json.
"""
import json
import math
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
HAND_PROPS = ["hand_r", "hand_l", "spine_03", "head", "back", "spine_02", "pelvis"]
R, Q = "/Game/ROG_Creatures", "/Game/QuadrapedCreatures"
UD = "/Game/UndeadPack"

# unit: variant, mesh, head height cm (world, actor scale 1), rig, sockets (quadruped), role clips, named clips, keep props, source
UNITS = {
    "dire_wolf": dict(variant="FabRogWolf", mesh=R + "/Wolf/Meshes/SK_Wolf_Fur_Full_G", head=150, rig="quadruped",
        sockets=dict(head="b_head", pelvis="b_spine_2", spine_03="b_spine_3", hand_l="b_L_front_leg_5", hand_r="b_R_front_leg_5",
                     foot_l="b_L_back_leg_4", foot_r="b_R_back_leg_4", ball_l="b_L_front_leg_5", ball_r="b_R_front_leg_5"),
        roles=dict(idle="A_Wolf_Idle_combat", walk="A_Wolf_walk", run="A_Wolf_run", attack="A_Wolf_Attack", attackAlt="A_Wolf_Attack_light",
                   hit="A_Wolf_Hit_body_left_combat", death="A_Wolf_Death"), named=dict(war_cry="A_Wolf_howl"), folder=R + "/Wolf/Animations"),
    "bristleback": dict(variant="FabRogBoar", mesh=R + "/Boar/Meshes/SK_Boar_Fur_Full", head=80, rig="quadruped",
        sockets=dict(head="b_head", pelvis="b_spine_2", spine_03="b_spine_3", hand_l="b_L_front_leg_6", hand_r="b_R_front_leg_6",
                     foot_l="b_L_back_leg_5", foot_r="b_R_back_leg_5", ball_l="b_L_front_leg_6", ball_r="b_R_front_leg_6"),
        roles=dict(idle="A_Boar_idle_combat", walk="A_Boar_walk", run="A_Boar_run", attack="A_Boar_Attack", attackAlt="A_Boar_Attack_2",
                   hit="A_Boar_Hit_Body_left", death="A_Boar_Death"), named=dict(war_cry="A_Boar_aggressive"), folder=R + "/Boar/Animations"),
    "feral_ursoth": dict(variant="FabRogBear", mesh=R + "/Bear/Meshes/SK_Bear_Fur_Full", head=150, rig="quadruped",
        sockets=dict(head="b_head", pelvis="b_spine_02", spine_03="b_spine_03", hand_l="b_L_F_toes", hand_r="b_R_F_toes",
                     foot_l="b_L_B_toes", foot_r="b_R_B_toes", ball_l="b_L_F_toes", ball_r="b_R_F_toes"),
        roles=dict(idle="A_Bear_idle", walk="A_Bear_walk", run="A_Bear_run", attack="A_Bear_attack_01", attackAlt="A_Bear_attack_03",
                   hit="A_Bear_Hit_Body_Front", death="A_Bear_death"), named=dict(war_cry="A_Bear_aggressive_1", ground_slam="A_Bear_attack_04"),
        folder=R + "/Bear/Animations"),
    "feral_mammoth": dict(variant="FabRogMammoth", mesh=R + "/Mammoth/Meshes/SK_Mammoth_Fur_full", head=240, rig="quadruped",
        sockets=dict(head="b_head", pelvis="b_spine_2", spine_03="b_spine_3", hand_l="b_L_F_toe", hand_r="b_R_F_toe",
                     foot_l="b_L_B_toe", foot_r="b_R_B_toe", ball_l="b_L_F_toe", ball_r="b_R_F_toe"),
        roles=dict(idle="A_Mammoth_Idle", walk="A_Mammoth_Walk", run="A_Mammoth_Run", attack="A_Mammoth_attack_1", attackAlt="A_Mammoth_attack_2",
                   hit="A_Mammoth_Hit_Body_Front_Left", death="A_Mammoth_death"), named=dict(war_cry="A_Mammoth_aggressive_1", ground_slam="A_Mammoth_attack_2"),
        folder=R + "/Mammoth/Animations"),
    "grave_hound": dict(variant="FabBarghest", mesh=Q + "/Barghest/Meshes/SK_BARGHEST", head=135, rig="quadruped",
        sockets=dict(head="BARGHEST_-Head", pelvis="BARGHEST_-Spine1", spine_03="BARGHEST_-Spine2", hand_l="BARGHEST_-L-Finger0", hand_r="BARGHEST_-R-Finger0",
                     foot_l="BARGHEST_-L-Toe0", foot_r="BARGHEST_-R-Toe0", ball_l="BARGHEST_-L-Finger0", ball_r="BARGHEST_-R-Finger0"),
        roles=dict(idle="BARGHEST_idleAggressive", walk="BARGHEST_walk", run="BARGHEST_run", attack="BARGHEST_biteAggressive", attackAlt="BARGHEST_biteNormal",
                   hit="BARGHEST_getHitAggressive", death="BARGHEST_deathAggressive"), named=dict(), folder=Q + "/Barghest/Animations"),
    "wild_outrider": dict(variant="FabCentaur", mesh=Q + "/Centaur/Meshes/SK_Centaur", head=215, rig="quadruped",
        sockets=dict(head="CENTAUR_-Head", pelvis="CENTAUR_-Spine", spine_03="CENTAUR_-Spine2", hand_l="CENTAUR_HAND_L", hand_r="CENTAUR_HAND_R",
                     foot_l="CENTAUR_-L-Foot", foot_r="CENTAUR_-R-Foot", ball_l="CENTAUR_-L-Foot", ball_r="CENTAUR_-R-Foot"),
        # The outrider is the feral kin's archer: the bow set, the loose on every shot, bow + armour + mane as parts.
        roles=dict(idle="ANIM_Centaur_IdleBow", walk="ANIM_Centaur_WalkBow", run="ANIM_Centaur_GallopBow", attack="ANIM_Centaur_ShootArrowToIdleAiming",
                   attackAlt="ANIM_Centaur_LegAttackBow", hit="ANIM_Centaur_GetHitBow", death="ANIM_Centaur_DeathBow"),
        named=dict(cast_a_spell="ANIM_Centaur_IdleNormalToIdleAiming", war_cry="ANIM_Centaur_LegAttackBow"),
        parts=["SK_Body_Armor", "SK_Shoulder_Pads", "SK_Mane", "SK_Beard", "SK_Bow_Action"],
        folder=Q + "/Centaur/Animations"),
    "hollow_infantry": dict(variant="FabSkeleton", mesh=UD + "/SkeletonEnemy/Mesh/SK_Skeleton", head=172, humanoid=True,
        roles=dict(idle="Anim_Idle_Sword", walk="Anim_Walk_Sword", run="Anim_Run_Sword", attack="Anim_Attack_Sword_1", attackAlt="Anim_Attack_Sword_2",
                   hit="Anim_Hit_Sword", death="Anim_Death_Sword"), named=dict(), folder=UD + "/SkeletonEnemy/Animations",
        alternates=["hollow_infantry_ghoul"]),
    "hollow_infantry_ghoul": dict(variant="FabGhoul", mesh=UD + "/Ghoul/Mesh/SK_Ghoul_Full", head=168, humanoid=True, alternate=True,
        roles=dict(idle="Anim_Idle", walk="Anim_Walk", run="Anim_Run", attack="Anim_Attack_Right", attackAlt="Anim_Attack_Left",
                   hit="Anim_Hit", death="Anim_Death"), named=dict(), folder=UD + "/Ghoul/Animations"),
    # hollow_siegebreaker: the Undead Pack zombie reads as a cartoon ghoul (off-tone); the Tripo siegebreaker stays.
}


def exists(p):
    return u.EditorAssetLibrary.does_asset_exist(p)


def measure(spec):
    mesh = u.load_asset(spec["mesh"])
    skeleton = mesh.get_editor_property("skeleton")
    clips = {}
    for role, name in list(spec["roles"].items()) + list(spec["named"].items()):
        p = "%s/%s" % (spec["folder"], name)
        a = u.load_asset(p) if exists(p) else None
        if isinstance(a, u.AnimSequence) and a.get_editor_property("skeleton") == skeleton:
            clips[role] = a
    ref = u.AnimPoseExtensions.get_reference_pose(skeleton)
    names = [str(n) for n in u.AnimPoseExtensions.get_bone_names(ref)]
    root_name = names[0]
    root_ref = u.AnimPoseExtensions.get_bone_pose(ref, root_name, u.AnimPoseSpaces.WORLD)

    def loc(pose, bone):  # root held at its reference transform (lockRoot)
        world = u.AnimPoseExtensions.get_bone_pose(pose, bone, u.AnimPoseSpaces.WORLD).translation
        root = u.AnimPoseExtensions.get_bone_pose(pose, root_name, u.AnimPoseSpaces.WORLD)
        return u.MathLibrary.transform_location(root_ref, u.MathLibrary.inverse_transform_location(root, world))
    s = dict(spec.get("sockets") or dict(head="head", pelvis="pelvis", foot_l="foot_l", foot_r="foot_r", ball_l="ball_l", ball_r="ball_r"))
    comp = u.SkeletalMeshComponent(); comp.set_skeletal_mesh_asset(mesh)
    parent = {n: str(comp.get_parent_bone(n)) for n in names}

    def descendants(b):
        out = []
        for n in names:
            x = n
            while x and x != "None":
                if x == b:
                    out.append(n); break
                x = parent.get(x)
        return out
    if spec.get("rig"):
        # Feet/paws end below the ankle joint: every foot socket uses the lowest bone of its chain.
        for f in ("foot_l", "foot_r", "ball_l", "ball_r", "hand_l", "hand_r"):
            chain = descendants(s[f]) or [s[f]]
            if f.startswith("hand") and s[f].upper().find("HAND") >= 0:
                continue  # real hands (centaur) keep the wrist
            s[f] = min(chain, key=lambda n: loc(ref, n).z)
        spec["sockets"] = s
    idle = clips.get("idle")
    ipose = u.AnimPoseExtensions.get_anim_pose_at_time(idle, 0.0, u.AnimPoseEvaluationOptions()) if idle else ref
    head, body = loc(ipose, s["head"]), loc(ipose, s["pelvis"])
    # The packs pivot at the ground: a lowest foot joint standing above the pivot means hooves/paws end below it.
    lowest_foot = min(loc(ipose, s[f]).z for f in ("foot_l", "foot_r", "ball_l", "ball_r"))
    sole_raw = max(0.0, lowest_foot)
    feet_z = min(0.0, min(loc(ipose, n).z for n in names))  # ground
    if spec.get("humanoid"):
        fl, bl = loc(ref, "foot_l"), loc(ref, "ball_l")
        forward = (bl.x - fl.x, bl.y - fl.y)
    else:
        forward = (head.x - body.x, head.y - body.y)
    yaw = -math.degrees(math.atan2(forward[1], forward[0]))
    yaw = round(yaw / 90.0) * 90.0 if abs(yaw - round(yaw / 90.0) * 90.0) < 20 else yaw  # packs author on the axes
    reach = max(math.hypot(loc(ref, n).x, loc(ref, n).y) for n in names)
    fx, fy = forward[0] / max(1e-6, math.hypot(*forward)), forward[1] / max(1e-6, math.hypot(*forward))
    rows = {}
    opts = u.AnimPoseEvaluationOptions()
    # Body anchor ("pelvis" socket): the spine bone that stays closest to the capsule centre through idle/walk/run.
    candidates = [n for n in names if any(t in n.lower() for t in ("spine", "pelvis", "body", "chest"))]
    anchor_worst = {}

    def loc_raw(a, t, bone):
        return u.AnimPoseExtensions.get_bone_pose(u.AnimPoseExtensions.get_anim_pose_at_time(a, t, opts), bone, u.AnimPoseSpaces.WORLD).translation
    paw = s["ball_l"]
    for key, a in clips.items():
        length = float(a.get_play_length())
        samples = []
        for k in range(17):
            pose = u.AnimPoseExtensions.get_anim_pose_at_time(a, length * k / 16, opts)
            p, b = loc(pose, paw), loc(pose, s["pelvis"])
            samples.append(((p.x - b.x) * fx + (p.y - b.y) * fy, loc(pose, s["head"]).z))
        along = [x for x, _ in samples]
        # Root-motion clips carry their true ground speed on the root bone.
        r0 = u.AnimPoseExtensions.get_bone_pose(u.AnimPoseExtensions.get_anim_pose_at_time(a, 0.0, opts), root_name, u.AnimPoseSpaces.WORLD).translation
        r1 = u.AnimPoseExtensions.get_bone_pose(u.AnimPoseExtensions.get_anim_pose_at_time(a, length, opts), root_name, u.AnimPoseSpaces.WORLD).translation
        p0 = loc_raw(a, 0.0, s["pelvis"]); p1 = loc_raw(a, length, s["pelvis"])
        travel = max(math.hypot(r1.x - r0.x, r1.y - r0.y), math.hypot(p1.x - p0.x, p1.y - p0.y))
        feet_track = []
        anchors = {}
        for k in range(17):
            pose = u.AnimPoseExtensions.get_anim_pose_at_time(a, length * k / 16, opts)
            feet_track.append(min(loc(pose, s[f]).z for f in ("foot_l", "foot_r", "ball_l", "ball_r")))
            if spec.get("rig") and key in ("idle", "walk", "run"):
                for n in candidates:
                    q = loc(pose, n)
                    anchors[n] = max(anchors.get(n, 0.0), math.hypot(q.x, q.y))
        for n, v in anchors.items():
            anchor_worst[n] = max(anchor_worst.get(n, 0.0), v)
        rows[key] = {"asset": a.get_path_name(), "length": round(length, 3), "stride": round(max(along) - min(along), 1), "travel": round(travel, 1),
                     "maxLowestFoot": round(max(feet_track) - feet_z, 1),
                     "headMin": round(min(h for _, h in samples), 1), "headEnd": round(samples[-1][1], 1)}
    if spec.get("rig") and anchor_worst:
        s["pelvis"] = min(anchor_worst, key=anchor_worst.get)
        spec["sockets"] = s
    return {"yaw": round(yaw, 1), "headZ": round(head.z - feet_z, 1), "soleRaw": round(sole_raw, 1), "anchor": s.get("pelvis"), "reach": round(reach, 1), "clips": rows, "bones": len(names)}


def main():
    report, units = {}, {}
    try:
        for unit, spec in UNITS.items():
            if not exists(spec["mesh"]):
                report[unit] = {"skipped": "pack not installed"}
                continue
            m = measure(spec)
            report[unit] = m
            need = ("idle", "walk", "run", "attack", "hit", "death")
            if any(r not in m["clips"] for r in need):
                report[unit]["error"] = "missing roles " + ",".join(r for r in need if r not in m["clips"])
                continue
            scale = spec["head"] / max(1.0, m["headZ"])
            walk, run = m["clips"]["walk"], m["clips"]["run"]
            # a planted paw sweeps its stride while the body moves over it: walk duty ~0.7, run ~0.55 of the cycle
            # Exact when the vendor clip travels (root motion / pelvis drift); else the planted-paw sweep estimate.
            def gait_speed(c, duty):
                if c["travel"] > .25 * c["stride"]:
                    return c["travel"] * scale / max(.1, c["length"])
                return c["stride"] * scale / max(.1, 2 * duty * c["length"])
            walk_speed, run_speed = gait_speed(walk, .6), gait_speed(run, .45)
            report[unit]["speedSource"] = {k: ("travel" if c["travel"] > .25 * c["stride"] else "stride") for k, c in (("walk", walk), ("run", run))}
            if spec["roles"]["run"] == spec["roles"]["walk"]:
                run_speed = walk_speed * 1.6
            walk_speed = min(300.0, max(60.0, walk_speed))
            row = {"variant": spec["variant"], "mesh": "%s.%s" % (spec["mesh"], spec["mesh"].rsplit("/", 1)[1]), "meshScale": round(scale, 4),
                   "yaw": m["yaw"], "heightCm": round(spec["head"] / (.93 if spec.get("humanoid") else .95), 1), "lockRoot": True,
                   "walkSpeedCm": round(walk_speed, 1), "runSpeedCm": round(min(900.0, max(walk_speed * 1.5, run_speed)), 1),
                   "reachCm": round(max(m["reach"] * scale * 1.6, spec["head"] * 3.0), 1),
                   "animations": {r: m["clips"][r]["asset"] for r in spec["roles"] if r in m["clips"]},
                   "source": "Fab (licensed, local only): see Docs/FAB-PURCHASED.md"}
            named = {n: m["clips"][n]["asset"] for n in spec["named"] if n in m["clips"]}
            if named:
                row["animations"]["all"] = named
            if m["soleRaw"] * scale > 5:
                row["soleCm"] = round(m["soleRaw"] * scale, 1)
            air = m["clips"]["run"]["maxLowestFoot"] * scale - 30.0
            if air > 0:
                row["airborneCm"] = round(min(60.0, air + 5.0), 1)
            parts = []  # leader-pose parts on the same skeleton (UCireMonsterArt::ApplyBody)
            for p in spec.get("parts", []):
                path = "%s/%s" % (spec["mesh"].rsplit("/", 1)[0], p)
                part = u.load_asset(path) if exists(path) else None
                if part and part.get_editor_property("skeleton") == u.load_asset(spec["mesh"]).get_editor_property("skeleton"):
                    parts.append("%s.%s" % (path, p))
            if parts:
                row["parts"] = parts
            if spec.get("rig"):
                row["rig"] = spec["rig"]
                row["sockets"] = spec["sockets"]
                row["dropPropBones"] = HAND_PROPS
            if spec.get("alternate"):
                units.setdefault("_alternates", {})[unit] = row
            else:
                units[unit] = row
        alts = units.pop("_alternates", {})
        for unit, spec in UNITS.items():
            for alt in spec.get("alternates", []):
                if unit in units and alt in alts:
                    units[unit].setdefault("alternates", []).append(alts[alt])
        path = ROOT / "Content/Data/RaceMeshes.fab.json"
        doc = json.loads(path.read_text(encoding="utf-8"))
        doc["units"] = units
        path.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
        u.log("CIRE_FAB_CREATURES_PASS units=%d" % len(units))
    except Exception as error:
        import traceback
        report["error"] = traceback.format_exc()
        u.log_error("CIRE_FAB_CREATURES_FAIL " + str(error))
    finally:
        (ROOT / "Saved/FabCreatures.json").write_text(json.dumps(report, indent=1), encoding="utf-8")


if not globals().get("CIRE_IMPORT_ONLY"):  # Tools/BuildFabChampionCreatures.py reuses measure()
    main()
