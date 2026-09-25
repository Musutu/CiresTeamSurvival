"""UE 5.8 commandlet: bring every unused creature of the purchased Fab packs into the game (monster-expansion).

Writes Content/Data/RaceMeshes.fabx.json (same shape as RaceMeshes.fab.json, read right after it by CireMonsterArt) for the
Bestiary.json creatures: treasure goblin, gilded stag, rotting shambler, lich revenant, storm griffon, cinder drake,
frostfang alpha, horned brute, bone archer and centaur blademaster. Docs/MonsterExpansion.md.

For each creature it:
  - measures facing, head height -> meshScale, reach and walk/run ground speed with Tools/BuildFabCreatures.py measure();
  - retargets missing clips from a pack with the same (UE4-mannequin) bone names when the creature's own pack has no
    attack/hit/death (the Khornes monster only ships locomotion: it takes the Undead Pack skeleton's sword set), through
    the proven IK-rig route of Tools/RetargetTripo.py into /Game/FabDerived/Expansion/<Unit> (local only, never committed);
  - records a "reskin" for creatures that must be recoloured to fit the dark-fantasy look (the vendor's base colour and
    normal textures are fed to /Game/Art/Materials/M_CireMonsterSkin at runtime with the tint below), and "attachments"
    for props that live on their own skeleton (the skeleton archer's bow).
Units whose pack is missing are skipped; the runtime re-checks every path (clean clones fall back to Bestiary.json fallbacks).

Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/BuildFabExpansionCreatures.py -unattended -nullrhi
     [-CireExpansionOnly=unit+unit] [-CireExpansionKeep]  (keep = reuse already retargeted clips)
Log marker CIRE_FAB_EXPANSION_PASS / _FAIL; report Saved/FabExpansion.json.
"""
import importlib.util
import json
import math
import stat
import traceback
from pathlib import Path

import unreal as u

ROOT = Path(u.Paths.convert_relative_path_to_full(u.Paths.project_dir()))
TOOLS = ROOT / "Tools"
_src = (TOOLS / "BuildFabCreatures.py").read_text(encoding="utf-8")
# Clips may be absolute object paths (retargeted copies) as well as names inside the pack folder.
_src = _src.replace('p = "%s/%s" % (spec["folder"], name)', 'p = name if name.startswith("/Game/") else "%s/%s" % (spec["folder"], name)')
_ns = {"CIRE_IMPORT_ONLY": True, "__file__": str(TOOLS / "BuildFabCreatures.py")}
exec(compile(_src, "BuildFabCreatures.py", "exec"), _ns)
measure, exists = _ns["measure"], _ns["exists"]
lib = u.EditorAssetLibrary

UD, R, Q, M = "/Game/UndeadPack", "/Game/ROG_Creatures", "/Game/QuadrapedCreatures", "/Game/Monster"
OUT = "/Game/FabDerived/Expansion"
HAND_PROPS = ["hand_r", "hand_l", "spine_03", "head", "back", "spine_02", "pelvis"]
SKEL_ANIM = UD + "/SkeletonEnemy/Animations/"
GDH_BOW = "/Game/GDHBundle/ArcheryCombatAnimV1/Animation/Character/IP/"
GDH_MANNY = "/Game/GDHBundle/ArcheryCombatAnimV1/DEMO/Character/Mannequins/Meshes/SKM_Manny"
SKEL_MESH = UD + "/SkeletonEnemy/Mesh/SK_Skeleton"
QG, QD, QC = Q + "/Griffon", Q + "/MountainDragon", Q + "/Centaur"

# unit: mesh, head height (cm, actor scale 1) or explicit scale, rig/sockets, role clips, named clips, folder,
#       retarget (role -> source clip on SKEL_MESH), parts, reskin (tint...), attachments, source note
UNITS = {
    # Bonus Loot Wave: the goblin, re-dyed in treasure gold (reads as a loot creature, not the pack's green grunt).
    "treasure_goblin": dict(variant="FabTreasureGoblin", mesh=UD + "/EnemyGoblin/Mesh/SM_EnemyGoblin", head=112, humanoid=True,
        roles=dict(idle="Anim_Idle", walk="Anim_Walk", run="Anim_Run", attack="Anim_Attack1", attackAlt="Anim_Attack2", hit="Anim_Hit", death="Anim_Lose"),
        named=dict(war_cry="Anim_Agr"), folder=UD + "/EnemyGoblin/Animations",
        reskin=dict(tint=[0.95, 0.6, 0.1], tintStrength=0.92, rim=[1.0, 0.72, 0.15], rimStrength=0.45, body=0.0)),
    # Bonus Loot Wave: the ROG deer (fur-less mesh: every slot opaque, so it takes the gilded skin cleanly).
    "gilded_stag": dict(variant="FabGildedStag", mesh=R + "/Deer/Meshes/SK_Deer_No_Fur", head=185, rig="quadruped",
        sockets=dict(head="deer_head", pelvis="spine_J01", spine_03="spine_J02", hand_l="LF_toe", hand_r="RF_toe",
                     foot_l="LB_toe", foot_r="RB_toe", ball_l="LF_toe", ball_r="RF_toe"),
        roles=dict(idle="A_Deer_Idle_Aggressive", walk="A_Deer_Walk", run="A_Deer_Run", attack="A_Deer_Aggressive", attackAlt="A_Deer_Attack_Light",
                   hit="A_Deer_Hit_Body_Left", death="A_Deer_Death"), named=dict(war_cry="A_Deer_Aggressive"), folder=R + "/Deer/Animations",
        reskin=dict(tint=[0.95, 0.62, 0.12], tintStrength=0.85, rim=[1.0, 0.75, 0.2], rimStrength=0.55, body=0.0, slots=[0, 1])),
    # Hollow Legion line variant: the cartoon zombie, desaturated to a grave-green corpse with a sickly rim.
    "rotting_shambler": dict(variant="FabRottingShambler", mesh=UD + "/Zombie/Mesh/SK_Zombie", head=178, humanoid=True,
        roles=dict(idle="Anim_Idle", walk="Anim_Walk", run="Anim_Walk", attack="Anim_Attack1", attackAlt="Anim_Attack2", hit="Anim_Hit", death="Anim_Death"),
        named=dict(), folder=UD + "/Zombie/Animations",
        reskin=dict(tint=[0.46, 0.52, 0.38], tintStrength=0.72, rim=[0.35, 0.9, 0.3], rimStrength=0.45, body=0.0)),
    # Rare: the Undead Pack lich floats on its cloak tail (no legs): the lowest tail joint is its "sole".
    "lich_revenant": dict(variant="FabLich", mesh=UD + "/Lich/Mesh/SK_Lich_Full", head=235, rig="biped_custom", yaw=-90.0, spectral=True,
        sockets=dict(head="Head_M", pelvis="Pelvis", spine_03="Chest_M", hand_l="Wrist_L", hand_r="Wrist_R",
                     foot_l="Tail5_M", foot_r="Tail5_M", ball_l="Tail5_M", ball_r="Tail5_M"),
        roles=dict(idle="Anim_Idle", walk="Anim_Walk", run="Anim_Walk", attack="Anim_Attack_Right", attackAlt="Anim_Attack_Left", hit="Anim_Hit", death="Anim_Death"),
        named=dict(cast_a_spell="Anim_Spell", war_cry="Anim_Call", emerge="Anim_Emergence"), folder=UD + "/Lich/Animations"),
    # Rare: the griffon on the ground (gallop / claws / bite; its flight set stays unused: monsters walk the lane).
    "storm_griffon": dict(variant="FabGriffon", mesh=QG + "/Meshes/SK_Griffon", head=250, rig="quadruped",
        sockets=dict(head="GRIFFON_-Head", pelvis="GRIFFON_-Pelvis", spine_03="GRIFFON_-Spine2", hand_l="GRIFFON_-L-Finger0", hand_r="GRIFFON_-R-Finger0",
                     foot_l="GRIFFON_-L-Toe0", foot_r="GRIFFON_-R-Toe0", ball_l="GRIFFON_-L-Finger0", ball_r="GRIFFON_-R-Finger0"),
        roles=dict(idle="ANIM_Griffon_Idle", walk="ANIM_Griffon_Walk", run="ANIM_Griffon_Gallop", attack="ANIM_Griffon_LeftClawsAttack",
                   attackAlt="ANIM_Griffon_BiteAttack", hit="ANIM_Griffon_GetHit2", death="ANIM_Griffon_Death"),
        named=dict(war_cry="ANIM_Griffon_2HitComboAttack", ground_slam="ANIM_Griffon_3HitComboAttack", cast_a_spell="ANIM_Griffon_RightClawsAttack"),
        folder=QG + "/Animations"),
    # Rare: the mountain dragon as a drake a little taller than a champion (the 19 m vendor scale was the old rejection).
    "cinder_drake": dict(variant="FabCinderDrake", mesh=QD + "/Meshes/SK_MOUNTAIN_DRAGON", scale_length=1000.0, rig="quadruped", soleCm=5.0, lockRoot=False,
        sockets=dict(head="MOUNTAIN_DRAGON_-Head", pelvis="MOUNTAIN_DRAGON_-Pelvis", spine_03="MOUNTAIN_DRAGON_-Spine2",
                     hand_l="MOUNTAIN_DRAGON_-L-Finger0", hand_r="MOUNTAIN_DRAGON_-R-Finger0", foot_l="MOUNTAIN_DRAGON_-L-Toe0",
                     foot_r="MOUNTAIN_DRAGON_-R-Toe0", ball_l="MOUNTAIN_DRAGON_-L-Finger0", ball_r="MOUNTAIN_DRAGON_-R-Finger0"),
        # The drake's gallop dives its head to the ground; it prowls on the walk cycle at every speed (runSpeed = 1.6 x walk).
        roles=dict(idle="ANIM_MOUNTAIN_DRAGON_idleBreathe", walk="ANIM_MOUNTAIN_DRAGON_walk", run="ANIM_MOUNTAIN_DRAGON_walk",
                   attack="ANIM_MOUNTAIN_DRAGON_bite", attackAlt="ANIM_MOUNTAIN_DRAGON_rightClawsAttackForward", hit="ANIM_MOUNTAIN_DRAGON_getHitFront",
                   death="ANIM_MOUNTAIN_DRAGON_death"),
        named=dict(cast_a_spell="ANIM_MOUNTAIN_DRAGON_spitFireBall", ground_slam="ANIM_MOUNTAIN_DRAGON_ClawsAttack2HitComboForward",
                   war_cry="ANIM_MOUNTAIN_DRAGON_LeftClawsAttackForward"), folder=QD + "/Animations",
        # Re-lit as a cinder drake: charred red scales with an ember rim (the vendor's sandy mountain hide read as dull).
        reskin=dict(tint=[0.34, 0.07, 0.03], tintStrength=0.85, rim=[1.0, 0.35, 0.04], rimStrength=0.8, body=0.0,
                    textures={"BaseColorTex": QD + "/Textures/T_MOUNTAIN_DRAGON_ALBEDO.T_MOUNTAIN_DRAGON_ALBEDO", "NormalTex": QD + "/Textures/T_MOUNTAIN_DRAGON_NRM.T_MOUNTAIN_DRAGON_NRM"})),
    # Rare: the ROG wolf's white coat (the grey one is the dire wolf), a size up.
    "frostfang_alpha": dict(variant="FabFrostfang", mesh=R + "/Wolf/Meshes/SK_Wolf_Fur_Full_W", head=175, rig="quadruped",
        sockets=dict(head="b_head", pelvis="b_spine_2", spine_03="b_spine_3", hand_l="b_L_front_leg_5", hand_r="b_R_front_leg_5",
                     foot_l="b_L_back_leg_4", foot_r="b_R_back_leg_4", ball_l="b_L_front_leg_5", ball_r="b_R_front_leg_5"),
        roles=dict(idle="A_Wolf_Idle_combat", walk="A_Wolf_walk", run="A_Wolf_run", attack="A_Wolf_Attack", attackAlt="A_Wolf_Attack_light",
                   hit="A_Wolf_Hit_body_left_combat", death="A_Wolf_Death"), named=dict(war_cry="A_Wolf_howl"), folder=R + "/Wolf/Animations"),
    # Rare / Ironhide variant: the Khornes monster. Its pack ships only locomotion; the Undead Pack skeleton's sword set
    # (same UE4 bone names) is retargeted onto it for the strikes, the flinch and the death.
    "horned_brute": dict(variant="FabHornedBrute", mesh=M + "/Mesh/SKM_Monster", head=240, humanoid=True,
        roles=dict(idle="AS_Idle_Monster", walk="AS_Walk_Monster", run="AS_Run_Monster", attack="attack", attackAlt="attackAlt", hit="hit", death="death"),
        named=dict(war_cry="AS_Jump_Start_Monster"), folder=M + "/Demo/Animations",
        retarget=dict(attack=(SKEL_ANIM + "Anim_Attack_Sword_1", SKEL_MESH), attackAlt=(SKEL_ANIM + "Anim_Attack_Sword_2", SKEL_MESH),
                      hit=(SKEL_ANIM + "Anim_Hit_Sword", SKEL_MESH), death=(SKEL_ANIM + "Anim_Death_Sword", SKEL_MESH))),
    # Hollow Legion ranged variant: the Undead Pack skeleton with its bow set, helmet, armour and quiver.
    "bone_archer": dict(variant="FabBoneArcher", mesh=SKEL_MESH, head=172, humanoid=True,
        # The pack's bow shot lives on the bow's own skeleton: the GDH archery loose (UE5 Manny) is retargeted onto the skeleton.
        roles=dict(idle="Anim_Idle_Bow", walk="Anim_Walk_Bow", run="Anim_Walk_Bow", attack="attack", attackAlt="attackAlt", hit="Anim_Hit_Bow", death="Anim_Death_Bow"),
        named=dict(), folder=UD + "/SkeletonEnemy/Animations",
        retarget=dict(attack=(GDH_BOW + "AS_ArcheryAnimV1_Attack1_IP", GDH_MANNY), attackAlt=(GDH_BOW + "AS_ArcheryAnimV1_Attack2_IP", GDH_MANNY)),
        parts=[UD + "/SkeletonEnemy/Mesh/Equipment/SK_Helmet", UD + "/SkeletonEnemy/Mesh/Equipment/SK_Armor", UD + "/SkeletonEnemy/Mesh/Equipment/SK_Quiver",
               UD + "/SkeletonEnemy/Mesh/Equipment/SK_Belt", UD + "/SkeletonEnemy/Mesh/Equipment/SK_Rag5"],
        attachments=[dict(mesh=UD + "/SkeletonEnemy/Mesh/Weapon/Bow/SK_Bow", bone="hand_l", socket_hint="bow")]),
    # Feral Kin variant: the centaur's sword set (sword in hand, armour, pauldrons, mane, sheath).
    "centaur_blademaster": dict(variant="FabCentaurBlade", mesh=QC + "/Meshes/SK_Centaur", head=222, rig="quadruped",
        sockets=dict(head="CENTAUR_-Head", pelvis="CENTAUR_-Spine", spine_03="CENTAUR_-Spine2", hand_l="CENTAUR_HAND_L", hand_r="CENTAUR_HAND_R",
                     foot_l="CENTAUR_-L-Foot", foot_r="CENTAUR_-R-Foot", ball_l="CENTAUR_-L-Foot", ball_r="CENTAUR_-R-Foot"),
        roles=dict(idle="ANIM_Centaur_IdleSword", walk="ANIM_Centaur_WalkSword", run="ANIM_Centaur_GallopSword", attack="ANIM_Centaur_attack1Sword",
                   attackAlt="ANIM_Centaur_Attack2Sword", hit="ANIM_Centaur_GetHitSword", death="ANIM_Centaur_DeathSword"),
        named=dict(war_cry="ANIM_Centaur_LegAttackSword", ground_slam="ANIM_Centaur_2HitComboSword", cast_a_spell="ANIM_Centaur_DrawSword"),
        parts=["SK_Body_Armor", "SK_Shoulder_Pads", "SK_Mane", "SK_Mustaches", "SK_Sword_Action", "SK_Sheath"], folder=QC + "/Animations"),
}


def load_module(name):
    spec = importlib.util.spec_from_file_location(name, str(TOOLS / (name + ".py")))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def path_of(obj):
    return obj.get_path_name().split(".")[0]


def writable_delete(path):
    f = ROOT / "Content" / (path[len("/Game/"):] + ".uasset")
    if f.exists():
        f.chmod(f.stat().st_mode | stat.S_IWRITE)
    if lib.does_asset_exist(path):
        lib.delete_asset(path)


def retarget(unit, spec, keep, report):
    """Retarget spec["retarget"] (role -> (clip, source mesh)) onto the unit's mesh: one IK rig pair per source skeleton."""
    RT = load_module("RetargetTripo")
    out_dir = "%s/%s" % (OUT, unit)
    dst_mesh = u.load_asset(spec["mesh"])
    done = {role: "%s/A_%s_%s" % (out_dir, unit, role) for role in spec["retarget"]}
    groups = {}
    for role, (clip, src) in spec["retarget"].items():
        if keep and exists(done[role]):
            continue
        if not exists(clip) or not exists(src):
            report.setdefault("retargetErrors", {})[role] = "source missing " + clip
            done.pop(role, None)
            continue
        groups.setdefault(src, []).append((role, clip))
    for index, (src, todo) in enumerate(sorted(groups.items())):
        rig_dir = "%s/Rigs/S%d" % (out_dir, index)
        src_mesh = u.load_asset(src)
        for name in ("IK_Src", "IK_Dst", "RTG"):
            writable_delete("%s/%s" % (rig_dir, name))
        src_rig = RT.create_asset("IK_Src", rig_dir, u.IKRigDefinition, u.IKRigDefinitionFactory()); RT.configure_rig(src_rig, src_mesh)
        dst_rig = RT.create_asset("IK_Dst", rig_dir, u.IKRigDefinition, u.IKRigDefinitionFactory()); RT.configure_rig(dst_rig, dst_mesh)
        rtg = RT.create_asset("RTG", rig_dir, u.IKRetargeter, u.IKRetargetFactory()); RT.configure_retargeter(rtg, src_rig, dst_rig, src_mesh, dst_mesh)
        for a in (src_rig, dst_rig, rtg):
            lib.save_loaded_asset(a, False)
        staging = out_dir + "/_staging"
        if lib.does_directory_exist(staging):
            lib.delete_directory(staging)
        inputs = u.IKRetargetBatchOperationInputs()
        for key, value in {"assets_to_retarget": [lib.find_asset_data(c) for _, c in todo], "source_mesh": src_mesh, "target_mesh": dst_mesh,
                           "ik_retarget_asset": rtg, "target_path": staging, "suffix": "", "prefix": "", "use_source_path": False,
                           "include_referenced_assets": False, "overwrite_existing_files": True}.items():
            try:
                inputs.set_editor_property(key, value)
            except Exception:
                pass
        results = {path_of(d.get_asset()).rsplit("/", 1)[1]: d.get_asset() for d in u.IKRetargetBatchOperation.run_batch_retarget(inputs)}
        for role, clip in todo:
            final = done[role]
            out = results.get(clip.rsplit("/", 1)[1])
            if not isinstance(out, u.AnimSequence):
                report.setdefault("retargetErrors", {})[role] = "retarget produced nothing"
                done.pop(role, None)
                continue
            writable_delete(final)
            lib.rename_asset(path_of(out), final)
            out = u.load_asset(final)
            out.set_editor_property("enable_root_motion", False)
            out.set_editor_property("force_root_lock", True)
            lib.save_loaded_asset(out, False)
        if lib.does_directory_exist(staging):
            lib.delete_directory(staging)
    return done


def texture_params(mi):
    """Base colour / normal textures of a vendor material (instance values, else the parent's defaults)."""
    mel = u.MaterialEditingLibrary
    base = mi.get_base_material()
    out = {}
    for n in mel.get_texture_parameter_names(base):
        try:
            t = mel.get_material_instance_texture_parameter_value(mi, n) if isinstance(mi, u.MaterialInstance) else mel.get_material_default_texture_parameter_value(base, n)
        except Exception:
            t = None
        if not t:
            continue
        key, low = str(n), str(n).lower()
        obj = t.get_path_name()
        if "normal" in low:
            out.setdefault("NormalTex", obj)
        elif any(k in low for k in ("diffuse", "albedo", "color", "basecolor")) or low in ("texture",):
            out.setdefault("BaseColorTex", obj)
    if "BaseColorTex" not in out:
        # Materials with baked texture samples (no parameters, e.g. the Quadruped Fantasy dragon): pick by texture name.
        try:
            used = mel.get_used_textures(base)
        except Exception:
            used = []
        for t in used or []:
            name = t.get_name().lower()
            if any(k in name for k in ("_n", "normal")) and name.rstrip("0123456789").endswith(("_n", "normal", "_nm")):
                out.setdefault("NormalTex", t.get_path_name())
            elif any(name.endswith(k) or ("%s_" % k) in name for k in ("_d", "_bc", "_basecolor", "_albedo", "_diffuse", "_color", "_col")):
                out.setdefault("BaseColorTex", t.get_path_name())
        out["_used"] = [t.get_name() for t in used or []]
    return out


def socket_for(mesh, hint):
    """A socket on the skeleton (or mesh) whose name contains the hint (the vendor's own weapon socket)."""
    names = []
    for owner in (mesh, mesh.get_editor_property("skeleton")):
        try:
            for s in owner.get_editor_property("sockets"):
                names.append((str(s.get_editor_property("socket_name")), str(s.get_editor_property("bone_name"))))
        except Exception:
            pass
    for name, bone in names:
        if hint.lower() in name.lower():
            return name, bone, names
    return None, None, names


# Creature props measured with Tools/MeasureFabWeapons.py measure() into WeaponGrips.fabx.json (CireGrip reads it after .fab).
PROPS = {M + "/Mesh/SM_Monster_Sword": ("guard", {"tilt": 25})}


def measure_props(report):
    src = (TOOLS / "MeasureFabWeapons.py").read_text(encoding="utf-8").rsplit("\nmain()", 1)[0]
    ns = {"__file__": str(TOOLS / "MeasureFabWeapons.py")}
    exec(compile(src, "MeasureFabWeapons.py", "exec"), ns)
    weapons = {}
    for path, (kind, extra) in PROPS.items():
        if not exists(path):
            continue
        info, grip = ns["measure"](path, kind)
        grip.update(extra)
        weapons["%s.%s" % (path, path.rsplit("/", 1)[1])] = grip
        report.setdefault("props", {})[path] = {**info, "grip": grip}
    (ROOT / "Content/Data/WeaponGrips.fabx.json").write_text(json.dumps({
        "schemaVersion": 1,
        "description": "monster-expansion: grip data for the bestiary creatures' Fab weapon props (Tools/BuildFabExpansionCreatures.py). Same fields as WeaponGrips.json.",
        "weapons": weapons}, indent=1) + "\n", encoding="utf-8")


def main():
    report, units = {}, {}
    only = None
    keep = False
    for token in u.SystemLibrary.get_command_line().split():
        if token.lower().startswith("-cireexpansiononly="):
            only = set(token.split("=", 1)[1].split("+"))
        if token.lower() == "-cireexpansionkeep":
            keep = True
    path = ROOT / "Content/Data/RaceMeshes.fabx.json"
    doc = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {"schemaVersion": 1}
    if only:
        units = dict(doc.get("units", {}))
    try:
        for unit, spec in UNITS.items():
            if only and unit not in only:
                continue
            spec = dict(spec)
            if not exists(spec["mesh"]):
                report[unit] = {"skipped": "pack not installed"}
                continue
            entry = report.setdefault(unit, {})
            if spec.get("retarget"):
                derived = retarget(unit, spec, keep, entry)
                spec["roles"] = {r: derived.get(r, n) if n in spec["retarget"] or r in spec["retarget"] else n for r, n in spec["roles"].items()}
            rig = spec.get("rig")
            if rig == "biped_custom":
                spec["rig"] = "quadruped"  # measure(): explicit sockets, lowest-joint soles; yaw below
            m = measure(spec)
            entry.update(m)
            need = ("idle", "walk", "run", "attack", "hit", "death")
            if any(r not in m["clips"] for r in need):
                entry["error"] = "missing roles " + ",".join(r for r in need if r not in m["clips"])
                continue
            mesh = u.load_asset(spec["mesh"])
            # Some vendor rigs evaluate their poses in a different unit than the rendered mesh (the griffon's root carries
            # x100): normalise every pose measurement to the mesh bounds before deriving scale and speeds.
            bh = 2 * mesh.get_bounds().box_extent.z
            k = 1.0
            while m["headZ"] / k > 3.0 * bh:
                k *= 10.0
            if k > 1.0:
                entry["poseUnits"] = k
                m["headZ"] /= k; m["soleRaw"] /= k; m["reach"] /= k
                for c in m["clips"].values():
                    for f in ("stride", "travel", "maxLowestFoot", "headMin", "headEnd"):
                        c[f] = c[f] / k
            if spec.get("scale_length"):
                b = mesh.get_bounds().box_extent
                scale = spec["scale_length"] / max(1.0, 2 * max(b.x, b.y))
                head_cm = m["headZ"] * scale
            else:
                scale = spec["head"] / max(1.0, m["headZ"])
                head_cm = spec["head"]
            walk, run = m["clips"]["walk"], m["clips"]["run"]

            def gait_speed(c, duty):
                if c["travel"] > .25 * c["stride"]:
                    return c["travel"] * scale / max(.1, c["length"])
                return c["stride"] * scale / max(.1, 2 * duty * c["length"])
            walk_speed, run_speed = gait_speed(walk, .6), gait_speed(run, .45)
            if spec["roles"]["run"] == spec["roles"]["walk"]:
                run_speed = walk_speed * 1.6
            walk_speed = min(300.0, max(60.0, walk_speed))
            yaw = spec.get("yaw", m["yaw"])
            row = {"variant": spec["variant"], "mesh": "%s.%s" % (spec["mesh"], spec["mesh"].rsplit("/", 1)[1]), "meshScale": round(scale, 4),
                   "yaw": yaw, "heightCm": round(head_cm / (.93 if spec.get("humanoid") else .95), 1), "lockRoot": spec.get("lockRoot", True),
                   "walkSpeedCm": round(walk_speed, 1), "runSpeedCm": round(min(900.0, max(walk_speed * 1.5, run_speed)), 1),
                   "reachCm": round(max(m["reach"] * scale * 1.6, head_cm * 3.0), 1),
                   "animations": {r: m["clips"][r]["asset"] for r in spec["roles"] if r in m["clips"]},
                   "source": "Fab (licensed, local only): see Docs/FAB-PURCHASED.md, Docs/MonsterExpansion.md"}
            named = {n: m["clips"][n]["asset"] for n in spec["named"] if n in m["clips"]}
            if named:
                row["animations"]["all"] = named
            if m["soleRaw"] * scale > 5:
                row["soleCm"] = round(m["soleRaw"] * scale, 1)
            if spec.get("spectral"):
                row["spectral"] = True
            if "soleCm" in spec:  # the measured sole can be a tall claw joint that the stride later plants (the drake)
                row["soleCm"] = spec["soleCm"]
            air = m["clips"]["run"]["maxLowestFoot"] * scale - 30.0
            if air > 0:
                row["airborneCm"] = round(min(60.0, air + 5.0), 1)
            parts = []
            for p in spec.get("parts", []):
                ppath = p if p.startswith("/Game/") else "%s/%s" % (spec["mesh"].rsplit("/", 1)[0], p)
                part = u.load_asset(ppath) if exists(ppath) else None
                if part and part.get_editor_property("skeleton") == mesh.get_editor_property("skeleton"):
                    parts.append("%s.%s" % (ppath, ppath.rsplit("/", 1)[1]))
            if parts:
                row["parts"] = parts
            atts = []
            for a in spec.get("attachments", []):
                if not exists(a["mesh"]):
                    continue
                socket, bone, names = socket_for(mesh, a.get("socket_hint", ""))
                entry["sockets"] = names
                atts.append({"mesh": "%s.%s" % (a["mesh"], a["mesh"].rsplit("/", 1)[1]), "socket": socket or a["bone"]})
            if atts:
                row["attachments"] = atts
            if spec.get("reskin"):
                rs = dict(spec["reskin"])
                mats = mesh.get_editor_property("materials")
                slots = rs.pop("slots", list(range(len(mats))))
                explicit = rs.pop("textures", None)  # vendor materials without texture parameters name their maps here
                tex = {str(i): dict(explicit) for i in slots} if explicit else {}
                for i in slots:
                    mi = mats[i].get_editor_property("material_interface") if i < len(mats) else None
                    t = texture_params(mi) if mi else {}
                    used = t.pop("_used", None)
                    if used:
                        entry.setdefault("usedTextures", {})[str(i)] = used
                    if "BaseColorTex" in t and not explicit:
                        tex[str(i)] = t
                if tex:
                    rs["slots"] = tex
                    row["reskin"] = rs
                else:
                    entry["reskinError"] = "no base colour texture found"
            if spec.get("rig"):
                row["rig"] = "quadruped"
                row["sockets"] = spec["sockets"]
                row["dropPropBones"] = HAND_PROPS
            units[unit] = row
        measure_props(report)
        doc["schemaVersion"] = 1
        doc["priority"] = "highest"
        doc["description"] = ("monster-expansion: purchased Fab creature bodies for the Bestiary.json creatures (Docs/MonsterExpansion.md). "
                              "Generated by Tools/BuildFabExpansionCreatures.py; read by CireMonsterArt right after RaceMeshes.fab.json. A body is used only "
                              "when its mesh and idle clip exist locally (licensed packs, never committed); otherwise the unit keeps its fallback body.")
        doc["units"] = units
        path.write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
        u.log("CIRE_FAB_EXPANSION_PASS units=%d" % len(units))
    except Exception as error:
        report["error"] = traceback.format_exc()
        u.log_error("CIRE_FAB_EXPANSION_FAIL " + str(error))
    finally:
        (ROOT / "Saved/FabExpansion.json").write_text(json.dumps(report, indent=1, default=str), encoding="utf-8")


main()
