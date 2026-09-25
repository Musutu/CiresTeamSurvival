"""Write Art/Fab/FabAnimMap.json: which Fab clips each champion weapon set uses (Tools/RetargetFabAnimations.py input).

Sets (GDH All Animation Bundle unless noted), each with attacks (rotating combo), hit, death, roll, jump and an
8-direction walk/run locomotion set on the same weapon stance:
  sword_shield  GDH SwordShield V1        warden, footman, paladin, hammer_shield
  one_hand      GDH OneHandSword V1       miner, chieftain
  two_hand      GDH TwoHandSword V1       aetheri_warden, behemoth
  dual          GDH DualSword V1          troll_melee, dual_daggers
  bow           GDH Archery V1            ranger
  spell         GDH SpellCombat V1        scholar, summoner, wizard, dryad, keeper, artificer, witch_slayer
  spear         GDH Spear V1              lancer, huntress (glaive)
  gun           Gun & Sword (9CG)         gunblade (melee combo; the ranged shot keeps the Tripo clip)
  unarmed       GDH Unarmed V1 + Male Locomotion Set   ether golems
  crossbow      Crossbow Animation Set (2DragoH)   ranger_crossbow (extra set on the Ranger body: shots, hit, death,
                roll; the body keeps its bow locomotion)
  throw         GDH DaggerCombat throws             troll_ranged (extra set on the troll body: thrown axes)
fab-coverage (2026-09-25) adds to every set:
  skillN   distinct ability strikes (standalone heavy attacks / Gun & Sword skill attacks) so skills stop reusing the
           basic-attack combo;
  shout    Gun & Sword "Buff" (weapon raised, chest out) for shouts, roars, guards and stances;
  cast     GDH SpellCombat Heal (hands raised) for heals and spells cast by weapon champions;
  roll     for the spear (Spear V2), gun (Gun & Sword Roll Combat) and unarmed (Unarmed V2) sets, which had none.
Timing: clips have no authored contact frame, so RetargetFabAnimations measures it on the source (peak wrist speed).
Only paths that exist in the local packs are written. Run: python Tools/BuildFabAnimMap.py
"""
import json
from pathlib import Path
import subprocess

REPO = Path(__file__).resolve().parent.parent
main = Path(subprocess.run(["git", "worktree", "list", "--porcelain"], cwd=REPO, capture_output=True, text=True).stdout.splitlines()[0].split(" ", 1)[1])
CONTENT = main / "Content"
G = "/Game/GDHBundle/"
DIRS8 = {"f": "Fw", "fl": "FL", "fr": "FR", "l": "L", "r": "R", "b": "Bw", "bl": "BL", "br": "BR"}


def gdh_set(base, prefix, attacks, idle, hit, death, roll, jump, loco_fmt):
    """loco_fmt: '{gait}_{dir}_Loopable_IP' style name tail (gait Walk/Run)."""
    clips = {}
    for i, a in enumerate(attacks, 1):
        clips["attack%d" % i] = base + "/" + prefix + a
    for key, tail in (("hit", hit), ("death", death), ("roll", roll), ("jump", jump)):
        if tail:
            clips[key] = tail if tail.startswith("/Game/") else base + "/" + prefix + tail
    loco = {"idle": base + "/" + prefix + idle}
    for gait in ("Walk", "Run"):
        for k, d in DIRS8.items():
            loco["%s_%s" % (gait.lower(), k)] = base + "/" + prefix + loco_fmt.format(gait=gait, dir=d)
    return clips, loco


SETS = {}
JUMP = "/Game/MaleLocomotionSet/Animations/Mannequin/InPlace/Jump/A_INP_JumpIdle_inAir"  # airborne pose (weighted by AirWeight)
ss = G + "SwordShield/SwordShieldAnimV1/Animation"
SETS["sword_shield"] = gdh_set(ss, "", [], "", "", "", "", "", "")  # placeholder, filled below
SETS["sword_shield"] = (
    {**{"attack%d" % i: "%s/IP/AS_SwordAndShieldAnimV1_Attack%d_Stage1_IP" % (ss, i) for i in (3, 4, 6, 10)},
     "hit": ss + "/RM/AS_SwordAndShieldAnimV1_Defense_Hit_Fw_RM", "death": G + "OneHandSword/OneHandSwordV1/Animation/RM/AS_OneHandedSwordAnimV1_DeathV1_RM",
     "roll": ss + "/RM/AS_SwordAndShieldAnimV1_Roll_Fw_RM", "jump": JUMP},
    {"idle": ss + "/IP/AS_SwordAndShieldAnimV1_Idle1_IP",
     **{"%s_%s" % (g.lower(), k): "%s/IP/AS_SwordAndShieldAnimV1_%s_%s_Loopable_IP" % (ss, g, d) for g in ("Walk", "Run") for k, d in DIRS8.items()}})
oh = G + "OneHandSword/OneHandSwordV1/Animation"
SETS["one_hand"] = (
    {**{"attack%d" % i: "%s/IP/AS_OneHandedSwordAnimV1_Attack%d_Stage1_IP" % (oh, i) for i in (1, 3, 6, 7)},
     "hit": oh + "/RM/AS_OneHandedSwordAnimV1_HitV1_RM", "death": oh + "/RM/AS_OneHandedSwordAnimV1_DeathV1_RM",
     "roll": oh + "/RM/AS_OneHandedSwordAnimV1_RollV1_Fw_RM", "jump": JUMP},
    {"idle": oh + "/IP/AS_OneHandedSwordAnimV1_Idle1_IP",
     **{"%s_%s" % (g.lower(), k): "%s/IP/AS_OneHandedSwordAnimV1_%s_%s_Loopable_IP" % (oh, g, d) for g in ("Walk", "Run") for k, d in DIRS8.items()}})
th = G + "TwoHandSword/TwoHandedSwordAnimationsV1/Animation"
th_walk = {"f": "Fw", "fl": "FL", "fr": "FR", "l": "L", "r": "R", "b": "Bw", "bl": "Bw_L", "br": "Bw_R"}
SETS["two_hand"] = (
    {**{"attack%d" % i: "%s/IP/AS_TwoHandedSwordAnimationV1_AttackCombo%d_Stage1_IP" % (th, i) for i in (1, 2, 3, 4)},
     "hit": th + "/IP/AS_TwoHandedSwordAnimationV1_Hit_IP", "death": th + "/RM/AS_TwoHandedSwordAnimationV1_Death_RM",
     "roll": th + "/RM/AS_TwoHandedSwordAnimationV1_Roll_Fw_RM", "jump": JUMP},
    {"idle": th + "/IP/AS_TwoHandedSwordAnimationV1_Idle1_IP",
     **{"walk_%s" % k: "%s/IP/AS_TwoHandedSwordAnimationV1_Walk_%s_IP" % (th, d) for k, d in th_walk.items()},
     **{"run_%s" % k: "%s/IP/AS_TwoHandedSwordAnimationV1_Run_%s_IP" % (th, d) for k, d in DIRS8.items()}})
du = G + "DualSword/DualSwordAnimationV1/Animation"
SETS["dual"] = (
    {**{"attack%d" % i: "%s/IP/AS_DualSwordAnimV1_Attack%d_Stage1_IP" % (du, i) for i in (1, 2, 3, 4)},
     "hit": du + "/RM/AS_DualSwordAnimV1_Hit_Fw_RM", "death": du + "/RM/AS_DualSwordAnimV1_Death_Fw_RM",
     "roll": du + "/RM/AS_DualSwordAnimV1_Roll_Fw_RM", "jump": JUMP},
    {"idle": du + "/IP/AS_DualSwordAnimV1_Idle1_IP",
     **{"%s_%s" % (g.lower(), k): "%s/IP/AS_DualSwordAnimV1_%s_%s_Loopable_IP" % (du, g, d) for g in ("Walk", "Run") for k, d in DIRS8.items()}})
ar = G + "ArcheryCombatAnimV1/Animation/Character"
SETS["bow"] = (
    {**{"attack%d" % i: "%s/IP/AS_ArcheryAnimV1_Attack%d_IP" % (ar, i) for i in (1, 2, 3)},
     "hit": ar + "/RM/AS_ArcheryAnimV1_Hit_Chest_RM", "death": ar + "/RM/AS_ArcheryAnimV1_Hit_Chest_Death_RM",
     "roll": ar + "/RM/AS_ArcheryAnimV1_Roll_Fw_RM", "jump": JUMP},
    {"idle": ar + "/IP/AS_ArcheryAnimV1_Idle_IP",
     **{"%s_%s" % (g.lower(), k): "%s/IP/AS_ArcheryAnimV1_%s_%s_IP" % (ar, g, d) for g in ("Walk", "Run") for k, d in DIRS8.items()}})
sp = G + "SpellCombatAnimV1/Animation"
SETS["spell"] = (
    {**{"attack%d" % i: "%s/IP/AS_SpellCombatAnimationV1_Attack%d_IP" % (sp, i) for i in (1, 2, 4, 6, 8)},
     "heal": sp + "/IP/AS_SpellCombatAnimationV1_Heal_IP",
     "hit": sp + "/IP/AS_SpellCombatAnimationV1_Hit_Fw_IP", "death": sp + "/RM/AS_SpellCombatAnimationV1_Death_RM",
     "roll": sp + "/RM/AS_SpellCombatAnimationV1_Roll_Fw_RM", "jump": JUMP},
    {"idle": sp + "/IP/AS_SpellCombatAnimationV1_Idle1_IP",
     **{"%s_%s" % (g.lower(), k): "%s/IP/AS_SpellCombatAnimationV1_%s_%s_Loopable_IP" % (sp, g, d) for g in ("Walk", "Run") for k, d in DIRS8.items()}})
sr = G + "Spear/SpearCombatAnimationV1/Animation"
SETS["spear"] = (
    {**{"attack%d" % i: "%s/IP/AS_SpearCombatAnimationV1_AttackCombo%d_Stage1_IP" % (sr, i) for i in (1, 2, 3, 4)},
     "hit": sr + "/IP/AS_SpearCombatAnimationV1_Hit_IP", "death": sr + "/RM/AS_SpearCombatAnimationV1_Death_RM",
     "jump": JUMP},
    {"idle": sr + "/IP/AS_SpearCombatAnimationV1_Idle_IP",
     **{"%s_%s" % (g.lower(), k): "%s/IP/AS_SpearCombatAnimationV1_%s_%s_IP" % (sr, g, d) for g in ("Walk", "Run") for k, d in DIRS8.items()}})
gs = "/Game/Gun_and_Sword/Animations/Sequence"
ml = "/Game/MaleLocomotionSet/Animations/Mannequin/InPlace"
ML_DIR = {"f": "Fwd", "fl": "FwdLt_45", "fr": "FwdRt_45", "l": "FwdLt_90", "r": "FwdRt_90", "b": "Bwd", "bl": "BwdLt_45", "br": "BwdRt_45"}
male_loco = {"idle": ml + "/Idle/A_INP_Idle",
             **{"walk_%s" % k: "%s/Walk/A_INP_Walk%s_Loop" % (ml, d) for k, d in ML_DIR.items()},
             **{"run_%s" % k: "%s/Jog/A_INP_Jog%s_Loop" % (ml, d) for k, d in ML_DIR.items()}}
SETS["gun"] = (
    {**{"attack%d" % i: "%s/02_Attack/0%d_Combo_Attack_0%d/AS_Combo_Attack_0%d_01_Seq" % (gs, i, i, i) for i in (1, 2, 3, 4)},
     "hit": gs + "/08_Hit/02_Hit_Combat/AS_Hit_Combat_F_Seq", "death": gs + "/08_Hit/02_Hit_Combat/AS_Hit_Combat_Death_Seq", "jump": JUMP},
    male_loco)
un = G + "Unarmed/UnarmedFightingAnimationV1/Animation"
SETS["unarmed"] = (
    {**{"attack%d" % i: "%s/IP/AS_UnarmedFightingAnimationsV1_%d_IP" % (un, i) for i in (1, 2, 3, 4)},
     "hit": un + "/AS_UnarmedFightingAnimationsV1_Hit_1", "death": un + "/AS_UnarmedFightingAnimationsV1_Dead_1",
     "jump": JUMP},
    male_loco)

xb = "/Game/CrossbowPackAnim/Animation/UE5"
SETS["crossbow"] = (
    {"attack1": xb + "/Attack/Crossbow_Attack_01", "attack2": xb + "/Attack/Crossbow_Attack_03", "attack3": xb + "/Attack/Crossbow_Attack_02",
     "hit": xb + "/Hit/Crossbow_HitF", "death": xb + "/Death/Crossbow_Death", "roll": xb + "/Roll/Crossbow_RollF", "jump": JUMP},
    {})

# --- fab-coverage additions -------------------------------------------------------------------------------
SHOUT = "/Game/Gun_and_Sword/Animations/Sequence/02_Attack/12_Buff/AS_Buff_Seq"
CAST = sp + "/IP/AS_SpellCombatAnimationV1_Heal_IP"


def standalone(base, prefix, names):
    """Single heavy strikes: the Stage1 cut of a combo when the pack has one, else the whole clip."""
    out = []
    for n in names:
        stage = "%s/IP/%s%s_Stage1_IP" % (base, prefix, n)
        out.append(stage if exists(stage) else "%s/IP/%s%s_IP" % (base, prefix, n))
    return out


def exists(path):
    return (CONTENT / (path[len("/Game/"):] + ".uasset")).exists()


SKILLS = {
    "sword_shield": standalone(ss, "AS_SwordAndShieldAnimV1_", ["Attack1", "Attack2", "Attack5"]),
    "one_hand": standalone(oh, "AS_OneHandedSwordAnimV1_", ["Attack2", "Attack4", "Attack5"]),
    "two_hand": standalone(th, "AS_TwoHandedSwordAnimationV1_", ["AttackCombo5", "AttackCombo6", "AttackCombo7"]),
    "dual": standalone(du, "AS_DualSwordAnimV1_", ["Attack5", "Attack6", "Attack7"]),
    "bow": [ar + "/IP/AS_ArcheryAnimV1_Attack1_Charged_IP", ar + "/IP/AS_ArcheryAnimV1_Attack5_IP"],
    "spell": [sp + "/IP/AS_SpellCombatAnimationV1_Attack%d_IP" % i for i in (10, 12, 14)],
    "spear": standalone(sr, "AS_SpearCombatAnimationV1_", ["AttackCombo5", "AttackCombo6", "AttackCombo7"]),
    "gun": [gs + "/02_Attack/10_Skill_Attack/AS_Skill_0%d_Seq" % i for i in (2, 3, 4)],
    "unarmed": ["%s/IP/AS_UnarmedFightingAnimationsV1_%d_IP" % (un, i) for i in (5, 6, 7)],
    "crossbow": [],
}
ROLLS = {
    "spear": G + "Spear/SpearCombatAnimationV2/Animation/RM/AS_SpearCombatAnimationV2_Roll_Fw_RM",
    "gun": gs + "/07_Roll/02_Roll_Combat/01_RM/AS_Roll_Combat_F_0_Seq",
    "unarmed": G + "Unarmed/UnarmedFightingAnimationV2/Animation/RM/AS_UnarmedFightingAnimationsV2_Roll_Fw_RM",
}
for _name, (_clips, _loco) in SETS.items():
    for _i, _path in enumerate(SKILLS.get(_name, []), 1):
        _clips["skill%d" % _i] = _path
    if _name == "crossbow":
        continue  # extra set on the Ranger body: its shout/cast come from the bow set (INHERIT); one source clip
        #           retargeted twice onto one body in a batch collides on the output name
    _clips["shout"] = SHOUT
    if _name != "spell":
        _clips["cast"] = CAST
    if _name in ROLLS and "roll" not in _clips:
        _clips["roll"] = ROLLS[_name]
dg = G + "DaggerCombatAnimationV1/Animation/IP"
SETS["throw"] = ({"attack1": dg + "/AS_DaggerCombatAnimationV1_Throw1_IP", "attack2": dg + "/AS_DaggerCombatAnimationV1_Throw2_IP"}, {})

STYLES = {  # WeaponLoadouts preset -> set
    "warden": "sword_shield", "footman": "sword_shield", "paladin": "sword_shield", "hammer_shield": "sword_shield",
    "miner": "one_hand", "chieftain": "one_hand", "aetheri_warden": "two_hand", "behemoth": "two_hand",
    "troll_melee": "dual", "dual_daggers": "dual", "ranger": "bow",
    "scholar": "spell", "summoner": "spell", "wizard": "spell", "dryad": "spell", "keeper": "spell", "artificer": "spell",
    "witch_slayer": "spell", "tripo_witch_slayer": "spell", "lancer": "spear", "huntress": "spear", "tripo_huntress": "spear",
    "gunblade": "gun", "tripo_gunblade": "gun", "unarmed": "unarmed", "ranger_crossbow": "crossbow",
    "troll_ranged": "dual",  # fab-coverage: its throws come from the extra "throw" set (THROW_STYLES)
}
THROW_STYLES = {"troll_ranged": "throw"}  # style -> set whose attacks replace the base set's basic attack
INHERIT = {"ranger_crossbow": "bow"}  # style on an extra set -> the body's base set for skills, shout and cast
EXTRA_SETS = {  # body folder -> additional sets retargeted onto it (alternate loadouts; clips only, no locomotion)
    "Ranger": ["crossbow"],
    "troll_berserker_melee": ["throw"],  # fab-coverage: the ranged troll shares this body
}
BODIES = {  # ChampionAttacks02 body folder -> set (its drafted profile's preset)
    "Warden": "sword_shield", "drakish_footman": "sword_shield", "paladin_holy": "sword_shield",
    "dwarf_miner": "one_hand", "orc_chieftain": "one_hand", "TripoAetheriWarden": "two_hand", "totemic_behemoth": "two_hand",
    "troll_berserker_melee": "dual", "Ranger": "bow",
    "Scholar": "spell", "summoner": "spell", "wizard": "spell", "dryad": "spell", "keeper_of_light": "spell",
    "TripoAetheriArtificer": "spell", "TripoWitchSlayer": "spell", "lancer": "spear", "TripoHuntress": "spear",
    "TripoGunblade": "gun", "ether_golem_bruiser": "unarmed", "ether_golem_support": "unarmed", "ether_golem_tank": "unarmed",
}


# fab-coverage: Tripo monster bodies whose rallies / enrages / guards / self circles have no shout or slam clip get
# the Gun & Sword "Buff" (monster_war_cry) and "Air to Floor" landing strike (monster_ground_slam), retargeted by
# RetargetFabAnimations.py -CireFabAnimMonsters and listed in Content/Data/MonsterFabClips.json.
MONSTER_CLIPS = {"monster_war_cry": SHOUT,
                 "monster_ground_slam": gs + "/02_Attack/14_Attack_Air_to_Floor_01/AS_Attack_Air_to_Floor_01_End_Seq"}
NEEDS_SHOUT = {"targetcircle", "guard", "provoke", "rally", "enrage", "healally", "shieldwall", "summon", "deploy", "pull"}


def monster_bodies():
    data = lambda n: json.loads((REPO / "Content/Data" / n).read_text(encoding="utf-8"))
    races, npc_arch = data("Races.json")["races"], data("NPCArchetypes.json")["archetypes"]
    fab = data("RaceMeshes.fab.json")["units"]
    tables = [data("RaceMeshes.tripo.json")["archetypes"], data("NPCMeshes.tripo.json")["archetypes"]]
    bodies = {}
    for race in races.values():
        for uid, unit in race["units"].items():
            if uid in fab:
                continue  # Fab creature bodies use their own pack's clips
            entry = next((t[uid] for t in tables if uid in t), None)
            if not entry:
                continue
            abilities = (unit.get("archetype") or {}).get("abilities") or unit.get("abilities") or npc_arch.get(uid, {}).get("abilities", [])
            kinds = {str(a.get("type", "")).lower() for a in abilities if isinstance(a, dict)}
            for body in [entry] + list(entry.get("alternates", [])):
                named = set((body.get("animations", {}).get("all") or {}).keys())
                want = ("selfcircle" in kinds and "ground_slam" not in named) or \
                       (bool(kinds & NEEDS_SHOUT) and not named & {"war_cry", "cast_a_spell"})
                if want and body.get("mesh"):
                    bodies[body["mesh"]] = body.get("variant", uid)
    return bodies


def main():
    clips, loco, missing, replace = {}, {}, [], {}
    for name, (c, l) in SETS.items():
        ok = {}
        for key, path in c.items():
            if exists(path):
                ok[key] = path
                clips["%s_%s" % (name, key)] = {"path": "%s.%s" % (path, path.rsplit("/", 1)[1]), "set": name, "kind": "attack" if key.startswith("attack") else key}
            else:
                missing.append(path)
        good = {}
        for key, path in l.items():
            (good.__setitem__(key, "%s.%s" % (path, path.rsplit("/", 1)[1])) if exists(path) else missing.append(path))
        loco[name] = good
        attacks = ["%s_%s" % (name, k) for k in ok if k.startswith("attack")]
        skills = ["%s_%s" % (name, k) for k in ok if k.startswith("skill")]
        # fab-coverage: skills rotate through their own heavy strikes only (Pick rotates from a per-cast seed, so
        # listing the combo too would often pick a basic swing). A set without skill clips keeps the combo.
        row = {"attack": attacks, "ability": skills or attacks}
        if name == "spell":
            # fab-coverage: casters' spells rotate the heavy spell casts and the heal (the combo only as a fallback set).
            row["spell"] = (skills + (["spell_heal"] if "heal" in ok else [])) or attacks
        elif "cast" in ok:
            row["spell"] = ["%s_cast" % name]
        if "shout" in ok:
            row["shout"] = ["%s_shout" % name]
        for k in ("hit", "death", "roll", "jump"):
            if k in ok:
                row[k] = ["%s_%s" % (name, k)]
        replace[name] = row
    out = {
        "_comment": "Generated by Tools/BuildFabAnimMap.py. clips.<set>_<key>: source clip; timing measured by RetargetFabAnimations.py. "
                    "locomotion.<set>: idle + walk/run in 8 directions. bodies: ChampionAttacks02 folder -> set. styles: WeaponLoadouts preset -> set.",
        "clips": clips, "locomotion": {k: v for k, v in loco.items() if v}, "bodies": BODIES, "extraSets": EXTRA_SETS,
        "monsters": {"clips": {k: {"path": "%s.%s" % (v, v.rsplit("/", 1)[1]), "kind": "shout" if "war_cry" in k else "attack"}
                               for k, v in MONSTER_CLIPS.items() if exists(v)}, "bodies": monster_bodies()},
        "replace": {("style:" + preset): dict(replace[s], **({"attack": replace[THROW_STYLES[preset]]["attack"]}
                                                             if preset in THROW_STYLES else {}),
                                              **({k: replace[INHERIT[preset]][k] for k in ("spell", "shout")
                                                  if k in replace[INHERIT[preset]]} if preset in INHERIT else {}))
                    for preset, s in STYLES.items()},
    }
    (REPO / "Art/Fab/FabAnimMap.json").write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    print("clips %d, locomotion sets %d, missing %d" % (len(clips), len(loco), len(missing)))
    for m in missing:
        print("  missing " + m)


main()
