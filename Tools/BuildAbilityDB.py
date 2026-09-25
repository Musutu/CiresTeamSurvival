"""Build the unified ability database: Content/Data/Abilities.json + Docs/Abilities.md.

One record per champion ability and passive: the 39 skills in the native draft/shop
pool (numbers pulled from CombatTuning.json / AstraAbilities.json / the native cast
table) and every planned signature skill in ChampionRoster.json. Also writes each
champion's purchasable identity kit (role pool + hybrid roles + signature skills),
per-level scaling curves (no level cap; see Cires::Abilities::Scale), crowd-control
effect data, void zones and buff modifier summaries for the UI.

    python Tools/BuildAbilityDB.py            # writes both files
    python Tools/BuildAbilityDB.py --check    # validate only (used by tests)
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
SCHOOLS = ["physical", "fire", "cold", "earth", "tide", "holy", "shadow", "void", "poison", "nature", "arcane", "storm"]
TYPES = {"tank": "TANK", "dps": "DPS", "heal": "HEAL"}
TARGETING = ["self", "ally", "enemy", "aim", "passive"]
EFFECT_TYPES = ["stun", "slow", "silence", "interrupt", "healCut", "healCutDone", "armorBreak", "taunt", "guard", "lethal", "cleanse"]

# Default curves by kind. Scale() in CiresRules.cpp is the authority; the docs table mirrors it.
CURVES = {
    "active":   dict(effectGrowth=0.35, effectHalfLevels=4, effectCap=0, costCapMultiplier=1.5, costRampLevels=10, cooldownFloorFraction=0.6, cooldownDecayLevels=15, minCooldownSeconds=1.0),
    "ultimate": dict(effectGrowth=0.30, effectHalfLevels=5, effectCap=0, costCapMultiplier=1.4, costRampLevels=12, cooldownFloorFraction=0.7, cooldownDecayLevels=20, minCooldownSeconds=10.0),
    "passive":  dict(effectGrowth=0.25, effectHalfLevels=5, effectCap=0, costCapMultiplier=1.0, costRampLevels=10, cooldownFloorFraction=0.7, cooldownDecayLevels=20, minCooldownSeconds=60.0),
}


def scale(base, curve, level):
    """Python mirror of Cires::Abilities::Scale (docs only)."""
    level = max(1, level)
    if level == 1:
        return dict(base)
    steps = level - 1
    effect = base["effect"] * (1 + curve["effectGrowth"] * math.log1p(steps / curve["effectHalfLevels"]))
    if curve["effectCap"] > 0:
        effect = min(effect, max(base["effect"], curve["effectCap"]))
    cost = 1 + (curve["costCapMultiplier"] - 1) * steps / (steps + curve["costRampLevels"])
    cdm = curve["cooldownFloorFraction"] + (1 - curve["cooldownFloorFraction"]) * math.exp(-steps / curve["cooldownDecayLevels"])
    cd = 0 if base["cooldown"] <= 0 else max(min(base["cooldown"], curve["minCooldownSeconds"]), base["cooldown"] * cdm)
    return dict(effect=effect, manaCost=base["manaCost"] * cost, energyCost=base["energyCost"] * cost, cooldown=cd, castTime=base["castTime"])


def fx(kind, zone="target", duration=0.0, magnitude=0.0, radius=0.0, lockout=0.0, label=""):
    e = dict(type=kind, zone=zone, duration=duration, magnitude=magnitude, radius=radius)
    if lockout:
        e["lockoutSeconds"] = lockout
    if label:
        e["label"] = label
    return e


# id: (name, roles, kind, school, targeting, castTime, mana, energy, cooldown, effect, effectLabel, range, radius, duration, description, effects, extra)
POOL = {
    "iron_guard": ("Iron Guard", ["tank", "dps", "heal"], "active", "physical", "self", 0, 0, 25, 14, 40, "% damage reduction", 0, 0, 8,
                   "Take {effect}% less damage for 8s.", [fx("guard", "self", 8, 0.4, label="DEF +40%")], {"curve": {"effectCap": 60}}),
    "shield_slam": ("Shield Slam", ["tank"], "active", "physical", "enemy", 0, 0, 25, 7, 45, "damage", 240, 0, 2,
                    "Melee STR strike for {effect} damage: interrupts the target's cast (2s school lockout), slows for 2s and draws monster attention.",
                    [fx("interrupt", "target", 0, 0, lockout=2.0, label="Interrupted"), fx("slow", "target", 2, 0.35, label="Move -35%"), fx("taunt", "target", 2)], {}),
    "war_cry": ("War Cry", ["tank"], "active", "physical", "self", 0, 0, 30, 18, 6, "s taunt", 0, 600, 6,
                "Taunt nearby monsters/bots for {effect}s; guard yourself for 3s.", [fx("taunt", "area", 6, radius=600), fx("guard", "self", 3, 0.4, label="DEF +40%")], {}),
    "chain_spark": ("Chain Spark", ["dps", "heal"], "active", "storm", "enemy", 0, 45, 0, 10, 60, "damage per target", 1200, 0, 0,
                    "Lightning hits up to 4 nearby enemies for {effect} + INT damage.", [], {}),
    "frost_bind": ("Frost Bind", ["tank", "dps", "heal"], "active", "cold", "aim", 0, 35, 0, 12, 65, "damage", 1200, 34, 4,
                   "Frost projectile: {effect} damage and a 35% slow for 4s.", [fx("slow", "target", 4, 0.35, label="Move -35%")], {}),
    "cleaving_strike": ("Cleaving Strike", ["tank", "dps"], "active", "physical", "self", 0, 0, 30, 8, 55, "damage", 300, 300, 0,
                        "Primary-stat strike hits every enemy around you for {effect} damage.", [], {}),
    "shadow_step": ("Shadow Step", ["tank", "dps"], "active", "void", "enemy", 0, 0, 35, 14, 50, "damage", 850, 420, 0,
                    "Void-blink to an enemy and strike for {effect} AGI damage. The void rift stuns enemies in the inner circle (1s) and slows those in the outer ring (40%, 2.5s); you mend 5% max health.",
                    [], {"void": dict(innerRadius=180, outerRadius=420, innerEffect="stun", innerDuration=1.0, outerEffect="slow", outerDuration=2.5, outerMagnitude=0.4, damage=30, selfHealMaxHealthFraction=0.05)}),
    "restoring_light": ("Restoring Light", ["heal"], "active", "holy", "ally", 1.5, 45, 0, 6, 90, "healing", 1200, 0, 0,
                        "Cast 1.5s: heal an ally (or yourself) for {effect} + 3x INT.", [], {}),
    "sanctuary": ("Sanctuary", ["heal"], "active", "holy", "self", 2.0, 70, 0, 16, 60, "healing per ally", 0, 700, 3,
                  "Cast 2s: heal nearby allies for {effect} + INT and guard them for 3s.", [fx("guard", "area", 3, 0.4, 700, label="DEF +40%")], {}),
    "purify": ("Purify", ["heal"], "active", "holy", "ally", 1.0, 25, 0, 8, 40, "healing", 1200, 0, 0,
               "Cast 1s: remove an ally's slow and restore {effect} + INT health.", [fx("cleanse", "target")], {}),
    "summoned_wall": ("Runestone Wall", ["tank", "dps", "heal"], "active", "earth", "aim", 0, 60, 0, 22, 450, "wall health", 750, 220, 8,
                      "Raise a wall that blocks units and projectiles: {effect} health for 8s.", [], {}),
    "protection_dome": ("Aegis Dome", ["tank", "dps", "heal"], "active", "holy", "aim", 0, 55, 0, 18, 300, "dome health", 800, 240, 6,
                        "Place a projectile-blocking dome: {effect} health for 6s.", [], {}),
    "oathbound_guardian": ("Oathbound Guardian", ["tank", "dps", "heal"], "active", "void", "aim", 0, 45, 0, 20, 400, "guardian health", 600, 45, 30,
                           "Summon a commandable guardian ({effect} health, 22 damage) for 30s.", [], {}),
    "spectral_pack": ("Spectral Pack", ["dps"], "active", "void", "aim", 0, 55, 0, 24, 14, "damage per hit", 600, 230, 18,
                      "Summon three spectral hunters that attack your target for {effect} per hit for 18s.", [], {}),
    "second_wind": ("Second Wind", ["tank", "dps", "heal"], "active", "nature", "self", 0, 0, 30, 20, 18, "% max health", 0, 0, 0,
                    "Heal only yourself for {effect}% of your maximum health.", [], {"curve": {"effectCap": 35}}),
    "decimating_strike": ("Decimating Strike", ["tank", "dps"], "active", "physical", "enemy", 0, 0, 40, 300, 150, "damage (non-lethal targets)", 300, 500, 10,
                          "Kill the target outright (bosses take {effect} damage; champions take 30% of max health). Enemies within 5m have their armor reduced by 50% for 10s.",
                          [fx("lethal", "target"), fx("armorBreak", "area", 10, 0.5, 500, label="Armor -50%")], {"curve": {"minCooldownSeconds": 180}}),
    "stone_skin": ("Stone Skin", ["tank", "dps", "heal"], "passive", "earth", "passive", 0, 0, 0, 0, 10, "% damage reduction", 0, 0, 0,
                   "Take {effect}% less damage.", [], {"curve": {"effectCap": 25}}),
    "battle_rhythm": ("Battle Rhythm", ["tank", "dps", "heal"], "passive", "physical", "passive", 0, 0, 0, 0, 20, "% attack speed", 0, 0, 0,
                      "Basic attacks are {effect}% faster.", [], {"curve": {"effectCap": 45}}),
    "deep_reserves": ("Deep Reserves", ["tank", "dps", "heal"], "passive", "arcane", "passive", 0, 0, 0, 0, 50, "% resource regeneration", 0, 0, 0,
                      "Mana and energy regenerate {effect}% faster.", [], {"curve": {"effectCap": 100}}),
    "soul_conduit": ("Soul Conduit", ["tank", "dps", "heal"], "passive", "holy", "passive", 0, 0, 0, 0, 25, "% healing", 0, 0, 0,
                     "Your healing is {effect}% stronger.", [], {"curve": {"effectCap": 50}}),
    "executioner": ("Executioner", ["dps"], "passive", "physical", "passive", 0, 0, 0, 300, 100, "damage vs bosses", 0, 0, 0,
                    "Every 5 minutes your next basic attack is lethal. Bosses take a normal hit (the charge is kept); champions take 30% of their maximum health.",
                    [fx("lethal", "target")], {"curve": {"cooldownFloorFraction": 0.8, "minCooldownSeconds": 180}}),
    "bastion_of_dawn": ("Bastion of Dawn", ["tank", "heal"], "ultimate", "holy", "self", 0, 0, 45, 75, 30, "% max health healed", 0, 700, 8,
                        "Heal yourself for {effect}% max health; nearby allies take 40% less damage for 8s.", [fx("guard", "area", 8, 0.4, 700, label="DEF +40%")], {"curve": {"effectCap": 50}}),
    "cataclysm": ("Cataclysm", ["dps"], "ultimate", "fire", "enemy", 0, 150, 0, 80, 160, "damage", 1500, 450, 0,
                  "Blast up to 12 enemies near your target for {effect} + 3.5x INT damage.", [], {}),
    "executioners_verdict": ("Executioner's Verdict", ["dps"], "ultimate", "physical", "enemy", 0, 0, 60, 60, 100, "damage", 1500, 0, 0,
                             "Deal {effect} + 3x primary, plus 25% of the target's missing health (bonus capped at 300).", [], {}),
    "renewal": ("Renewal", ["heal"], "ultimate", "holy", "self", 2.5, 140, 0, 90, 200, "healing per ally", 0, 900, 0,
                "Cast 2.5s: cleanse nearby allies' slows and heal each for {effect} + 4x INT.", [fx("cleanse", "area", radius=900)], {}),
    "last_stand": ("Last Stand", ["tank"], "ultimate", "physical", "self", 0, 0, 50, 85, 40, "% max health healed", 0, 0, 0,
                   "Clear your slow and heal yourself for {effect}% maximum health.", [fx("cleanse", "self")], {"curve": {"effectCap": 60}}),
    "challenge_of_iron": ("Challenge of Iron", ["tank"], "ultimate", "physical", "self", 0, 0, 65, 90, 12, "s guard", 0, 850, 12,
                          "Taunt monsters within 8.5m (10s cap) and take 40% less damage for {effect}s.", [fx("taunt", "area", 10, radius=850), fx("guard", "self", 12, 0.4, label="DEF +40%")], {}),
    "seismic_reprisal": ("Seismic Reprisal", ["tank"], "ultimate", "earth", "self", 0, 0, 60, 65, 160, "damage", 0, 450, 0,
                         "After 0.8s the ground around you bursts for {effect} + 3x primary damage and stuns for 1s.", [fx("stun", "area", 1.0, radius=450, label="Stunned")], {}),
    "starfall": ("Starfall", ["dps"], "ultimate", "arcane", "aim", 0, 130, 0, 80, 180, "damage", 1500, 500, 0,
                 "Aim a 5m circle; after 1.25s deal {effect} + 3.5x primary damage.", [], {}),
    "spectral_hunt": ("Spectral Hunt", ["dps"], "ultimate", "void", "enemy", 0, 125, 0, 90, 58, "damage per hit", 1200, 0, 18,
                      "Three hunters attack your target for 18s, {effect} + 0.4x primary per hit.", [], {}),
    "mass_aegis": ("Mass Aegis", ["heal"], "ultimate", "holy", "self", 0, 120, 0, 85, 12, "s guard", 0, 900, 12,
                   "Cleanse nearby allies' slows and guard them for {effect}s.", [fx("guard", "area", 12, 0.4, 900, label="DEF +40%"), fx("cleanse", "area", radius=900)], {}),
    "wellspring": ("Wellspring", ["heal"], "ultimate", "tide", "ally", 2.0, 130, 0, 75, 220, "healing", 1200, 0, 5,
                   "Cast 2s: heal an ally (or yourself) for {effect} + 4x INT and guard for 5s.", [fx("guard", "target", 5, 0.4, label="DEF +40%")], {}),
    # Astra ground areas (numbers merged from AstraAbilities.json below).
    "venom_ground": ("Venom Ground", ["dps"], "active", "poison", "aim", 0, 35, 0, 8, 60, "impact damage", 900, 280, 5,
                     "Poison circle: {effect} impact damage, then damage over time for 5s.", [], {}),
    "cinder_cone": ("Cinder Cone", ["dps"], "active", "fire", "aim", 0, 35, 0, 8, 95, "impact damage", 900, 600, 5,
                    "Fire cone: {effect} impact damage, then burning ground for 5s.", [], {}),
    "grave_line": ("Grave Line", ["dps"], "active", "shadow", "aim", 0, 35, 0, 8, 85, "impact damage", 900, 280, 5,
                   "Shadow line: {effect} impact damage and silences enemies it hits for 2s.", [fx("silence", "area", 2.0, label="Silenced")], {}),
    "ashen_square": ("Ashen Ward", ["dps"], "active", "fire", "aim", 0, 35, 0, 8, 60, "impact damage", 900, 280, 4,
                     "Ash square: {effect} impact damage, then smouldering ground for 4s.", [], {}),
    "blight_sigil": ("Blight Sigil", ["dps"], "active", "poison", "aim", 0, 35, 0, 8, 60, "impact damage", 900, 280, 5,
                     "Blight sigil: {effect} impact damage; victims receive 50% less healing for 6s.", [fx("healCut", "area", 6.0, 0.5, label="Healing -50%")], {}),
    "piercing_shot": ("Piercing Shot", ["dps"], "active", "physical", "aim", 0, 0, 25, 9, 90, "damage", 1500, 30, 0,
                      "Long-range piercing shot for {effect} AGI-scaled damage.", [], {}),
    "ember_lance": ("Ember Lance", ["dps", "heal"], "active", "fire", "aim", 0, 40, 0, 6, 95, "damage", 1200, 30, 0,
                    "Fire skillshot for {effect} + 2x INT damage.", [], {}),
}
VFX_SCHOOL = {"holy": "holy", "light": "holy", "frost": "cold", "fire": "fire", "ember": "fire", "ash": "fire", "venom": "poison",
              "shadow": "shadow", "steel": "physical", "war": "physical", "blood": "physical", "arcane": "arcane", "spectral": "void",
              "spirit": "arcane", "stone": "earth", "earth": "earth", "primal": "physical", "nature": "nature", "ether": "arcane",
              "fel": "fire", "dragon": "fire"}
DELIVERY_TARGET = {"targeted": "enemy", "ground_cone": "aim", "ground_line": "aim", "ground_circle": "aim", "ground_square": "aim",
                   "ground_polygon": "aim", "self": "self", "ally": "ally", "projectile": "aim", "summon": "aim", "construct": "aim",
                   "transformation": "self", "passive": "passive", "chain": "enemy"}
ROLE_OF = {"tank": "tank", "damage": "dps", "healer": "heal", "support": "heal"}
# Extra signature assignments for the new skills (identity kits).
SIGNATURE_EXTRA = {
    "decimating_strike": ["drakish_footman", "ether_golem_bruiser", "orc_chieftain", "troll_berserker_melee"],
    "executioner": ["ranger", "lancer", "troll_berserker_melee", "troll_berserker_ranged"],
}
# Planned teleport / portal skills get a void component (radii for the aiming indicator).
VOID_PLANNED = {"troll_blood_leap", "bear_charge", "whisp_fey_trail", "centaur_trailblaze"}
PLANNED_CC = {
    "bear_roar": [fx("interrupt", "area", 0, 0, 500, lockout=2.0, label="Interrupted")],
    "miner_faultline": [fx("stun", "area", 0.8, label="Stunned")],
    "dryad_root_snare": [fx("slow", "area", 2.5, 0.6, label="Move -60%")],
    "keeper_dawn_beam": [fx("healCut", "area", 4.0, 0.3, label="Healing -30%")],
    "chieftain_earthshout": [fx("silence", "area", 1.5, label="Silenced")],
    "behemoth_stampede": [fx("stun", "area", 1.0, label="Stunned")],
}
BUFF_MODIFIERS = {
    "stunned": [dict(stat="actions", value=-1.0, label="Stunned")],
    "silenced": [dict(stat="casting", value=-1.0, label="Silenced")],
    "npc_silenced": [dict(stat="casting", value=-1.0, label="Silenced")],
    "school_locked": [dict(stat="casting_school", value=-1.0, label="School locked")],
    "heal_cut": [dict(stat="healing_received", value=-0.5, label="Healing -50%")],
    "heal_cut_done": [dict(stat="healing_done", value=-0.5, label="Healing done -50%")],
    "armor_broken": [dict(stat="armor", value=-0.5, label="Armor -50%")],
    "slowed": [dict(stat="move_speed", value=-0.35, label="Move -35%")],
    "iron_guard": [dict(stat="damage_taken", value=-0.4, label="DEF +40%")],
    "guarded": [dict(stat="damage_taken", value=-0.4, label="DEF +40%")],
    "executioner_ready": [dict(stat="next_attack", value=1.0, label="Next attack lethal")],
}


def build():
    tuning = json.loads((ROOT / "Content/Data/CombatTuning.json").read_text(encoding="utf-8"))
    astra = json.loads((ROOT / "Content/Data/AstraAbilities.json").read_text(encoding="utf-8-sig"))
    roster = json.loads((ROOT / "Content/Data/ChampionRoster.json").read_text(encoding="utf-8"))
    rules = (ROOT / "Source/CiresTeamSurvival/Rules/CiresRules.cpp").read_text(encoding="utf-8")
    catalog = dict(re.findall(r'\{"([a-z_]+)",\s*"[^"]+",\s*SkillKind::(Active|Passive|Ultimate)\}', rules))
    missing = sorted(set(catalog) - set(POOL))
    if missing:
        raise SystemExit("Pool skills without database rows: " + ", ".join(missing))
    # Pull authoritative numbers where tuning data exists.
    overrides = {}
    for s in tuning["skillshots"]:
        overrides[s["id"]] = dict(manaCost=s["manaCost"], energyCost=s["energyCost"], cooldown=s["cooldownSeconds"], effect=s["damage"], range=s["castRange"])
    for c in tuning["constructs"]:
        overrides[c["id"]] = dict(manaCost=c["manaCost"], energyCost=c["energyCost"], cooldown=c["cooldownSeconds"], effect=c["maxHealth"], range=c["castRange"])
    for s in tuning["summons"]:
        overrides[s["id"]] = dict(manaCost=s["manaCost"], energyCost=s["energyCost"], cooldown=s["cooldownSeconds"], range=s["castRange"])
    for r in tuning["roleSkills"]:
        o = dict(manaCost=r["manaCost"], energyCost=r["energyCost"], cooldown=r["cooldownSeconds"])
        if r["flatPower"]:
            o["effect"] = r["flatPower"]
        overrides[r["id"]] = o
    for a in astra["abilities"]:
        overrides[a["id"]] = dict(manaCost=a["manaCost"], energyCost=a["energyCost"], cooldown=a["cooldownSeconds"], effect=a["area"]["burstDamage"], range=a["castRange"], duration=a["area"]["durationSeconds"])
    abilities = {}
    for sid, row in POOL.items():
        name, roles, kind, school, targeting, cast, mana, energy, cd, effect, label, rng, radius, dur, desc, effects, extra = row
        base = dict(effect=effect, manaCost=mana, energyCost=energy, cooldown=cd, castTime=cast, range=rng, radius=radius, duration=dur)
        for k, v in overrides.get(sid, {}).items():
            base[k] = v
        curve = dict(CURVES[kind])
        curve.update(extra.get("curve", {}))
        rec = dict(id=sid, name=name, icon=f"/Game/UI/Abilities/T_{sid}", types=[TYPES[r] for r in roles], kind=kind, school=school,
                   targeting=targeting, castTime=cast, base=base, effectLabel=label, curve=curve, status="implemented",
                   description=desc, effects=effects, champions=[], signatureOf=[])
        if "void" in extra:
            rec["void"] = extra["void"]
        abilities[sid] = rec
    # Planned signature skills from the roster.
    for c in roster["champions"]:
        prim = ROLE_OF[c["threatRole"]]
        for slot, skills in (("active", c["actives"]), ("passive", [c["passive"]]), ("ultimate", [c["ultimate"]])):
            for s in skills:
                if s["status"] == "implemented" or s["id"] in abilities:
                    continue
                kind = slot
                cd = 0 if kind == "passive" else (70 if kind == "ultimate" else 10)
                cost = 0 if kind == "passive" else (110 if kind == "ultimate" else 35)
                school = VFX_SCHOOL.get(s["vfxFamily"], "arcane")
                heal_like = any(w in s["mechanic"].lower() for w in ("heal", "restore", "mend", "shield", "sustain"))
                effect = 0 if kind == "passive" else (140 if kind == "ultimate" else 55)
                rec = dict(id=s["id"], name=s["displayName"], icon=f"/Game/UI/Abilities/T_{s['id']}", types=[TYPES["heal" if heal_like and prim == "heal" else prim]],
                           kind=kind, school=school, targeting=DELIVERY_TARGET.get(s["delivery"], "enemy"), castTime=1.5 if (heal_like and prim == "heal" and kind != "passive") else 0,
                           base=dict(effect=effect if kind != "passive" else 10, manaCost=cost if c["primaryStat"] == "intelligence" else 0,
                                     energyCost=cost if c["primaryStat"] != "intelligence" else 0, cooldown=cd, castTime=0, range=0 if s["delivery"] in ("self", "passive") else 900,
                                     radius=300 if s["delivery"].startswith("ground") else 0, duration=0),
                           effectLabel="healing" if heal_like else ("% bonus" if kind == "passive" else "damage"),
                           curve=dict(CURVES[kind]), status="planned", description=s["mechanic"],
                           effects=PLANNED_CC.get(s["id"], []), champions=[], signatureOf=[])
                rec["base"]["castTime"] = rec["castTime"]
                if s["id"] in VOID_PLANNED:
                    rec["void"] = dict(innerRadius=160, outerRadius=380, innerEffect="stun", innerDuration=0.8, outerEffect="slow", outerDuration=2.0, outerMagnitude=0.35, damage=25, selfHealMaxHealthFraction=0.0)
                abilities[s["id"]] = rec
    # Champion identity kits.
    role_bits = {"tank": 1, "dps": 2, "heal": 4}
    champions = {}
    for c in roster["champions"]:
        prim = ROLE_OF[c["threatRole"]]
        roles = sorted({ROLE_OF[r] for r in c["roles"]} | {prim}, key=["tank", "dps", "heal"].index)
        signature = [s["id"] for s in c["actives"] + [c["passive"], c["ultimate"]]]
        signature += [sid for sid, owners in SIGNATURE_EXTRA.items() if c["id"] in owners]
        mask = sum(role_bits[r] for r in roles)
        pool = [sid for sid, a in abilities.items() if a["status"] == "implemented" and sum(role_bits[t.lower()] for t in a["types"]) & mask]
        purchasable = list(dict.fromkeys(signature + pool))
        champions[c["id"]] = dict(name=c["displayName"], primaryRole=TYPES[prim], roles=[TYPES[r] for r in roles], signature=signature,
                                  purchasable=purchasable, purchasableImplemented=[s for s in purchasable if abilities[s]["status"] == "implemented"])
        for sid in purchasable:
            abilities[sid]["champions"].append(c["id"])
        for sid in signature:
            abilities[sid]["signatureOf"].append(c["id"])
    return dict(schemaVersion=1, generator="Tools/BuildAbilityDB.py", schools=SCHOOLS, types=list(TYPES.values()),
                scalingFormula="effect*(1+g*ln(1+(L-1)/h)) capped at effectCap; cost*(1+(cap-1)(L-1)/(L-1+ramp)); cooldown*(floor+(1-floor)e^-((L-1)/decay)), min minCooldownSeconds",
                abilities=abilities, champions=champions, buffModifiers=BUFF_MODIFIERS)


def validate(db):
    errors = []
    for sid, a in db["abilities"].items():
        if a["school"] not in SCHOOLS: errors.append(f"{sid}: school {a['school']}")
        if a["targeting"] not in TARGETING: errors.append(f"{sid}: targeting {a['targeting']}")
        if not a["types"] or any(t not in TYPES.values() for t in a["types"]): errors.append(f"{sid}: types")
        if a["kind"] not in CURVES: errors.append(f"{sid}: kind")
        b = a["base"]
        if any(not isinstance(b[k], (int, float)) or b[k] < 0 for k in ("effect", "manaCost", "energyCost", "cooldown", "castTime")): errors.append(f"{sid}: base")
        for e in a["effects"]:
            if e["type"] not in EFFECT_TYPES: errors.append(f"{sid}: effect {e['type']}")
        if "void" in a and not 0 < a["void"]["innerRadius"] < a["void"]["outerRadius"]: errors.append(f"{sid}: void radii")
        prev = scale(b, a["curve"], 1)
        for level in range(2, 201):
            cur = scale(b, a["curve"], level)
            if cur["effect"] < prev["effect"] - 1e-9 or cur["manaCost"] < prev["manaCost"] - 1e-9 or cur["cooldown"] > prev["cooldown"] + 1e-9:
                errors.append(f"{sid}: curve not monotone at {level}"); break
            prev = cur
    for cid, c in db["champions"].items():
        if len(c["purchasableImplemented"]) < 8: errors.append(f"{cid}: fewer than 8 implemented purchasable skills")
    return errors


def docs(db):
    lines = ["# Ability database", "", "Generated by `python Tools/BuildAbilityDB.py` from the native pool, CombatTuning.json,",
             "AstraAbilities.json and ChampionRoster.json. Runtime accessor: `CireAbilityDB` (see the API section).", "",
             "## Scaling (no level cap)", "", "`" + db["scalingFormula"] + "`", "",
             "Level 1 equals the base. Effects grow logarithmically (some capped, e.g. percentage reductions),",
             "costs rise toward a capped multiplier and cooldowns decay toward a floor. Maths: `Cires::Abilities::Scale`.", "",
             "## Pool skills", "", "| Skill | Type | Kind | School | Target | Cast | Cost | CD | Effect L1 / L10 / L50 | CC / notes |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for sid, a in sorted(db["abilities"].items(), key=lambda kv: (kv[1]["status"], kv[1]["kind"], kv[0])):
        if a["status"] != "implemented":
            continue
        b = a["base"]
        l10, l50 = scale(b, a["curve"], 10), scale(b, a["curve"], 50)
        cost = (f"{b['manaCost']:.0f} MP" if b["manaCost"] else "") + (f"{b['energyCost']:.0f} EN" if b["energyCost"] else "") or "-"
        cc = ", ".join(e.get("label", e["type"]) for e in a["effects"])
        if "void" in a:
            v = a["void"]; cc += (", " if cc else "") + f"void: stun <= {v['innerRadius']}cm, slow <= {v['outerRadius']}cm"
        lines.append(f"| {a['name']} (`{sid}`) | {'/'.join(a['types'])} | {a['kind']} | {a['school']} | {a['targeting']} | {a['castTime']:g}s | {cost} | {b['cooldown']:g}s | "
                     f"{b['effect']:g} / {l10['effect']:.0f} / {l50['effect']:.0f} {a['effectLabel']} | {cc} |")
    lines += ["", "## Planned signature skills", "", "| Skill | Champion | Type | Kind | School | Target | Notes |", "|---|---|---|---|---|---|---|"]
    for sid, a in sorted(db["abilities"].items()):
        if a["status"] == "planned":
            extra = ", ".join(e.get("label", e["type"]) for e in a["effects"]) + (" void zones" if "void" in a else "")
            lines.append(f"| {a['name']} (`{sid}`) | {', '.join(a['signatureOf'])} | {'/'.join(a['types'])} | {a['kind']} | {a['school']} | {a['targeting']} | {extra} |")
    lines += ["", "## Champion identity kits", "", "| Champion | Roles | Signature | Implemented purchasable |", "|---|---|---|---|"]
    for cid, c in db["champions"].items():
        lines.append(f"| {c['name']} (`{cid}`) | {'/'.join(c['roles'])} | {', '.join(c['signature'])} | {len(c['purchasableImplemented'])} |")
    return "\n".join(lines) + "\n"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--check", action="store_true")
    a = p.parse_args()
    db = build()
    errors = validate(db)
    if errors:
        raise SystemExit("\n".join(errors))
    if not a.check:
        (ROOT / "Content/Data/Abilities.json").write_text(json.dumps(db, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
        tail = (ROOT / "Docs/Abilities.api.md").read_text(encoding="utf-8") if (ROOT / "Docs/Abilities.api.md").is_file() else ""
        (ROOT / "Docs/Abilities.md").write_text(docs(db) + ("\n" + tail if tail else ""), encoding="utf-8")
    print(f"{len(db['abilities'])} abilities, {len(db['champions'])} champions, ok")


if __name__ == "__main__":
    main()
