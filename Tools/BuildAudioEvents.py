"""Build the data-driven sound-event table (Docs/Audio.md "Sound events").

Writes, deterministically:
  Content/Data/AudioEvents.json   event -> cue mapping: elements, kinds x slots, weapons, aliases, one row per
                                  Ability DB entry (element / kind / weapon), outcomes, deaths, reactions, UI
  Content/Data/AudioCues.json     the generated cues (spell.*, weapon.*, impact.*, combat.*, death.*, hit.*, ui_*,
                                  cc.*) with their shipped fallback sounds; hand-written cues are kept as they are.
                                  Every cue listed in Art/Audio/FabAudioMap.json gets a "pack" list (Fab pack
                                  members, preferred at runtime when installed, see Tools/MapFabAudio.py).
  Content/Data/BuffVisuals.json   adds an "end" (expire) cue to buff/CC rows that have none
  Docs/AudioCoverage.md           ability sound coverage report (every DB ability -> sound set; gaps listed)

Fab content is never copied or committed: the map holds only /Game/<Pack>/... object paths, and a clean clone
plays the fallback. Run with any Python 3:
  F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/BuildAudioEvents.py [--check]
--check exits 1 when the committed files differ from what would be written, or an ability has no sound set.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, OrderedDict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "Content" / "Data"
ABILITIES = DATA / "Abilities.json"
EVENTS = DATA / "AudioEvents.json"
CUES = DATA / "AudioCues.json"
BUFFS = DATA / "BuffVisuals.json"
FABMAP = ROOT / "Art" / "Audio" / "FabAudioMap.json"
AUDIO = ROOT / "Content" / "Audio"
REPORT = ROOT / "Docs" / "AudioCoverage.md"

ELEMENTS = ["physical", "fire", "frost", "nature", "shadow", "arcane", "holy", "earth", "water", "lightning"]
SLOTS = ["cast", "channel", "projectile", "impact", "heal"]
# ECireSchool names (CireAbilityShapes::SchoolName) and Ability DB schools -> element.
SCHOOLS = OrderedDict([
    ("steel", "physical"), ("physical", "physical"), ("fire", "fire"), ("frost", "frost"), ("cold", "frost"),
    ("storm", "lightning"), ("lightning", "lightning"), ("shadow", "shadow"), ("void", "shadow"), ("blood", "shadow"),
    ("life", "nature"), ("nature", "nature"), ("poison", "nature"), ("holy", "holy"), ("arcane", "arcane"),
    ("spirit", "arcane"), ("earth", "earth"), ("tide", "water"), ("water", "water"),
])

# ---- shipped fallback sounds (CC0 / CC BY, already under /Game/Audio) ---------------------------------------
# Each entry: (sounds, volume, pitch range). Loops must use waves imported as loops (ProcessReport "loop").
FB = {
    "physical": {"cast": (["CireCombat/S_Swing"], .5, [.95, 1.05]), "impact": (["CireCombat/S_Impact"], .6, [.92, 1.06]),
                 "heal": (["CireCombat/S_Heal"], .45, [.95, 1.05]), "channel": (["Ambience/AMB_WindRolling"], .35, [1.25, 1.35]),
                 "projectile": (["Ambience/AMB_WindRolling"], .3, [1.7, 1.9])},
    "fire": {"cast": (["Auras/AUR_FlameBurst"], .55, [.85, .95]), "impact": (["Auras/AUR_FlameBurst"], .6, [1.05, 1.2]),
             "heal": (["CireCombat/S_Heal"], .45, [.85, .9]), "channel": (["Auras/AUR_FireLoop"], .45, [.95, 1.05]),
             "projectile": (["Auras/AUR_FireLoop"], .35, [1.3, 1.45])},
    "frost": {"cast": (["Auras/AUR_IceCrack"], .55, [1.05, 1.15]), "impact": (["Auras/AUR_IceShatter_{01..03}"], .85, [.95, 1.08]),
              "heal": (["CireCombat/S_Heal"], .45, [1.1, 1.15]), "channel": (["Ambience/AMB_WindHowl"], .35, [1.2, 1.3]),
              "projectile": ([], .4, [1, 1])},
    "nature": {"cast": (["Auras/AUR_Whoosh"], .5, [.8, .9]), "impact": (["CireCombat/S_MagicImpact"], .55, [.8, .88]),
               "heal": (["CireCombat/S_Heal"], .5, [.92, 1.0]), "channel": (["Ambience/AMB_WindChimes"], .4, [.85, .95]),
               "projectile": ([], .4, [1, 1])},
    "shadow": {"cast": (["CireCombat/S_MagicCast"], .5, [.7, .78]), "impact": (["CireCombat/S_MagicImpact"], .6, [.68, .76]),
               "heal": (["CireCombat/S_Heal"], .45, [.7, .75]), "channel": (["Auras/AUR_Heartbeat"], .35, [.7, .75]),
               "projectile": ([], .4, [1, 1])},
    "arcane": {"cast": (["CireCombat/S_MagicCast"], .5, [.97, 1.05]), "impact": (["CireCombat/S_MagicImpact"], .6, [.97, 1.05]),
               "heal": (["CireCombat/S_Heal"], .45, [1.0, 1.06]), "channel": (["SFX/SFX_TeleportChannel"], .4, [.95, 1.05]),
               "projectile": (["SFX/SFX_TeleportChannel"], .3, [1.45, 1.6])},
    "holy": {"cast": (["Auras/AUR_Chime"], .5, [.97, 1.05]), "impact": (["CireCombat/S_MagicImpact"], .55, [1.2, 1.3]),
             "heal": (["CireCombat/S_Heal"], .55, [1.0, 1.06]), "channel": (["SFX/SFX_TeleportChannel"], .4, [1.2, 1.3]),
             "projectile": ([], .4, [1, 1])},
    "earth": {"cast": (["Footsteps/FS_Golem_{01..05}"], .55, [.7, .8]), "impact": (["Footsteps/FS_Golem_{01..05}"], .7, [.6, .72]),
              "heal": (["CireCombat/S_Heal"], .45, [.8, .85]), "channel": (["Arenas/AMB_ArenaCanyonWind"], .4, [.55, .65]),
              "projectile": (["Arenas/AMB_ArenaCanyonWind"], .3, [.9, 1.0])},
    "water": {"cast": (["Auras/AUR_Bubble"], .55, [.85, .95]), "impact": (["Auras/AUR_Bubble"], .6, [.7, .8]),
              "heal": (["Auras/AUR_Bubble"], .5, [1.15, 1.25]), "channel": (["Auras/AUR_BubblesLoop"], .4, [.9, 1.0]),
              "projectile": (["Auras/AUR_BubblesLoop"], .3, [1.3, 1.4])},
    "lightning": {"cast": (["Auras/AUR_Sparkle"], .55, [.6, .7]), "impact": (["CireCombat/S_Critical"], .55, [1.3, 1.45]),
                  "heal": (["CireCombat/S_Heal"], .45, [1.2, 1.3]), "channel": (["Arenas/AMB_ArenaStationDrone"], .4, [1.5, 1.7]),
                  "projectile": (["Arenas/AMB_ArenaStationDrone"], .3, [1.9, 2.1])},
}

# weapon -> {slot: (cue, fallback sounds, volume, pitch)}; kind; ranged form beyond a distance.
W = "weapon."
WEAPONS = OrderedDict([
    ("sword", {"kind": "melee", "slots": {"swing": ("sword.swing", ["CireCombat/S_Swing"], .5, [1.0, 1.1]),
                                          "impact": ("sword.impact", ["CireCombat/S_Impact"], .6, [1.02, 1.12])}}),
    ("axe", {"kind": "melee", "ranged": "thrown_axe", "rangedAbove": 420,
             "slots": {"swing": ("axe.swing", ["CireCombat/S_Swing"], .55, [.82, .9]),
                       "impact": ("axe.impact", ["CireCombat/S_Impact"], .8, [.84, .92])}}),
    ("thrown_axe", {"kind": "shot", "slots": {"draw": ("axe.swing", None, 0, None), "release": ("axe.throw", ["Auras/AUR_Whoosh"], .5, [1.1, 1.2]),
                    "projectile": ("axe.spin", ["Ambience/AMB_WindRolling"], .3, [1.9, 2.1]), "impact": ("axe.impact", None, 0, None)}}),
    ("mace", {"kind": "melee", "slots": {"swing": ("mace.swing", ["CireCombat/S_Swing"], .55, [.75, .82]),
                                         "impact": ("mace.impact", ["CireCombat/S_Impact"], .9, [.72, .8])}}),
    ("dagger", {"kind": "melee", "slots": {"swing": ("dagger.swing", ["CireCombat/S_Swing"], .45, [1.3, 1.42]),
                                           "impact": ("dagger.impact", ["CireCombat/S_Impact"], .5, [1.3, 1.4])}}),
    ("glaive", {"kind": "shot", "slots": {"swing": ("glaive.swing", ["CireCombat/S_Swing"], .5, [1.12, 1.2]),
                                          "draw": ("glaive.swing", None, 0, None),
                                          "release": ("glaive.throw", ["Auras/AUR_Whoosh"], .55, [1.2, 1.3]),
                                          "projectile": ("glaive.spin", ["Ambience/AMB_WindRolling"], .3, [2.0, 2.2]),
                                          "impact": ("glaive.impact", ["CireCombat/S_Impact"], .6, [1.15, 1.25])}}),
    ("spear", {"kind": "melee", "ranged": "thrown_spear", "rangedAbove": 420,
               "slots": {"swing": ("spear.thrust", ["CireCombat/S_Lance"], .5, [1.0, 1.08]),
                         "impact": ("spear.impact", ["CireCombat/S_Impact"], .6, [.95, 1.02])}}),
    ("thrown_spear", {"kind": "shot", "slots": {"draw": ("spear.thrust", None, 0, None), "release": ("spear.throw", ["CireCombat/S_Lance"], .55, [.9, .98]),
                      "projectile": ("arrow.flight", None, 0, None), "impact": ("spear.impact", None, 0, None)}}),
    ("claws", {"kind": "melee", "slots": {"swing": ("claws.swipe", ["Auras/AUR_Swing"], .5, [.8, .9]),
                                          "impact": ("claws.impact", ["Auras/AUR_BloodSplat_{01..03}"], .55, [.85, .95])}}),
    ("staff", {"kind": "melee", "slots": {"swing": ("staff.swing", ["CireCombat/S_Swing"], .45, [.9, .95]),
                                          "impact": ("staff.impact", ["CireCombat/S_Impact"], .55, [.88, .95])}}),
    ("bow", {"kind": "shot", "slots": {"draw": ("bow.draw", ["Footsteps/FS_LeatherCreak_{01..04}"], .5, [1.1, 1.25]),
                                       "release": ("bow.release", ["CireCombat/S_Bow"], .55, [.97, 1.05]),
                                       "swing": ("bow.release", None, 0, None),
                                       "projectile": ("arrow.flight", [], .35, [1, 1]),
                                       "impact": ("bow.impact", ["CireCombat/S_Impact"], .55, [1.18, 1.3])}}),
    ("crossbow", {"kind": "shot", "slots": {"draw": ("crossbow.draw", ["Footsteps/FS_LeatherCreak_{01..04}"], .55, [.8, .9]),
                                            "release": ("crossbow.release", ["CireCombat/S_Bow"], .6, [.78, .86]),
                                            "swing": ("crossbow.release", None, 0, None),
                                            "projectile": ("arrow.flight", None, 0, None),
                                            "impact": ("crossbow.impact", ["CireCombat/S_Impact"], .6, [1.05, 1.12])}}),
    ("pistol", {"kind": "shot", "slots": {"draw": ("pistol.cock", ["UI/UI_Click_{01..03}"], .5, [.6, .7]),
                                          "release": ("pistol.shot", ["CireCombat/S_Critical"], .75, [.55, .62]),
                                          "swing": ("pistol.shot", None, 0, None),
                                          "impact": ("bullet.impact", ["CireCombat/S_Impact"], .6, [1.35, 1.5])}}),
    ("gunblade", {"kind": "melee", "ranged": "pistol", "rangedAbove": 320,
                  "slots": {"swing": ("gunblade.swing", ["CireCombat/S_Swing"], .55, [1.05, 1.15]),
                            "impact": ("gunblade.impact", ["CireCombat/S_Impact"], .65, [1.05, 1.12])}}),
    ("blunderbuss", {"kind": "shot", "slots": {"draw": ("pistol.cock", None, 0, None),
                                               "release": ("blunderbuss.shot", ["CireCombat/S_Critical"], .8, [.42, .48]),
                                               "swing": ("blunderbuss.shot", None, 0, None),
                                               "impact": ("bullet.impact", None, 0, None)}}),
    ("shield", {"kind": "melee", "slots": {"swing": ("shield.bash", ["Auras/AUR_Swing"], .5, [.8, .85]),
                                           "impact": ("shield.impact", ["Auras/AUR_Clang_{01..03}"], .6, [.7, .78])}}),
])

ALIASES = OrderedDict([
    ("sword", "sword"), ("sword_strike", "sword"), ("basic_sword", "sword"),
    ("axes", "axe"), ("axe", "axe"), ("axes_strike", "axe"), ("thrown_axe", "thrown_axe"),
    ("flail", "mace"), ("flail_strike", "mace"), ("mace", "mace"), ("totem", "mace"), ("totem_strike", "mace"),
    ("dagger", "dagger"), ("dagger_strike", "dagger"),
    ("claws", "claws"), ("claws_strike", "claws"),
    ("staff", "staff"), ("staff_strike", "staff"),
    ("lance", "spear"), ("lance_strike", "spear"), ("basic_lance", "spear"), ("thrown_lance", "thrown_spear"),
    ("bow", "bow"), ("bow_shot", "bow"), ("basic_bow", "bow"), ("arrow", "bow"), ("basic_arrow", "bow"), ("bow_strike", "bow"),
    ("crossbow", "crossbow"), ("bolt", "crossbow"), ("crossbow_shot", "crossbow"),
    ("gunblade", "gunblade"), ("gunblade_strike", "gunblade"), ("falchion_slash", "gunblade"),
    ("pistol_shot", "pistol"), ("pistol", "pistol"),
    ("glaive", "glaive"), ("glaive_strike", "glaive"),
    ("blunderbuss", "blunderbuss"), ("blunderbuss_strike", "blunderbuss"), ("arcane_shot", "blunderbuss"),
    ("shield", "shield"), ("npc_wall_strike", "mace"),
])

KINDS = OrderedDict([
    ("spell", {"cast": "spell.{element}.cast", "launch": "spell.{element}.cast", "projectile": "spell.{element}.projectile",
               "impact": "spell.{element}.impact", "critical": "spell.{element}.impact", "channel": "spell.{element}.channel",
               "area": "spell.{element}.channel", "wall": "ability.wall", "protection": "ability.barrier"}),
    ("heal", {"cast": "spell.{element}.cast", "launch": "spell.{element}.cast", "projectile": "spell.{element}.projectile",
              "impact": "spell.{element}.heal", "critical": "spell.{element}.heal", "channel": "spell.{element}.channel",
              "area": "spell.{element}.channel", "wall": "ability.wall", "protection": "ability.barrier"}),
    ("buff", {"cast": "spell.{element}.cast", "launch": "spell.{element}.cast", "projectile": "spell.{element}.projectile",
              "impact": "spell.{element}.heal", "critical": "spell.{element}.heal", "channel": "spell.{element}.channel",
              "area": "spell.{element}.channel", "wall": "ability.wall", "protection": "ability.barrier"}),
    ("guard", {"cast": "ability.guard", "launch": "ability.guard", "impact": "ability.guard", "critical": "ability.guard",
               "channel": "spell.{element}.channel", "area": "spell.{element}.channel", "protection": "ability.barrier"}),
    ("shout", {"cast": "ability.shout", "launch": "ability.shout", "impact": "weapon:impact", "critical": "weapon:impact",
               "channel": "spell.{element}.channel", "area": "spell.{element}.channel"}),
    ("melee", {"cast": "weapon:swing", "launch": "weapon:swing", "projectile": "weapon:projectile", "impact": "weapon:impact",
               "critical": "weapon:impact", "channel": "spell.{element}.channel", "area": "spell.{element}.channel",
               "wall": "ability.wall", "protection": "ability.barrier"}),
    # Shot abilities fire on cast (instant); a timed cast plays the draw / cock at its start ("channel" one-shot).
    ("shot", {"cast": "weapon:release", "launch": "weapon:release", "projectile": "weapon:projectile", "impact": "weapon:impact",
              "critical": "weapon:impact", "channel": "weapon:draw", "area": "spell.{element}.channel"}),
    ("summon", {"cast": "ability.summon", "launch": "spell.{element}.cast", "projectile": "spell.{element}.projectile",
                "impact": "spell.{element}.impact", "critical": "spell.{element}.impact", "channel": "spell.{element}.channel",
                "area": "spell.{element}.channel", "wall": "ability.wall", "protection": "ability.barrier"}),
    ("passive", {"cast": "spell.{element}.cast", "launch": "spell.{element}.cast", "projectile": "spell.{element}.projectile",
                 "impact": "spell.{element}.impact", "critical": "spell.{element}.impact", "channel": "spell.{element}.channel",
                 "area": "spell.{element}.channel"}),
])

# Hand decisions the heuristics cannot make (name/description based); element defaults to the DB school.
OVERRIDES = {
    "shield_slam": {"kind": "melee", "weapon": "shield"},
    "war_cry": {"kind": "shout"}, "challenge_of_iron": {"kind": "shout"}, "taunting_tumble": {"kind": "shout"},
    "bear_roar": {"kind": "shout"}, "chieftain_earthshout": {"kind": "shout"}, "repulsor_pulse": {"kind": "spell"},
    "iron_guard": {"kind": "guard"}, "warding_talisman": {"kind": "guard"}, "drakish_scale_guard": {"kind": "guard"},
    "shield_tumble": {"kind": "guard"}, "stone_skin": {"kind": "guard"}, "evasive_stance": {"kind": "buff"},
    "cleaving_strike": {"kind": "melee"}, "decimating_strike": {"kind": "melee"}, "executioners_verdict": {"kind": "melee"},
    "blade_flurry": {"kind": "melee", "weapon": "gunblade"}, "tumble_strike": {"kind": "melee"},
    "shadow_step": {"kind": "melee", "weapon": "dagger"}, "spectral_blade": {"kind": "melee", "weapon": "sword"},
    "piercing_shot": {"kind": "shot", "weapon": "bow"}, "silver_shot": {"kind": "shot", "weapon": "pistol"},
    "collect_the_bounty": {"kind": "shot", "weapon": "pistol", "cues": {"cast": "weapon.pistol.heavy", "launch": "weapon.pistol.heavy"}}, "arcane_blunderbuss": {"kind": "shot", "weapon": "blunderbuss"},
    "powder_flask": {"kind": "spell", "cues": {"impact": "spell.explosion", "critical": "spell.explosion"}},
    "cataclysm": {"kind": "spell", "cues": {"impact": "spell.explosion", "critical": "spell.explosion"}},
    "skitter_swarm": {"kind": "summon", "cues": {"impact": "spell.explosion_small"}},
    "arc_mine": {"kind": "summon", "cues": {"impact": "spell.explosion_small"}}, "hunters_stride": {"kind": "buff"},
    "bouncing_glaive": {"kind": "shot", "weapon": "glaive"}, "crescent_volley": {"kind": "shot", "weapon": "glaive"},
    "glaive_storm": {"kind": "melee", "weapon": "glaive"}, "moon_glaive": {"kind": "shot", "weapon": "glaive"},
    "sabercat_pounce": {"kind": "melee", "weapon": "claws"}, "sabercat_rake": {"kind": "melee", "weapon": "claws"},
    "bear_maul": {"kind": "melee", "weapon": "claws"}, "bear_charge": {"kind": "melee", "weapon": "claws"},
    "bear_colossus": {"kind": "shout"}, "bear_hibernate": {"kind": "heal"}, "bear_ancient_hide": {"kind": "guard"},
    "troll_axe_frenzy": {"kind": "melee", "weapon": "axe"}, "troll_blood_leap": {"kind": "melee", "weapon": "axe"},
    "troll_twin_throw": {"kind": "shot", "weapon": "thrown_axe"}, "troll_returning_axes": {"kind": "shot", "weapon": "thrown_axe"},
    "troll_red_moon": {"kind": "shout"}, "troll_hunger": {"kind": "passive"},
    "chieftain_axe_hook": {"kind": "melee", "weapon": "axe"}, "chieftain_banner": {"kind": "summon"},
    "miner_pickfall": {"kind": "melee", "weapon": "axe"}, "golem_granite_fist": {"kind": "melee", "weapon": "mace"},
    "golem_fel_fist": {"kind": "melee", "weapon": "mace"}, "behemoth_totem_sweep": {"kind": "melee", "weapon": "mace"},
    "behemoth_tusk_line": {"kind": "melee", "weapon": "mace"}, "behemoth_totem_bulwark": {"kind": "summon"},
    "paladin_righteous_flail": {"kind": "melee", "weapon": "mace"}, "paladin_holy_flail": {"kind": "melee", "weapon": "mace"},
    "paladin_relic_vow": {"kind": "buff"}, "drakish_wing_rebuke": {"kind": "melee", "weapon": "sword"},
    "drakish_dragon_oath": {"kind": "buff"}, "drakish_ancient_pact": {"kind": "buff"},
    "centaur_grove_javelin": {"kind": "shot", "weapon": "thrown_spear"},
    "summoned_wall": {"kind": "summon"}, "protection_dome": {"kind": "summon"}, "oathbound_guardian": {"kind": "summon"},
    "spectral_pack": {"kind": "summon"}, "spectral_hunt": {"kind": "summon"}, "centaur_herd_call": {"kind": "summon"},
    "photon_turret": {"kind": "summon"}, "warp_obelisk": {"kind": "summon"}, "skitter_swarm": {"kind": "summon"},
    "mine_layer": {"kind": "summon", "cues": {"impact": "spell.explosion_small"}}, "spirit_lantern": {"kind": "summon"},
    "disruption_pylon": {"kind": "summon"}, "aegis_pylon": {"kind": "summon"}, "haste_pylon": {"kind": "summon"},
    "gravity_pylon": {"kind": "summon"}, "aether_nexus": {"kind": "summon"}, "stasis_snare": {"kind": "summon"},
    "miner_lantern": {"kind": "summon"}, "keeper_lantern_ward": {"kind": "summon"}, "keeper_beacon": {"kind": "summon"},
    "miner_mountain": {"kind": "summon"}, "overcharge": {"kind": "buff"}, "moonlit_sprint": {"kind": "buff"},
    "owl_scout": {"kind": "spell"}, "hex_mark": {"kind": "spell"}, "witchfinders_mark": {"kind": "spell"},
    "second_wind": {"kind": "heal"}, "last_stand": {"kind": "heal"}, "bastion_of_dawn": {"kind": "heal"},
    "shadow_dance": {"kind": "buff"}, "venom_tumble": {"kind": "buff"}, "golem_ether_furnace": {"kind": "buff"},
    "golem_worldstone": {"kind": "buff"}, "golem_ether_anchor": {"kind": "spell"},
}

# Basic attacks, monster melee and other presentation ids that are not Ability DB entries.
EXTRA_IDS = OrderedDict([
    ("basic_attack", {"element": "physical", "kind": "melee", "weapon": "caster"}),
    ("npc_melee", {"element": "physical", "kind": "melee", "weapon": "caster"}),
    ("npc_interrupted", {"element": "arcane", "kind": "spell", "cues": {"impact": "combat.interrupt"}}),
    ("arcane", {"element": "arcane", "kind": "spell"}), ("basic_arcane", {"element": "arcane", "kind": "spell"}),
    ("arcane_bolt", {"element": "arcane", "kind": "spell"}), ("arcane_strike", {"element": "arcane", "kind": "spell"}),
    ("void_rift", {"element": "shadow", "kind": "spell"}),
    ("fire", {"element": "fire", "kind": "spell"}), ("frost", {"element": "frost", "kind": "spell"}),
    ("shadow", {"element": "shadow", "kind": "spell"}), ("poison", {"element": "nature", "kind": "spell"}),
    ("holy", {"element": "holy", "kind": "spell"}), ("storm", {"element": "lightning", "kind": "spell"}),
    ("nature", {"element": "nature", "kind": "spell"}), ("tide", {"element": "water", "kind": "spell"}),
    ("void", {"element": "shadow", "kind": "spell"}),
])

SHOT_WORDS = ("shot", "volley", "javelin", "throw")
SUMMON_WORDS = ("summon", "turret", "pylon", "obelisk", "guardian", "totem", "banner", "ward", "mine", "snare", "lantern", "nexus")


def classify(aid: str, a: dict) -> dict:
    element = SCHOOLS.get(a.get("school", "physical"), "physical")
    o = OVERRIDES.get(aid, {})
    if "kind" in o:
        kind = o["kind"]
    elif a.get("kind") == "passive":
        kind = "passive"
    else:
        effects = {e.get("type") for e in a.get("effects", [])}
        label = (a.get("effectLabel") or "").lower()
        text = (aid + " " + a.get("name", "")).lower()
        if a.get("targeting") == "self" and effects & {"guard", "shield"} and "damage" not in label and "heal" not in label:
            kind = "guard"
        elif "taunt" in effects and a.get("targeting") == "self":
            kind = "shout"
        elif "heal" in label or "health" in label and "per second" in label or a.get("targeting") == "ally":
            kind = "heal"
        elif any(w in text for w in SUMMON_WORDS):
            kind = "summon"
        elif element == "physical" and any(w in text for w in SHOT_WORDS):
            kind = "shot"
        elif element == "physical":
            kind = "melee"
        elif a.get("targeting") == "self" and "damage" not in label:
            kind = "buff"
        else:
            kind = "spell"
    row = OrderedDict([("name", a.get("name", aid)), ("element", o.get("element", element)), ("kind", kind)])
    if o.get("weapon"):
        row["weapon"] = o["weapon"]
    elif kind in ("melee", "shot"):
        row["weapon"] = "caster"
    if o.get("cues"):
        row["cues"] = o["cues"]
    return row


# ---- generated cues ------------------------------------------------------------------------------------------
def cue(sounds, volume, pitch, **extra):
    c = OrderedDict([("sounds", list(sounds or [])), ("volume", round(volume, 3))])
    if pitch and pitch != [1, 1]:
        c["pitch"] = pitch
    c.update(extra)
    return c


def generated_cues() -> "OrderedDict[str, dict]":
    out = OrderedDict()
    combat = dict(bus="sfx", combat=True)
    # Spells: 10 elements x cast / channel / projectile / impact / heal.
    for e in ELEMENTS:
        for slot in SLOTS:
            sounds, vol, pitch = FB[e][slot]
            if slot in ("channel", "projectile"):
                out[f"spell.{e}.{slot}"] = cue(sounds, vol, pitch, loop=True, attenuation="loop", maxVoices=4 if slot == "channel" else 6,
                                               priority=8, **combat)
            elif slot == "impact":
                out[f"spell.{e}.{slot}"] = cue(sounds, vol, pitch, cooldown=.03, attenuation="large" if e == "lightning" else "combat",
                                               maxVoices=6, priority=20, **combat)
            elif slot == "heal":
                out[f"spell.{e}.{slot}"] = cue(sounds, vol, pitch, cooldown=.05, attenuation="combat", maxVoices=4, priority=22, **combat)
            else:
                out[f"spell.{e}.{slot}"] = cue(sounds, vol, pitch, cooldown=.04, attenuation="combat", maxVoices=5, priority=15, **combat)
    # Weapons.
    done = set()
    for wid, w in WEAPONS.items():
        for slot, (cid, sounds, vol, pitch) in w["slots"].items():
            name = W + cid
            if sounds is None or name in done:
                continue
            done.add(name)
            if slot == "projectile":
                out[name] = cue(sounds, vol, pitch, loop=True, attenuation="loop", maxVoices=6, priority=8, **combat)
            elif slot == "impact":
                out[name] = cue(sounds, vol, pitch, cooldown=.03, attenuation="combat", maxVoices=6, priority=18, **combat)
            elif cid.endswith(("shot", "release", "throw")):
                out[name] = cue(sounds, vol, pitch, cooldown=.03, attenuation="large" if "shot" in cid else "combat", maxVoices=5, priority=16, **combat)
            else:
                out[name] = cue(sounds, vol, pitch, cooldown=.03, attenuation="close", maxVoices=6, priority=10, **combat)
    out[W + "arrow.flight"] = cue([], .35, None, loop=True, attenuation="loop", maxVoices=6, priority=8, **combat)
    out[W + "pistol.heavy"] = cue(["CireCombat/S_Critical"], .85, [.45, .5], cooldown=.05, attenuation="large", maxVoices=3, priority=30, **combat)
    out["spell.explosion"] = cue(["Auras/AUR_FlameBurst"], .9, [.6, .68], cooldown=.05, attenuation="large", maxVoices=3, priority=35,
                                 duck=.25, duckSeconds=.5, **combat)
    out["spell.explosion_small"] = cue(["Auras/AUR_FlameBurst"], .7, [.85, .95], cooldown=.05, attenuation="combat", maxVoices=4, priority=22, **combat)
    # Body layers under physical hits, by the target's armour class.
    out["impact.armor"] = cue(["Footsteps/FS_ArmorRattle_{01..08}"], .45, [.95, 1.1], cooldown=.03, attenuation="combat", maxVoices=5, priority=12, **combat)
    out["impact.flesh"] = cue(["Footsteps/FS_Thump_{01..05}"], .45, [1.1, 1.25], cooldown=.03, attenuation="combat", maxVoices=5, priority=12, **combat)
    out["impact.stone"] = cue(["Footsteps/FS_Golem_{01..05}"], .5, [.95, 1.05], cooldown=.03, attenuation="combat", maxVoices=4, priority=12, **combat)
    out["impact.wood"] = cue(["Ambience/AMB_Chop_01"], .45, [1.1, 1.25], cooldown=.04, attenuation="combat", maxVoices=4, priority=12, **combat)
    # Outcomes and abilities.
    out["combat.block"] = cue(["Auras/AUR_Clang_{01..03}"], .85, [.92, 1.02], cooldown=.02, attenuation="combat", maxVoices=4, priority=60,
                              with_=["impact.armor"], **combat)
    out["combat.deflect"] = cue(["Auras/AUR_Clang_{01..03}"], .75, [1.25, 1.4], cooldown=.02, attenuation="combat", maxVoices=4, priority=55,
                                with_=["combat.whiz"], **combat)
    out["combat.whiz"] = cue(["Auras/AUR_Whoosh"], .35, [1.5, 1.7], cooldown=.05, attenuation="close", maxVoices=3, priority=20, **combat)
    out["combat.dodge"] = cue(["Auras/AUR_Whoosh"], .45, [1.15, 1.3], cooldown=.08, attenuation="close", maxVoices=3, priority=25, **combat)
    out["combat.miss"] = cue(["Auras/AUR_Swing"], .4, [1.2, 1.35], cooldown=.08, attenuation="close", maxVoices=3, priority=15, **combat)
    out["combat.resist"] = cue(["Auras/AUR_ForceField"], .4, [1.3, 1.4], cooldown=.1, attenuation="close", maxVoices=2, priority=20, **combat)
    out["combat.crit"] = cue(["CireCombat/S_Critical"], .55, [.95, 1.05], cooldown=.03, attenuation="combat", maxVoices=4, priority=30, **combat)
    out["combat.interrupt"] = cue(["Auras/AUR_Gong"], .5, [1.35, 1.45], cooldown=.2, attenuation="combat", maxVoices=2, priority=35, **combat)
    out["ability.guard"] = cue(["Auras/AUR_Clang_{01..03}"], .55, [.78, .86], cooldown=.1, attenuation="combat", maxVoices=3, priority=25,
                               with_=["impact.armor"], **combat)
    out["ability.shout"] = cue(["Auras/AUR_WarCry"], .65, [.9, 1.0], cooldown=.3, attenuation="large", maxVoices=2, priority=30, bus="voice", combat=True)
    out["ability.summon"] = cue(["SFX/SFX_TeleportArrive"], .5, [.85, .95], cooldown=.15, attenuation="combat", maxVoices=3, priority=22, **combat)
    out["ability.wall"] = cue(["Footsteps/FS_Golem_{01..05}"], .7, [.55, .62], cooldown=.2, attenuation="combat", maxVoices=2, priority=25, **combat)
    out["ability.barrier"] = cue(["Auras/AUR_ForceField"], .6, [.95, 1.05], cooldown=.2, attenuation="combat", maxVoices=2, priority=25, **combat)
    # Hit reactions (the local player) and deaths.
    out["hit.player"] = cue(["Footsteps/FS_Thump_{01..05}"], .6, [1.0, 1.15], cooldown=.07, priority=70, bus="voice", combat=True)
    out["hit.player_heavy"] = cue(["CireCombat/S_Impact"], .85, [.72, .8], cooldown=.25, priority=75, bus="voice", combat=True,
                                  duck=.35, duckSeconds=.6, with_=["hit.player"])
    out["death.hero"] = cue(["Footsteps/FS_Thump_{01..05}"], .8, [.7, .78], cooldown=.1, attenuation="large", maxVoices=3, priority=45,
                            with_=["impact.armor"], bus="voice", combat=True)
    out["death.player"] = cue(["Auras/AUR_Gong"], .75, [.58, .62], cooldown=2.0, priority=95, bus="sfx", duck=.7, duckSeconds=3.0, with_=["sting.death"])
    out["death.humanoid"] = cue(["Footsteps/FS_Thump_{01..05}"], .6, [.8, .92], cooldown=.05, attenuation="combat", maxVoices=4, priority=14,
                                with_=["impact.armor"], bus="voice", combat=True)
    out["death.creature"] = cue(["Auras/AUR_Snarl"], .6, [.7, .8], cooldown=.08, attenuation="combat", maxVoices=3, priority=14, bus="voice", combat=True)
    out["death.golem"] = cue(["Footsteps/FS_Golem_{01..05}"], .75, [.5, .58], cooldown=.1, attenuation="large", maxVoices=3, priority=16, **combat)
    out["death.ethereal"] = cue(["Footsteps/FS_Shimmer_{01..02}"], .6, [.7, .8], cooldown=.1, attenuation="combat", maxVoices=2, priority=14, **combat)
    out["death.boss"] = cue(["SFX/SFX_PackLeaderRoar"], 1.0, [.72, .78], cooldown=2.0, attenuation="large", priority=80, bus="voice", combat=True,
                            duck=.55, duckSeconds=2.5)
    # Crowd control and expiry (BuffVisuals.json "end").
    out["cc.polymorph.start"] = cue(["Auras/AUR_Bubble"], .7, [1.6, 1.75], cooldown=.1, attenuation="combat", maxVoices=3, priority=35,
                                    with_=["cc.polymorph.sparkle"], **combat)
    out["cc.polymorph.sparkle"] = cue(["Auras/AUR_Sparkle"], .5, [1.3, 1.45], cooldown=.1, attenuation="combat", maxVoices=3, priority=20, **combat)
    out["cc.polymorph.end"] = cue(["Auras/AUR_Bubble"], .55, [1.1, 1.2], cooldown=.1, attenuation="combat", maxVoices=3, priority=25, **combat)
    out["cc.stun.end"] = cue(["Auras/AUR_Whoosh"], .35, [1.4, 1.5], cooldown=.15, attenuation="close", maxVoices=2, priority=12, **combat)
    out["cc.silence.end"] = cue(["Auras/AUR_Chime"], .3, [1.4, 1.5], cooldown=.15, attenuation="close", maxVoices=2, priority=12, **combat)
    out["cc.root.end"] = cue(["Ambience/AMB_Chop_01"], .35, [1.3, 1.4], cooldown=.15, attenuation="close", maxVoices=2, priority=12, **combat)
    out["cc.slow.end"] = cue(["Auras/AUR_IceShatter_{01..03}"], .3, [1.4, 1.5], cooldown=.15, attenuation="close", maxVoices=2, priority=12, **combat)
    out["buff.expire"] = cue(["Auras/AUR_ForceField"], .28, [.78, .84], cooldown=.2, attenuation="close", maxVoices=2, priority=10, **combat)
    out["debuff.expire"] = cue(["Auras/AUR_Whoosh"], .25, [1.25, 1.35], cooldown=.2, attenuation="close", maxVoices=2, priority=10, **combat)
    # UI (one consistent kit: every menu routes here).
    ui = dict(bus="ui", concurrency="UI")
    out["ui_tab"] = cue(["UI/UI_Click_{01..03}"], .45, [1.15, 1.22], cooldown=.05, **ui)
    out["ui_ready"] = cue(["UI/UI_Confirm"], .7, [1.0, 1.03], cooldown=.2, with_=["ui_ready_bell"], **ui)
    out["ui_ready_bell"] = cue(["Ambience/AMB_Bell_{01..03}"], .25, [1.45, 1.55], cooldown=.2, bus="ui")
    out["ui_unready"] = cue(["UI/UI_Close"], .5, [.95, 1.0], cooldown=.2, **ui)
    out["ui_error"] = cue(["/Game/UI/Shop/S_ShopError"], .6, [.98, 1.02], cooldown=.25, **ui)
    out["ui_error_mana"] = cue(["/Game/UI/Shop/S_ShopError"], .55, [.8, .84], cooldown=.35, **ui)
    out["ui_error_gold"] = cue(["SFX/SFX_CoinsSell"], .45, [.7, .75], cooldown=.35, with_=["ui_error"], **ui)
    out["ui_target"] = cue(["/Game/UI/WowUI/Sounds/S_TargetSelect"], .55, [.98, 1.03], cooldown=.06, **ui)
    out["ui_threat"] = cue(["/Game/UI/WowUI/Sounds/S_ThreatWarning"], .7, None, cooldown=1.0, **ui)
    out["ui_aggro_lost"] = cue(["/Game/UI/WowUI/Sounds/S_AggroLost"], .7, None, cooldown=1.0, **ui)
    out["ui_skill_buy"] = cue(["UI/UI_Confirm"], .6, [1.05, 1.1], cooldown=.1, with_=["coins_buy"], **ui)
    out["ui_skill_offer"] = cue(["/Game/UI/Draft/Sounds/S_SkillOffer"], .6, None, cooldown=.3, **ui)
    out["ui_skill_learned"] = cue(["/Game/UI/Draft/Sounds/S_SkillLearned"], .85, None, cooldown=.3, **ui)
    out["ui_skill_hover"] = cue(["UI/UI_Hover_{01..04}"], .3, [1.05, 1.1], cooldown=.05, **ui)
    out["ui_undo"] = cue(["UI/UI_Close"], .5, [1.1, 1.15], cooldown=.1, **ui)
    out["loot_common"] = cue(["SFX/SFX_LootPickup"], .65, [.98, 1.04], cooldown=.1, **ui)
    out["loot_magic"] = cue(["SFX/SFX_LootPickup"], .75, [1.05, 1.1], cooldown=.1, with_=["loot_shine"], **ui)
    out["loot_rare"] = cue(["SFX/SFX_LootPickup"], .8, [1.08, 1.12], cooldown=.1, with_=["loot_shine", "coins_buy"], **ui)
    out["loot_epic"] = cue(["SFX/SFX_TeleportArrive"], .8, [1.0, 1.05], cooldown=.2, with_=["loot_choir"], duck=.3, duckSeconds=1.2, **ui)
    out["loot_shine"] = cue(["Auras/AUR_Sparkle"], .45, [1.15, 1.25], cooldown=.1, bus="ui")
    out["loot_choir"] = cue(["Auras/AUR_Choir"], .45, [1.1, 1.15], cooldown=.3, bus="ui")
    for sting, duck in (("sting.prep", 0), ("sting.wave", .3), ("sting.arena", .35), ("sting.boss", .5), ("sting.challenge", .2),
                        ("sting.cleared", 0), ("sting.levelup", 0), ("sting.death", 0)):
        out[sting] = cue([], .75, None, cooldown=2.0, bus="ui", **({"duck": duck, "duckSeconds": 2.5} if duck else {}))
    out["ui_buy_confirm"] = cue([], .55, None, cooldown=.08, **ui)
    out["ui_sell_confirm"] = cue([], .55, None, cooldown=.08, **ui)
    # tidy the "with_" helper key
    for c in out.values():
        if "with_" in c:
            c["with"] = c.pop("with_")
    return out


# Hand-written cues that gain a layer (pack-only stings/confirms; the shipped horn, bell and coins stay underneath).
HAND_LAYERS = OrderedDict([
    ("banner_prep", ["sting.prep"]), ("banner_wave", ["sting.wave"]), ("banner_arena", ["sting.arena"]), ("banner_boss", ["sting.boss"]),
    ("banner_challenge", ["sting.challenge"]), ("banner_cleared", ["sting.cleared"]), ("level_up", ["sting.levelup"]),
    ("coins_buy", ["ui_buy_confirm"]), ("coins_sell", ["ui_sell_confirm"]),
])

HUD_LEGACY = OrderedDict([("0", "level_up"), ("1", "aggro_taken"), ("2", "ui_threat"), ("3", "ui_aggro_lost"), ("4", "ui_target")])
SHOP_LEGACY = OrderedDict([
    ("S_ShopBuy", "coins_buy"), ("S_ShopSell", "coins_sell"), ("S_LootPickup", "loot_pickup"), ("S_TeleportArrive", "teleport_arrive"),
    ("S_TeleportChannel", ""), ("S_ShopOpen", "ui_open"), ("S_ShopTab", "ui_tab"), ("S_ShopUndo", "ui_undo"), ("S_ShopError", "ui_error"),
    ("S_ShopErrorGold", "ui_error_gold"), ("S_SkillLearn", "ui_skill_buy"),
    ("S_LootCommon", "loot_common"), ("S_LootMagic", "loot_magic"), ("S_LootRare", "loot_rare"), ("S_LootEpic", "loot_epic"),
])
UI_LEGACY = OrderedDict([
    ("/Game/UI/Draft/Sounds/S_SkillOffer", "ui_skill_offer"), ("/Game/UI/Draft/Sounds/S_SkillLearned", "ui_skill_learned"),
    ("/Game/UI/Draft/Sounds/S_SkillHover", "ui_skill_hover"),
])
ATTENUATION = OrderedDict([
    ("close", {"inner": 120, "falloff": 1900, "lowPassAtMax": 3500, "reverb": [.1, .4]}),
    ("combat", {"inner": 220, "falloff": 3400, "lowPassAtMax": 3000, "reverb": [.12, .5]}),
    ("loop", {"inner": 150, "falloff": 2200, "lowPassAtMax": 2600, "reverb": [.1, .4]}),
    ("large", {"inner": 900, "falloff": 9000, "lowPassAtMax": 2600, "reverb": [.25, .7]}),
])


def events_table(abilities: dict) -> OrderedDict:
    rows = OrderedDict((aid, classify(aid, a)) for aid, a in abilities.items())
    weapons = OrderedDict()
    for wid, w in WEAPONS.items():
        d = OrderedDict([("kind", w["kind"])])
        if "ranged" in w:
            d["ranged"] = w["ranged"]; d["rangedAbove"] = w["rangedAbove"]
        for slot, (cid, *_rest) in w["slots"].items():
            d[slot] = W + cid
        weapons[wid] = d
    return OrderedDict([
        ("schemaVersion", 1),
        ("generator", "Tools/BuildAudioEvents.py"),
        ("notes", "Sound-event table (Docs/Audio.md 'Sound events'). Presentation cues resolve ability id -> {element, kind, weapon} -> "
                  "kind slot template -> AudioCues.json cue, plus layers (critical ring; flesh/armour/stone/wood body layer on physical hits "
                  "by the target's armour class). Ids missing here fall back to their school, so nothing is silent. Generated: edit the tool."),
        ("elements", ELEMENTS),
        ("schools", SCHOOLS),
        ("kinds", KINDS),
        ("weapons", weapons),
        ("weaponAliases", ALIASES),
        ("armorLayers", OrderedDict([("plate", "armor"), ("plate_heavy", "armor"), ("mail", "armor"), ("leather", "flesh"), ("cloth", "flesh"),
                                     ("bark", "wood"), ("bear", "flesh"), ("behemoth", "flesh"), ("beast", "flesh"), ("golem", "stone"),
                                     ("hooves", "flesh"), ("whisp", "none")])),
        ("armorWeapons", OrderedDict([("plate", "sword"), ("plate_heavy", "mace"), ("mail", "spear"), ("leather", "axe"), ("cloth", "staff"),
                                      ("bark", "staff"), ("bear", "claws"), ("behemoth", "mace"), ("beast", "claws"), ("golem", "mace"),
                                      ("hooves", "spear"), ("whisp", "staff")])),
        ("layers", OrderedDict([("impact", "impact.{armor}"), ("critical", ["combat.crit"])])),
        ("outcomes", OrderedDict([("block", "combat.block"), ("deflect", "combat.deflect"), ("dodge", "combat.dodge"), ("miss", "combat.miss"),
                                  ("resist", "combat.resist")])),
        ("reactions", OrderedDict([("playerHit", "hit.player"), ("playerHitHeavy", "hit.player_heavy"), ("heavyFraction", .12)])),
        ("deaths", OrderedDict([("hero", "death.hero"), ("player", "death.player"), ("humanoid", "death.humanoid"), ("creature", "death.creature"),
                                ("golem", "death.golem"), ("ethereal", "death.ethereal"), ("boss", "death.boss")])),
        ("deathClasses", OrderedDict([("plate", "humanoid"), ("plate_heavy", "humanoid"), ("mail", "humanoid"), ("leather", "humanoid"),
                                      ("cloth", "humanoid"), ("bark", "golem"), ("bear", "creature"), ("behemoth", "creature"),
                                      ("beast", "creature"), ("golem", "golem"), ("hooves", "creature"), ("whisp", "ethereal")])),
        ("ui", OrderedDict([("ready", "ui_ready"), ("unready", "ui_unready"), ("error", "ui_error"), ("errorMana", "ui_error_mana"),
                            ("errorEnergy", "ui_error_mana"), ("errorGold", "ui_error_gold")])),
        ("mix", OrderedDict([("otherVolume", .8), ("localPriorityBoost", 30), ("incomingPriorityBoost", 20), ("castLoopRadius", 3200),
                             ("deathRadius", 5200), ("areaLoopVolume", .6), ("maxCastLoops", 6)])),
        ("extraIds", EXTRA_IDS),
        ("abilities", rows),
    ])


# ---- JSON writers (one cue per line, like the hand-written file) ------------------------------------------------
def compact(v) -> str:
    return json.dumps(v, ensure_ascii=False, separators=(", ", ": "))


def write_cues(root: OrderedDict) -> str:
    lines = ["{"]
    keys = list(root.keys())
    for i, k in enumerate(keys):
        comma = "," if i < len(keys) - 1 else ""
        v = root[k]
        if k == "cues":
            lines.append('  "cues": {')
            ids = list(v.keys())
            for j, cid in enumerate(ids):
                lines.append(f"    {json.dumps(cid)}: {compact(v[cid])}{',' if j < len(ids) - 1 else ''}")
            lines.append("  }" + comma)
        elif isinstance(v, dict):
            lines.append(f"  {json.dumps(k)}: {{")
            sub = list(v.keys())
            for j, sk in enumerate(sub):
                lines.append(f"    {json.dumps(sk)}: {compact(v[sk])}{',' if j < len(sub) - 1 else ''}")
            lines.append("  }" + comma)
        else:
            lines.append(f"  {json.dumps(k)}: {compact(v)}{comma}")
    lines.append("}")
    return "\n".join(lines) + "\n"


def write_events(root: OrderedDict) -> str:
    out = ["{"]
    keys = list(root.keys())
    for i, k in enumerate(keys):
        comma = "," if i < len(keys) - 1 else ""
        v = root[k]
        if isinstance(v, dict) and k in ("abilities", "extraIds", "kinds", "weapons"):
            out.append(f"  {json.dumps(k)}: {{")
            sub = list(v.keys())
            for j, sk in enumerate(sub):
                out.append(f"    {json.dumps(sk)}: {compact(v[sk])}{',' if j < len(sub) - 1 else ''}")
            out.append("  }" + comma)
        else:
            out.append(f"  {json.dumps(k)}: {compact(v)}{comma}")
    out.append("}")
    return "\n".join(out) + "\n"


# ---- resolution mirror (for the coverage report; the runtime is CireSoundEvents::Resolve) -----------------------
def resolve_slot(ev: dict, row: dict, slot: str, caster_weapon="sword") -> str:
    if slot in (row.get("cues") or {}):
        return row["cues"][slot]
    kind = row["kind"]
    weapon = row.get("weapon") or "caster"
    if weapon == "caster":
        weapon = caster_weapon
    t = ev["kinds"].get(kind, {}).get(slot)
    if not t or t == "none":
        return ""
    if t.startswith("weapon:"):
        w = ev["weapons"].get(weapon) or {}
        return w.get(t[7:]) or ev["weapons"]["sword"].get(t[7:], "")
    return t.replace("{element}", row["element"]).replace("{weapon}", weapon)


def asset_exists(name: str) -> bool:
    if name.startswith("/Game/"):
        rel = name[len("/Game/"):].split(".")[0]
        return (ROOT / "Content" / (rel + ".uasset")).exists()
    return (AUDIO / (name + ".uasset")).exists()


def expand(pattern: str) -> list[str]:
    m = re.search(r"\{(\d+)\.\.(\d+)\}", pattern)
    if not m:
        return [pattern]
    a, b = m.group(1), m.group(2)
    return [pattern[:m.start()] + str(i).zfill(len(a)) + pattern[m.end():] for i in range(int(a), int(b) + 1)]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    db = json.loads(ABILITIES.read_text("utf-8"))
    abilities = db["abilities"]
    ev = events_table(abilities)
    fabmap = json.loads(FABMAP.read_text("utf-8")) if FABMAP.exists() else {"cues": {}}
    pack_cues = {k: v for k, v in (fabmap.get("cues") or {}).items() if not k.startswith("_")}

    cues_root = json.loads(CUES.read_text("utf-8"), object_pairs_hook=OrderedDict)
    cues = cues_root["cues"]
    gen = generated_cues()
    prev_generated = set(cues_root.get("generatedCues", []))
    for cid in list(cues.keys()):
        if cid in prev_generated and cid not in gen:
            del cues[cid]
    for cid, c in gen.items():
        cues[cid] = c
    for cid, layers in HAND_LAYERS.items():
        if cid in cues:
            cues[cid]["with"] = layers
    for cid, c in cues.items():
        c.pop("pack", None)
        paths = pack_cues.get(cid)
        if paths:
            c["pack"] = list(paths)
            extra = (fabmap.get("tuning") or {}).get(cid) or {}
            for k in ("packVolume", "packPitch"):
                if k in extra:
                    c[k] = extra[k]
    new_root = OrderedDict()
    for k, v in cues_root.items():
        if k in ("attenuation", "maxCombatVoices", "generatedCues", "uiLegacy"):
            continue
        new_root[k] = v
        if k == "notes":
            new_root["maxCombatVoices"] = 28
            new_root["attenuation"] = ATTENUATION
    new_root["notes"] = ("Named cues for CireAudio::PlayCue / PlayCue2D / PlayAttached / PlayCueEx (Docs/Audio.md). 'sounds' is the shipped fallback "
                         "(Folder/Name under /Game/Audio, or a full /Game/... path); 'pack' lists Fab pack members (Art/Audio/FabAudioMap.json, "
                         "never committed content) that play instead when installed. {01..04} expands to a numbered set; one member is picked "
                         "at random, never twice in a row. volume/pitch/volumeJitter randomise every play; cooldown drops repeats within that "
                         "many seconds (the local player's own events have their own cooldown); maxVoices caps live copies; 'combat' cues share "
                         "the maxCombatVoices budget ordered by priority; bus/attenuation/concurrency override the sound's own; duck lowers the "
                         "music; 'with' layers other cues. The spell./weapon./impact./combat./death./hit./cc./ui_ cues are generated by "
                         "Tools/BuildAudioEvents.py (listed in generatedCues); aura buff./debuff./stance./item./npc. cues are named by "
                         "Content/Data/BuffVisuals.json.")
    new_root["hudLegacy"] = OrderedDict([("notes", "ACireHUD::PlayWowSound index -> cue. Unlisted indices keep the original synthesized UI sound.")] + list(HUD_LEGACY.items()))
    shop = OrderedDict([("notes", cues_root.get("shopLegacy", {}).get("notes", "CireShopUI sound name -> cue."))])
    shop.update(SHOP_LEGACY)
    new_root["shopLegacy"] = shop
    new_root["uiLegacy"] = OrderedDict([("notes", "Hard-coded UI sound paths (CireSkillOfferHUD) -> cue, so every menu shares one kit.")] + list(UI_LEGACY.items()))
    new_root["generatedCues"] = sorted(gen.keys())
    cues_text = write_cues(new_root)
    events_text = write_events(ev)

    # BuffVisuals: expiry sounds for CC and buffs without an end cue.
    buffs_raw = BUFFS.read_text("utf-8")
    buffs = json.loads(buffs_raw, object_pairs_hook=OrderedDict)
    cc_end = {"stunned": "cc.stun.end", "silenced": "cc.silence.end", "npc_silenced": "cc.silence.end", "witch_mark": "cc.silence.end",
              "npc_rooted": "cc.root.end", "slowed": "cc.slow.end", "polymorphed": "cc.polymorph.end", "banished": "cc.stun.end"}
    cc_start = {"polymorphed": "cc.polymorph.start"}
    new_buffs_text = buffs_raw
    edits = 0
    for bid, row in buffs.get("buffs", {}).items():
        sound = row.get("sound")
        if sound is None or row.get("kind") == "passive":
            continue
        want_end = cc_end.get(bid) or ("debuff.expire" if row.get("kind") == "debuff" else "buff.expire")
        want_start = cc_start.get(bid)
        new_sound = OrderedDict(sound)
        if "end" not in sound:
            new_sound["end"] = want_end
        if want_start and sound.get("start") != want_start:
            new_sound["start"] = want_start
        if new_sound != sound:
            # Rewrite this row's sound block in place, keeping the file's hand formatting everywhere else.
            pat = re.compile(r'("%s"\s*:\s*\{.*?"sound"\s*:\s*)(\{[^{}]*\})' % re.escape(bid), re.S)
            m = pat.search(new_buffs_text)
            if m:
                new_buffs_text = new_buffs_text[:m.start(2)] + compact(new_sound).replace(", ", ", ") + new_buffs_text[m.end(2):]
                edits += 1

    # Coverage report.
    rows = ev["abilities"]
    lines = ["# Ability sound coverage", "",
             "Generated by `Tools/BuildAudioEvents.py` from `Content/Data/Abilities.json` and `Content/Data/AudioEvents.json`.",
             "Every Ability DB entry maps to an element and a kind; the kind picks the cue for each presentation slot",
             "(`CireSoundEvents::Resolve`). A physical hit also layers the target's armour (`impact.armor` / `flesh` / `stone` / `wood`),",
             "a critical adds `combat.crit`, and a melee/shot hit of another element adds that element's impact.",
             "`pack` = the cue has installed Fab pack members listed in `Art/Audio/FabAudioMap.json`; otherwise the shipped fallback plays.", ""]
    missing = []
    gen_all = {**{k: v for k, v in cues.items()}}
    kinds = Counter(r["kind"] for r in rows.values())
    elements = Counter(r["element"] for r in rows.values())
    table = ["| Ability | School | Element | Kind | Weapon | Cast | Impact / heal | Fab pack |", "|---|---|---|---|---|---|---|---|"]
    for aid, r in rows.items():
        cast = resolve_slot(ev, r, "cast")
        hit = resolve_slot(ev, r, "impact")
        ok = bool(cast and cast in gen_all and hit and hit in gen_all)
        for c in (cast, hit):
            if c in gen_all:
                members = [m for s in gen_all[c].get("sounds", []) for m in expand(s)]
                if not members or not all(asset_exists(m) for m in members):
                    ok = False
        if not ok:
            missing.append(aid)
        packed = "yes" if (cast in pack_cues or hit in pack_cues) else "-"
        table.append(f"| `{aid}` | {abilities[aid].get('school')} | {r['element']} | {r['kind']} | {r.get('weapon', '-')} | `{cast}` | `{hit}` | {packed} |")
    covered = len(rows) - len(missing)
    pack_count = sum(1 for aid, r in rows.items() if resolve_slot(ev, r, "cast") in pack_cues or resolve_slot(ev, r, "impact") in pack_cues)
    lines += [f"**{covered} / {len(rows)} abilities have a sound set** (cast and hit resolve to cues whose fallback sounds exist). "
              f"{pack_count} use at least one Fab pack cue.", ""]
    lines += ["Gaps: " + (", ".join(f"`{m}`" for m in missing) if missing else "none."), ""]
    lines += ["| Kind | Abilities |", "|---|---|"] + [f"| {k} | {n} |" for k, n in kinds.most_common()] + [""]
    lines += ["| Element | Abilities |", "|---|---|"] + [f"| {k} | {n} |" for k, n in elements.most_common()] + [""]
    lines += ["## Element sets", "", "| Element | " + " | ".join(SLOTS) + " |", "|---|" + "---|" * len(SLOTS)]
    for e in ELEMENTS:
        cells = []
        for s in SLOTS:
            cid = f"spell.{e}.{s}"
            cells.append(("pack" if cid in pack_cues else "fallback") if cues.get(cid, {}).get("sounds") or cid in pack_cues else "pack only")
        lines.append(f"| {e} | " + " | ".join(cells) + " |")
    lines += ["", "## Weapons", "", "| Weapon | Slots | Fab pack |", "|---|---|---|"]
    for wid, w in ev["weapons"].items():
        slots = [f"{k}: `{v}`" for k, v in w.items() if k not in ("kind", "ranged", "rangedAbove")]
        lines.append(f"| {wid} ({w['kind']}) | " + ", ".join(slots) + " | " +
                     ("yes" if any(v in pack_cues for k, v in w.items() if isinstance(v, str)) else "-") + " |")
    lines += ["", "## Abilities", ""] + table + [""]
    report_text = "\n".join(lines)

    outputs = [(EVENTS, events_text), (CUES, cues_text), (BUFFS, new_buffs_text), (REPORT, report_text)]
    if args.check:
        stale = [str(p.relative_to(ROOT)) for p, t in outputs if not p.exists() or p.read_text("utf-8") != t]
        print("stale: " + (", ".join(stale) if stale else "none"))
        print(f"coverage: {covered}/{len(rows)} gaps={missing}")
        return 1 if stale or missing else 0
    for p, t in outputs:
        p.write_text(t, "utf-8", newline="\n")
    print(f"wrote {EVENTS.name} ({len(rows)} abilities), {CUES.name} ({len(gen)} generated cues, {len(pack_cues)} with pack members), "
          f"{BUFFS.name} ({edits} sound blocks), {REPORT.relative_to(ROOT)}")
    print(f"coverage: {covered}/{len(rows)} gaps={missing}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
