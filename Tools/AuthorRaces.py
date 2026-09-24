"""Author the monster race bible: Content/Data/Races.json and Docs/Races.md from one source.

monster-races: nine monster races, each with six unit types (slots line, bruiser, tank, caster, ranged,
special) and two bosses (warlord, colossus). Every unit is a full NPC archetype (same schema as
NPCArchetypes.json) plus race data: slot, Tripo art prompt ("look"), fallback body, and a themed skill pool.
The runtime (Source/CiresTeamSurvival/CireRaces.cpp) draws a seeded subset of each pool per match and
unlocks skills by wave (Waves.json "skillProgression").

Also keeps three shared tables in step (text insertion, existing formatting untouched):
  * AudioFootsteps.json "monsters": a footstep class for every race unit (the audio smoke requires one),
  * AudioCues.json "cues": the npc.<theme>.* cues the race abilities and visuals name,
  * BuffVisuals.json "buffs": the npc_* themed visuals applied by race abilities.

Usage: python Tools/AuthorRaces.py [--check]   (--check fails if the committed files are stale)
"""
from __future__ import annotations

import argparse
import copy
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "Content/Data"

# --------------------------------------------------------------------------------------------- ranks
RANKS = {
    "normal":   {"label": "Normal",   "color": [0.82, 0.80, 0.74], "health": 1.00, "damage": 1.00, "size": 1.00, "skillBonus": 0,
                 "armorTint": 0.00, "bodyTint": 0.00, "glow": 0.00, "rim": 0.00, "classification": "normal",
                 "note": "Race base colours only: the plain soldier of the race."},
    "veteran":  {"label": "Veteran",  "color": [0.12, 1.00, 0.10], "health": 1.30, "damage": 1.10, "size": 1.05, "skillBonus": 0,
                 "armorTint": 0.70, "bodyTint": 0.10, "glow": 0.9, "rim": 0.55, "classification": "normal",
                 "note": "WoW uncommon green: armour trim and a green rim."},
    "elite":    {"label": "Elite",    "color": [0.05, 0.45, 1.00], "health": 1.60, "damage": 1.25, "size": 1.10, "skillBonus": 1,
                 "armorTint": 0.80, "bodyTint": 0.14, "glow": 1.3, "rim": 0.80, "classification": "elite",
                 "note": "WoW rare blue: blue armour, a blue glowing rim, one extra skill."},
    "champion": {"label": "Champion", "color": [0.70, 0.22, 1.00], "health": 2.20, "damage": 1.40, "size": 1.18, "skillBonus": 1,
                 "armorTint": 0.90, "bodyTint": 0.18, "glow": 1.8, "rim": 1.00, "classification": "elite",
                 "note": "WoW epic purple: purple armour and trim glow, larger, one extra skill."},
    "warlord":  {"label": "Warlord",  "color": [1.00, 0.50, 0.02], "health": 1.00, "damage": 1.00, "size": 1.00, "skillBonus": 2,
                 "armorTint": 0.90, "bodyTint": 0.16, "glow": 2.2, "rim": 1.15, "classification": "boss",
                 "note": "WoW legendary orange: every boss. Stats come from the boss archetype; two extra skills."},
    "mythic":   {"label": "Mythic",   "color": [1.00, 0.12, 0.08], "trim": [1.0, 0.78, 0.25], "health": 1.50, "damage": 1.30, "size": 1.12,
                 "skillBonus": 3, "armorTint": 0.90, "bodyTint": 0.22, "glow": 2.8, "rim": 1.40, "classification": "boss",
                 "note": "Red body glow with gold trim: late-cycle bosses and hand-placed terrors; three extra skills."},
}

# --------------------------------------------------------------------------------- ability builders
def _ab(id, name, desc, type, **kw):
    a = {"id": id, "name": name, "description": desc, "type": type}
    a.update(kw)
    return a

def melee(name, desc, rng=170):
    return _ab("npc_melee", name, desc, "melee", basic=True, range=rng)

def bolt(name, desc, skillshot="npc_shadow_bolt", cast=1.4, interruptible=True, rng=650, id="npc_bolt"):
    return _ab(id, name, desc, "projectile", basic=True, interruptible=interruptible, skillshot=skillshot, castTime=cast, range=rng)

def shot(name, desc, cast=0.6, rng=650, id="npc_shot"):
    return _ab(id, name, desc, "projectile", basic=True, skillshot="npc_barbed_shot", castTime=cast, range=rng)

# Every pool skill names a themed buff visual (BuffVisuals.json) and an audio cue (AudioCues.json).
def cone(id, name, desc, mult, color, cd=9, cast=1.1, radius=360, angle=80, buff="", cue="", **kw):
    return _ab(id, name, desc, "cone", cooldown=cd, castTime=cast, range=radius, radius=radius, angle=angle,
               damageMultiplier=mult, color=color, buff=buff, cue=cue, **kw)

def circle(id, name, desc, mult, color, cd=11, cast=1.2, radius=220, rng=650, buff="", cue="", **kw):
    return _ab(id, name, desc, "targetCircle", cooldown=cd, castTime=cast, range=rng, radius=radius,
               damageMultiplier=mult, color=color, buff=buff, cue=cue, **kw)

def pool(id, name, desc, dps, duration, color, cd=12, cast=1.2, radius=220, rng=650, buff="", cue="", **kw):
    return _ab(id, name, desc, "targetCircle", cooldown=cd, castTime=cast, range=rng, radius=radius,
               damagePerSecond=dps, duration=duration, color=color, buff=buff, cue=cue, **kw)

def ring(id, name, desc, mult, color, cd=10, cast=1.2, radius=380, buff="", cue="", **kw):
    return _ab(id, name, desc, "selfCircle", cooldown=cd, castTime=cast, range=radius, radius=radius,
               damageMultiplier=mult, color=color, buff=buff, cue=cue, **kw)

def charge(id, name, desc, mult, color, cd=13, cast=0.9, length=900, width=170, rng=900, minr=320, buff="", cue="", **kw):
    return _ab(id, name, desc, "charge", cooldown=cd, castTime=cast, minRange=minr, range=rng, length=length, width=width,
               damageMultiplier=mult, color=color, buff=buff, cue=cue, **kw)

def pull(id, name, desc, mult, color, cd=14, cast=1.0, length=900, width=160, rng=900, minr=300, buff="", cue="", **kw):
    return _ab(id, name, desc, "pull", cooldown=cd, castTime=cast, minRange=minr, range=rng, length=length, width=width,
               damageMultiplier=mult, color=color, buff=buff, cue=cue, **kw)

def summon(id, name, desc, unit, count, color, cd=24, cast=1.6, buff="", cue="", **kw):
    return _ab(id, name, desc, "summon", cooldown=cd, castTime=cast, range=5000, radius=450, summon=unit, count=count,
               color=color, interruptible=True, buff=buff, cue=cue, **kw)

def heal(id, name, desc, frac=0.2, cd=12, cast=2.0, threshold=0.6, buff="", cue="", **kw):
    return _ab(id, name, desc, "healAlly", interruptible=True, cooldown=cd, castTime=cast, range=900, radius=900,
               magnitude=frac, healthThreshold=threshold, buff=buff, cue=cue, **kw)

def rally(id, name, desc, mag=0.25, dur=8, cd=20, cast=1.2, radius=1000, color=(1.0, 0.75, 0.2, 0.25), buff="", cue="", **kw):
    return _ab(id, name, desc, "rally", cooldown=cd, castTime=cast, range=5000, radius=radius, duration=dur, magnitude=mag,
               color=list(color), buff=buff, cue=cue, **kw)

def provoke(id, name, desc, cd=16, buff="", cue="", **kw):
    return _ab(id, name, desc, "provoke", cooldown=cd, castTime=0.5, range=550, radius=550, duration=6, magnitude=0.35,
               color=[0.35, 0.55, 1.0, 0.3], buff=buff, cue=cue, **kw)

def guard(id, name, desc, cd=14, buff="", cue="", **kw):
    return _ab(id, name, desc, "guard", cooldown=cd, castTime=0.4, range=700, radius=700, duration=8, magnitude=0.4, buff=buff, cue=cue, **kw)

def wall(id, name, desc, cd=30, threshold=0.5, mag=0.5, buff="", cue="", **kw):
    return _ab(id, name, desc, "shieldWall", cooldown=cd, castTime=0.5, range=5000, duration=6, magnitude=mag,
               healthThreshold=threshold, buff=buff, cue=cue, **kw)

def enrage(id, name, desc, mag=0.4, threshold=0.3, buff="", cue="", **kw):
    return _ab(id, name, desc, "enrage", cooldown=0, castTime=1.0, range=5000, magnitude=mag, healthThreshold=threshold,
               buff=buff, cue=cue, **kw)

def disengage(id, name, desc, cd=10, length=600, buff="", cue="", **kw):
    return _ab(id, name, desc, "disengage", cooldown=cd, castTime=0, range=260, length=length, buff=buff, cue=cue, **kw)

# ------------------------------------------------------------------------------------ role templates
FALLBACK = {"line": "hollow_infantry", "bruiser": "ironbound_bruiser", "tank": "hollow_shieldbearer", "caster": "blight_caster",
            "ranged": "barbed_hunter", "special_melee": "hollow_infantry", "special_ranged": "blight_caster",
            "warlord": "gravemaw_pack_leader", "colossus": "hollow_siegebreaker", "warlord_caster": "blight_caster"}
PROPS = {
    "hollow_infantry": [{"asset": "/Game/Art/Weapons/ArmoryPrototype01/SM_Dagger.SM_Dagger", "bone": "hand_r", "scale": 1.0}],
    "ironbound_bruiser": [{"asset": "/Game/Art/Weapons/ArmoryPrototype01/SM_WarAxe.SM_WarAxe", "bone": "hand_r", "scale": 1.0}],
    "hollow_shieldbearer": [{"asset": "/Game/Art/Weapons/CombatPrototype01/SM_PrototypeShield.SM_PrototypeShield", "bone": "hand_l", "scale": 1.0},
                            {"asset": "/Game/Art/Weapons/CombatPrototype01/SM_PrototypeSword.SM_PrototypeSword", "bone": "hand_r", "scale": 1.0}],
    "blight_caster": [{"asset": "/Game/Art/Weapons/ArmoryPrototype01/SM_RiftStaff.SM_RiftStaff", "bone": "hand_r", "scale": 1.0}],
    "barbed_hunter": [{"asset": "/Game/Art/Weapons/CombatPrototype01/SM_PrototypeBow.SM_PrototypeBow", "bone": "hand_l", "scale": 1.0}],
    "gravemaw_pack_leader": [{"asset": "/Game/Art/Weapons/ArmoryPrototype01/SM_WarAxe.SM_WarAxe", "bone": "hand_r", "scale": 1.25},
                             {"asset": "/Game/Art/Weapons/ArmoryPrototype01/SM_Totem.SM_Totem", "bone": "spine_03", "x": -18, "z": 10, "scale": 0.8}],
    "hollow_siegebreaker": [{"asset": "/Game/Art/Weapons/ArmoryPrototype01/SM_WarHammer.SM_WarHammer", "bone": "hand_r", "scale": 1.2}],
}
STATS = {
    "line":    {"role": "bruiser", "tuningKind": "basic",   "moveSpeed": 210, "attackRange": 170, "attackInterval": 1.8, "scale": 1.0},
    "bruiser": {"role": "bruiser", "tuningKind": "bruiser", "moveSpeed": 185, "attackRange": 170, "attackInterval": 1.8, "scale": 1.1},
    "tank":    {"role": "tank", "healthMultiplier": 2.2, "damage": 12, "eliteDamageMultiplier": 0.7, "moveSpeed": 180, "armor": 0.25,
                "attackRange": 170, "attackInterval": 2.0, "scale": 1.12},
    "caster":  {"role": "caster", "tuningKind": "caster", "moveSpeed": 195, "attackRange": 650, "preferredRange": 560, "kiteRange": 320,
                "attackInterval": 2.2, "scale": 1.0},
    "ranged":  {"role": "ranged", "tuningKind": "ranged", "moveSpeed": 205, "attackRange": 650, "preferredRange": 520, "kiteRange": 300,
                "attackInterval": 2.2, "scale": 1.0},
    "support": {"role": "support", "tuningKind": "caster", "moveSpeed": 195, "attackRange": 650, "preferredRange": 560, "kiteRange": 320,
                "attackInterval": 2.4, "scale": 0.95, "healthScale": 0.9},
    "swarm":   {"role": "swarm", "tuningKind": "basic", "moveSpeed": 250, "attackRange": 150, "attackInterval": 1.4, "scale": 0.8,
                "healthScale": 0.55, "damageScale": 0.65},
    "skirmisher": {"role": "bruiser", "tuningKind": "basic", "moveSpeed": 250, "attackRange": 160, "attackInterval": 1.5, "scale": 0.95,
                   "healthScale": 0.85},
    "warlord": {"role": "bruiser", "classification": "boss", "tuningKind": "bruiser", "damage": 38, "moveSpeed": 200, "armor": 0.1,
                "attackRange": 210, "attackInterval": 2.0, "scale": 1.6, "leakCost": 10, "healthScale": 1.0},
    "warlord_caster": {"role": "caster", "classification": "boss", "tuningKind": "caster", "damage": 34, "moveSpeed": 170, "armor": 0.1,
                       "attackRange": 700, "preferredRange": 600, "kiteRange": 300, "attackInterval": 2.2, "scale": 1.45, "leakCost": 10,
                       "healthScale": 1.85},
    "colossus": {"role": "tank", "classification": "boss", "tuningKind": "bruiser", "damage": 40, "moveSpeed": 150, "armor": 0.2,
                 "attackRange": 200, "attackInterval": 2.2, "scale": 1.5, "leakCost": 10, "healthScale": 1.05},
}

# Themed buff visuals and audio cues (id -> row). Kept small: abilities share them by theme.
def vis(name, kind, school, primary, secondary, core, layers, cue, source):
    return {"name": name, "kind": kind, "school": school, "source": source,
            "palette": {"primary": primary, "secondary": secondary, "core": core}, "priority": 58,
            "layers": layers, "lifecycle": {"burstSeconds": 0.4, "fadeSeconds": 0.35}, "sound": {"start": cue}}

VISUALS = {
    "npc_rooted": vis("Rooted", "debuff", "nature", [0.45, 0.72, 0.2], [0.3, 0.2, 0.1], [0.85, 1.0, 0.6],
                      [{"shape": "hands", "attach": "ground", "size": 0.9, "speed": 0.6}, {"shape": "ring", "attach": "ground", "style": "runes", "count": 6, "speed": 0.3}],
                      "npc.root.start", "monster races: root rider (cannot move)"),
    "npc_silenced": vis("Silenced", "debuff", "arcane", [0.72, 0.4, 1.0], [0.3, 0.1, 0.5], [0.95, 0.85, 1.0],
                        [{"shape": "glyph", "attach": "overhead", "style": "eye", "size": 0.9}, {"shape": "swirl", "attach": "body", "speed": 0.8}],
                        "npc.silence.start", "monster races: silence rider (cannot cast skills)"),
    "npc_tide": vis("Drenched", "debuff", "tide", [0.2, 0.85, 0.85], [0.05, 0.3, 0.4], [0.8, 1.0, 1.0],
                    [{"shape": "drips", "attach": "body", "count": 10, "speed": 1.0}, {"shape": "ripple", "attach": "ground", "style": "shock", "speed": 0.8}],
                    "npc.tide.start", "Drowned Deep tide and wave skills"),
    "npc_ink": vis("Inked", "debuff", "abyss", [0.25, 0.1, 0.35], [0.05, 0.02, 0.08], [0.6, 0.4, 0.8],
                   [{"shape": "drips", "attach": "body", "count": 12, "speed": 0.7}, {"shape": "pool", "attach": "ground", "speed": 0.4}],
                   "npc.ink.start", "Drowned Deep ink skills"),
    "npc_mind": vis("Mind-Flayed", "debuff", "arcane", [0.8, 0.35, 1.0], [0.35, 0.1, 0.55], [1.0, 0.85, 1.0],
                    [{"shape": "swirl", "attach": "overhead", "speed": 1.1}, {"shape": "glyph", "attach": "overhead", "style": "eye", "size": 0.8}],
                    "npc.mind.start", "Drowned Deep psychic skills"),
    "npc_thorns": vis("Thorned", "debuff", "nature", [0.55, 0.7, 0.2], [0.35, 0.22, 0.08], [0.9, 1.0, 0.55],
                      [{"shape": "crystals", "attach": "body", "count": 8, "speed": 0.6}],
                      "npc.thorns.start", "Blightwood thorn and briar skills"),
    "npc_spores": vis("Spore-Choked", "debuff", "venom", [0.8, 0.85, 0.3], [0.4, 0.5, 0.1], [1.0, 1.0, 0.7],
                      [{"shape": "motes", "attach": "body", "style": "glint", "count": 14, "speed": 0.5}, {"shape": "pool", "attach": "ground", "speed": 0.3}],
                      "npc.spores.start", "Blightwood spore and rot skills"),
    "npc_bloodlust": vis("Bloodlust", "buff", "war", [1.0, 0.25, 0.1], [0.55, 0.05, 0.03], [1.0, 0.75, 0.5],
                         [{"shape": "flames", "attach": "body", "count": 8, "speed": 1.3}, {"shape": "glyph", "attach": "overhead", "style": "horn"}],
                         "npc.warcry.start", "Ironhide war cries, rallies and frenzies"),
    "npc_sundered": vis("Sundered", "debuff", "steel", [0.85, 0.35, 0.2], [0.4, 0.1, 0.05], [1.0, 0.8, 0.6],
                        [{"shape": "cracks", "attach": "ground", "speed": 0.8}],
                        "npc.cleave.start", "heavy cleaves, slams and trample hits"),
    "npc_dragonfire": vis("Dragonfire", "debuff", "fire", [1.0, 0.5, 0.1], [0.7, 0.15, 0.02], [1.0, 0.9, 0.5],
                          [{"shape": "flames", "attach": "body", "count": 10, "speed": 1.2}],
                          "npc.fire.start", "Drakkari fire breath, pillars and embers"),
    "npc_scaleward": vis("Scale Ward", "buff", "fire", [0.95, 0.65, 0.2], [0.55, 0.3, 0.05], [1.0, 0.92, 0.6],
                         [{"shape": "plates", "attach": "body", "count": 6, "speed": 0.7}],
                         "npc.ward.start", "Drakkari and Stoneborn defensive wards"),
    "npc_runic": vis("Runebound", "debuff", "arcane", [0.3, 0.6, 1.0], [0.1, 0.25, 0.6], [0.85, 0.95, 1.0],
                     [{"shape": "glyph", "attach": "overhead", "style": "shield", "size": 0.85}, {"shape": "ring", "attach": "ground", "style": "runes", "count": 8, "speed": 0.6}],
                     "npc.rune.start", "Stoneborn runes and ether"),
    "npc_feral": vis("Mauled", "debuff", "blood", [0.85, 0.2, 0.12], [0.45, 0.08, 0.05], [1.0, 0.7, 0.5],
                     [{"shape": "drips", "attach": "body", "count": 8, "speed": 1.2}],
                     "npc.feral.start", "Feral Kin bites, mauls and gores"),
    "npc_profane": vis("Profaned", "debuff", "holy", [0.9, 0.82, 0.3], [0.45, 0.35, 0.05], [1.0, 1.0, 0.7],
                       [{"shape": "halo", "attach": "overhead", "count": 12, "speed": 0.8}, {"shape": "chains", "attach": "body", "count": 10, "speed": 0.5}],
                       "npc.profane.start", "Fallen Order corrupted light and chains"),
    "npc_void": vis("Void-Touched", "debuff", "void", [0.65, 0.25, 1.0], [0.15, 0.05, 0.3], [0.95, 0.8, 1.0],
                    [{"shape": "swirl", "attach": "body", "speed": 1.4}, {"shape": "tether", "attach": "link", "style": "beam", "burstOnly": True}],
                    "npc.void.start", "Voidborn rifts and gravity"),
}
CUES = {
    "npc.root.start":    {"sounds": ["Auras/AUR_Snarl"], "volume": 0.55, "pitch": [0.8, 0.9], "cooldown": 0.2},
    "npc.silence.start": {"sounds": ["Auras/AUR_Gong"], "volume": 0.45, "pitch": [1.3, 1.4], "cooldown": 0.3},
    "npc.tide.start":    {"sounds": ["Auras/AUR_Bubble"], "volume": 0.6, "pitch": [0.7, 0.8], "cooldown": 0.2},
    "npc.tide.cast":     {"sounds": ["Auras/AUR_Bubble"], "volume": 0.55, "pitch": [0.55, 0.62], "cooldown": 0.4},
    "npc.ink.start":     {"sounds": ["Auras/AUR_BloodSplat_{01..03}"], "volume": 0.5, "pitch": [0.6, 0.7], "cooldown": 0.2},
    "npc.mind.start":    {"sounds": ["Auras/AUR_Heartbeat"], "volume": 0.55, "pitch": [0.85, 0.95], "cooldown": 0.3},
    "npc.mind.cast":     {"sounds": ["Auras/AUR_Choir"], "volume": 0.45, "pitch": [0.55, 0.62], "cooldown": 0.5},
    "npc.thorns.start":  {"sounds": ["Auras/AUR_IceCrack"], "volume": 0.5, "pitch": [0.7, 0.8], "cooldown": 0.2},
    "npc.spores.start":  {"sounds": ["Auras/AUR_Whoosh"], "volume": 0.45, "pitch": [0.6, 0.7], "cooldown": 0.2},
    "npc.grove.cast":    {"sounds": ["Ambience/AMB_Chop_01"], "volume": 0.5, "pitch": [0.6, 0.75], "cooldown": 0.4},
    "npc.warcry.start":  {"sounds": ["Auras/AUR_WarCry"], "volume": 0.7, "pitch": [0.75, 0.85], "cooldown": 0.4},
    "npc.cleave.start":  {"sounds": ["Auras/AUR_Clang_{01..03}"], "volume": 0.55, "pitch": [0.7, 0.8], "cooldown": 0.15},
    "npc.fire.start":    {"sounds": ["Auras/AUR_FlameBurst"], "volume": 0.6, "pitch": [0.85, 0.95], "cooldown": 0.2},
    "npc.fire.cast":     {"sounds": ["Auras/AUR_FlameBurst"], "volume": 0.5, "pitch": [0.68, 0.75], "cooldown": 0.5},
    "npc.ward.start":    {"sounds": ["Auras/AUR_ForceField"], "volume": 0.5, "pitch": [0.8, 0.9], "cooldown": 0.3},
    "npc.rune.start":    {"sounds": ["Auras/AUR_Chime"], "volume": 0.5, "pitch": [0.7, 0.8], "cooldown": 0.2},
    "npc.feral.start":   {"sounds": ["Auras/AUR_Snarl"], "volume": 0.6, "pitch": [0.9, 1.05], "cooldown": 0.2},
    "npc.roar.cast":     {"sounds": ["SFX/SFX_PackLeaderRoar"], "volume": 0.8, "pitch": [0.9, 1.1], "cooldown": 1.0},
    "npc.profane.start": {"sounds": ["Auras/AUR_Choir"], "volume": 0.5, "pitch": [0.7, 0.78], "cooldown": 0.3},
    "npc.void.start":    {"sounds": ["Auras/AUR_Hourglass"], "volume": 0.5, "pitch": [0.6, 0.7], "cooldown": 0.2},
    "npc.void.cast":     {"sounds": ["Auras/AUR_ForceField"], "volume": 0.5, "pitch": [0.5, 0.58], "cooldown": 0.5},
    "npc.summon.cast":   {"sounds": ["SFX/SFX_WarHornDistant"], "volume": 0.6, "pitch": [0.7, 0.8], "cooldown": 1.0},
    "npc.heal.cast":     {"sounds": ["CireCombat/S_Heal"], "volume": 0.45, "pitch": [0.7, 0.8], "cooldown": 0.3},
    "amb.race.drowned":  {"sounds": ["Auras/AUR_BubblesLoop"], "volume": 0.3, "pitch": [0.6, 0.7], "loop": True},
    "amb.race.grove":    {"sounds": ["Ambience/AMB_Crow_{01..05}"], "volume": 0.35, "pitch": [0.8, 0.9]},
    "amb.race.war":      {"sounds": ["SFX/SFX_WarDrums"], "volume": 0.35, "pitch": [0.9, 1.0]},
    "amb.race.hollow":   {"sounds": ["Ambience/AMB_Chains_{01..04}"], "volume": 0.35, "pitch": [0.8, 0.9]},
    "voice.race.growl":  {"sounds": ["SFX/SFX_PackLeaderGrowl"], "volume": 0.6, "pitch": [0.8, 1.1], "cooldown": 3.0},
}

# ------------------------------------------------------------------------------------------ colours
def C(r, g, b, a=0.35):
    return [r, g, b, a]

TEAL, VIOLET, INK, CORAL = C(0.15, 0.75, 0.8), C(0.6, 0.25, 0.9), C(0.2, 0.08, 0.3, 0.45), C(0.9, 0.45, 0.35)
MOSS, THORN, SPORE, ROT = C(0.35, 0.65, 0.15), C(0.5, 0.55, 0.12), C(0.8, 0.85, 0.25), C(0.35, 0.45, 0.08)
BLOOD, IRON, EMBER, GOLD = C(0.85, 0.12, 0.05, 0.4), C(0.55, 0.5, 0.45), C(1.0, 0.45, 0.08, 0.4), C(1.0, 0.75, 0.2, 0.3)
RUNE, STONE, FERAL, PROFANE, VOID = C(0.3, 0.55, 1.0), C(0.55, 0.5, 0.42), C(0.8, 0.4, 0.15), C(0.9, 0.8, 0.3), C(0.55, 0.2, 0.95, 0.4)

# ============================================================================================== RACES
RACES: list[dict] = []

# ---------------------------------------------------------------------------------------- DROWNED DEEP
RACES.append({
    "id": "drowned_deep", "name": "The Drowned Deep", "short": "Drowned",
    "lore": "Squid-faced heralds of a sleeping god, dragged from the trench by the Breach's tide. They drown the land to make it theirs.",
    "origin": {"playerRaces": ["human"], "profiles": ["scholar", "summoner"],
               "note": "No champion is drowned-born: the Deep answers the Veil Scholar's forbidden tide-lore and the Rift Summoner's pacts. Its counterpart is humanity's drowned coast."},
    "palette": {"base": [0.10, 0.36, 0.40], "accent": [0.20, 0.90, 0.85], "secondary": [0.35, 0.15, 0.45], "glow": [0.30, 1.00, 0.90],
                "variants": [{"name": "Abyssal", "base": [0.10, 0.36, 0.40], "accent": [0.20, 0.90, 0.85]},
                             {"name": "Bleached Coral", "base": [0.72, 0.62, 0.55], "accent": [0.95, 0.45, 0.40]},
                             {"name": "Crimson Tide", "base": [0.45, 0.07, 0.12], "accent": [1.00, 0.30, 0.30]}]},
    "footsteps": "cloth", "footstepPitch": 0.82, "ambienceCue": "amb.race.drowned", "voiceCue": "voice.race.growl",
    "units": [
        {"id": "abyssal_stalker", "slot": "line", "tmpl": "skirmisher", "name": "Abyssal Stalker", "fallback": "hollow_infantry",
         "footsteps": "leather",
         "look": "Lean hunched cephalopod humanoid assassin, squid head with a beard of short writhing tentacles, huge black eyes, translucent blue-grey skin with glowing cyan spots, long arms ending in hooked bone claws, tattered kelp sash, barefoot webbed feet, low predatory crouch.",
         "basic": melee("Hooked Claws", "Two quick raking claws at its current target.", 160),
         "pool": [charge("drowned_stalker_lunge", "Riptide Lunge", "Marks a line and darts along it, raking everyone in the path.", 1.6, TEAL, cd=11, cast=0.7, length=750, buff="npc_tide", cue="npc.tide.start"),
                  cone("drowned_rend", "Riptide Rend", "A frontal rake that drenches and slows everyone in the cone for 3s.", 1.8, TEAL, cd=9, cast=0.9, radius=320, angle=90, slow=3, buff="npc_tide", cue="npc.tide.start"),
                  pull("drowned_undertow", "Undertow Grab", "Marks a line to a distant champion, then hooks and drags them back to the Stalker.", 1.0, INK, cd=16, cast=1.0, length=850, buff="npc_ink", cue="npc.ink.start"),
                  disengage("drowned_ink_veil", "Ink Veil", "Squirts ink and leaps away when a champion closes in.", cd=12, buff="npc_ink", cue="npc.ink.start")],
         "poolDraw": 3},
        {"id": "deepspawn_thrall", "slot": "bruiser", "tmpl": "bruiser", "name": "Deepspawn Thrall", "fallback": "ironbound_bruiser",
         "look": "Hulking barnacle-crusted squid-headed brute, one arm a massive crab claw, the other a club of fused coral, thick tentacle beard, slick dark-teal hide with pale belly, rusted anchor chain wrapped around its torso, heavy stooped stance.",
         "basic": melee("Coral Club", "A heavy coral-studded swing at its current target."),
         "pool": [cone("drowned_tidal_slam", "Tidal Slam", "Winds up a wave-crash slam that knocks champions in the cone back.", 2.3, TEAL, cd=9, cast=1.1, radius=360, angle=85, knockback=450, buff="npc_tide", cue="npc.tide.start"),
                  charge("drowned_barnacle_charge", "Barnacle Charge", "Marks a line and barrels through it, hitting everyone in the line.", 2.0, CORAL, cd=14, buff="npc_sundered", cue="npc.cleave.start"),
                  ring("drowned_undertow_stomp", "Crushing Undertow", "Marks a ring around itself; the undertow slows everyone inside for 3s.", 1.5, TEAL, cd=12, radius=360, slow=3, buff="npc_tide", cue="npc.tide.start"),
                  enrage("drowned_brine_frenzy", "Brine Frenzy", "At 35% health it frenzies: +35% damage and faster attacks.", mag=0.35, threshold=0.35, buff="npc_bloodlust", cue="npc.roar.cast")],
         "poolDraw": 3},
        {"id": "coralshell_guardian", "slot": "tank", "tmpl": "tank", "name": "Coralshell Guardian", "fallback": "hollow_shieldbearer",
         "footsteps": "plate",
         "look": "Squat armoured cephalopod warrior in a carapace of layered pink-white coral plates, mantis-shrimp shell pauldrons, a huge round shield grown from brain coral, short trident, squid face peering from a coral helm, stocky immovable stance.",
         "basic": melee("Coral Bash", "A shield bash at its current target."),
         "pool": [provoke("drowned_abyssal_roar", "Abyssal Roar", "Taunts nearby champions: for 6s they deal 35% less damage to anything but the Guardian.", buff="npc_mind", cue="npc.mind.start"),
                  wall("drowned_coral_bulwark", "Coral Bulwark", "Below 50% health it closes its shell: 50% less damage for 6s.", buff="npc_scaleward", cue="npc.ward.start"),
                  guard("drowned_tidewall_oath", "Tidewall Oath", "Guards the most injured nearby ally for 8s, taking 40% of its damage.", buff="npc_tide", cue="npc.ward.start"),
                  cone("drowned_shell_bash", "Shell Crash", "A short shield-crash cone that knocks champions back.", 1.6, CORAL, cd=10, cast=0.9, radius=280, angle=70, knockback=380, buff="npc_sundered", cue="npc.cleave.start")],
         "poolDraw": 3},
        {"id": "tidecaller", "slot": "caster", "tmpl": "caster", "name": "Tidecaller", "fallback": "blight_caster",
         "look": "Tall robed cephalopod priest, flowing sea-green robes crusted with shells, long drooping tentacle beard glowing at the tips, a conch-and-driftwood staff with a swirling water orb, pearl necklaces, hovering droplets of water around its hands.",
         "basic": bolt("Tide Bolt", "Interruptible cast. Hurls a bolt of seawater in a fixed direction."),
         "pool": [pool("drowned_drowning_pool", "Drowning Pool", "Marks the ground under its target, then floods it with a choking pool for 5s.", 13, 5, TEAL, cd=12, buff="npc_tide", cue="npc.tide.cast"),
                  circle("drowned_whirlpool", "Whirlpool", "Marks a circle; the whirlpool roots everyone inside for 2s.", 1.2, TEAL, cd=14, cast=1.3, radius=240, root=2.0, buff="npc_rooted", cue="npc.tide.cast"),
                  heal("drowned_brine_mending", "Brine Mending", "Interruptible 2s cast that restores 20% health to the most injured ally below 60%.", buff="npc_tide", cue="npc.heal.cast"),
                  cone("drowned_crashing_wave", "Crashing Wave", "A wave rolls out in a wide cone, knocking champions back.", 1.6, TEAL, cd=11, cast=1.2, radius=520, angle=70, knockback=500, buff="npc_tide", cue="npc.tide.start"),
                  circle("drowned_riptide", "Riptide", "Marks a circle; the riptide slows everyone inside for 4s.", 1.3, TEAL, cd=10, cast=1.0, radius=260, slow=4, buff="npc_tide", cue="npc.tide.start")],
         "poolDraw": 3},
        {"id": "barbspitter", "slot": "ranged", "tmpl": "ranged", "name": "Barbspitter", "fallback": "barbed_hunter",
         "footsteps": "leather",
         "look": "Frog-postured cephalopod with a bulbous siphon mouth, quills and barbed spines along its back, mottled olive and violet skin, glowing yellow slit eyes, harpoon of whalebone slung on its back, crouched spitting pose.",
         "basic": shot("Barbed Spine", "Short aim, then spits a barbed spine in a fixed direction."),
         "pool": [pool("drowned_ink_spit", "Ink Spit", "Spits a blinding ink puddle at its target: poison for 4s.", 12, 4, INK, cd=11, cast=1.0, radius=200, buff="npc_ink", cue="npc.ink.start"),
                  circle("drowned_spine_volley", "Spine Volley", "Marks a circle on its target, then a volley of spines lands there.", 1.8, CORAL, cd=10, cast=1.0, radius=200, buff="npc_sundered", cue="npc.cleave.start"),
                  disengage("drowned_jet_retreat", "Jet Retreat", "Jets away from melee on a spray of water.", cd=10, buff="npc_tide", cue="npc.tide.start"),
                  pull("drowned_harpoon_spine", "Harpoon Spine", "Marks a line and fires a tethered harpoon that drags the champion to it.", 1.2, CORAL, cd=16, cast=1.1, length=950, width=140, rng=950, buff="npc_ink", cue="npc.ink.start")],
         "poolDraw": 3},
        {"id": "mind_leech", "slot": "special", "tmpl": "support", "name": "Mind Leech", "fallback": "blight_caster",
         "look": "Small floating cephalopod with an oversized exposed brain-like mantle, pulsing violet veins, trailing thin tentacles instead of legs, a single cluster of glowing magenta eyes, faint psychic haze around its head.",
         "basic": bolt("Psychic Lash", "Interruptible cast. A lash of psychic force in a fixed direction.", cast=1.3),
         "pool": [ring("drowned_mind_scream", "Mind Scream", "Marks a ring around itself; champions inside are silenced for 2.5s.", 0.9, VIOLET, cd=16, cast=1.3, radius=380, silence=2.5, buff="npc_silenced", cue="npc.mind.cast"),
                  circle("drowned_dread_whisper", "Dread Whisper", "Marks a circle on its target; champions inside are silenced for 2s.", 0.8, VIOLET, cd=14, cast=1.2, radius=220, silence=2.0, buff="npc_silenced", cue="npc.mind.cast"),
                  heal("drowned_leech_mending", "Leech Mending", "Interruptible cast: siphons vitality into the most injured ally (18% health).", frac=0.18, buff="npc_mind", cue="npc.heal.cast"),
                  guard("drowned_abyssal_ward", "Abyssal Ward", "Wraps the most injured ally in a psychic ward, taking 40% of its damage for 8s.", buff="npc_mind", cue="npc.mind.start"),
                  circle("drowned_maddening_gaze", "Maddening Gaze", "Marks a circle; champions inside are paralysed (rooted) for 1.5s.", 0.8, VIOLET, cd=15, cast=1.2, radius=200, root=1.5, buff="npc_mind", cue="npc.mind.cast")],
         "poolDraw": 3},
    ],
    "bosses": [
        {"id": "drowned_prophet", "slot": "warlord", "tmpl": "warlord_caster", "name": "The Drowned Prophet", "fallback": "blight_caster",
         "look": "Towering gaunt cephalopod prophet in tattered abyssal-blue vestments, crown of black coral and pearls, a mass of long tentacles for a beard and a skirt of dragging tentacles, glowing cyan eyes, a floating barnacled tome and a staff topped with an eldritch eye.",
         "basic": bolt("Word of the Deep", "Interruptible cast. A bolt of drowning scripture.", cast=1.2, rng=700),
         "pool": [pool("drowned_tidal_prophecy", "Tidal Prophecy", "Marks a wide circle; the sea rises there as a drowning pool for 6s.", 16, 6, TEAL, cd=12, cast=1.5, radius=320, rng=700, buff="npc_tide", cue="npc.tide.cast"),
                  summon("drowned_call_of_the_deep", "Call of the Deep", "Interruptible cast: two Abyssal Stalkers crawl out of the tide at its side.", "abyssal_stalker", 2, TEAL, cd=26, buff="npc_tide", cue="npc.summon.cast"),
                  ring("drowned_mind_shatter", "Mind Shatter", "Marks a large ring; champions inside take damage and are silenced for 3s.", 1.4, VIOLET, cd=18, cast=1.6, radius=520, silence=3.0, buff="npc_silenced", cue="npc.mind.cast"),
                  pull("drowned_drowning_grasp", "Drowning Grasp", "A tentacle marks a line to the farthest champion and drags them in.", 1.2, INK, cd=16, cast=1.2, length=1200, rng=1200, targeting="farthest", buff="npc_ink", cue="npc.ink.start"),
                  enrage("drowned_prophet_madness", "Prophet's Madness", "At 30% health: +40% damage and faster casting until killed.", buff="npc_mind", cue="npc.mind.cast", core=True)]},
        {"id": "maw_of_the_deep", "slot": "colossus", "tmpl": "colossus", "name": "Maw of the Deep", "fallback": "hollow_siegebreaker",
         "look": "Colossal kraken-crab abomination walking on two thick barnacled legs, a gaping circular maw of needle teeth ringed by long tentacles where its head should be, armoured shell back overgrown with coral and anchor chains, enormous crab claw arm, bioluminescent cyan lures.",
         "basic": melee("Crushing Claw", "A crushing claw blow at its current target.", 200),
         "pool": [cone("drowned_tentacle_sweep", "Tentacle Sweep", "Winds up a wide tentacle sweep that knocks champions back. Only the tank should stand in front.", 2.2, TEAL, cd=9, cast=1.3, radius=440, angle=120, knockback=500, buff="npc_sundered", cue="npc.cleave.start"),
                  pull("drowned_devour", "Devour", "Marks a line to the farthest champion, drags them into the maw and bites.", 1.8, INK, cd=18, cast=1.4, length=1200, width=200, rng=1200, targeting="farthest", buff="npc_ink", cue="npc.ink.start"),
                  ring("drowned_tsunami_slam", "Tsunami Slam", "Marks a ring around itself, then slams; everyone inside is slowed for 3s.", 1.7, TEAL, cd=11, cast=1.3, radius=420, slow=3, buff="npc_tide", cue="npc.tide.start"),
                  pool("drowned_ink_tide", "Ink Tide", "Vomits a huge ink pool under its target that burns for 6s.", 18, 6, INK, cd=14, cast=1.3, radius=300, buff="npc_ink", cue="npc.ink.start"),
                  enrage("drowned_leviathan_rage", "Leviathan Rage", "At 30% health it enrages: +40% damage and faster attacks until killed.", buff="npc_bloodlust", cue="npc.roar.cast", core=True)]},
    ],
})

# ------------------------------------------------------------------------------------------ BLIGHTWOOD
RACES.append({
    "id": "blightwood", "name": "The Blightwood", "short": "Blightwood",
    "lore": "The old forest woke sick. Treants, briars and spore-things march out of the rotting groves to reclaim the fields root by root.",
    "origin": {"playerRaces": ["sylvan", "fey", "construct"], "profiles": ["dryad", "evergrove_centaur", "ether_golem_support"],
               "note": "The corrupted mirror of the Thornweave Dryad's grove, the Evergrove Centaur's woods and the Verdant ether golem's living stone."},
    "palette": {"base": [0.30, 0.22, 0.14], "accent": [0.30, 0.55, 0.18], "secondary": [0.55, 0.45, 0.25], "glow": [0.70, 1.00, 0.30],
                "variants": [{"name": "Blightgrove", "base": [0.30, 0.22, 0.14], "accent": [0.30, 0.55, 0.18]},
                             {"name": "Autumn Rot", "base": [0.55, 0.25, 0.08], "accent": [0.90, 0.55, 0.10]},
                             {"name": "Winter Deadwood", "base": [0.55, 0.55, 0.60], "accent": [0.60, 0.85, 0.95]},
                             {"name": "Bloodroot", "base": [0.40, 0.08, 0.08], "accent": [0.85, 0.20, 0.15]}]},
    "footsteps": "bark", "footstepPitch": 0.9, "ambienceCue": "amb.race.grove", "voiceCue": "voice.race.growl",
    "units": [
        {"id": "vinelasher", "slot": "line", "tmpl": "skirmisher", "name": "Vinelasher", "fallback": "hollow_infantry",
         "look": "Wiry humanoid woven from braided thorny vines and dry bark strips, whip-like vine arms trailing to the ground, a knot of glowing green sap for a face, leaves and small red berries sprouting from its shoulders, lithe forward-leaning stance.",
         "basic": melee("Vine Lash", "A stinging whip of thorny vines at its current target.", 170),
         "pool": [cone("blight_vine_lash", "Thorn Whip", "Whips a cone of thorny vines that slows everyone hit for 3s.", 1.8, THORN, cd=9, cast=0.9, radius=340, angle=85, slow=3, buff="npc_thorns", cue="npc.thorns.start"),
                  circle("blight_strangling_vines", "Strangling Vines", "Marks a circle; vines burst out and root everyone inside for 2s.", 1.0, MOSS, cd=14, cast=1.2, radius=220, root=2.0, buff="npc_rooted", cue="npc.root.start"),
                  pull("blight_vine_snare", "Vine Snare", "Marks a line and lashes a vine around a distant champion, reeling them in.", 1.0, MOSS, cd=16, cast=1.0, length=850, buff="npc_thorns", cue="npc.thorns.start"),
                  charge("blight_briar_rush", "Briar Rush", "Marks a line and rushes through it, raking everyone with thorns.", 1.5, THORN, cd=12, cast=0.8, length=750, buff="npc_thorns", cue="npc.thorns.start")],
         "poolDraw": 3},
        {"id": "sapling_brute", "slot": "bruiser", "tmpl": "bruiser", "name": "Sapling Brute", "fallback": "ironbound_bruiser",
         "look": "Young bulky treant brute of pale ash wood, one arm ending in a massive splintered stump club, mossy shoulders with fresh green shoots, cracked bark chest showing amber glowing heartwood, short stubby root legs, aggressive hunched stance.",
         "basic": melee("Stump Club", "A heavy stump-club blow at its current target."),
         "pool": [cone("blight_stump_slam", "Stump Slam", "Winds up an overhead slam; champions in the cone are knocked back.", 2.4, MOSS, cd=9, cast=1.1, radius=360, angle=80, knockback=420, buff="npc_sundered", cue="npc.cleave.start"),
                  charge("blight_uproot_charge", "Uproot Charge", "Tears itself from the ground and charges along a marked line.", 2.0, THORN, cd=14, buff="npc_sundered", cue="npc.grove.cast"),
                  ring("blight_splinter_burst", "Splinter Burst", "Marks a ring around itself, then bursts into flying splinters.", 1.7, THORN, cd=11, radius=340, buff="npc_thorns", cue="npc.thorns.start"),
                  enrage("blight_sap_frenzy", "Sap Frenzy", "At 35% health its sap boils: +35% damage and faster attacks.", mag=0.35, threshold=0.35, buff="npc_bloodlust", cue="npc.roar.cast")],
         "poolDraw": 3},
        {"id": "barkhide_warden", "slot": "tank", "tmpl": "tank", "name": "Barkhide Warden", "fallback": "hollow_shieldbearer",
         "footsteps": "golem",
         "look": "Thick squat oak treant guardian covered in overlapping shield-like bark plates, a slab of petrified wood as a tower shield on one arm, knotted face with deep-set amber eyes, thick moss mantle, gnarled root feet planted wide.",
         "basic": melee("Bark Bash", "A bark-shield bash at its current target."),
         "pool": [provoke("blight_grove_challenge", "Grove Challenge", "Creaks a challenge: for 6s nearby champions deal 35% less damage to anything but the Warden.", buff="npc_thorns", cue="npc.grove.cast"),
                  wall("blight_rooted_stance", "Rooted Stance", "Below 50% health it roots itself: 50% less damage for 6s.", buff="npc_scaleward", cue="npc.ward.start"),
                  guard("blight_barkskin_oath", "Barkskin Oath", "Grows bark over the most injured ally, taking 40% of its damage for 8s.", buff="npc_scaleward", cue="npc.ward.start"),
                  ring("blight_root_quake", "Root Quake", "Marks a ring around itself; roots erupt and hold everyone inside for 1.5s.", 1.2, MOSS, cd=14, radius=360, root=1.5, buff="npc_rooted", cue="npc.root.start")],
         "poolDraw": 3},
        {"id": "rotbloom_shaman", "slot": "caster", "tmpl": "caster", "name": "Rotbloom Shaman", "fallback": "blight_caster",
         "look": "Hunched fungal treant shaman, head a huge drooping rotten flower bloom with a glowing sickly-green core, bark robe hung with mushroom shelves, crooked branch staff sprouting pale fungus and dangling seed pods, spores drifting around it.",
         "basic": bolt("Rot Bolt", "Interruptible cast. A bolt of rotting sap in a fixed direction."),
         "pool": [pool("blight_rot_pool", "Rot Pool", "Marks the ground under its target, then leaves a rotting pool for 5s.", 13, 5, ROT, cd=12, buff="npc_spores", cue="npc.spores.start"),
                  circle("blight_entangle", "Entangle", "Marks a circle; roots burst out and hold everyone inside for 2s.", 1.0, MOSS, cd=14, cast=1.3, radius=230, root=2.0, buff="npc_rooted", cue="npc.root.start"),
                  heal("blight_sap_mending", "Sap Mending", "Interruptible 2s cast that restores 20% health to the most injured ally below 60%.", buff="npc_thorns", cue="npc.heal.cast"),
                  circle("blight_wither_curse", "Wither Curse", "Marks a circle; champions inside wither and are slowed for 4s.", 1.2, ROT, cd=11, cast=1.1, radius=240, slow=4, buff="npc_spores", cue="npc.spores.start"),
                  cone("blight_thorn_burst", "Thorn Burst", "Sprays a cone of thorns in front of it.", 1.9, THORN, cd=10, cast=1.0, radius=460, angle=60, buff="npc_thorns", cue="npc.thorns.start")],
         "poolDraw": 3},
        {"id": "thornspitter", "slot": "ranged", "tmpl": "ranged", "name": "Thornspitter", "fallback": "barbed_hunter",
         "look": "Walking carnivorous pitcher-plant creature on short root legs, a tall veined green-and-crimson pitcher body with a lidded mouth full of thorn teeth, two leafy arms, bundles of long thorns bristling from its back like quivers.",
         "basic": shot("Thorn Spit", "Short aim, then spits a thorn in a fixed direction."),
         "pool": [circle("blight_thorn_volley", "Thorn Volley", "Marks a circle on its target, then a volley of thorns lands there.", 1.8, THORN, cd=10, cast=1.0, radius=200, buff="npc_thorns", cue="npc.thorns.start"),
                  pool("blight_briar_patch", "Briar Patch", "Seeds a briar patch under its target: poison and thorns for 5s.", 11, 5, THORN, cd=12, cast=1.1, radius=220, buff="npc_thorns", cue="npc.thorns.start"),
                  circle("blight_seedpod_mortar", "Seedpod Mortar", "Lobs an exploding seedpod; champions in the circle are knocked back.", 1.6, SPORE, cd=12, cast=1.3, radius=230, knockback=420, buff="npc_spores", cue="npc.spores.start"),
                  disengage("blight_root_hop", "Root Hop", "Springs away on its roots when a champion reaches melee.", cd=10, buff="npc_thorns", cue="npc.thorns.start")],
         "poolDraw": 3},
        {"id": "sporeling", "slot": "special", "tmpl": "swarm", "name": "Sporeling", "fallback": "hollow_infantry",
         "footsteps": "bark",
         "look": "Small waddling mushroom creature knee-high to a man, a wide spotted red-brown cap with glowing yellow-green gills, stubby root legs and little twig arms, beady glowing eyes under the cap, a trail of drifting spores.",
         "basic": melee("Spore Nip", "A weak nip that leaves spores behind.", 150),
         "pool": [ring("blight_spore_cloud", "Spore Cloud", "Marks a ring and puffs a poison cloud around itself for 4s.", 0.0, SPORE, cd=12, cast=1.0, radius=260, dps=9, duration=4, buff="npc_spores", cue="npc.spores.start"),
                  ring("blight_choking_puff", "Choking Puffball", "Marks a small ring; champions inside choke and are silenced for 1.5s.", 0.6, SPORE, cd=15, cast=1.1, radius=240, silence=1.5, buff="npc_silenced", cue="npc.spores.start"),
                  heal("blight_mycelial_link", "Mycelial Link", "Interruptible cast: shares nutrients with the most injured ally (12% health).", frac=0.12, cd=14, cast=1.6, buff="npc_spores", cue="npc.heal.cast"),
                  disengage("blight_spore_scatter", "Spore Scatter", "Bounces away in a puff of spores.", cd=11, length=450, buff="npc_spores", cue="npc.spores.start")],
         "poolDraw": 3},
    ],
    "bosses": [
        {"id": "withered_matron", "slot": "warlord", "tmpl": "warlord_caster", "name": "The Withered Matron", "fallback": "blight_caster",
         "look": "Tall gaunt corrupted dryad queen, grey-violet dead-bark skin, a crown of blackened thorns and dead lilies, long flowing gown of dried roots and moss that drags like a train, hollow glowing green eyes, clawed branch fingers, drifting withered petals.",
         "basic": bolt("Withering Touch", "Interruptible cast. A bolt of withering sap.", cast=1.2, rng=700),
         "pool": [circle("blight_thornstorm", "Thornstorm", "Marks a wide circle, then a storm of thorns lands there.", 2.2, THORN, cd=11, cast=1.4, radius=320, rng=700, buff="npc_thorns", cue="npc.thorns.start"),
                  ring("blight_strangling_grove", "Strangling Grove", "Marks a large ring around herself; roots hold everyone inside for 2s.", 1.2, MOSS, cd=16, cast=1.5, radius=480, root=2.0, buff="npc_rooted", cue="npc.root.start"),
                  pool("blight_blight_bloom", "Blight Bloom", "Blooms a rotting flower under her target that poisons for 6s.", 16, 6, ROT, cd=13, cast=1.3, radius=300, buff="npc_spores", cue="npc.spores.start"),
                  summon("blight_matron_call", "Matron's Call", "Interruptible cast: three Sporelings sprout at her feet.", "sporeling", 3, SPORE, cd=24, buff="npc_spores", cue="npc.summon.cast"),
                  pull("blight_rotting_embrace", "Rotting Embrace", "Roots mark a line to the farthest champion and drag them to her.", 1.2, ROT, cd=16, cast=1.2, length=1200, rng=1200, targeting="farthest", buff="npc_thorns", cue="npc.root.start"),
                  enrage("blight_withering_wrath", "Withering Wrath", "At 30% health: +40% damage and faster casting until killed.", buff="npc_spores", cue="npc.roar.cast", core=True)]},
        {"id": "elder_oakheart", "slot": "colossus", "tmpl": "colossus", "name": "Elder Oakheart", "fallback": "hollow_siegebreaker",
         "footsteps": "golem",
         "look": "Ancient towering oak treant colossus, massive trunk body with deep bark fissures glowing with molten amber heartwood, a long hanging moss beard, antler-like branching crown with a few living leaves, huge root-fist arms, stone-embedded roots for feet, birds' nests and mushrooms in its bark.",
         "basic": melee("Bough Smash", "A crushing bough blow at its current target.", 200),
         "pool": [circle("blight_root_eruption", "Root Eruption", "Marks a circle under its target; giant roots erupt and hold everyone for 2s.", 1.6, MOSS, cd=12, cast=1.3, radius=300, rng=900, root=2.0, buff="npc_rooted", cue="npc.root.start"),
                  ring("blight_oakheart_stomp", "Oakheart Stomp", "Marks a ring around itself, then stomps, knocking everyone inside back.", 1.7, MOSS, cd=10, cast=1.3, radius=420, knockback=520, buff="npc_sundered", cue="npc.grove.cast"),
                  cone("blight_crushing_bough", "Crushing Bough", "Winds up a wide frontal sweep. Only the tank should stand in front.", 2.3, THORN, cd=9, cast=1.3, radius=440, angle=115, buff="npc_sundered", cue="npc.cleave.start"),
                  summon("blight_awaken_saplings", "Awaken Saplings", "Interruptible cast: two Sapling Brutes tear free of the ground.", "sapling_brute", 2, MOSS, cd=28, buff="npc_thorns", cue="npc.summon.cast"),
                  enrage("blight_heartwood_fury", "Heartwood Fury", "At 30% health its heartwood blazes: +40% damage and faster attacks.", buff="npc_bloodlust", cue="npc.roar.cast", core=True)]},
    ],
})

# ------------------------------------------------------------------------------------------ HOLLOW (existing)
RACES.append({
    "id": "hollow", "name": "The Hollow Legion", "short": "Hollow",
    "lore": "The dead of the old kingdom, raised hollow and marched back to the gates they once defended.",
    "origin": {"playerRaces": ["human"], "profiles": ["knight", "ranger", "scholar", "lancer"],
               "note": "The undead reflection of the human champions: Iron Warden, Ash Ranger, Veil Scholar, Dusk Lancer."},
    "palette": {"base": [0.62, 0.56, 0.44], "accent": [0.45, 0.42, 0.38], "secondary": [0.30, 0.28, 0.26], "glow": [0.55, 0.95, 0.85],
                "variants": [{"name": "Gravebound", "base": [0.62, 0.56, 0.44], "accent": [0.45, 0.42, 0.38]},
                             {"name": "Ashen", "base": [0.30, 0.30, 0.32], "accent": [0.70, 0.30, 0.15]},
                             {"name": "Frostgrave", "base": [0.55, 0.65, 0.75], "accent": [0.40, 0.75, 1.00]}]},
    "footsteps": "mail", "footstepPitch": 1.0, "ambienceCue": "amb.race.hollow", "voiceCue": "voice.race.growl",
    "units": [
        {"id": "hollow_infantry", "slot": "line", "extends": True, "name": "Hollow Infantry",
         "look": "(existing Tripo body) skeletal undead footsoldier in rusted mail, torn tabard, dagger.",
         "pool": [cone("hollow_rusted_cleave", "Rusted Cleave", "A telegraphed sweep of its rusted blade.", 1.8, IRON, cd=9, cast=1.0, radius=300, angle=90, buff="npc_sundered", cue="npc.cleave.start"),
                  wall("hollow_deathless", "Deathless Resolve", "Below 40% health it refuses to fall: 40% less damage for 5s.", threshold=0.4, mag=0.4, buff="npc_scaleward", cue="npc.ward.start")],
         "poolDraw": 2},
        {"id": "ironbound_bruiser", "slot": "bruiser", "extends": True, "name": "Ironbound Bruiser",
         "look": "(existing Tripo body) hulking undead brute in riveted iron plates, war axe.",
         "pool": [ring("hollow_iron_stomp", "Iron Stomp", "Marks a ring around itself; the stomp slows everyone inside for 3s.", 1.5, IRON, cd=12, radius=340, slow=3, buff="npc_sundered", cue="npc.cleave.start"),
                  pull("hollow_hook_chain", "Hook Chain", "Marks a line and throws a hooked chain that drags a distant champion to it.", 1.1, IRON, cd=16, cast=1.0, length=850, buff="npc_sundered", cue="npc.cleave.start")],
         "poolDraw": 3},
        {"id": "hollow_shieldbearer", "slot": "tank", "extends": True, "name": "Hollow Shieldbearer",
         "look": "(existing Tripo body) undead soldier with a battered kite shield and sword.",
         "pool": [charge("hollow_shield_rush", "Shield Rush", "Marks a short line and rushes behind its shield, knocking champions aside.", 1.3, IRON, cd=14, cast=0.9, length=650, minr=280, rng=650, knockback=350, buff="npc_sundered", cue="npc.cleave.start")],
         "poolDraw": 3},
        {"id": "blight_caster", "slot": "caster", "extends": True, "name": "Blight Caster",
         "look": "(existing Tripo body) robed undead warlock with a bone staff.",
         "pool": [circle("hollow_grave_silence", "Grave Silence", "Marks a circle; champions inside are silenced for 2s.", 0.8, C(0.4, 0.3, 0.6), cd=14, cast=1.2, radius=220, silence=2.0, buff="npc_silenced", cue="npc.silence.start"),
                  circle("hollow_withering_hex", "Withering Hex", "Marks a circle; champions inside are slowed for 4s.", 1.1, C(0.3, 0.5, 0.1), cd=11, cast=1.0, radius=240, slow=4, buff="npc_spores", cue="npc.spores.start")],
         "poolDraw": 3},
        {"id": "barbed_hunter", "slot": "ranged", "extends": True, "name": "Barbed Hunter",
         "look": "(existing Tripo body) undead archer in leather with a bow or crossbow.",
         "pool": [circle("hollow_pinning_net", "Pinning Net", "Marks a small circle on its target, then a weighted net roots everyone inside for 1.5s.", 0.8, IRON, cd=14, cast=1.0, radius=180, root=1.5, buff="npc_rooted", cue="npc.root.start")],
         "poolDraw": 2},
        {"id": "grave_hound", "slot": "special", "tmpl": "swarm", "name": "Grave Hound", "fallback": "hollow_infantry",
         "footsteps": "beast",
         "look": "Emaciated undead hound the size of a wolf, exposed ribs and spine through torn grey hide, glowing pale-green eyes, iron collar with a broken chain, long skeletal jaw, running on four legs (fallback: hunched two-legged ghoul).",
         "basic": melee("Grave Bite", "A tearing bite at its current target.", 150),
         "pool": [charge("hollow_pounce", "Pounce", "Marks a short line and pounces along it.", 1.5, C(0.6, 0.5, 0.35), cd=11, cast=0.7, length=650, minr=260, rng=650, buff="npc_feral", cue="npc.feral.start"),
                  cone("hollow_rending_bite", "Rending Bite", "A telegraphed bite that slows the victims for 3s.", 1.6, C(0.7, 0.2, 0.1), cd=9, cast=0.8, radius=260, angle=70, slow=3, buff="npc_feral", cue="npc.feral.start"),
                  rally("hollow_pack_howl", "Pack Howl", "Howls: nearby monsters gain +20% damage and attack speed for 8s.", mag=0.2, buff="npc_bloodlust", cue="npc.roar.cast")],
         "poolDraw": 2},
    ],
    "bosses": [
        {"id": "gravemaw_pack_leader", "slot": "warlord", "extends": True, "name": "Gravemaw, Pack Leader",
         "look": "(existing Tripo body) blood-red undead war chief with a greataxe and a totem on its back.",
         "pool": [pull("hollow_bone_hook", "Bone Hook", "Marks a line to the farthest champion and hooks them into the pack.", 1.2, BLOOD, cd=18, cast=1.2, length=1200, rng=1200, targeting="farthest", buff="npc_feral", cue="npc.feral.start")],
         "laneBossHealthMultiplier": 1.6},
        {"id": "hollow_siegebreaker", "slot": "colossus", "extends": True, "name": "Hollow Siegebreaker",
         "look": "(existing Tripo body) giant undead siege warrior with a fused maul.",
         "pool": [circle("hollow_rubble_toss", "Rubble Toss", "Hurls a chunk of wall at its target; champions in the circle are knocked back.", 1.8, STONE, cd=13, cast=1.4, radius=260, rng=900, knockback=400, buff="npc_sundered", cue="npc.cleave.start")]},
    ],
})

# ------------------------------------------------------------------------------------------ IRONHIDE
RACES.append({
    "id": "ironhide", "name": "The Ironhide Warband", "short": "Ironhide",
    "lore": "Orc clans and red-moon trolls who broke their blood-oaths to raid the Breach. They fight for plunder and the drums never stop.",
    "origin": {"playerRaces": ["orc", "troll"], "profiles": ["orc_chieftain", "troll_berserker_melee", "troll_berserker_ranged"],
               "note": "The oath-breaking clans of the Blood-Oath Chieftain and the Red-Moon Berserkers."},
    "palette": {"base": [0.30, 0.40, 0.18], "accent": [0.15, 0.14, 0.13], "secondary": [0.55, 0.08, 0.05], "glow": [1.00, 0.30, 0.10],
                "variants": [{"name": "Blood Clan", "base": [0.30, 0.40, 0.18], "accent": [0.55, 0.08, 0.05]},
                             {"name": "Ashskin", "base": [0.35, 0.33, 0.32], "accent": [0.90, 0.45, 0.10]},
                             {"name": "Red Moon", "base": [0.35, 0.50, 0.55], "accent": [0.80, 0.10, 0.10]}]},
    "footsteps": "plate_heavy", "footstepPitch": 0.9, "ambienceCue": "amb.race.war", "voiceCue": "voice.race.growl",
    "units": [
        {"id": "ironhide_grunt", "slot": "line", "tmpl": "line", "name": "Ironhide Grunt", "fallback": "hollow_infantry",
         "footsteps": "mail",
         "look": "Muscular green-skinned orc grunt with jutting tusks, topknot, crude black-iron shoulder plate on one side, fur loincloth and leather straps, notched cleaver axe, war paint in red handprints.",
         "basic": melee("Cleaver", "A brutal cleaver chop at its current target."),
         "pool": [cone("ironhide_cleave", "Cleave", "Winds up a wide cleave in front of it.", 1.9, BLOOD, cd=9, cast=1.0, radius=320, angle=100, buff="npc_sundered", cue="npc.cleave.start"),
                  charge("ironhide_war_charge", "War Charge", "Marks a line and charges through it, hitting everyone in the line.", 1.7, BLOOD, cd=13, buff="npc_sundered", cue="npc.warcry.start"),
                  rally("ironhide_bloodlust", "Bloodlust", "Bellows: nearby monsters gain +20% damage and attack speed for 8s.", mag=0.2, buff="npc_bloodlust", cue="npc.warcry.start"),
                  cone("ironhide_gut_punch", "Gut Punch", "A short shoulder-charge cone that knocks champions back.", 1.3, IRON, cd=10, cast=0.8, radius=240, angle=70, knockback=380, buff="npc_sundered", cue="npc.cleave.start")],
         "poolDraw": 3},
        {"id": "redmoon_ravager", "slot": "bruiser", "tmpl": "bruiser", "name": "Red-Moon Ravager", "fallback": "ironbound_bruiser",
         "footsteps": "leather",
         "look": "Tall lean blue-grey troll berserker with long arms, huge tusks and a red mohawk, ritual bone jewellery and red cloth wraps, a notched axe in each hand, scars and tribal tattoos, hunched loping stance.",
         "basic": melee("Twin Axes", "A double axe chop at its current target."),
         "pool": [ring("ironhide_whirlwind", "Whirlwind", "Marks a ring and spins with both axes, hitting everyone around it.", 1.8, BLOOD, cd=10, radius=320, buff="npc_sundered", cue="npc.cleave.start"),
                  enrage("ironhide_berserk", "Berserk", "At 40% health it goes berserk: +40% damage and faster attacks.", threshold=0.4, buff="npc_bloodlust", cue="npc.warcry.start"),
                  charge("ironhide_leaping_axe", "Leaping Axes", "Marks a line and leaps along it, axes first.", 1.9, BLOOD, cd=13, buff="npc_sundered", cue="npc.cleave.start"),
                  cone("ironhide_rend", "Rend", "A telegraphed rending chop that slows everyone hit for 3s.", 1.9, BLOOD, cd=9, cast=1.0, radius=300, angle=80, slow=3, buff="npc_feral", cue="npc.cleave.start")],
         "poolDraw": 3},
        {"id": "ironhide_bulwark", "slot": "tank", "tmpl": "tank", "name": "Ironhide Bulwark", "fallback": "hollow_shieldbearer",
         "footsteps": "plate_heavy",
         "look": "Massive orc in layered black-iron plate, a tower shield made from a spiked iron door, heavy chain and meat hook at the belt, horned helm with tusks protruding through the visor, broad planted stance.",
         "basic": melee("Door Bash", "A shield bash with its iron door."),
         "pool": [provoke("ironhide_war_cry", "Challenging Shout", "Taunts nearby champions: for 6s they deal 35% less damage to anything but the Bulwark.", buff="npc_bloodlust", cue="npc.warcry.start"),
                  wall("ironhide_shield_wall", "Iron Door", "Below 50% health it hides behind its door: 50% less damage for 6s.", buff="npc_scaleward", cue="npc.ward.start"),
                  guard("ironhide_bodyguard", "Bodyguard", "Guards the most injured nearby ally for 8s, taking 40% of its damage.", buff="npc_scaleward", cue="npc.ward.start"),
                  pull("ironhide_chain_hook", "Meat Hook", "Marks a line and throws a chained hook that drags a champion in.", 1.1, IRON, cd=16, cast=1.0, length=850, buff="npc_sundered", cue="npc.cleave.start")],
         "poolDraw": 3},
        {"id": "blood_hexer", "slot": "caster", "tmpl": "caster", "name": "Blood Hexer", "fallback": "blight_caster",
         "look": "Wiry troll witch-doctor with a bone mask over its face, dreadlocks woven with feathers and small skulls, necklaces of teeth, shrunken-head totems on a crooked staff, red and black face paint, hunched shuffling pose.",
         "basic": bolt("Hex Bolt", "Interruptible cast. A crackling bolt of blood magic."),
         "pool": [circle("ironhide_blood_hex", "Blood Hex", "Marks a circle; champions inside are silenced for 2s.", 0.9, BLOOD, cd=14, cast=1.2, radius=220, silence=2.0, buff="npc_silenced", cue="npc.silence.start"),
                  heal("ironhide_spirit_mend", "Spirit Mend", "Interruptible 2s cast that restores 20% health to the most injured ally below 60%.", buff="npc_bloodlust", cue="npc.heal.cast"),
                  pool("ironhide_bog_curse", "Bog Curse", "Curses the ground under its target: poison for 5s.", 13, 5, C(0.35, 0.4, 0.1), cd=12, buff="npc_spores", cue="npc.spores.start"),
                  rally("ironhide_frenzy_ritual", "Frenzy Ritual", "A blood ritual: nearby monsters gain +25% damage and attack speed for 8s.", buff="npc_bloodlust", cue="npc.warcry.start")],
         "poolDraw": 3},
        {"id": "redmoon_axethrower", "slot": "ranged", "tmpl": "ranged", "name": "Red-Moon Axe-Thrower", "fallback": "barbed_hunter",
         "look": "Lean troll with a bandolier of throwing axes across the chest, red cloth wraps, bone earrings, one axe raised to throw, long legs in a wide throwing stance.",
         "basic": shot("Thrown Axe", "Short aim, then hurls an axe in a fixed direction."),
         "pool": [circle("ironhide_axe_barrage", "Axe Barrage", "Marks a circle on its target, then a barrage of axes lands there.", 1.9, BLOOD, cd=10, cast=1.0, radius=200, buff="npc_sundered", cue="npc.cleave.start"),
                  circle("ironhide_hamstring_axe", "Hamstring Axe", "Marks a small circle; the axe hamstrings and roots everyone inside for 1.5s.", 0.9, BLOOD, cd=14, cast=0.9, radius=170, root=1.5, buff="npc_rooted", cue="npc.cleave.start"),
                  disengage("ironhide_bounding_retreat", "Bounding Retreat", "Leaps away when a champion reaches melee range.", buff="npc_bloodlust", cue="npc.warcry.start"),
                  cone("ironhide_spinning_axe", "Spinning Axe", "Throws a spinning axe in a narrow long cone.", 1.8, BLOOD, cd=11, cast=1.0, radius=600, angle=25, buff="npc_sundered", cue="npc.cleave.start")],
         "poolDraw": 3},
        {"id": "ironhide_drummer", "slot": "special", "tmpl": "support", "name": "Warband Drummer", "fallback": "blight_caster",
         "footsteps": "mail",
         "look": "Stocky orc carrying an enormous skin war drum strapped to its back and a pair of bone mallets, rows of trophies on the drum rim, fur cloak, open roaring mouth.",
         "basic": bolt("Thrown Stone", "A thrown stone in a fixed direction.", cast=1.0, interruptible=False),
         "pool": [rally("ironhide_war_drums", "War Drums", "Pounds the drums: nearby monsters gain +25% damage and attack speed for 10s.", dur=10, buff="npc_bloodlust", cue="npc.warcry.start"),
                  heal("ironhide_battle_hymn", "Battle Hymn", "Interruptible cast: rallies the most injured ally (15% health).", frac=0.15, buff="npc_bloodlust", cue="npc.heal.cast"),
                  ring("ironhide_deafening_boom", "Deafening Boom", "Marks a ring; the drum's boom knocks back and silences everyone inside for 1.5s.", 0.8, GOLD, cd=16, cast=1.2, radius=360, knockback=380, silence=1.5, buff="npc_silenced", cue="npc.warcry.start")],
         "poolDraw": 2},
    ],
    "bosses": [
        {"id": "ironhide_warchief", "slot": "warlord", "tmpl": "warlord", "name": "Skullsplitter Warchief", "fallback": "gravemaw_pack_leader",
         "look": "Huge scarred orc warchief in black-iron and bone plate, a cloak of wolf pelts, a banner pole of skulls strapped to his back, a colossal two-handed cleaver, one tusk capped in gold, glowing red war paint.",
         "basic": melee("Skullsplitter", "A savage cleaver blow at its current target.", 210),
         "pool": [rally("ironhide_warchief_roar", "Warchief's Roar", "1.5s roar (gold ring): monsters within 12m gain +25% damage and attack speed for 10s.", dur=10, cast=1.5, radius=1200, buff="npc_bloodlust", cue="npc.roar.cast"),
                  cone("ironhide_skull_cleave", "Skull Cleave", "Winds up a 120-degree cleave for heavy damage. Only the tank should stand in front.", 2.6, BLOOD, cd=9, cast=1.2, radius=440, angle=120, buff="npc_sundered", cue="npc.cleave.start"),
                  charge("ironhide_warpath", "Warpath", "Marks a line to the farthest champion within 12m and charges through it.", 1.8, BLOOD, cd=14, cast=1.0, length=1250, width=200, rng=1200, minr=300, targeting="farthest", buff="npc_sundered", cue="npc.warcry.start"),
                  summon("ironhide_call_the_clans", "Call the Clans", "Interruptible cast: two Ironhide Grunts answer his horn.", "ironhide_grunt", 2, BLOOD, cd=26, buff="npc_bloodlust", cue="npc.summon.cast"),
                  enrage("ironhide_blood_fury", "Blood Fury", "At 30% health: +50% damage, faster attacks and movement until killed.", mag=0.5, buff="npc_bloodlust", cue="npc.roar.cast", core=True)]},
        {"id": "ironhide_juggernaut", "slot": "colossus", "tmpl": "colossus", "name": "Mountain Troll Juggernaut", "fallback": "hollow_siegebreaker",
         "look": "Colossal mountain troll with stone-grey hide and moss, iron plates riveted into its skin, a whole tree trunk bound with iron bands as a club, broken chains dangling from its wrists, tiny eyes under a heavy brow.",
         "basic": melee("Trunk Club", "A crushing tree-trunk blow at its current target.", 200),
         "pool": [ring("ironhide_earthsplitter", "Earthsplitter", "Marks a ring around itself, then slams, knocking everyone inside back.", 1.7, IRON, cd=10, cast=1.3, radius=420, knockback=520, buff="npc_sundered", cue="npc.cleave.start"),
                  cone("ironhide_trunk_sweep", "Trunk Sweep", "Winds up a wide frontal sweep. Only the tank should stand in front.", 2.3, IRON, cd=9, cast=1.3, radius=440, angle=115, buff="npc_sundered", cue="npc.cleave.start"),
                  circle("ironhide_boulder_hurl", "Boulder Hurl", "Hurls a boulder at its target; the circle is crushed and slowed for 3s.", 2.0, STONE, cd=12, cast=1.4, radius=260, rng=1000, slow=3, buff="npc_sundered", cue="npc.cleave.start"),
                  wall("ironhide_thick_hide", "Troll Hide", "Below 50% health its hide hardens: 40% less damage for 6s.", mag=0.4, buff="npc_scaleward", cue="npc.ward.start"),
                  enrage("ironhide_juggernaut_rage", "Juggernaut Rage", "At 30% health it enrages: +40% damage and faster attacks.", buff="npc_bloodlust", cue="npc.roar.cast", core=True)]},
    ],
})

# ------------------------------------------------------------------------------------------ DRAKKARI
RACES.append({
    "id": "drakkari", "name": "The Drakkari Brood", "short": "Drakkari",
    "lore": "Dragon-blooded broods who never kept the Dragon Pact. They burn what their cousins swore to guard.",
    "origin": {"playerRaces": ["drakkari"], "profiles": ["drakish_footman"],
               "note": "The pact-breaking brood of the Drakish Footman's dragon line."},
    "palette": {"base": [0.55, 0.16, 0.08], "accent": [0.80, 0.60, 0.20], "secondary": [0.20, 0.12, 0.10], "glow": [1.00, 0.55, 0.10],
                "variants": [{"name": "Ember Brood", "base": [0.55, 0.16, 0.08], "accent": [0.80, 0.60, 0.20]},
                             {"name": "Obsidian Brood", "base": [0.14, 0.12, 0.13], "accent": [1.00, 0.40, 0.05]},
                             {"name": "Verdigris Brood", "base": [0.18, 0.45, 0.35], "accent": [0.85, 0.75, 0.30]}]},
    "footsteps": "mail", "footstepPitch": 0.9, "ambienceCue": "amb.race.war", "voiceCue": "voice.race.growl",
    "units": [
        {"id": "drakkari_whelpguard", "slot": "line", "tmpl": "line", "name": "Drakkari Whelpguard", "fallback": "hollow_infantry",
         "look": "Young lizard-headed dragonkin footsoldier with red-orange scales, small horns, a short spear and a scale-mail skirt, thin tail, ember glow in its throat, upright soldier stance.",
         "basic": melee("Spear Thrust", "A quick spear thrust at its current target."),
         "pool": [charge("drakkari_spear_lunge", "Spear Lunge", "Marks a line and lunges along it, spear first.", 1.6, EMBER, cd=12, cast=0.8, length=750, buff="npc_sundered", cue="npc.cleave.start"),
                  cone("drakkari_tail_sweep", "Tail Sweep", "Sweeps its tail through a wide cone, knocking champions back.", 1.4, EMBER, cd=10, cast=0.9, radius=260, angle=130, knockback=380, buff="npc_sundered", cue="npc.cleave.start"),
                  cone("drakkari_ember_breath", "Ember Breath", "Breathes a short cone of embers.", 1.8, EMBER, cd=10, cast=1.0, radius=320, angle=60, buff="npc_dragonfire", cue="npc.fire.start")],
         "poolDraw": 2},
        {"id": "drakkari_scalebreaker", "slot": "bruiser", "tmpl": "bruiser", "name": "Scalebreaker", "fallback": "ironbound_bruiser",
         "look": "Brawny drakonid bruiser with small leathery wings folded on its back, thick red scales and gold horn-tips, a heavy stone-headed hammer, bone and gold armbands, broad chest glowing with inner fire through the scale seams.",
         "basic": melee("Scale Hammer", "A heavy hammer blow at its current target."),
         "pool": [ring("drakkari_wing_buffet", "Wing Buffet", "Marks a ring and beats its wings, knocking everyone inside back.", 1.3, EMBER, cd=12, radius=340, knockback=480, buff="npc_sundered", cue="npc.fire.start"),
                  charge("drakkari_dive_charge", "Dive Charge", "Marks a line and dives along it on half-open wings.", 2.0, EMBER, cd=14, buff="npc_sundered", cue="npc.fire.start"),
                  cone("drakkari_molten_slam", "Molten Slam", "Winds up a slam that splashes molten rock in a cone.", 2.4, EMBER, cd=9, cast=1.1, radius=360, angle=80, buff="npc_dragonfire", cue="npc.fire.start"),
                  enrage("drakkari_draconic_fury", "Draconic Fury", "At 35% health: +35% damage and faster attacks.", mag=0.35, threshold=0.35, buff="npc_dragonfire", cue="npc.roar.cast")],
         "poolDraw": 3},
        {"id": "drakkari_scaleguard", "slot": "tank", "tmpl": "tank", "name": "Brood Scaleguard", "fallback": "hollow_shieldbearer",
         "footsteps": "plate",
         "look": "Heavily armoured dragonkin with overlapping bronze scale plates, a large shield shaped like a dragon's wing, a horned helm with a crest, short sword, thick tail.",
         "basic": melee("Wing-Shield Bash", "A shield bash at its current target."),
         "pool": [provoke("drakkari_dragon_roar", "Dragon Roar", "Taunts nearby champions: for 6s they deal 35% less damage to anything but the Scaleguard.", buff="npc_dragonfire", cue="npc.roar.cast"),
                  wall("drakkari_scale_ward", "Scale Ward", "Below 50% health its scales harden: 50% less damage for 6s.", buff="npc_scaleward", cue="npc.ward.start"),
                  guard("drakkari_brood_oath", "Brood Oath", "Guards the most injured nearby ally for 8s, taking 40% of its damage.", buff="npc_scaleward", cue="npc.ward.start"),
                  charge("drakkari_shield_charge", "Shield Charge", "Marks a short line and charges behind its shield, knocking champions aside.", 1.3, EMBER, cd=14, cast=0.9, length=650, minr=280, rng=650, knockback=350, buff="npc_sundered", cue="npc.cleave.start")],
         "poolDraw": 3},
        {"id": "drakkari_flamecaller", "slot": "caster", "tmpl": "caster", "name": "Flamecaller", "fallback": "blight_caster",
         "look": "Slender horned dragonkin pyromancer in charred crimson robes with gold trim, a staff topped with a caged burning coal, flames licking from its open jaws and fingertips, long tail wrapped in cloth.",
         "basic": bolt("Fire Bolt", "Interruptible cast. Hurls a fire bolt in a fixed direction."),
         "pool": [circle("drakkari_flame_pillar", "Flame Pillar", "Marks a circle under its target, then a pillar of fire erupts there.", 2.0, EMBER, cd=10, cast=1.2, radius=210, buff="npc_dragonfire", cue="npc.fire.cast"),
                  pool("drakkari_ember_pool", "Ember Pool", "Leaves a pool of burning embers under its target for 5s.", 14, 5, EMBER, cd=12, buff="npc_dragonfire", cue="npc.fire.cast"),
                  heal("drakkari_cauterize", "Cauterize", "Interruptible 2s cast that restores 20% health to the most injured ally below 60%.", buff="npc_dragonfire", cue="npc.heal.cast"),
                  cone("drakkari_searing_breath", "Searing Breath", "Breathes a long cone of fire.", 1.9, EMBER, cd=11, cast=1.2, radius=480, angle=55, buff="npc_dragonfire", cue="npc.fire.start")],
         "poolDraw": 3},
        {"id": "drakkari_wingshot", "slot": "ranged", "tmpl": "ranged", "name": "Drakkari Wingshot", "fallback": "barbed_hunter",
         "look": "Agile winged dragonkin archer with folded bat-like wings, light bronze scale vest, a heavy crossbow with a dragon-head prow, quiver of glowing ember bolts, crest of small horns.",
         "basic": shot("Ember Bolt", "Short aim, then fires an ember bolt in a fixed direction."),
         "pool": [circle("drakkari_fire_rain", "Fire Rain", "Marks a circle on its target, then burning bolts rain there.", 1.8, EMBER, cd=10, cast=1.0, radius=210, buff="npc_dragonfire", cue="npc.fire.start"),
                  disengage("drakkari_wing_leap", "Wing Leap", "Leaps away on its wings when a champion reaches melee.", buff="npc_dragonfire", cue="npc.fire.start"),
                  pool("drakkari_incendiary_bolt", "Incendiary Bolt", "An incendiary bolt leaves burning ground under its target for 4s.", 12, 4, EMBER, cd=12, cast=1.0, radius=190, buff="npc_dragonfire", cue="npc.fire.start"),
                  circle("drakkari_pinning_bolt", "Pinning Bolt", "Marks a small circle; the bolt pins everyone inside for 1.5s.", 0.9, EMBER, cd=14, cast=0.9, radius=170, root=1.5, buff="npc_rooted", cue="npc.root.start")],
         "poolDraw": 3},
        {"id": "ember_whelp", "slot": "special", "tmpl": "swarm", "name": "Ember Whelp", "fallback": "hollow_infantry",
         "footsteps": "beast",
         "look": "Small dog-sized dragon whelp walking upright on hind legs, oversized head with stubby horns, little flapping wings, bright orange scales with a glowing belly, smoke puffing from its nostrils.",
         "basic": melee("Whelp Bite", "A snapping bite at its current target.", 150),
         "pool": [charge("drakkari_whelp_dive", "Whelp Dive", "Marks a short line and dives along it.", 1.4, EMBER, cd=11, cast=0.7, length=650, minr=260, rng=650, buff="npc_dragonfire", cue="npc.fire.start"),
                  cone("drakkari_cinder_spit", "Cinder Spit", "Spits a small cone of cinders.", 1.5, EMBER, cd=9, cast=0.8, radius=260, angle=60, buff="npc_dragonfire", cue="npc.fire.start"),
                  enrage("drakkari_whelp_frenzy", "Whelp Frenzy", "At 50% health it frenzies: +30% damage and faster attacks.", mag=0.3, threshold=0.5, buff="npc_dragonfire", cue="npc.roar.cast")],
         "poolDraw": 2},
    ],
    "bosses": [
        {"id": "drakkari_broodmother", "slot": "warlord", "tmpl": "warlord_caster", "name": "Broodmother Vyrsha", "fallback": "blight_caster",
         "look": "Tall regal dragonkin matriarch with wide ragged wings half-spread, a crown of curling golden horns, flowing robes of scorched silk over scale armour, a staff with a dragon egg glowing inside a gold cage, fire smouldering in her mouth.",
         "basic": bolt("Brood Flame", "Interruptible cast. A gout of dragonfire in a fixed direction.", cast=1.2, rng=700),
         "pool": [summon("drakkari_hatch_the_brood", "Hatch the Brood", "Interruptible cast: three Ember Whelps hatch at her feet.", "ember_whelp", 3, EMBER, cd=24, buff="npc_dragonfire", cue="npc.summon.cast"),
                  ring("drakkari_flame_nova", "Flame Nova", "Marks a large ring around her, then a nova of fire bursts out.", 1.9, EMBER, cd=12, cast=1.4, radius=480, buff="npc_dragonfire", cue="npc.fire.cast"),
                  ring("drakkari_wing_gust", "Wing Gust", "Marks a ring; a gust of her wings knocks everyone inside back.", 1.0, GOLD, cd=14, cast=1.1, radius=420, knockback=600, buff="npc_sundered", cue="npc.fire.start"),
                  pool("drakkari_magma_rain", "Magma Rain", "Marks a wide circle; magma rains and burns there for 6s.", 17, 6, EMBER, cd=13, cast=1.4, radius=320, rng=700, buff="npc_dragonfire", cue="npc.fire.cast"),
                  enrage("drakkari_broodmother_wrath", "Broodmother's Wrath", "At 30% health: +40% damage and faster casting until killed.", buff="npc_dragonfire", cue="npc.roar.cast", core=True)]},
        {"id": "drakkari_ashwing", "slot": "colossus", "tmpl": "colossus", "name": "Ashwing the Scorcher", "fallback": "hollow_siegebreaker",
         "footsteps": "behemoth",
         "look": "Massive bipedal fire drake standing upright, huge folded wings like a cloak, thick black-and-crimson scales with molten cracks, a long horned head with smoke pouring from its jaws, heavy clawed arms, tail dragging behind.",
         "basic": melee("Rending Claws", "A rending claw strike at its current target.", 200),
         "pool": [cone("drakkari_inferno_breath", "Inferno Breath", "Winds up a long cone of fire breath. Get out of the cone.", 2.4, EMBER, cd=10, cast=1.4, radius=560, angle=60, buff="npc_dragonfire", cue="npc.fire.cast"),
                  cone("drakkari_ashwing_tail", "Tail Lash", "Lashes its tail through a wide arc, knocking champions back.", 1.7, EMBER, cd=11, cast=1.1, radius=400, angle=140, knockback=500, buff="npc_sundered", cue="npc.cleave.start"),
                  pool("drakkari_ash_fall", "Ash Fall", "Marks a wide circle under its target; burning ash falls there for 6s.", 18, 6, EMBER, cd=13, cast=1.3, radius=300, rng=900, buff="npc_dragonfire", cue="npc.fire.start"),
                  ring("drakkari_ashwing_stomp", "Scorching Stomp", "Marks a ring and stomps, knocking everyone inside back.", 1.6, EMBER, cd=12, cast=1.3, radius=400, knockback=450, buff="npc_sundered", cue="npc.cleave.start"),
                  enrage("drakkari_ashwing_fury", "Molten Fury", "At 30% health its scales melt open: +40% damage and faster attacks.", buff="npc_dragonfire", cue="npc.roar.cast", core=True)]},
    ],
})

# ------------------------------------------------------------------------------------------ STONEBORN
RACES.append({
    "id": "stoneborn", "name": "The Stoneborn", "short": "Stoneborn",
    "lore": "Runic golems and constructs of a lost dwarven hold, woken by the Breach's ether and bound to no master.",
    "origin": {"playerRaces": ["construct", "dwarf"], "profiles": ["ether_golem_tank", "ether_golem_bruiser", "ether_golem_support", "dwarf_miner"],
               "note": "The masterless cousins of the Ether Golem and the constructs of the Deepdelve Miner's forgotten forges."},
    "palette": {"base": [0.40, 0.40, 0.42], "accent": [0.25, 0.50, 0.90], "secondary": [0.30, 0.25, 0.20], "glow": [0.35, 0.75, 1.00],
                "variants": [{"name": "Runic Granite", "base": [0.40, 0.40, 0.42], "accent": [0.25, 0.50, 0.90]},
                             {"name": "Felforged", "base": [0.18, 0.16, 0.15], "accent": [0.35, 0.95, 0.20]},
                             {"name": "Sunstone", "base": [0.65, 0.55, 0.40], "accent": [1.00, 0.70, 0.20]}]},
    "footsteps": "golem", "footstepPitch": 1.0, "ambienceCue": "amb.race.war", "voiceCue": "voice.race.growl",
    "units": [
        {"id": "rune_sentinel", "slot": "line", "tmpl": "line", "name": "Rune Sentinel", "fallback": "hollow_infantry",
         "look": "Man-sized stone construct soldier of carved grey granite blocks, glowing blue rune lines along its limbs, one forearm ending in a stone blade, a faceless helm-shaped head with a single rune slit, bronze joint rings.",
         "basic": melee("Stone Blade", "A stone blade strike at its current target."),
         "pool": [cone("stoneborn_rune_strike", "Rune Strike", "A telegraphed sweep of its rune-lit blade.", 1.9, RUNE, cd=9, cast=1.0, radius=320, angle=90, buff="npc_runic", cue="npc.rune.start"),
                  charge("stoneborn_rune_rush", "Rune Rush", "Marks a line and slides along it on glowing runes.", 1.6, RUNE, cd=13, buff="npc_runic", cue="npc.rune.start"),
                  ring("stoneborn_static_pulse", "Static Pulse", "Marks a ring; a pulse of ether silences everyone inside for 1.5s.", 0.8, RUNE, cd=15, cast=1.1, radius=320, silence=1.5, buff="npc_silenced", cue="npc.rune.start")],
         "poolDraw": 2},
        {"id": "granite_crusher", "slot": "bruiser", "tmpl": "bruiser", "name": "Granite Crusher", "fallback": "ironbound_bruiser",
         "look": "Hulking boulder golem with oversized fists of rough granite, a small head sunk between huge rock shoulders, glowing blue cracks across its chest, moss in the crevices, heavy knuckle-dragging stance.",
         "basic": melee("Stone Fist", "A crushing stone fist at its current target."),
         "pool": [charge("stoneborn_boulder_charge", "Boulder Charge", "Marks a line and rolls through it, knocking champions aside.", 1.9, STONE, cd=14, knockback=350, buff="npc_sundered", cue="npc.cleave.start"),
                  ring("stoneborn_ground_pound", "Ground Pound", "Marks a ring and pounds the ground, slowing everyone inside for 3s.", 1.7, STONE, cd=11, radius=360, slow=3, buff="npc_sundered", cue="npc.cleave.start"),
                  cone("stoneborn_granite_smash", "Granite Smash", "Winds up a two-fisted smash in a cone.", 2.4, STONE, cd=9, cast=1.1, radius=360, angle=80, buff="npc_sundered", cue="npc.cleave.start"),
                  enrage("stoneborn_overload", "Ether Overload", "At 35% health its core overloads: +35% damage and faster attacks.", mag=0.35, threshold=0.35, buff="npc_runic", cue="npc.rune.start")],
         "poolDraw": 3},
        {"id": "bastion_golem", "slot": "tank", "tmpl": "tank", "name": "Bastion Golem", "fallback": "hollow_shieldbearer",
         "look": "Tall monolithic golem shaped like a walking fortress wall, a slab shield carved with a dwarven rune on one arm, crenellated shoulders, deep blue ether core visible through a grille in its chest, thick pillar legs.",
         "basic": melee("Slab Bash", "A slab-shield bash at its current target."),
         "pool": [provoke("stoneborn_runic_taunt", "Runic Challenge", "A rune flares: for 6s nearby champions deal 35% less damage to anything but the Bastion.", buff="npc_runic", cue="npc.rune.start"),
                  wall("stoneborn_stoneskin", "Stoneskin", "Below 50% health it petrifies: 50% less damage for 6s.", buff="npc_scaleward", cue="npc.ward.start"),
                  guard("stoneborn_warding_link", "Warding Link", "Links a rune to the most injured ally, taking 40% of its damage for 8s.", buff="npc_runic", cue="npc.rune.start"),
                  ring("stoneborn_tremor", "Tremor", "Marks a ring; the tremor roots everyone inside for 1.5s.", 1.2, STONE, cd=14, radius=360, root=1.5, buff="npc_rooted", cue="npc.cleave.start")],
         "poolDraw": 3},
        {"id": "deepforge_runesmith", "slot": "caster", "tmpl": "caster", "name": "Deepforge Runesmith", "fallback": "blight_caster",
         "footsteps": "plate",
         "look": "Stout dwarf-shaped construct of bronze and dark iron with a stone beard carved in braids, a forge-hammer staff crackling with blue runic lightning, glowing furnace belly, rune-etched pauldrons.",
         "basic": bolt("Rune Bolt", "Interruptible cast. A bolt of runic lightning."),
         "pool": [circle("stoneborn_rune_mine", "Rune Mine", "Etches a slow rune under its target; it detonates after a long fuse.", 2.6, RUNE, cd=12, cast=2.0, radius=220, buff="npc_runic", cue="npc.rune.start"),
                  heal("stoneborn_forge_mending", "Forge Mending", "Interruptible 2s cast that restores 20% health to the most injured ally below 60%.", buff="npc_runic", cue="npc.heal.cast"),
                  circle("stoneborn_arc_lattice", "Arc Lattice", "Marks a circle; a lattice of lightning silences everyone inside for 2s.", 0.9, RUNE, cd=14, cast=1.2, radius=230, silence=2.0, buff="npc_silenced", cue="npc.rune.start"),
                  pool("stoneborn_molten_slag", "Molten Slag", "Pours molten slag under its target that burns for 5s.", 14, 5, EMBER, cd=12, buff="npc_dragonfire", cue="npc.fire.start")],
         "poolDraw": 3},
        {"id": "crystal_ballista", "slot": "ranged", "tmpl": "ranged", "name": "Crystal Ballista", "fallback": "barbed_hunter",
         "look": "Low spider-legged stone construct carrying a crystal launcher on its back like a crossbow, glowing blue crystal shards loaded in a rack, a lens-eye on its front, bronze gears on the leg joints.",
         "basic": shot("Crystal Shard", "Short aim, then fires a crystal shard in a fixed direction."),
         "pool": [circle("stoneborn_shard_volley", "Shard Volley", "Marks a circle on its target, then a volley of shards lands there.", 1.9, RUNE, cd=10, cast=1.0, radius=200, buff="npc_runic", cue="npc.rune.start"),
                  circle("stoneborn_pinning_shard", "Pinning Shard", "Marks a small circle; crystals pin everyone inside for 1.5s.", 0.9, RUNE, cd=14, cast=0.9, radius=170, root=1.5, buff="npc_rooted", cue="npc.rune.start"),
                  disengage("stoneborn_recoil_jump", "Recoil Jump", "Fires into the ground and recoils away from melee.", buff="npc_runic", cue="npc.rune.start"),
                  cone("stoneborn_piercing_beam", "Piercing Beam", "Charges its lens, then fires a long narrow beam.", 2.0, RUNE, cd=12, cast=1.3, radius=750, angle=12, buff="npc_runic", cue="npc.rune.start")],
         "poolDraw": 3},
        {"id": "ether_mote", "slot": "special", "tmpl": "support", "name": "Ether Mote", "fallback": "blight_caster",
         "footsteps": "whisp",
         "look": "Floating construct of a glowing blue ether crystal core held in a cage of orbiting stone rings, a few small stone plates hovering around it, trailing sparks, no legs.",
         "basic": bolt("Ether Spark", "Interruptible cast. A spark of raw ether.", cast=1.2),
         "pool": [heal("stoneborn_ether_repair", "Ether Repair", "Interruptible cast: repairs the most injured ally (20% health).", buff="npc_runic", cue="npc.heal.cast"),
                  guard("stoneborn_shield_matrix", "Shield Matrix", "Projects a matrix over the most injured ally, taking 40% of its damage for 8s.", buff="npc_scaleward", cue="npc.ward.start"),
                  rally("stoneborn_overcharge", "Overcharge", "Overcharges nearby monsters: +25% damage and attack speed for 8s.", buff="npc_runic", cue="npc.rune.start"),
                  ring("stoneborn_ether_burst", "Ether Burst", "Marks a ring and bursts, silencing everyone inside for 1.5s.", 0.9, RUNE, cd=15, cast=1.1, radius=320, silence=1.5, buff="npc_silenced", cue="npc.rune.start")],
         "poolDraw": 3},
    ],
    "bosses": [
        {"id": "stoneborn_forgelord", "slot": "warlord", "tmpl": "warlord", "name": "Forgelord Thrainor", "fallback": "gravemaw_pack_leader",
         "footsteps": "plate_heavy",
         "look": "Towering dwarven forge-king construct of blackened iron and bronze, a braided beard of chain and stone, a crown of anvil horns, a massive forge hammer glowing white-hot, furnace chest with a roaring blue fire, runic pauldrons trailing sparks.",
         "basic": melee("Forge Hammer", "A white-hot hammer blow at its current target.", 210),
         "pool": [summon("stoneborn_forge_sentinels", "Forge Sentinels", "Interruptible cast: two Rune Sentinels step out of the forge.", "rune_sentinel", 2, RUNE, cd=26, buff="npc_runic", cue="npc.summon.cast"),
                  pool("stoneborn_slag_eruption", "Slag Eruption", "Marks a wide circle; molten slag erupts and burns for 6s.", 17, 6, EMBER, cd=13, cast=1.4, radius=320, rng=900, buff="npc_dragonfire", cue="npc.fire.start"),
                  cone("stoneborn_anvil_cleave", "Anvil Cleave", "Winds up a heavy 110-degree cleave. Only the tank should stand in front.", 2.5, STONE, cd=9, cast=1.2, radius=440, angle=110, buff="npc_sundered", cue="npc.cleave.start"),
                  ring("stoneborn_runic_shockwave", "Runic Shockwave", "Marks a ring; a runic shockwave knocks back and silences everyone inside for 2s.", 1.2, RUNE, cd=15, cast=1.4, radius=440, knockback=450, silence=2.0, buff="npc_silenced", cue="npc.rune.start"),
                  enrage("stoneborn_forge_fury", "Forge Fury", "At 30% health: +50% damage, faster attacks and movement until killed.", mag=0.5, buff="npc_dragonfire", cue="npc.roar.cast", core=True)]},
        {"id": "stoneborn_colossus", "slot": "colossus", "tmpl": "colossus", "name": "Primeval Colossus", "fallback": "hollow_siegebreaker",
         "footsteps": "behemoth",
         "look": "Colossal ancient stone giant covered in moss and ruins, a broken tower fused into its back, huge boulder fists, glowing blue ether seams across its body, a cavernous face with glowing eyes, dust falling from its joints.",
         "basic": melee("Boulder Fist", "A crushing boulder fist at its current target.", 200),
         "pool": [ring("stoneborn_quake", "Quake", "Marks a ring around itself, then quakes, knocking everyone inside back.", 1.7, STONE, cd=10, cast=1.3, radius=440, knockback=520, buff="npc_sundered", cue="npc.cleave.start"),
                  circle("stoneborn_boulder_toss", "Boulder Toss", "Hurls a boulder at its target; the circle is crushed and slowed for 3s.", 2.0, STONE, cd=12, cast=1.4, radius=260, rng=1000, slow=3, buff="npc_sundered", cue="npc.cleave.start"),
                  cone("stoneborn_crush", "Colossal Crush", "Winds up a two-fisted frontal crush. Only the tank should stand in front.", 2.4, STONE, cd=9, cast=1.3, radius=440, angle=110, buff="npc_sundered", cue="npc.cleave.start"),
                  circle("stoneborn_petrify", "Petrifying Gaze", "Marks a circle; champions inside are petrified (rooted) for 2s.", 1.0, RUNE, cd=15, cast=1.4, radius=280, rng=900, root=2.0, buff="npc_rooted", cue="npc.rune.start"),
                  enrage("stoneborn_colossus_rage", "Awakened Wrath", "At 30% health it fully awakens: +40% damage and faster attacks.", buff="npc_runic", cue="npc.roar.cast", core=True)]},
    ],
})

# ------------------------------------------------------------------------------------------ FERAL KIN
RACES.append({
    "id": "feral_kin", "name": "The Feral Kin", "short": "Feral",
    "lore": "Beasts driven mad by the Breach: werebears, dire wolves, tusked behemoths and the wild centaurs who ride with them.",
    "origin": {"playerRaces": ["beast", "centaur"], "profiles": ["bear", "totemic_behemoth", "evergrove_centaur"],
               "note": "The maddened wild kin of the Gravewood Bear, the Totemic Behemoth and the Evergrove Centaur."},
    "palette": {"base": [0.40, 0.28, 0.18], "accent": [0.85, 0.80, 0.70], "secondary": [0.20, 0.15, 0.10], "glow": [1.00, 0.80, 0.20],
                "variants": [{"name": "Umberhide", "base": [0.40, 0.28, 0.18], "accent": [0.85, 0.80, 0.70]},
                             {"name": "Frostpelt", "base": [0.75, 0.78, 0.82], "accent": [0.40, 0.60, 0.90]},
                             {"name": "Blackmaw", "base": [0.12, 0.10, 0.10], "accent": [0.90, 0.20, 0.10]}]},
    "footsteps": "beast", "footstepPitch": 1.0, "ambienceCue": "amb.race.grove", "voiceCue": "voice.race.growl",
    "units": [
        {"id": "dire_wolf", "slot": "line", "tmpl": "skirmisher", "name": "Dire Wolf", "fallback": "hollow_infantry",
         "look": "Huge shaggy dire wolf, shoulder-high to a man, dark brown fur with a pale mane, scarred muzzle and yellow eyes, bone fetishes tied into its fur (fallback: hunched two-legged wolfman).",
         "basic": melee("Savage Bite", "A savage bite at its current target.", 160),
         "pool": [charge("feral_pounce", "Pounce", "Marks a short line and pounces along it.", 1.6, FERAL, cd=11, cast=0.7, length=700, minr=260, rng=700, buff="npc_feral", cue="npc.feral.start"),
                  cone("feral_hamstring", "Hamstring", "A telegraphed hamstring bite that slows the victims for 3s.", 1.6, FERAL, cd=9, cast=0.8, radius=260, angle=70, slow=3, buff="npc_feral", cue="npc.feral.start"),
                  rally("feral_pack_howl", "Pack Howl", "Howls: nearby monsters gain +20% damage and attack speed for 8s.", mag=0.2, buff="npc_bloodlust", cue="npc.roar.cast")],
         "poolDraw": 2},
        {"id": "werebear_mauler", "slot": "bruiser", "tmpl": "bruiser", "name": "Werebear Mauler", "fallback": "ironbound_bruiser",
         "look": "Towering upright werebear with thick dark brown fur, massive clawed paws, torn remains of leather armour and a broken shackle, root growth and moss across its shoulders, amber eyes, roaring open jaws.",
         "basic": melee("Maul", "A mauling claw swipe at its current target."),
         "pool": [cone("feral_maul", "Savage Maul", "Winds up a two-pawed maul in a cone.", 2.4, FERAL, cd=9, cast=1.1, radius=360, angle=85, buff="npc_feral", cue="npc.feral.start"),
                  charge("feral_bear_charge", "Bear Charge", "Marks a line and charges on all fours, knocking champions aside.", 1.9, FERAL, cd=14, knockback=350, buff="npc_sundered", cue="npc.roar.cast"),
                  wall("feral_thick_hide", "Thick Hide", "Below 50% health its hide thickens: 40% less damage for 6s.", mag=0.4, buff="npc_scaleward", cue="npc.ward.start"),
                  enrage("feral_rage", "Feral Rage", "At 35% health it rages: +35% damage and faster attacks.", mag=0.35, threshold=0.35, buff="npc_bloodlust", cue="npc.roar.cast")],
         "poolDraw": 3},
        {"id": "tusked_behemoth", "slot": "tank", "tmpl": "tank", "name": "Tusked Behemoth", "fallback": "hollow_shieldbearer",
         "footsteps": "behemoth",
         "look": "Massive upright elephant-rhino hybrid warrior with grey leathery hide, curved ivory tusks and a nose horn, carved stone plates tied on as armour, a small ritual totem shield, painted tribal markings, pillar-thick legs.",
         "basic": melee("Tusk Gore", "A goring tusk strike at its current target."),
         "pool": [provoke("feral_trumpet", "Trumpeting Challenge", "Trumpets: for 6s nearby champions deal 35% less damage to anything but the Behemoth.", buff="npc_feral", cue="npc.roar.cast"),
                  charge("feral_trample", "Trample", "Marks a line and tramples through it, knocking champions aside.", 1.6, FERAL, cd=14, knockback=420, buff="npc_sundered", cue="npc.cleave.start"),
                  guard("feral_herd_guard", "Herd Guard", "Guards the most injured nearby ally for 8s, taking 40% of its damage.", buff="npc_scaleward", cue="npc.ward.start"),
                  ring("feral_earthshaker", "Earthshaker", "Marks a ring and stamps; everyone inside is rooted for 1.5s.", 1.2, STONE, cd=14, radius=360, root=1.5, buff="npc_rooted", cue="npc.cleave.start")],
         "poolDraw": 3},
        {"id": "feral_shaman", "slot": "caster", "tmpl": "caster", "name": "Antlered Shaman", "fallback": "blight_caster",
         "footsteps": "hooves",
         "look": "Hunched goat-legged beastman shaman with a deer skull mask and large antlers, a cloak of feathers and pelts, a staff of bone and antler hung with bells and charms, glowing yellow eyes.",
         "basic": bolt("Spirit Bolt", "Interruptible cast. A bolt of wild spirit energy."),
         "pool": [heal("feral_spirit_mending", "Spirit Mending", "Interruptible 2s cast that restores 20% health to the most injured ally below 60%.", buff="npc_feral", cue="npc.heal.cast"),
                  rally("feral_savage_totem", "Savage Chant", "Chants: nearby monsters gain +25% damage and attack speed for 8s.", buff="npc_bloodlust", cue="npc.roar.cast"),
                  circle("feral_thorn_hex", "Bramble Hex", "Marks a circle; brambles root everyone inside for 2s.", 1.0, MOSS, cd=14, cast=1.2, radius=220, root=2.0, buff="npc_rooted", cue="npc.root.start"),
                  summon("feral_spirits", "Feral Spirits", "Interruptible cast: calls two Dire Wolves to its side.", "dire_wolf", 2, FERAL, cd=28, buff="npc_feral", cue="npc.summon.cast")],
         "poolDraw": 3},
        {"id": "wild_outrider", "slot": "ranged", "tmpl": "ranged", "name": "Wild Outrider", "fallback": "barbed_hunter",
         "footsteps": "hooves",
         "look": "Wild centaur archer with a shaggy chestnut horse body, bare painted human torso, antler headdress, a long recurve bow, quiver of feathered arrows, braided mane with beads (fallback: two-legged archer).",
         "basic": shot("Wild Arrow", "Short aim, then looses an arrow in a fixed direction."),
         "pool": [circle("feral_volley", "Volley", "Marks a circle on its target, then a volley lands there.", 1.8, FERAL, cd=10, cast=1.0, radius=200, buff="npc_feral", cue="npc.feral.start"),
                  disengage("feral_gallop_away", "Gallop Away", "Gallops away when a champion reaches melee range.", length=700, buff="npc_feral", cue="npc.feral.start"),
                  charge("feral_trample_charge", "Trampling Charge", "Marks a line and gallops through it.", 1.7, FERAL, cd=15, knockback=300, buff="npc_sundered", cue="npc.feral.start"),
                  circle("feral_crippling_arrow", "Crippling Arrow", "Marks a circle; the arrows slow everyone inside for 4s.", 1.2, FERAL, cd=11, cast=0.9, radius=200, slow=4, buff="npc_feral", cue="npc.feral.start")],
         "poolDraw": 3},
        {"id": "bristleback", "slot": "special", "tmpl": "swarm", "name": "Bristleback Boar", "fallback": "hollow_infantry",
         "footsteps": "beast",
         "look": "Knee-high wild boar with a ridge of long quill-like bristles, curved yellow tusks, muddy black hide, tiny furious red eyes (fallback: squat two-legged boarman).",
         "basic": melee("Gore", "A goring charge at its current target.", 150),
         "pool": [charge("feral_boar_gore", "Gore Rush", "Marks a short line and rushes along it.", 1.5, FERAL, cd=11, cast=0.7, length=650, minr=260, rng=650, buff="npc_feral", cue="npc.feral.start"),
                  ring("feral_bristle_burst", "Bristle Burst", "Marks a ring and shakes out its bristles, hitting everyone around it.", 1.4, FERAL, cd=12, cast=1.0, radius=260, buff="npc_thorns", cue="npc.thorns.start"),
                  enrage("feral_boar_frenzy", "Boar Frenzy", "At 50% health it frenzies: +30% damage and faster attacks.", mag=0.3, threshold=0.5, buff="npc_bloodlust", cue="npc.roar.cast")],
         "poolDraw": 2},
    ],
    "bosses": [
        {"id": "feral_ursoth", "slot": "warlord", "tmpl": "warlord", "name": "Ursoth, the Elder Bear", "fallback": "gravemaw_pack_leader",
         "footsteps": "bear",
         "look": "Gigantic ancient werebear chieftain with grizzled silver-streaked fur, a mantle of antlers and bones, a massive tree growing from its hunched back, huge scarred claws, one blind white eye and one glowing amber eye.",
         "basic": melee("Elder Claws", "A crushing claw blow at its current target.", 210),
         "pool": [rally("feral_elder_roar", "Elder Roar", "1.5s roar (gold ring): monsters within 12m gain +25% damage and attack speed for 10s.", dur=10, cast=1.5, radius=1200, buff="npc_bloodlust", cue="npc.roar.cast"),
                  cone("feral_rending_maul", "Rending Maul", "Winds up a 120-degree maul for heavy damage. Only the tank should stand in front.", 2.6, FERAL, cd=9, cast=1.2, radius=440, angle=120, buff="npc_feral", cue="npc.feral.start"),
                  charge("feral_ursoth_charge", "Crushing Charge", "Marks a line to the farthest champion within 12m and charges through it.", 1.8, FERAL, cd=14, cast=1.0, length=1250, width=200, rng=1200, minr=300, targeting="farthest", buff="npc_sundered", cue="npc.roar.cast"),
                  summon("feral_call_of_the_wild", "Call of the Wild", "Interruptible cast: two Dire Wolves answer the call.", "dire_wolf", 2, FERAL, cd=26, buff="npc_feral", cue="npc.summon.cast"),
                  enrage("feral_elder_fury", "Elder Fury", "At 30% health: +50% damage, faster attacks and movement until killed.", mag=0.5, buff="npc_bloodlust", cue="npc.roar.cast", core=True)]},
        {"id": "feral_mammoth", "slot": "colossus", "tmpl": "colossus", "name": "Totemic Mammoth", "fallback": "hollow_siegebreaker",
         "footsteps": "behemoth",
         "look": "Colossal upright mammoth-behemoth with shaggy brown wool, enormous curling tusks capped in carved stone, a huge ritual totem pole strapped across its back, stone plates tied to its shoulders, glowing yellow war paint.",
         "basic": melee("Tusk Sweep", "A sweeping tusk strike at its current target.", 200),
         "pool": [ring("feral_mammoth_stomp", "Titan Stomp", "Marks a ring around itself, then stomps, knocking everyone inside back.", 1.7, STONE, cd=10, cast=1.3, radius=440, knockback=520, buff="npc_sundered", cue="npc.cleave.start"),
                  charge("feral_mammoth_trample", "Stampede", "Marks a long line to the farthest champion and tramples through it.", 1.9, FERAL, cd=15, cast=1.2, length=1250, width=240, rng=1200, minr=300, targeting="farthest", buff="npc_sundered", cue="npc.roar.cast"),
                  cone("feral_tusk_sweep", "Great Tusk Sweep", "Winds up a wide frontal tusk sweep. Only the tank should stand in front.", 2.3, FERAL, cd=9, cast=1.3, radius=440, angle=115, buff="npc_sundered", cue="npc.cleave.start"),
                  ring("feral_earthquake", "Earthquake", "Marks a huge ring; the quake roots everyone inside for 1.5s.", 1.0, STONE, cd=16, cast=1.6, radius=560, root=1.5, buff="npc_rooted", cue="npc.cleave.start"),
                  enrage("feral_mammoth_rage", "Primal Rage", "At 30% health it goes wild: +40% damage and faster attacks.", buff="npc_bloodlust", cue="npc.roar.cast", core=True)]},
    ],
})

# ------------------------------------------------------------------------------------------ FALLEN ORDER
RACES.append({
    "id": "fallen_order", "name": "The Fallen Order", "short": "Fallen",
    "lore": "Knights and paladins who kept their vows past death and faith. Their light has curdled into something that burns the living.",
    "origin": {"playerRaces": ["human"], "profiles": ["knight", "paladin_righteous", "paladin_holy", "keeper_of_light"],
               "note": "The corrupted mirror of the Iron Warden, the Relic Paladins and the Keeper of the Light."},
    "palette": {"base": [0.35, 0.33, 0.30], "accent": [0.70, 0.55, 0.15], "secondary": [0.35, 0.05, 0.08], "glow": [0.90, 0.85, 0.30],
                "variants": [{"name": "Tarnished Oath", "base": [0.35, 0.33, 0.30], "accent": [0.70, 0.55, 0.15]},
                             {"name": "Bone White", "base": [0.80, 0.78, 0.70], "accent": [0.60, 0.10, 0.10]},
                             {"name": "Blackguard", "base": [0.10, 0.10, 0.12], "accent": [0.55, 0.85, 0.30]}]},
    "footsteps": "plate", "footstepPitch": 0.95, "ambienceCue": "amb.race.hollow", "voiceCue": "voice.race.growl",
    "units": [
        {"id": "fallen_squire", "slot": "line", "tmpl": "line", "name": "Fallen Squire", "fallback": "hollow_infantry",
         "footsteps": "mail",
         "look": "Gaunt pale squire in dented tarnished mail and a torn crimson tabard with a defaced sun emblem, open-faced helm, arming sword and a small buckler, hollow glowing gold eyes.",
         "basic": melee("Arming Sword", "A sword stroke at its current target."),
         "pool": [cone("fallen_oath_strike", "Broken Oath", "A telegraphed sweeping stroke.", 1.8, PROFANE, cd=9, cast=1.0, radius=300, angle=90, buff="npc_profane", cue="npc.profane.start"),
                  charge("fallen_shield_rush", "Buckler Rush", "Marks a line and rushes along it behind its buckler.", 1.5, IRON, cd=13, buff="npc_sundered", cue="npc.cleave.start"),
                  wall("fallen_last_stand", "Last Stand", "Below 40% health it braces: 40% less damage for 5s.", threshold=0.4, mag=0.4, buff="npc_profane", cue="npc.ward.start")],
         "poolDraw": 2},
        {"id": "dread_knight", "slot": "bruiser", "tmpl": "bruiser", "name": "Dread Knight", "fallback": "ironbound_bruiser",
         "look": "Tall knight in blackened fluted full plate with spikes on the pauldrons, a tattered crimson cloak, a horned great-helm with a glowing gold visor slit, a notched two-handed greatsword trailing dark smoke.",
         "basic": melee("Greatsword", "A heavy greatsword stroke at its current target."),
         "pool": [cone("fallen_dark_cleave", "Dark Cleave", "Winds up a smoking greatsword cleave in a cone.", 2.4, PROFANE, cd=9, cast=1.1, radius=360, angle=85, buff="npc_profane", cue="npc.cleave.start"),
                  charge("fallen_deathcharge", "Deathcharge", "Marks a line and charges through it, hitting everyone in the line.", 2.0, PROFANE, cd=14, buff="npc_sundered", cue="npc.cleave.start"),
                  enrage("fallen_unholy_frenzy", "Unholy Frenzy", "At 35% health: +35% damage and faster attacks.", mag=0.35, threshold=0.35, buff="npc_profane", cue="npc.profane.start"),
                  pull("fallen_chain_grasp", "Chains of Penance", "Marks a line and hurls a chain that drags a champion to it.", 1.1, PROFANE, cd=16, cast=1.0, length=850, buff="npc_profane", cue="npc.profane.start")],
         "poolDraw": 3},
        {"id": "oathbreaker_templar", "slot": "tank", "tmpl": "tank", "name": "Oathbreaker Templar", "fallback": "hollow_shieldbearer",
         "footsteps": "plate_heavy",
         "look": "Massive templar in heavy gothic plate gone green with corrosion, a huge tower shield bearing a cracked sun relic that leaks sickly gold light, a flanged mace, a tall plumed helm with a skull-like faceplate.",
         "basic": melee("Relic Bash", "A relic-shield bash at its current target."),
         "pool": [provoke("fallen_judgment_taunt", "Judgment", "Condemns nearby champions: for 6s they deal 35% less damage to anything but the Templar.", buff="npc_profane", cue="npc.profane.start"),
                  wall("fallen_consecrated_wall", "Profane Aegis", "Below 50% health: 50% less damage for 6s.", buff="npc_profane", cue="npc.ward.start"),
                  guard("fallen_martyrs_oath", "Martyr's Oath", "Guards the most injured nearby ally for 8s, taking 40% of its damage.", buff="npc_profane", cue="npc.ward.start"),
                  ring("fallen_consecrate", "Profane Consecration", "Marks a ring; the ground around it burns with corrupt light for 5s.", 0.0, PROFANE, cd=14, cast=1.2, radius=340, dps=12, duration=5, buff="npc_profane", cue="npc.profane.start")],
         "poolDraw": 3},
        {"id": "blighted_chaplain", "slot": "caster", "tmpl": "caster", "name": "Blighted Chaplain", "fallback": "blight_caster",
         "footsteps": "cloth",
         "look": "Hunched robed chaplain in stained ivory and crimson vestments, a swinging censer on a chain pouring green-gold smoke, a mitre-like hood hiding a withered face with glowing eyes, prayer beads of teeth.",
         "basic": bolt("Profane Smite", "Interruptible cast. A bolt of corrupted light."),
         "pool": [circle("fallen_profane_light", "Profane Light", "Marks a circle under its target, then corrupt light sears it.", 2.0, PROFANE, cd=10, cast=1.2, radius=210, buff="npc_profane", cue="npc.profane.start"),
                  heal("fallen_dark_absolution", "Dark Absolution", "Interruptible 2s cast that restores 20% health to the most injured ally below 60%.", buff="npc_profane", cue="npc.heal.cast"),
                  circle("fallen_censer_smoke", "Censer Smoke", "Marks a circle; choking incense silences everyone inside for 2s.", 0.8, PROFANE, cd=14, cast=1.2, radius=230, silence=2.0, buff="npc_silenced", cue="npc.silence.start"),
                  rally("fallen_hymn_of_wrath", "Hymn of Wrath", "Chants: nearby monsters gain +25% damage and attack speed for 8s.", buff="npc_profane", cue="npc.profane.start")],
         "poolDraw": 3},
        {"id": "fallen_inquisitor_crossbow", "slot": "ranged", "tmpl": "ranged", "name": "Crossbow Inquisitor", "fallback": "barbed_hunter",
         "footsteps": "leather",
         "look": "Lean inquisitor in a wide-brimmed hat and long dark leather coat over a breastplate, a heavy repeating crossbow, bandolier of silver bolts, a sun pendant cracked in half, pale face with glowing gold eyes.",
         "basic": shot("Silver Bolt", "Short aim, then fires a silver bolt in a fixed direction."),
         "pool": [circle("fallen_purging_bolts", "Purging Bolts", "Marks a circle on its target, then a burst of bolts lands there.", 1.9, PROFANE, cd=10, cast=1.0, radius=200, buff="npc_profane", cue="npc.profane.start"),
                  circle("fallen_shackle_bolt", "Shackle Bolt", "Marks a small circle; chained bolts root everyone inside for 1.5s.", 0.9, IRON, cd=14, cast=0.9, radius=170, root=1.5, buff="npc_rooted", cue="npc.root.start"),
                  disengage("fallen_tactical_retreat", "Tactical Retreat", "Leaps away when a champion reaches melee range.", buff="npc_profane", cue="npc.profane.start"),
                  circle("fallen_condemn", "Condemn", "Marks a circle; the condemned are silenced for 2s.", 0.8, PROFANE, cd=15, cast=1.1, radius=200, silence=2.0, buff="npc_silenced", cue="npc.silence.start")],
         "poolDraw": 3},
        {"id": "flagellant", "slot": "special", "tmpl": "swarm", "name": "Flagellant", "fallback": "hollow_infantry",
         "footsteps": "cloth",
         "look": "Emaciated bare-chested zealot in a hooded sackcloth robe, back covered in lash scars, a barbed flail in each hand, rope belt with prayer scrolls, wild fanatical stance.",
         "basic": melee("Barbed Flail", "A frenzied flail lash at its current target.", 150),
         "pool": [enrage("fallen_zealous_frenzy", "Zealous Frenzy", "At 50% health it frenzies: +30% damage and faster attacks.", mag=0.3, threshold=0.5, buff="npc_profane", cue="npc.profane.start"),
                  charge("fallen_zealous_leap", "Zealous Leap", "Marks a short line and leaps along it.", 1.4, PROFANE, cd=11, cast=0.7, length=650, minr=260, rng=650, buff="npc_profane", cue="npc.profane.start"),
                  ring("fallen_self_mortify", "Mortification", "Marks a ring and lashes wildly, hitting everyone around it.", 1.4, BLOOD, cd=11, cast=1.0, radius=260, buff="npc_feral", cue="npc.cleave.start")],
         "poolDraw": 2},
    ],
    "bosses": [
        {"id": "fallen_high_inquisitor", "slot": "warlord", "tmpl": "warlord_caster", "name": "High Inquisitor Maledict", "fallback": "blight_caster",
         "footsteps": "cloth",
         "look": "Tall gaunt high inquisitor in layered crimson and black robes with gold filigree, a towering mitre, chains wrapped around his arms ending in hooks, a staff topped with a burning sun relic leaking green-gold fire, a burning book chained at his hip.",
         "basic": bolt("Verdict", "Interruptible cast. A lance of corrupted light.", cast=1.2, rng=700),
         "pool": [pull("fallen_chains_of_judgment", "Chains of Judgment", "Chains mark a line to the farthest champion and drag them to him.", 1.2, PROFANE, cd=15, cast=1.2, length=1200, rng=1200, targeting="farthest", buff="npc_profane", cue="npc.profane.start"),
                  ring("fallen_mass_silence", "Anathema", "Marks a large ring; everyone inside is silenced for 3s.", 1.2, PROFANE, cd=18, cast=1.6, radius=520, silence=3.0, buff="npc_silenced", cue="npc.silence.start"),
                  pool("fallen_pyre", "Heretic's Pyre", "Marks a wide circle; a pyre of corrupt fire burns there for 6s.", 17, 6, PROFANE, cd=13, cast=1.4, radius=320, rng=700, buff="npc_profane", cue="npc.fire.start"),
                  summon("fallen_summon_flagellants", "Call the Penitent", "Interruptible cast: three Flagellants rush to his side.", "flagellant", 3, PROFANE, cd=24, buff="npc_profane", cue="npc.summon.cast"),
                  enrage("fallen_inquisitor_zeal", "Righteous Fury", "At 30% health: +40% damage and faster casting until killed.", buff="npc_profane", cue="npc.profane.start", core=True)]},
        {"id": "fallen_crusader", "slot": "colossus", "tmpl": "colossus", "name": "The Hollow Crusader", "fallback": "hollow_siegebreaker",
         "footsteps": "plate_heavy",
         "look": "Giant armoured crusader three times a man's height, ornate tarnished gold-and-steel plate with a crumbling sun relief on the breastplate, an empty helm with gold fire inside, a greatsword taller than a man, a torn banner on its back.",
         "basic": melee("Crusader's Blade", "A massive greatsword stroke at its current target.", 210),
         "pool": [cone("fallen_crusader_cleave", "Sundering Verdict", "Winds up a wide frontal cleave. Only the tank should stand in front.", 2.4, PROFANE, cd=9, cast=1.3, radius=440, angle=115, buff="npc_sundered", cue="npc.cleave.start"),
                  charge("fallen_crusade", "Crusade", "Marks a line to the farthest champion and charges through it.", 1.9, PROFANE, cd=15, cast=1.2, length=1250, width=220, rng=1200, minr=300, targeting="farthest", buff="npc_sundered", cue="npc.cleave.start"),
                  ring("fallen_judgment_slam", "Judgment Slam", "Marks a ring around itself, then slams, knocking everyone inside back.", 1.7, PROFANE, cd=11, cast=1.3, radius=420, knockback=500, buff="npc_profane", cue="npc.profane.start"),
                  wall("fallen_crusader_aegis", "Aegis of Ruin", "Below 50% health: 40% less damage for 6s.", mag=0.4, buff="npc_profane", cue="npc.ward.start"),
                  enrage("fallen_crusader_wrath", "Fallen Wrath", "At 30% health: +40% damage and faster attacks until killed.", buff="npc_profane", cue="npc.roar.cast", core=True)]},
    ],
})

# ------------------------------------------------------------------------------------------ VOIDBORN
RACES.append({
    "id": "voidborn", "name": "The Voidborn", "short": "Voidborn",
    "lore": "Things from between the stars that crawl through every rift the Breach tears open. They unmake light, sound and flesh.",
    "origin": {"playerRaces": ["spirit", "human"], "profiles": ["whisp", "keeper_of_light", "summoner", "wizard"],
               "note": "What the Lantern Whisp and the Keeper of the Light hold back, and what the Rift Summoner and the Cinder Arcanist sometimes let through."},
    "palette": {"base": [0.12, 0.06, 0.18], "accent": [0.75, 0.20, 0.85], "secondary": [0.05, 0.05, 0.10], "glow": [0.60, 0.30, 1.00],
                "variants": [{"name": "Riftblack", "base": [0.12, 0.06, 0.18], "accent": [0.75, 0.20, 0.85]},
                             {"name": "Starfall", "base": [0.10, 0.12, 0.30], "accent": [0.40, 0.85, 1.00]},
                             {"name": "Null Pale", "base": [0.70, 0.68, 0.75], "accent": [0.55, 0.10, 0.60]}]},
    "footsteps": "whisp", "footstepPitch": 0.85, "ambienceCue": "amb.race.drowned", "voiceCue": "voice.race.growl",
    "units": [
        {"id": "rift_stalker", "slot": "line", "tmpl": "skirmisher", "name": "Rift Stalker", "fallback": "hollow_infantry",
         "footsteps": "leather",
         "look": "Lanky shadow humanoid with elongated limbs, skin like a starry night sky with drifting violet specks, a featureless head split by a vertical glowing magenta slit, long scythe-like finger blades, crouched flickering stance.",
         "basic": melee("Void Talons", "A slash of void talons at its current target.", 160),
         "pool": [charge("void_phase_strike", "Phase Strike", "Marks a line and phases through it, slashing everyone in the path.", 1.6, VOID, cd=11, cast=0.7, length=750, buff="npc_void", cue="npc.void.start"),
                  cone("void_rend", "Void Rend", "A telegraphed rend that slows everyone hit for 3s.", 1.7, VOID, cd=9, cast=0.9, radius=300, angle=85, slow=3, buff="npc_void", cue="npc.void.start"),
                  disengage("void_blink", "Blink", "Blinks away when a champion reaches melee range.", cd=10, buff="npc_void", cue="npc.void.start")],
         "poolDraw": 2},
        {"id": "void_ravager", "slot": "bruiser", "tmpl": "bruiser", "name": "Void Ravager", "fallback": "ironbound_bruiser",
         "look": "Hulking aberration with chitinous black-violet plates, four arms (two ending in crushing mandible claws), a head that is a gaping ring of teeth around a glowing void, cracks of starlight across its body.",
         "basic": melee("Mandible Claws", "A crushing claw strike at its current target."),
         "pool": [ring("void_gravity_slam", "Gravity Slam", "Marks a ring and slams, knocking everyone inside away.", 1.7, VOID, cd=11, radius=360, knockback=450, buff="npc_void", cue="npc.void.cast"),
                  charge("void_rift_charge", "Rift Charge", "Marks a line and tears through it, hitting everyone in the line.", 2.0, VOID, cd=14, buff="npc_void", cue="npc.void.start"),
                  cone("void_null_cleave", "Null Cleave", "Winds up a cleave that silences everyone hit for 1.5s.", 2.0, VOID, cd=11, cast=1.1, radius=340, angle=80, silence=1.5, buff="npc_silenced", cue="npc.void.start"),
                  enrage("void_unravel", "Unravel", "At 35% health it unravels: +35% damage and faster attacks.", mag=0.35, threshold=0.35, buff="npc_void", cue="npc.void.cast")],
         "poolDraw": 3},
        {"id": "null_warden", "slot": "tank", "tmpl": "tank", "name": "Null Warden", "fallback": "hollow_shieldbearer",
         "footsteps": "golem",
         "look": "Tall armoured aberration whose body is a hollow shell of obsidian plates around a swirling black hole core, a round shield made of a floating ring of dark stone, a single huge eye on its chest.",
         "basic": melee("Obsidian Bash", "A shield bash at its current target."),
         "pool": [provoke("void_gaze", "Void Gaze", "Its eye opens: for 6s nearby champions deal 35% less damage to anything but the Warden.", buff="npc_void", cue="npc.void.cast"),
                  wall("void_event_horizon", "Event Horizon", "Below 50% health it bends light around itself: 50% less damage for 6s.", buff="npc_void", cue="npc.ward.start"),
                  guard("void_tether", "Void Tether", "Tethers the most injured ally, taking 40% of its damage for 8s.", buff="npc_void", cue="npc.void.start"),
                  ring("void_gravity_well", "Gravity Well", "Marks a ring; the gravity well roots everyone inside for 1.5s.", 1.1, VOID, cd=14, radius=360, root=1.5, buff="npc_rooted", cue="npc.void.cast")],
         "poolDraw": 3},
        {"id": "rift_weaver", "slot": "caster", "tmpl": "caster", "name": "Rift Weaver", "fallback": "blight_caster",
         "footsteps": "cloth",
         "look": "Floating robed figure with no face under its hood, only a swirling rift, six thin arms weaving threads of violet light, tattered robes that fade into smoke, glowing glyphs orbiting its head.",
         "basic": bolt("Void Bolt", "Interruptible cast. A bolt of void energy."),
         "pool": [pool("void_rift", "Void Rift", "Tears a rift under its target that drains life for 5s.", 14, 5, VOID, cd=12, buff="npc_void", cue="npc.void.cast"),
                  circle("void_mind_spike", "Mind Spike", "Marks a circle; champions inside are silenced for 2s.", 0.9, VOID, cd=14, cast=1.2, radius=220, silence=2.0, buff="npc_silenced", cue="npc.mind.cast"),
                  cone("void_unmaking", "Unmaking", "Unleashes a cone of unmaking in front of it.", 2.0, VOID, cd=11, cast=1.2, radius=480, angle=55, buff="npc_void", cue="npc.void.cast"),
                  heal("void_rift_mending", "Rift Mending", "Interruptible 2s cast that restores 20% health to the most injured ally below 60%.", buff="npc_void", cue="npc.heal.cast")],
         "poolDraw": 3},
        {"id": "rift_gazer", "slot": "ranged", "tmpl": "ranged", "name": "Rift Gazer", "fallback": "barbed_hunter",
         "footsteps": "whisp",
         "look": "Floating orb-like aberration with one giant central eye and a crown of smaller eyes on writhing stalks, a leathery violet body, trailing tentacles beneath, glowing beam charging in the main eye.",
         "basic": shot("Eye Beam", "Short aim, then fires an eye beam in a fixed direction."),
         "pool": [cone("void_disintegrate", "Disintegrate", "Charges its eye, then fires a long narrow disintegration beam.", 2.1, VOID, cd=12, cast=1.3, radius=750, angle=12, buff="npc_void", cue="npc.void.cast"),
                  circle("void_orb", "Void Orb", "Marks a circle on its target, then a void orb detonates there.", 1.8, VOID, cd=10, cast=1.0, radius=210, buff="npc_void", cue="npc.void.start"),
                  disengage("void_warp", "Warp", "Warps away when a champion reaches melee range.", buff="npc_void", cue="npc.void.start"),
                  circle("void_paralyze_gaze", "Paralyzing Gaze", "Marks a circle; champions inside are paralysed (rooted) for 1.5s.", 0.8, VOID, cd=15, cast=1.1, radius=200, root=1.5, buff="npc_mind", cue="npc.mind.cast")],
         "poolDraw": 3},
        {"id": "voidling", "slot": "special", "tmpl": "swarm", "name": "Voidling", "fallback": "hollow_infantry",
         "footsteps": "whisp",
         "look": "Small scuttling void creature the size of a dog, a round body of black chitin with starlight cracks, many thin legs, a wide mouth of needle teeth, two pinprick magenta eyes (fallback: small hunched two-legged imp).",
         "basic": melee("Needle Bite", "A needle-toothed bite at its current target.", 150),
         "pool": [ring("void_voidling_burst", "Void Burst", "Marks a ring and bursts with void energy, hitting everyone around it.", 1.4, VOID, cd=12, cast=1.0, radius=260, buff="npc_void", cue="npc.void.start"),
                  pull("void_latch", "Latch", "Marks a line and latches onto a distant champion, dragging them in.", 0.8, VOID, cd=16, cast=1.0, length=750, minr=260, rng=750, buff="npc_void", cue="npc.void.start"),
                  enrage("void_voidling_frenzy", "Hunger", "At 50% health it frenzies: +30% damage and faster attacks.", mag=0.3, threshold=0.5, buff="npc_void", cue="npc.void.cast")],
         "poolDraw": 2},
    ],
    "bosses": [
        {"id": "voidborn_herald", "slot": "warlord", "tmpl": "warlord_caster", "name": "The Rift Herald", "fallback": "blight_caster",
         "footsteps": "whisp",
         "look": "Towering floating herald of the void, a tall robed silhouette with a halo of shattered black glass, a face that is a spiral galaxy, long trailing sleeves that become tentacles of starlight, a staff that is a floating tear in reality.",
         "basic": bolt("Word of Unmaking", "Interruptible cast. A bolt of void in a fixed direction.", cast=1.2, rng=700),
         "pool": [summon("void_open_the_rift", "Open the Rift", "Interruptible cast: three Voidlings crawl out of a rift beside it.", "voidling", 3, VOID, cd=24, buff="npc_void", cue="npc.summon.cast"),
                  ring("void_silence_of_stars", "Silence of the Stars", "Marks a large ring; everyone inside is silenced for 3s.", 1.3, VOID, cd=18, cast=1.6, radius=520, silence=3.0, buff="npc_silenced", cue="npc.void.cast"),
                  pool("void_collapsing_star", "Collapsing Star", "Marks a wide circle; a collapsing star burns there for 6s.", 17, 6, VOID, cd=13, cast=1.4, radius=320, rng=700, buff="npc_void", cue="npc.void.cast"),
                  pull("void_herald_grasp", "Grasp of the Void", "A tendril marks a line to the farthest champion and drags them in.", 1.2, VOID, cd=15, cast=1.2, length=1200, rng=1200, targeting="farthest", buff="npc_void", cue="npc.void.start"),
                  enrage("void_herald_ascension", "Ascension", "At 30% health: +40% damage and faster casting until killed.", buff="npc_void", cue="npc.void.cast", core=True)]},
        {"id": "voidborn_devourer", "slot": "colossus", "tmpl": "colossus", "name": "Devourer of Stars", "fallback": "hollow_siegebreaker",
         "footsteps": "behemoth",
         "look": "Colossal bipedal void leviathan, a hunched titan of black chitin and exposed starry void, a head that splits open into four mandibles around a black hole mouth, long arms ending in claws, rings of floating debris orbiting its body.",
         "basic": melee("Titan Claw", "A crushing claw blow at its current target.", 200),
         "pool": [pull("void_devour", "Devour", "Marks a line to the farthest champion and drags them into its maw.", 1.8, VOID, cd=17, cast=1.4, length=1200, width=220, rng=1200, targeting="farthest", buff="npc_void", cue="npc.void.cast"),
                  ring("void_singularity", "Singularity", "Marks a ring around itself; everyone inside is rooted by gravity for 2s.", 1.4, VOID, cd=15, cast=1.5, radius=480, root=2.0, buff="npc_rooted", cue="npc.void.cast"),
                  cone("void_breath", "Void Breath", "Winds up a long cone of void breath. Get out of the cone.", 2.4, VOID, cd=10, cast=1.4, radius=560, angle=60, buff="npc_void", cue="npc.void.cast"),
                  ring("void_titan_slam", "Titan Slam", "Marks a ring and slams, knocking everyone inside back.", 1.7, VOID, cd=11, cast=1.3, radius=420, knockback=520, buff="npc_sundered", cue="npc.cleave.start"),
                  enrage("void_devourer_hunger", "Endless Hunger", "At 30% health: +40% damage and faster attacks until killed.", buff="npc_void", cue="npc.void.cast", core=True)]},
    ],
})

# ================================================================================================ build
SLOTS = ["line", "bruiser", "tank", "caster", "ranged", "special"]
BOSS_SLOTS = ["warlord", "colossus"]
KIND_FOR_RIDERS = {"cone", "targetCircle", "selfCircle", "charge", "pull"}


def fix_ring_pools(a):
    """ring(..., dps=, duration=) authoring sugar -> damagePerSecond/duration on a selfCircle."""
    if "dps" in a:
        a["damagePerSecond"] = a.pop("dps")
        a["damageMultiplier"] = 0
    return a


def unit_archetype(race: dict, u: dict) -> dict:
    t = copy.deepcopy(STATS[u["tmpl"]])
    fallback = u.get("fallback") or FALLBACK[u["slot"]]
    arch = {"displayName": u["name"], "role": t.pop("role"), "classification": t.pop("classification", "normal")}
    for k in ("tuningKind", "healthMultiplier", "damage", "eliteDamageMultiplier", "moveSpeed", "armor", "attackRange",
              "attackInterval", "preferredRange", "kiteRange", "scale", "leakCost", "healthScale", "damageScale"):
        if k in t:
            arch[k] = t[k]
    for k in ("scale", "moveSpeed", "healthScale", "damageScale"):
        if k in u:
            arch[k] = u[k]
    arch["mesh"] = {"slot": f"race_{race['id']}_{u['slot']}", "path": "", "tint": race["palette"]["base"]}
    arch["props"] = copy.deepcopy(PROPS.get(fallback, []))
    basic = copy.deepcopy(u["basic"])
    arch["abilities"] = [basic] + [fix_ring_pools(copy.deepcopy(a)) for a in u["pool"]]
    return arch


def build_races_json() -> dict:
    out = {"schemaVersion": 1,
           "notes": "Monster races (Docs/Races.md), authored by Tools/AuthorRaces.py - edit the script, not this file. Every unit is an NPC archetype (NPCArchetypes.json schema) plus race data; 'extends' units keep their NPCArchetypes.json definition and add pool abilities. slot: line|bruiser|tank|caster|ranged|special (+ bosses warlord|colossus). fallback names the existing Tripo body drawn (reskinned with the race palette) until Content/Data/RaceMeshes.tripo.json provides real art. skillPool = every non-basic ability; each match draws poolDraw of them per unit type (seeded) and waves unlock them (Waves.json skillProgression). Ability riders: root/silence/slow seconds, knockback cm; kinds pull and summon are race additions. buff = BuffVisuals.json id, cue = AudioCues.json id.",
           "ranks": RANKS, "raceOrder": [r["id"] for r in RACES], "races": {}}
    for race in RACES:
        r = {k: race[k] for k in ("name", "short", "lore", "origin", "palette", "footsteps", "ambienceCue", "voiceCue")}
        r["slots"], r["bosses"], r["units"] = {}, {}, {}
        for u in race["units"] + race["bosses"]:
            slot = u["slot"]
            (r["bosses"] if slot in BOSS_SLOTS else r["slots"])[slot] = u["id"]
            entry = {"slot": slot, "look": u["look"], "poolDraw": u.get("poolDraw", 99)}
            if u.get("extends"):
                entry["extends"] = True
                entry["fallback"] = u["id"]
                entry["abilities"] = [fix_ring_pools(copy.deepcopy(a)) for a in u["pool"]]
                if "laneBossHealthMultiplier" in u:
                    entry["laneBossHealthMultiplier"] = u["laneBossHealthMultiplier"]
            else:
                entry["fallback"] = u.get("fallback") or FALLBACK[slot]
                entry["archetype"] = unit_archetype(race, u)
            r["units"][u["id"]] = entry
        assert list(r["slots"].keys()) == SLOTS or sorted(r["slots"]) == sorted(SLOTS), race["id"]
        assert sorted(r["bosses"]) == sorted(BOSS_SLOTS), race["id"]
        out["races"][race["id"]] = r
    return out


# ------------------------------------------------------------------------------------------ doc
def rgb_hex(c):
    return "#%02x%02x%02x" % tuple(int(max(0, min(1, v)) * 255) for v in c[:3])


RIDER_WORDS = [("root", "roots {}s"), ("silence", "silences {}s"), ("slow", "slows {}s"), ("knockback", "knockback {}cm")]


def ability_line(a: dict) -> str:
    bits = [a["type"]]
    if a.get("castTime"):
        bits.append(f"{a['castTime']}s cast")
    if a.get("cooldown"):
        bits.append(f"{a['cooldown']:g}s cd")
    for key, word in RIDER_WORDS:
        if a.get(key):
            bits.append(word.format(a[key]))
    if a.get("summon"):
        bits.append(f"summons {a.get('count', 1)}x {a['summon']}")
    if a.get("core"):
        bits.append("always in the kit")
    return f"`{a['id']}` **{a['name']}** ({', '.join(bits)}): {a['description']}"


def race_section(race: dict) -> str:
    p = race["palette"]
    lines = [f"## {race['name']} (`{race['id']}`)", "", f"*{race['lore']}*", "",
             f"- **Origin (player counterpart):** {race['origin']['note']} Player races: {', '.join(race['origin']['playerRaces'])}; profiles: {', '.join('`'+x+'`' for x in race['origin']['profiles'])}.",
             f"- **Palette:** base {rgb_hex(p['base'])} `{p['base']}`, accent {rgb_hex(p['accent'])} `{p['accent']}`, secondary {rgb_hex(p['secondary'])}, glow {rgb_hex(p['glow'])}.",
             "- **Reskin sets (palette variants):** " + "; ".join(f"{v['name']} (base {rgb_hex(v['base'])}, accent {rgb_hex(v['accent'])})" for v in p["variants"]) + ".",
             f"- **Footsteps:** `{race['footsteps']}` (per-unit overrides below). **Ambience cue:** `{race['ambienceCue']}`. **Voice cue:** `{race['voiceCue']}`.",
             "", "| Slot | Unit id | Name | Role | Fallback body | Draw per match |", "|---|---|---|---|---|---|"]
    for u in race["units"] + race["bosses"]:
        role = "(existing)" if u.get("extends") else STATS[u["tmpl"]]["role"] + (" boss" if u["slot"] in BOSS_SLOTS else "")
        fb = u["id"] if u.get("extends") else (u.get("fallback") or FALLBACK[u["slot"]])
        draw = "all" if u["slot"] in BOSS_SLOTS else str(u.get("poolDraw", "all"))
        lines.append(f"| {u['slot']} | `{u['id']}` | {u['name']} | {role} | `{fb}` | {draw} |")
    lines.append("")
    for u in race["units"] + race["bosses"]:
        lines.append(f"### {u['name']} (`{u['id']}`, {u['slot']})")
        lines.append("")
        lines.append(f"**Tripo art prompt:** {u['look']} Dark-fantasy, stylised PBR, readable silhouette, T-pose, no base.")
        lines.append("")
        if not u.get("extends"):
            lines.append(f"- Basic: **{u['basic']['name']}** ({u['basic']['type']}): {u['basic']['description']}")
        lines.append("- Skill pool" + (" (added to its existing kit)" if u.get("extends") else "") + ":")
        for a in u["pool"]:
            lines.append(f"  - {ability_line(fix_ring_pools(copy.deepcopy(a)))}")
        lines.append("")
    return "\n".join(lines)


DOC_HEAD = """# Monster races: the race bible, rank colours and skill progression

monster-races (branch `feat/monster-races`). Nine monster races, each with six unit types and two bosses. This file is
generated with `Content/Data/Races.json` by `Tools/AuthorRaces.py` (edit the script, then run it). The Tripo art
agent reads the **Tripo art prompt** lines; they describe the model to generate for each unit id.

## How it fits together

| Piece | Where |
|---|---|
| Race, unit, boss and skill-pool data | `Content/Data/Races.json` (generated), loaded by `CireRaces` and merged into the NPC archetype database |
| Real art per unit (overlay, by priority) | `Content/Data/RaceMeshes.tripo.json` (tripo-races agent), same shape as `NPCMeshes.tripo.json`'s `archetypes` object: `{ "archetypes": { "<unit id>": { "mesh", "meshScale", "heightCm", "yaw", "variant", "animations": {...}, "alternates": [...] } } }` |
| Fallback bodies | Until a unit has real art, it draws its `fallback` archetype's Tripo body (`NPCMeshes.tripo.json`) reskinned with the race palette through `M_CireMonsterSkin` |
| Rank colours | `Races.json` `ranks` (table below); applied by `CireRaces::ApplySkin` |
| Wave race, rotation and skill schedule | `Content/Data/Waves.json` `campaign` and `skillProgression` (`Docs/Waves.md`) |

Body priority for a unit id: `RaceMeshes.tripo.json` > `NPCMeshes.tripo.json` > its `fallback` archetype's body > the tinted mannequin.

## Slots

Every race fills the same six unit slots and two boss slots, so a wave can be written once by slot and played by any race:
`line` (the basic melee soldier), `bruiser` (heavy melee), `tank`, `caster`, `ranged`, `special` (swarm, skirmisher or support),
`warlord` (a boss that commands: rallies, summons, pulls) and `colossus` (a huge boss that smashes). Wave rows written with a
slot (`"slot": "caster"`) resolve to the wave's race; rows with an explicit `archetype` keep that unit.

## Roles

`bruiser`, `tank`, `caster`, `ranged` as before, plus **`support`** (keeps range like a caster; heals, wards, rallies and
silences; healer role icon) and **`swarm`** (small, fast, fragile melee; comes in numbers; bruiser behaviour, 0.55x health,
0.65x damage, 0.8x size).

## Rank colours

WoW-style quality tiers. The same body, reskinned per race palette and per rank, must read as a different unit:
the race palette recolours the body; the rank recolours the armour (metallic mask of the Tripo PBR set), adds an emissive
trim glow on the armour, a fresnel rim in the rank colour, a small body-wide tint and a size bump. Nameplates colour the
name by rank and the target frame uses the rank colour for its border and header. Neutral packs stay yellow.

| Rank | Colour | Health | Damage | Size | Extra skills | Classification | Look |
|---|---|---|---|---|---|---|---|
"""

DOC_RANK_TAIL = """
Classification mapping: the old `Normal / Elite / Boss` classification maps onto ranks (Normal -> normal, Elite -> elite,
Boss -> warlord), so every existing caller keeps working. Wave rows choose a rank (`"rank": "champion"`); the legacy
`"elite": true` flag is rank elite. Challenge-pack members are elite (tier 1-2) or champion (tier 3+); pack leaders are warlords.
From `campaign.promotions` cycles on, some normal wave units are promoted (veteran, elite, champion) and lane bosses become
mythic from `campaign.mythicBossFromCycle`.

## Skill progression and per-match randomisation

- **No monster skills in the first waves.** Before `skillProgression.firstSkillWave` (global wave number, default 4: waves 1-3
  of cycle 1) every monster, bosses included, uses its basic attack only.
- **Unlocks.** From that wave a normal unit has 1 skill, +1 every `unlockEveryWaves` waves (default 3), up to `maxSkills` (3).
  Ranks add `skillBonus` (elite +1, champion +1, warlord +2, mythic +3), capped by the unit's pool.
- **Stronger versions.** Skill tier I from the first skill wave, II and III every `tierEveryWaves` waves (default 5), up to
  `maxTier` (3). Each tier above I: +20% ability damage, -10% cooldown, +15% control duration (root, silence, slow). The
  ability name gets its tier numeral ("Tidal Slam II").
- **Per-match draw.** Each match picks a seed (`-CireRaceSeed=N` fixes it). For every unit type the pool is shuffled with that
  seed; normal and veteran units only ever use the first `poolDraw` skills of the shuffled pool, higher ranks can reach the
  rest. So a race plays differently match to match, but every skill is on theme. Abilities marked "always in the kit"
  (boss enrages) are not drawn: they join once skills unlock.
- **Replication.** The seed is on `ACireGameState::MonsterSkillSeed`; each monster's active skills, tier, rank, race and
  palette are replicated on `UCireNPCState` (`Loadout`, `SkillTier`, `Rank`, `PaletteIndex`). The target frame's ability
  list shows only the active skills with their tier.

## Engine additions for race skills

Riders on telegraphed skills (applied to champions inside the telegraph when it lands): `root` (cannot move; `npc_rooted`),
`silence` (cannot cast skills; `npc_silenced`), `slow` (the existing slow) and `knockback` (cm, away from the caster or the
circle centre). New kinds: `pull` (a line telegraph to the victim, or the farthest champion; champions in the line are dragged
to the caster) and `summon` (interruptible cast; `count` units of `summon` join the caster's wave, at most 2x count alive).
Every race skill names a themed visual (`buff`, BuffVisuals.json) shown on champions it hits and an audio cue (`cue`).

## Races at a glance

| Race | Player counterpart | line | bruiser | tank | caster | ranged | special | warlord | colossus |
|---|---|---|---|---|---|---|---|---|---|
"""

DOC_PLAYER_RACES = """
## Player races (ChampionRoster.json `race`)

| Champion profile | Player race | Monster counterpart |
|---|---|---|
"""

PLAYER_RACE = {
    "knight": ("human", "hollow, fallen_order"), "ranger": ("human", "hollow"), "scholar": ("human", "hollow, drowned_deep"),
    "lancer": ("human", "hollow"), "summoner": ("human", "voidborn, drowned_deep"), "bear": ("beast", "feral_kin"),
    "paladin_righteous": ("human", "fallen_order"), "paladin_holy": ("human", "fallen_order"), "dwarf_miner": ("dwarf", "stoneborn"),
    "ether_golem_tank": ("ether-construct", "stoneborn"), "ether_golem_support": ("ether-construct", "stoneborn, blightwood"),
    "ether_golem_bruiser": ("ether-construct", "stoneborn"), "orc_chieftain": ("orc", "ironhide"),
    "totemic_behemoth": ("beast", "feral_kin"), "drakish_footman": ("drakkari", "drakkari"), "wizard": ("human", "voidborn"),
    "troll_berserker_melee": ("troll", "ironhide"), "troll_berserker_ranged": ("troll", "ironhide"), "dryad": ("sylvan", "blightwood"),
    "whisp": ("spirit", "voidborn"), "evergrove_centaur": ("centaur", "blightwood, feral_kin"), "keeper_of_light": ("human", "voidborn, fallen_order"),
}


def build_doc(races_json: dict) -> str:
    out = [DOC_HEAD.rstrip(chr(10))]
    for rid, r in RANKS.items():
        extra = f" + trim {rgb_hex(r['trim'])}" if "trim" in r else ""
        out.append(f"| {r['label']} | {rgb_hex(r['color'])}{extra} `{r['color']}` | x{r['health']:g} | x{r['damage']:g} | x{r['size']:g} | +{r['skillBonus']} | {r['classification']} | {r['note']} |")
    out.append(DOC_RANK_TAIL.rstrip(chr(10)))
    for race in RACES:
        rj = races_json["races"][race["id"]]
        s, b = rj["slots"], rj["bosses"]
        out.append(f"| {race['name']} | {', '.join(race['origin']['profiles'])} | " + " | ".join(f"`{s[k]}`" for k in SLOTS) + " | " + " | ".join(f"`{b[k]}`" for k in BOSS_SLOTS) + " |")
    out.append(DOC_PLAYER_RACES.rstrip(chr(10)))
    for pid, (prace, counterpart) in PLAYER_RACE.items():
        out.append(f"| `{pid}` | {prace} | {counterpart} |")
    out.append("")
    # Eric's favourites first: the art agent generates these before the others.
    order = ["drowned_deep", "blightwood", "hollow", "ironhide", "drakkari", "stoneborn", "feral_kin", "fallen_order", "voidborn"]
    by_id = {r["id"]: r for r in RACES}
    for rid in order:
        out.append(race_section(by_id[rid]))
    return "\n".join(out).rstrip() + "\n"


# ------------------------------------------------------------------------------ shared-table sync
def insert_rows(path: Path, anchor: str, rows: dict[str, str], key_fmt: str) -> tuple[str, bool]:
    """Insert missing top-level rows right after the anchor line (keeps the file's formatting)."""
    text = path.read_text(encoding="utf-8")
    # Managed rows are one line each: drop the old copies, then insert the current ones.
    keep = [line for line in text.split("\n") if not any(line.lstrip().startswith(key_fmt.format(k)) for k in rows)]
    text = "\n".join(keep)
    missing = list(rows)
    idx = text.index(anchor) + len(anchor)
    block = "".join(rows[k] for k in missing)
    return text[:idx] + "\n" + block.rstrip("\n").rstrip(",") + ("," if text[idx:].lstrip().startswith('"') else "") + text[idx:], True


def footstep_rows() -> dict[str, str]:
    rows = {}
    for race in RACES:
        for u in race["units"] + race["bosses"]:
            if u.get("extends"):
                continue
            cls = u.get("footsteps", race["footsteps"])
            pitch = race.get("footstepPitch", 1.0)
            if u["tmpl"] in ("swarm",):
                pitch *= 1.15
            if u["slot"] in BOSS_SLOTS:
                pitch *= 0.88
            rows[u["id"]] = f'    "{u["id"]}": {{ "class": "{cls}", "pitch": {pitch:.2f} }},\n'
    return rows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    races_json = build_races_json()
    outputs = {DATA / "Races.json": json.dumps(races_json, indent=2) + "\n", ROOT / "Docs/Races.md": build_doc(races_json)}
    # Shared tables.
    fs_text, _ = insert_rows(DATA / "AudioFootsteps.json", '"monsters": {', footstep_rows(), '"{}":')
    outputs[DATA / "AudioFootsteps.json"] = fs_text
    json.loads(fs_text)
    cue_text = (DATA / "AudioCues.json").read_text(encoding="utf-8")
    new_cues = {k: f'    "{k}": {json.dumps(v)},\n' for k, v in CUES.items()}
    cue_text2, _ = insert_rows(DATA / "AudioCues.json", '"cues": {', new_cues, '"{}":')
    outputs[DATA / "AudioCues.json"] = cue_text2
    vis_rows = {k: f'    "{k}": {json.dumps(v)},\n' for k, v in VISUALS.items()}
    vis_text2, _ = insert_rows(DATA / "BuffVisuals.json", '"buffs": {', vis_rows, '"{}":')
    outputs[DATA / "BuffVisuals.json"] = vis_text2
    for text in (cue_text2, vis_text2):
        json.loads(text)  # must stay valid JSON
    stale = []
    for path, text in outputs.items():
        old = path.read_text(encoding="utf-8") if path.exists() else None
        if old != text:
            stale.append(path)
            if not args.check:
                path.write_text(text, encoding="utf-8", newline="\n")
    # Consistency: every ability buff/cue must exist, summons must name a unit of the same race.
    for race in RACES:
        ids = {u["id"] for u in race["units"] + race["bosses"]}
        for u in race["units"] + race["bosses"]:
            for a in u["pool"]:
                assert a.get("buff") in VISUALS, (u["id"], a["id"], a.get("buff"))
                assert a.get("cue") in CUES, (u["id"], a["id"], a.get("cue"))
                if a["type"] == "summon":
                    assert a["summon"] in ids, (u["id"], a["summon"])
    total_units = sum(len(r["units"]) for r in races_json["races"].values())
    print(f"AUTHOR_RACES races={len(races_json['races'])} units={total_units} " + ("STALE " + ",".join(p.name for p in stale) if stale else "up-to-date"))
    return 1 if (args.check and stale) else 0


if __name__ == "__main__":
    sys.exit(main())
