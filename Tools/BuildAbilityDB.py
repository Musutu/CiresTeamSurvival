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
EFFECT_TYPES = ["stun", "slow", "silence", "interrupt", "healCut", "healCutDone", "armorBreak", "taunt", "guard", "lethal", "cleanse",
                "mark", "purge", "banish", "haste", "weaken", "shield"]  # new-champions: marks, purges, banishment, construct fields

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

# new-champions: signature kits of the Gunblade, Witch Slayer, Huntress and the two Aetheri champions.
# Implemented natively by CireSignatureSkills / CireTechConstructs (Docs/NewChampions.md). They are
# signature-only: never added to the generic role pools, purchasable only by the champions whose roster
# kit lists them. Same tuple layout as POOL; extra "category": "construct" marks the Skill Shop's
# Constructs tab. Construct numbers beyond the headline effect live in CireTechConstructs.cpp.
CONSTRUCT = {"category": "construct"}
PET = {"category": "pet"}  # pets: companion commands (Skill Shop "COMPANION" filter)
NEW_CHAMPION_SKILLS = {
    # ---- Gunblade (Bounty Hunter): AGI, energy ----
    "silver_shot": ("Silver Shot", ["dps"], "active", "holy", "aim", 0, 0, 30, 8, 95, "damage", 1300, 28, 0,
                    "Aimed silver round: {effect} + 1.6x AGI damage; +60% against undead and void monsters.", [], {}),
    "hex_mark": ("Hex Mark", ["dps"], "active", "shadow", "enemy", 0, 0, 20, 12, 15, "% damage taken", 1100, 0, 12,
                 "Put a bounty on an enemy for 12s: it takes {effect}% more damage from every source, and killing it pays bounty gold (25, elites 60, bosses 150).",
                 [fx("mark", "target", 12, 0.15, label="Damage taken +15%")], {}),
    "powder_flask": ("Powder Flask", ["dps"], "active", "fire", "aim", 0, 0, 30, 12, 70, "impact damage", 900, 260, 2.5,
                     "Hurl a powder flask: after a short fuse it bursts for {effect} + 1x AGI fire damage in 2.6m and slows by 35% for 2.5s.",
                     [fx("slow", "area", 2.5, 0.35, 260, label="Move -35%")], {}),
    "blade_flurry": ("Blade Flurry", ["dps"], "active", "physical", "self", 0, 0, 35, 9, 40, "damage per slash", 0, 320, 0,
                     "Three quick falchion slashes around you, each {effect} + 0.8x AGI damage.", [], {}),
    "hunters_stride": ("Hunter's Stride", ["dps"], "active", "physical", "aim", 0, 0, 25, 11, 50, "% next shot damage", 550, 0, 4,
                       "Dash up to 5.5m; your next basic attack within 4s deals {effect}% more damage.", [], {}),
    "warding_talisman": ("Warding Talisman", ["dps"], "active", "holy", "self", 0, 0, 25, 18, 30, "% damage reduction", 0, 0, 4,
                         "Raise the talisman: take {effect}% less damage for 4s and shake off slows.",
                         [fx("guard", "self", 4, 0.3, label="DEF +30%"), fx("cleanse", "self")], {"curve": {"effectCap": 45}}),
    "price_on_every_soul": ("Price on Every Soul", ["dps"], "passive", "physical", "passive", 0, 0, 0, 0, 5, "bonus gold per kill", 0, 0, 0,
                            "Your killing blows pay {effect} bonus gold (x3 elites, x8 bosses) and restore 15 energy.", [], {}),
    "collect_the_bounty": ("Collect the Bounty", ["dps"], "ultimate", "physical", "enemy", 0, 0, 60, 70, 180, "damage", 950, 0, 0,
                           "Execution shot: {effect} + 3x AGI plus 40% of missing health (bonus capped at 400). Non-boss monsters under 20% health die; a kill refunds half the cooldown and pays a marked bounty twice.",
                           [fx("lethal", "target")], {}),
    # ---- Witch Slayer: INT, mana ----
    "arcane_blunderbuss": ("Arcane Blunderbuss", ["dps"], "active", "arcane", "aim", 0, 40, 0, 8, 110, "damage", 550, 550, 0,
                           "A 60-degree cone of arcane shot (5.5m): {effect} + 1.8x INT damage; +40% against casting enemies and interrupts their cast.",
                           [fx("interrupt", "area", 0, 0, 550, lockout=1.5, label="Interrupted")], {}),
    "spirit_lantern": ("Spirit Lantern", ["dps"], "active", "arcane", "aim", 0, 45, 0, 14, 60, "damage", 800, 260, 20,
                       "Set a spirit-lantern trap (20s, up to 2). The first enemy to come within 1.5m sets it off: {effect} + 1x INT damage and a 2s silence within 2.6m.",
                       [fx("silence", "area", 2, radius=260, label="Silenced")], CONSTRUCT),
    "purge": ("Purge", ["dps"], "active", "holy", "enemy", 0, 40, 0, 14, 60, "damage", 1000, 0, 2.5,
              "Strip every buff from an enemy and silence it for 2.5s; deals {effect} + 1x INT damage.",
              [fx("purge", "target"), fx("silence", "target", 2.5, label="Silenced")], {}),
    "banishment": ("Banishment", ["dps"], "active", "void", "enemy", 0, 55, 0, 22, 80, "damage on return", 900, 0, 2,
                   "Exile an enemy for 2s: it cannot act, move or be harmed, then returns for {effect} + 1.5x INT void damage. Bosses resist: silenced and slowed instead.",
                   [fx("banish", "target", 2, label="Banished")], {}),
    "witchfinders_mark": ("Witchfinder's Mark", ["dps"], "active", "arcane", "enemy", 0, 25, 0, 10, 12, "% damage taken", 1200, 0, 10,
                          "Mark an enemy for 10s: it is revealed and takes {effect}% more damage, tripled while it casts.",
                          [fx("mark", "target", 10, 0.12, label="Exposed")], {}),
    "spectral_blade": ("Spectral Blade", ["dps"], "active", "arcane", "enemy", 0, 30, 0, 7, 90, "damage", 600, 0, 0,
                       "Lunge up to 6m with the spectral blade: {effect} + 1.4x INT damage.", [], {}),
    "witchbane": ("Witchbane", ["dps"], "passive", "arcane", "passive", 0, 0, 0, 0, 20, "% bonus damage", 0, 0, 0,
                  "You deal {effect}% more damage to enemies that are casting, silenced or marked.", [], {"curve": {"effectCap": 40}}),
    "hexbane_judgment": ("Hexbane Judgment", ["dps"], "ultimate", "arcane", "aim", 0, 130, 0, 80, 200, "damage", 1100, 450, 3,
                         "Aim a 4.5m circle; after 1s it bursts for {effect} + 3x INT damage, strips every buff and silences for 3s.",
                         [fx("silence", "area", 3, radius=450, label="Silenced"), fx("purge", "area", radius=450)], {}),
    # ---- Huntress (glaive thrower on foot): AGI, energy. The sabercat skills command her companion Ashfang (CirePets, Docs/Pets.md) ----
    "bouncing_glaive": ("Bouncing Glaive", ["dps"], "active", "physical", "enemy", 0, 0, 30, 8, 80, "damage", 1300, 500, 0,
                        "Hurl a glaive that strikes the target and bounces to 4 more enemies within 5m, losing 20% per bounce: {effect} + 1.5x AGI.", [], {}),
    "sabercat_pounce": ("Ashfang: Pounce", ["dps"], "active", "physical", "enemy", 0, 0, 35, 12, 75, "damage", 700, 260, 2,
                        "Command Ashfang to leap up to 7m onto your target and maul everything within 2.6m on landing: {effect} + 1x AGI and a 40% slow for 2s. Needs your companion.",
                        [fx("slow", "area", 2, 0.4, 260, label="Move -40%")], PET),
    "owl_scout": ("Owl Scout", ["dps"], "active", "nature", "aim", 0, 0, 20, 14, 10, "% damage taken", 1600, 450, 8,
                  "Send the owl to a point: enemies within 4.5m are revealed and tracked for 8s, taking {effect}% more damage from you.",
                  [fx("mark", "area", 8, 0.10, 450, label="Tracked")], {}),
    "moonlit_sprint": ("Moonlit Sprint", ["dps"], "active", "arcane", "self", 0, 0, 20, 16, 40, "% move speed", 0, 0, 4,
                       "Sprint under the moon: +{effect}% movement speed for 4s and shake off slows.",
                       [fx("haste", "self", 4, 0.4, label="Move +40%"), fx("cleanse", "self")], {"curve": {"effectCap": 70}}),
    "crescent_volley": ("Crescent Volley", ["dps"], "active", "physical", "aim", 0, 0, 30, 10, 90, "damage", 1400, 40, 0,
                        "Loose a crescent glaive that flies 14m in a line and cuts through up to five enemies: {effect} + 1.4x AGI.", [], {}),
    "sabercat_maul": ("Ashfang: Maul", ["dps"], "active", "physical", "enemy", 0, 0, 25, 7, 70, "damage", 1600, 0, 0,
                      "Command Ashfang to maul your target: {effect} + 1.2x AGI, with 50% bonus threat so the cat holds it off you. Needs your companion.", [], PET),
    "sabercat_roar": ("Ashfang: Dread Roar", ["dps"], "active", "nature", "self", 0, 0, 20, 16, 30, "damage", 0, 450, 3,
                      "Ashfang roars: enemies within 4.5m of the cat take {effect} + 0.4x AGI, are slowed 35% for 3s and turn on Ashfang for 2s. Also the pet's special command. Needs your companion.",
                      [fx("slow", "area", 3, 0.35, 450, label="Move -35%"), fx("taunt", "area", 2, radius=450, label="Taunted by Ashfang")], PET),
    "moon_glaive": ("Moon Glaive", ["dps"], "passive", "physical", "passive", 0, 0, 0, 0, 60, "% bounce damage", 0, 450, 0,
                    "Your glaive throws bounce to 2 more enemies within 4.5m, for {effect}% and then 36% damage.", [], {"curve": {"effectCap": 85}}),
    "glaive_storm": ("Glaive Storm", ["dps"], "ultimate", "physical", "self", 0, 0, 70, 75, 45, "damage per tick", 0, 480, 6,
                     "A whirling storm of glaives surrounds you for 6s, striking enemies within 4.8m for {effect} + 0.5x AGI every 0.5s.", [], {}),
    # ---- Aetheri Artificer: INT, mana; Constructs ----
    "photon_turret": ("Photon Turret", ["dps"], "active", "arcane", "aim", 0, 55, 0, 14, 240, "turret health", 800, 950, 25,
                      "Warp in a turret for 25s ({effect} health, up to 2) that fires energy bolts at enemies within 9.5m: 18 + 0.35x INT every 0.8s.", [], CONSTRUCT),
    "skitter_swarm": ("Skitter Swarm", ["dps"], "active", "arcane", "aim", 0, 45, 0, 15, 70, "blast damage", 700, 200, 12,
                      "Deploy three skitter bombs (up to 6) that race at the nearest enemies and explode for {effect} + 0.8x INT in 2m.", [], CONSTRUCT),
    "arc_mine": ("Arc Mine", ["dps"], "active", "arcane", "aim", 0, 35, 0, 9, 110, "blast damage", 800, 240, 30,
                 "Plant a mine (30s, up to 3): the first enemy within 1.5m detonates it for {effect} + 1.2x INT in 2.4m.", [], CONSTRUCT),
    "disruption_pylon": ("Disruption Pylon", ["dps"], "active", "arcane", "aim", 0, 50, 0, 20, 25, "% damage dealt", 800, 450, 15,
                         "Warp in a pylon (15s): enemies inside its 4.5m field deal {effect}% less damage.",
                         [fx("weaken", "area", 0, 0.25, 450, label="Damage -25%")], dict(CONSTRUCT, curve={"effectCap": 40})),
    "phase_lance": ("Phase Lance", ["dps"], "active", "arcane", "aim", 0, 40, 0, 6, 100, "damage", 1300, 30, 0,
                    "Fire an energy lance skillshot: {effect} + 1.8x INT damage.", [], {}),
    "overcharge": ("Overcharge", ["dps"], "active", "arcane", "self", 0, 40, 0, 20, 100, "% turret fire rate", 0, 1200, 6,
                   "Overcharge your constructs within 12m for 6s: turrets fire {effect}% faster and every construct regains 25% health.", [], CONSTRUCT),
    "aether_engineering": ("Aether Engineering", ["dps"], "passive", "arcane", "passive", 0, 0, 0, 0, 25, "% construct health", 0, 0, 0,
                           "Your constructs have {effect}% more health and last 20% longer.", [], CONSTRUCT),
    "warp_obelisk": ("Warp Obelisk", ["dps"], "ultimate", "arcane", "aim", 0, 130, 0, 80, 600, "obelisk health", 800, 1300, 15,
                     "Warp in a siege obelisk for 15s ({effect} health): heavy beams at enemies within 13m for 60 + 1x INT, splashing 2m.", [], CONSTRUCT),
    # ---- Aetheri Warden: INT, mana; Constructs; Support with Tank hybrid ----
    "aegis_pylon": ("Aegis Pylon", ["heal", "tank"], "active", "arcane", "aim", 0, 55, 0, 18, 2, "% max health per second", 800, 450, 15,
                    "Warp in a pylon (15s): allies inside its 4.5m field regenerate {effect}% max health per second and take 15% less damage.",
                    [fx("shield", "area", 0, 0.15, 450, label="DEF +15%")], dict(CONSTRUCT, curve={"effectCap": 4})),
    "haste_pylon": ("Haste Pylon", ["heal", "tank"], "active", "arcane", "aim", 0, 45, 0, 20, 25, "% move and attack speed", 800, 450, 12,
                    "Warp in a pylon (12s): allies inside its 4.5m field move and attack {effect}% faster.",
                    [fx("haste", "area", 0, 0.25, 450, label="Haste +25%")], dict(CONSTRUCT, curve={"effectCap": 40})),
    "gravity_pylon": ("Gravity Pylon", ["heal", "tank"], "active", "arcane", "aim", 0, 45, 0, 18, 35, "% slow", 800, 450, 12,
                      "Warp in a pylon (12s): enemies inside its 4.5m field are slowed by {effect}%.",
                      [fx("slow", "area", 0, 0.35, 450, label="Move -35%")], dict(CONSTRUCT, curve={"effectCap": 50})),
    "stasis_snare": ("Stasis Snare", ["heal", "tank"], "active", "arcane", "aim", 0, 35, 0, 14, 20, "damage", 800, 150, 30,
                     "Plant a snare (30s, up to 3): the first enemy within 1.5m is locked in stasis for 1.5s and takes {effect} + 0.3x INT damage. Bosses are slowed instead.",
                     [fx("stun", "area", 1.5, radius=150, label="Stasis")], CONSTRUCT),
    "aether_mend": ("Aether Mend", ["heal"], "active", "arcane", "ally", 1.0, 40, 0, 6, 85, "healing", 1200, 0, 0,
                    "Cast 1s: mend an ally (or yourself) for {effect} + 2.5x INT.", [], {}),
    "repulsor_pulse": ("Repulsor Pulse", ["tank", "heal"], "active", "arcane", "self", 0, 35, 0, 10, 60, "damage", 0, 350, 2,
                       "Release a pulse around you: {effect} + 1x INT damage and a 30% slow for 2s to enemies within 3.5m; monsters turn on you briefly.",
                       [fx("slow", "area", 2, 0.3, 350, label="Move -30%"), fx("taunt", "area", 2, radius=350)], {}),
    "resonant_lattice": ("Resonant Lattice", ["heal", "tank"], "passive", "arcane", "passive", 0, 0, 0, 0, 10, "% damage reduction in fields", 0, 0, 0,
                         "Allies inside your Aegis Pylon or Nexus take a further {effect}% less damage, and your pylons last 20% longer.",
                         [], dict(CONSTRUCT, curve={"effectCap": 20})),
    "aether_nexus": ("Aether Nexus", ["heal", "tank"], "ultimate", "arcane", "aim", 0, 120, 0, 85, 5, "% max health per second", 800, 650, 10,
                     "Warp in a Nexus for 10s: allies inside its 6.5m field take 40% less damage and regenerate {effect}% max health per second; enemies inside are slowed.",
                     [fx("guard", "area", 0, 0.4, 650, label="DEF +40%")], dict(CONSTRUCT, curve={"effectCap": 8})),
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
    "sabercat_roar": ["huntress"],  # pets: the companion's third command joins her identity kit
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
# Modifier registry rows (Docs/BuffModifiers.md format) for the effect ids this DB adds.
# stunned / silenced / healing_cut / interrupted rows live in BuffModifiers.json (wow-ui).
BUFF_MODIFIERS = {
    "heal_cut_done": {"type": "curse", "control": "none", "mods": [{"stat": "Healing", "value": -50, "unit": "%"}],
                      "line": "Healing you deal is reduced.", "name": "Enfeebled"},
    "armor_broken": {"type": "physical", "control": "none", "mods": [{"stat": "Armor", "value": -50, "unit": "%", "duration": 10}],
                     "line": "Armor shattered by a Decimating Strike.", "name": "Armor Broken"},
    "executioner_ready": {"type": "none", "control": "none", "kind": "buff", "mods": [{"stat": "ATK", "value": 0, "unit": ""}],
                          "line": "Your next basic attack is lethal (bosses excepted; champions lose 30% max health).", "name": "Executioner Ready"},
    # champion-draft: dodge-roll skill states (CireRollSkills::BuffIds).
    "tumblers_edge": {"type": "none", "control": "none", "kind": "buff", "mods": [{"stat": "ATK", "value": 50, "unit": "%"}],
                      "line": "Your next basic attack deals more damage.", "name": "Tumbler's Edge"},
    "killer_instinct": {"type": "none", "control": "none", "kind": "buff", "mods": [{"stat": "ATK", "value": 0, "unit": ""}],
                        "line": "Your next basic attack is a guaranteed critical strike.", "name": "Killer Instinct"},
    "windrunner": {"type": "magic", "control": "none", "kind": "buff", "mods": [{"stat": "Move", "value": 10, "unit": "%"}],
                   "line": "Moves faster after a roll.", "name": "Windrunner"},
    "quickened_mind": {"type": "magic", "control": "none", "kind": "buff", "mods": [{"stat": "Haste", "value": 0, "unit": ""}],
                       "line": "Your next spell with a cast time is instant.", "name": "Quickened Mind"},
    "momentum": {"type": "none", "control": "none", "kind": "buff", "mods": [{"stat": "Damage Dealt", "value": 4, "unit": "%"}],
                 "line": "More damage per stack; each roll adds a stack (max 5).", "name": "Momentum"},
    "blur_step": {"type": "magic", "control": "none", "kind": "buff", "mods": [{"stat": "DEF", "value": 0, "unit": ""}],
                  "line": "Blurred: a chance to dodge incoming attacks.", "name": "Blur"},
    "mine_layer": {"type": "none", "control": "none", "kind": "stance", "callout": False, "mods": [{"stat": "ATK", "value": 0, "unit": ""}],
                   "line": "Your next roll drops a caltrop mine.", "name": "Caltrop Mine"},
    "taunting_tumble": {"type": "none", "control": "none", "kind": "stance", "mods": [{"stat": "DEF", "value": 20, "unit": "%", "duration": 1.5}],
                        "line": "Each roll taunts nearby monsters and guards you briefly.", "name": "Taunting Tumble"},
    "shield_tumble": {"type": "magic", "control": "none", "kind": "stance", "mods": [{"stat": "DEF", "value": 40, "unit": "%", "duration": 3}],
                      "line": "Each roll shields and heals the nearest ally.", "name": "Shield Tumble"},
    "venom_tumble": {"type": "poison", "control": "none", "kind": "stance", "mods": [{"stat": "Healing", "value": -30, "unit": "%", "duration": 5}],
                     "line": "Your rolls leave venom that cuts enemy healing.", "name": "Venom Tumble"},
    "shadow_dance": {"type": "magic", "control": "none", "kind": "stance", "mods": [{"stat": "Haste", "value": 70, "unit": "%"}],
                     "line": "Your dodge roll recovers faster and refunds its energy.", "name": "Shadow Dance"},
    "evasive_stance": {"type": "none", "control": "none", "kind": "stance", "mods": [{"stat": "DEF", "value": 0, "unit": ""}],
                       "line": "Rolls refund energy; dodged hits restore health.", "name": "Evasive Stance"},
}


# Dodge-roll synergy skills (CireRollSkills, Docs/Abilities.md "Dodge-roll skills"). Signature-only: purchasable by the
# champions listed in ROLL_AVAILABLE (agile DPS, rogue-likes, Gunblade, Huntress, bruiser tanks,
# mobile supports). "section" is the Skill Shop periodic-table section (progression-shop SECTIONS),
# "categories" the player-facing groups (primary first; Offensive, Defensive, Crowd Control, Summons,
# Constructs, Passives, Ultimates) and "effectTags" the card tags ("Roll" first, then Stun/Slow/Heal/...).
AGILE = ["ranger", "lancer", "troll_berserker_melee", "troll_berserker_ranged", "gunblade", "huntress", "witch_slayer"]
BRUISER = ["ether_golem_bruiser", "orc_chieftain", "drakish_footman", "paladin_righteous", "knight"]
MOBILE_SUPPORT = ["dryad", "whisp", "evergrove_centaur", "aetheri_warden", "scholar"]
CASTERS = ["wizard", "aetheri_artificer", "witch_slayer"]
ROLL_SKILLS = {
    # id: (name, roles, kind, school, targeting, mana, energy, cooldown, effect, effectLabel, range, radius, duration, section, categories, effectTags, description, effects, curveExtra, available)
    "riposte_roll": ("Riposte", ["dps", "tank"], "passive", "physical", "passive", 0, 0, 0, 60, "counter damage", 700, 0, 0, "passive", ["Passives", "Offensive"], ["Roll", "Counter", "I-Frame", "Damage"],
                     "A hit you dodge during the roll's i-frames triggers a counter strike on the attacker for {effect} + 1x primary stat damage (once per roll).", [], {}, AGILE + BRUISER),
    "tumblers_edge": ("Tumbler's Edge", ["dps"], "passive", "physical", "passive", 0, 0, 0, 50, "% next attack damage", 0, 0, 4, "passive", ["Passives", "Offensive"], ["Roll", "Empower", "Next Attack"],
                      "After a roll, your next basic attack within 4s deals {effect}% more damage.", [], {"effectCap": 120}, AGILE),
    "killer_instinct": ("Killer Instinct", ["dps"], "passive", "physical", "passive", 0, 0, 0, 100, "% crit chance", 0, 0, 4, "passive", ["Passives", "Offensive"], ["Roll", "Crit", "Next Attack"],
                        "After a roll, your next basic attack within 4s is a guaranteed critical strike.", [], {"effectCap": 100}, AGILE),
    "fleet_recovery": ("Fleet Recovery", ["tank", "dps", "heal"], "passive", "nature", "passive", 0, 0, 0, 5, "% max health per roll", 0, 0, 0, "passive", ["Passives", "Defensive"], ["Roll", "Heal"],
                       "Every dodge roll restores {effect}% of your maximum health + 0.5x primary stat.", [], {"effectCap": 12}, AGILE + BRUISER + MOBILE_SUPPORT),
    "windrunner": ("Windrunner", ["dps", "heal"], "passive", "storm", "passive", 0, 0, 0, 10, "% move speed", 0, 0, 5, "passive", ["Passives", "Defensive"], ["Roll", "Haste", "Move Speed"],
                   "Every dodge roll grants {effect}% movement speed for 5s.", [], {"effectCap": 25}, AGILE + MOBILE_SUPPORT),
    "quickened_mind": ("Quickened Mind", ["heal", "dps"], "passive", "arcane", "passive", 0, 0, 0, 6, "s window", 0, 0, 6, "passive", ["Passives", "Offensive"], ["Roll", "Instant Cast"],
                       "After a roll, your next spell with a cast time (within {effect}s) is cast instantly.", [], {"effectCap": 10}, MOBILE_SUPPORT + CASTERS),
    "hasted_tumble": ("Hasted Tumble", ["tank", "dps", "heal"], "passive", "arcane", "passive", 0, 0, 0, 15, "% cooldown cut per roll", 0, 0, 0, "passive", ["Passives", "Offensive"], ["Roll", "Cooldown"],
                      "Every dodge roll shortens the remaining cooldowns of your active skills by {effect}%.", [], {"effectCap": 30}, AGILE + MOBILE_SUPPORT + CASTERS),
    "ember_wake": ("Ember Wake", ["dps"], "passive", "fire", "passive", 0, 0, 0, 40, "fire damage", 0, 180, 0, "passive", ["Passives", "Offensive"], ["Roll", "Trail", "Damage"],
                   "Your roll leaves a line of embers: enemies within 1.8m of the path take {effect} + 0.6x primary stat fire damage.", [], {}, ["gunblade", "wizard", "troll_berserker_melee", "lancer", "drakish_footman"]),
    "frost_wake": ("Frost Wake", ["dps", "tank"], "passive", "cold", "passive", 0, 0, 0, 25, "frost damage", 0, 180, 3, "passive", ["Passives", "Crowd Control"], ["Roll", "Slow", "Trail", "Damage"],
                   "Your roll leaves a frost trail: enemies within 1.8m of the path take {effect} + 0.4x primary stat damage and are slowed 40% for 3s.",
                   [fx("slow", "area", 3, 0.4, 180, label="Move -40%")], {}, ["ranger", "huntress", "wizard", "lancer", "knight", "paladin_righteous", "orc_chieftain"]),
    "momentum": ("Momentum", ["dps", "tank"], "passive", "physical", "passive", 0, 0, 0, 4, "% damage per stack", 0, 0, 8, "passive", ["Passives", "Offensive"], ["Roll", "Stacking", "Empower"],
                 "Each roll grants a Momentum stack for 8s (max 5): +{effect}% damage per stack.", [], {"effectCap": 8}, AGILE + BRUISER),
    "blur_step": ("Blur", ["dps"], "passive", "shadow", "passive", 0, 0, 0, 25, "% dodge chance", 0, 0, 3, "passive", ["Passives", "Defensive"], ["Roll", "Dodge"],
                  "After a roll you blur for 3s: {effect}% chance to dodge incoming attacks.", [], {"effectCap": 50}, AGILE),
    "slippery_roll": ("Slippery", ["tank", "dps", "heal"], "passive", "nature", "passive", 0, 0, 0, 1, "debuff cleansed per roll", 0, 0, 0, "passive", ["Passives", "Defensive"], ["Roll", "Cleanse"],
                      "Every dodge roll cleanses one debuff: slows first, then healing cuts, silences and armor breaks.", [fx("cleanse", "self")], {"effectCap": 1}, AGILE + BRUISER + MOBILE_SUPPORT),
    "bloodrush": ("Bloodrush", ["dps", "tank"], "passive", "physical", "passive", 0, 0, 0, 100, "% roll reset", 0, 0, 0, "passive", ["Passives", "Offensive"], ["Roll", "Reset"],
                  "Killing an enemy resets your dodge roll cooldown and refunds its energy.", [], {"effectCap": 100}, AGILE + BRUISER),
    "tumble_strike": ("Tumble Strike", ["dps", "tank"], "active", "physical", "enemy", 0, 30, 10, 70, "damage", 700, 0, 0, "attack", ["Offensive"], ["Roll", "Gap Closer", "Damage"],
                      "Roll toward your target (ignores the dodge cooldown and triggers your roll skills), then strike for {effect} + 1.5x primary stat damage.", [], {}, AGILE + BRUISER),
    "mine_layer": ("Caltrop Mine", ["dps", "tank"], "active", "physical", "self", 0, 25, 16, 80, "mine damage", 0, 250, 20, "construct", ["Constructs", "Crowd Control"], ["Roll", "Trap", "Slow", "Damage"],
                   "For 8s your next roll drops a caltrop mine (20s, up to 2): the first enemy within 1.5m sets it off for {effect} + 1x primary stat damage and a 50% slow for 2s in 2.5m.",
                   [fx("slow", "area", 2, 0.5, 250, label="Move -50%")], {}, ["gunblade", "huntress", "ranger", "aetheri_artificer", "dwarf_miner"]),
    "taunting_tumble": ("Taunting Tumble", ["tank"], "active", "physical", "self", 0, 25, 18, 3, "s taunt", 0, 500, 8, "control", ["Crowd Control", "Defensive"], ["Roll", "Taunt", "Guard"],
                        "For 8s each roll taunts monsters within 5m for {effect}s and guards you (20% damage reduction) briefly.",
                        [fx("taunt", "area", 3, radius=500), fx("guard", "self", 1.5, 0.2, label="DEF +20%")], {}, BRUISER + ["bear", "ether_golem_tank", "totemic_behemoth", "dwarf_miner"]),
    "shield_tumble": ("Shield Tumble", ["heal", "tank"], "active", "holy", "self", 40, 0, 20, 5, "% max health heal", 0, 800, 10, "defensive", ["Defensive"], ["Roll", "Shield", "Heal", "Guard"],
                      "For 10s each roll grants the nearest ally within 8m a 40% guard for 3s and heals them for {effect}% of their max health + 1x your primary stat.",
                      [fx("guard", "target", 3, 0.4, label="DEF +40%")], {"effectCap": 10}, MOBILE_SUPPORT + ["paladin_righteous", "paladin_holy", "knight"]),
    "venom_tumble": ("Venom Tumble", ["dps"], "active", "poison", "self", 0, 25, 16, 30, "poison damage", 0, 180, 8, "control", ["Crowd Control", "Offensive"], ["Roll", "Trail", "Heal Cut", "Damage"],
                     "For 8s your rolls leave venom: enemies within 1.8m of the path take {effect} + 0.5x primary stat damage and receive 30% less healing for 5s.",
                     [fx("healCut", "area", 5, 0.3, 180, label="Healing -30%")], {}, ["ranger", "huntress", "troll_berserker_ranged", "dryad", "witch_slayer"]),
    "shadow_dance": ("Shadow Dance", ["dps"], "active", "shadow", "self", 0, 20, 30, 70, "% roll cooldown cut", 0, 0, 10, "defensive", ["Defensive"], ["Roll", "Cooldown", "Haste"],
                     "For 10s your dodge roll recovers {effect}% faster and refunds its energy.", [], {"effectCap": 85}, AGILE),
    "evasive_stance": ("Evasive Stance", ["dps", "tank"], "active", "physical", "self", 0, 20, 24, 3, "% max health per dodge", 0, 0, 6, "defensive", ["Defensive"], ["Roll", "I-Frame", "Heal"],
                       "For 6s rolls refund their energy and every hit you dodge during i-frames restores {effect}% max health + 0.4x primary stat.", [], {"effectCap": 8}, AGILE + BRUISER),
}


# Eric's rule (2026-09-25): every ability scales off the owner's primary stat (STR/AGI/INT). Damage, heals
# and shields of the roll skills = base effect + ratio x primary stat (CireRollSkills uses the same ratios).
ROLL_PRIMARY = {"riposte_roll": 1.0, "fleet_recovery": 0.5, "ember_wake": 0.6, "frost_wake": 0.4, "tumble_strike": 1.5,
                "mine_layer": 1.0, "shield_tumble": 1.0, "venom_tumble": 0.5, "evasive_stance": 0.4}


from UltimateUpgrades import ULTIMATE_UPGRADES, validate as validate_upgrades  # items-v2


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
    # new-champions: implemented signature-only kits.
    for sid, row in NEW_CHAMPION_SKILLS.items():
        name, roles, kind, school, targeting, cast, mana, energy, cd, effect, label, rng, radius, dur, desc, effects, extra = row
        curve = dict(CURVES[kind])
        curve.update(extra.get("curve", {}))
        rec = dict(id=sid, name=name, icon=f"/Game/UI/Abilities/T_{sid}", types=[TYPES[r] for r in roles], kind=kind, school=school,
                   targeting=targeting, castTime=cast, base=dict(effect=effect, manaCost=mana, energyCost=energy, cooldown=cd, castTime=cast,
                   range=rng, radius=radius, duration=dur), effectLabel=label, curve=curve, status="implemented", signatureOnly=True,
                   description=desc, effects=effects, champions=[], signatureOf=[])
        if extra.get("category"):
            rec["category"] = extra["category"]
        abilities[sid] = rec
    # Dodge-roll synergy skills.
    for sid, row in ROLL_SKILLS.items():
        name, roles, kind, school, targeting, mana, energy, cd, effect, label, rng, radius, dur, section, cats, tags, desc, effects, cx, _avail = row
        curve = dict(CURVES[kind]); curve.update(cx)
        abilities[sid] = dict(id=sid, name=name, icon=f"/Game/UI/Abilities/T_{sid}", types=[TYPES[r] for r in roles], kind=kind, school=school,
                              targeting=targeting, castTime=0, base=dict(effect=effect, manaCost=mana, energyCost=energy, cooldown=cd, castTime=0,
                              range=rng, radius=radius, duration=dur), effectLabel=label, curve=curve, status="implemented", signatureOnly=True,
                              section=section, categories=cats, effectTags=tags, description=desc, effects=effects, champions=[], signatureOf=[],
                              scaling=dict(stat="primary", ratio=ROLL_PRIMARY.get(sid, 0)), level15={})  # level15: filled by the level-15 bonus pass
        if section == "construct":
            abilities[sid]["category"] = "construct"  # the Skill Shop's Constructs tab (same flag as the Aetheri constructs)
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
        signature += [sid for sid, row in ROLL_SKILLS.items() if c["id"] in row[-1]]
        mask = sum(role_bits[r] for r in roles)
        pool = [sid for sid, a in abilities.items() if a["status"] == "implemented" and not a.get("signatureOnly") and sum(role_bits[t.lower()] for t in a["types"]) & mask]
        purchasable = list(dict.fromkeys(signature + pool))
        champions[c["id"]] = dict(name=c["displayName"], primaryRole=TYPES[prim], roles=[TYPES[r] for r in roles], signature=signature,
                                  purchasable=purchasable, purchasableImplemented=[s for s in purchasable if abilities[s]["status"] == "implemented"])
        for sid in purchasable:
            abilities[sid]["champions"].append(c["id"])
        for sid in signature:
            abilities[sid]["signatureOf"].append(c["id"])
    # items-v2: every ultimate carries the extra effect the Sigil of Apotheosis unlocks (Tools/UltimateUpgrades.py).
    for sid, upgrade in ULTIMATE_UPGRADES.items():
        if sid in abilities:
            abilities[sid]["ultimateUpgrade"] = upgrade
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
    errors += validate_upgrades(db["abilities"])  # items-v2
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
        if a["status"] != "implemented" or a.get("signatureOnly"):
            continue
        b = a["base"]
        l10, l50 = scale(b, a["curve"], 10), scale(b, a["curve"], 50)
        cost = (f"{b['manaCost']:.0f} MP" if b["manaCost"] else "") + (f"{b['energyCost']:.0f} EN" if b["energyCost"] else "") or "-"
        cc = ", ".join(e.get("label", e["type"]) for e in a["effects"])
        if "void" in a:
            v = a["void"]; cc += (", " if cc else "") + f"void: stun <= {v['innerRadius']}cm, slow <= {v['outerRadius']}cm"
        lines.append(f"| {a['name']} (`{sid}`) | {'/'.join(a['types'])} | {a['kind']} | {a['school']} | {a['targeting']} | {a['castTime']:g}s | {cost} | {b['cooldown']:g}s | "
                     f"{b['effect']:g} / {l10['effect']:.0f} / {l50['effect']:.0f} {a['effectLabel']} | {cc} |")
    lines += ["", "## Champion signature skills (implemented, signature-only)", "",
              "Native gameplay in `CireSignatureSkills` / `CireTechConstructs` (Docs/NewChampions.md). Only the champions listed can buy them;",
              "`construct` rows appear under the Skill Shop's CONSTRUCTS filter.", "",
              "| Skill | Champion | Type | Kind | School | Target | Cost | CD | Effect L1 / L10 / L50 | CC / notes |", "|---|---|---|---|---|---|---|---|---|---|"]
    for sid, a in sorted(db["abilities"].items(), key=lambda kv: (",".join(kv[1]["signatureOf"]), kv[1]["kind"], kv[0])):
        if not a.get("signatureOnly"):
            continue
        b = a["base"]
        l10, l50 = scale(b, a["curve"], 10), scale(b, a["curve"], 50)
        cost = (f"{b['manaCost']:.0f} MP" if b["manaCost"] else "") + (f"{b['energyCost']:.0f} EN" if b["energyCost"] else "") or "-"
        cc = ", ".join(e.get("label", e["type"]) for e in a["effects"]) + (" construct" if a.get("category") == "construct" else "")
        lines.append(f"| {a['name']} (`{sid}`) | {', '.join(a['signatureOf'])} | {'/'.join(a['types'])} | {a['kind']} | {a['school']} | {a['targeting']} | {cost} | {b['cooldown']:g}s | "
                     f"{b['effect']:g} / {l10['effect']:.0f} / {l50['effect']:.0f} {a['effectLabel']} | {cc} |")
    lines += ["", "## Planned signature skills", "", "| Skill | Champion | Type | Kind | School | Target | Notes |", "|---|---|---|---|---|---|---|"]
    for sid, a in sorted(db["abilities"].items()):
        if a["status"] == "planned":
            extra = ", ".join(e.get("label", e["type"]) for e in a["effects"]) + (" void zones" if "void" in a else "")
            lines.append(f"| {a['name']} (`{sid}`) | {', '.join(a['signatureOf'])} | {'/'.join(a['types'])} | {a['kind']} | {a['school']} | {a['targeting']} | {extra} |")
    lines += ["", "## Ultimate upgrades (Sigil of Apotheosis)", "",
              "Carrying the path-defining unique **Sigil of Apotheosis** adds one extra effect to your ultimate (numbers unchanged).",
              "Source: `Tools/UltimateUpgrades.py`; runtime: `CireUltimateUpgrades.cpp`; items: Docs/Items.md.", "",
              "| Ultimate | Upgrade | Added effect |", "|---|---|---|"]
    for sid, a in sorted(db["abilities"].items(), key=lambda kv: (kv[1]["status"], kv[0])):
        if a.get("ultimateUpgrade"):
            u = a["ultimateUpgrade"]
            lines.append(f"| {a['name']} (`{sid}`{', planned' if a['status'] == 'planned' else ''}) | {u['name']} | {u['text']} |")
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
