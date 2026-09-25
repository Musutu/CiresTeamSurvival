"""Author the new champions (Docs/NewChampions.md) into the shared data files.

Gunblade (Bounty Hunter), Witch Slayer, Huntress (glaive thrower with the sabercat companion Ashfang, Docs/Pets.md) and the two Aetheri
champions (Artificer, Warden). Skill names and descriptions come from Tools/BuildAbilityDB.py
(NEW_CHAMPION_SKILLS) so the roster, the Ability Database and the runtime never disagree.

Writes (idempotent, upserts by id; other rows are left byte-identical):
  Content/Data/ChampionRoster.json      profiles (display names are data: edit them here or in the JSON)
  Content/Data/ChampionArtBindings.json temporary bodies + the Tripo art slots
  Content/Data/WeaponLoadouts.json      presets and profile mapping (hunter/* props)
  Content/Data/WeaponGrips.json         grip frames for the new props
  Content/Data/DraftBackgrounds.json    painted champion-select backgrounds (role-themed fallback until art exists)

    python Tools/AuthorNewChampions.py            # write
    python Tools/AuthorNewChampions.py --check    # exit 1 when a file is stale

Then run Tools/BuildAbilityDB.py and Tools/AuthorRaces.py (player races, Aetheri monster race).
"""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "Content/Data"
sys.path.insert(0, str(ROOT / "Tools"))
import BuildAbilityDB as DB  # noqa: E402

PROVENANCE = ("Original design for Cire's Team Survival from Eric's reference sheets (inspiration only, no third-party names, "
              "text or likeness); temporary body until the Tripo agent delivers the bound model.")
VFX = {"holy": "holy", "shadow": "shadow", "fire": "fire", "physical": "steel", "arcane": "arcane", "void": "spectral", "nature": "nature"}
DELIVERY = {  # roster delivery vocabulary (CireChampionRoster.cpp)
    "silver_shot": "projectile", "hex_mark": "targeted", "powder_flask": "ground_circle", "blade_flurry": "self", "hunters_stride": "ground_line",
    "warding_talisman": "self", "price_on_every_soul": "passive", "collect_the_bounty": "targeted",
    "arcane_blunderbuss": "ground_cone", "spirit_lantern": "construct", "purge": "targeted", "banishment": "targeted", "witchfinders_mark": "targeted",
    "spectral_blade": "targeted", "witchbane": "passive", "hexbane_judgment": "ground_circle",
    "bouncing_glaive": "chain", "sabercat_pounce": "targeted", "owl_scout": "ground_circle", "moonlit_sprint": "self", "crescent_volley": "projectile",
    "sabercat_maul": "targeted", "sabercat_roar": "self", "moon_glaive": "passive", "glaive_storm": "self",
    "photon_turret": "construct", "skitter_swarm": "construct", "arc_mine": "construct", "disruption_pylon": "construct", "phase_lance": "projectile",
    "overcharge": "self", "aether_engineering": "passive", "warp_obelisk": "construct",
    "aegis_pylon": "construct", "haste_pylon": "construct", "gravity_pylon": "construct", "stasis_snare": "construct", "aether_mend": "ally",
    "repulsor_pulse": "self", "resonant_lattice": "passive", "aether_nexus": "construct",
}

CHAMPIONS = [
    dict(id="gunblade", displayName="Gunblade", familyId="gunblade", race="human", variant="Bounty Hunter", classType="Bounty Hunter", difficulty=2,
         lore="He hunts the wicked for coin and keeps a ledger of every soul he has priced.",
         quote="Some pray. I collect.",
         description="A hybrid gunner: a flintlock at mid range, a falchion up close, silver for the dead and a price on every head.",
         runtimeArchetype=1, primaryStat="agility", basicAttackRange=950, attackSeconds=1.5, attackStyle="gunblade", threatRole="damage", roles=["damage"],
         artFamily="wide-brim hat, weathered long coat and cape, flintlock pistol, falchion",
         actives=["silver_shot", "hex_mark", "powder_flask", "blade_flurry", "hunters_stride", "warding_talisman"],
         passive="price_on_every_soul", ultimate="collect_the_bounty"),
    dict(id="witch_slayer", displayName="Witch Slayer", familyId="witch_slayer", race="human", variant="Hexbane", classType="Witch Hunter", difficulty=3,
         lore="Covens whisper of a lantern no hex can put out, and of the hunter who carries it.",
         quote="Every ward has a seam.",
         description="An anti-caster hunter with an arcane blunderbuss and a spectral blade: purge, silence and banish whatever works magic.",
         runtimeArchetype=2, primaryStat="intelligence", basicAttackRange=900, attackSeconds=1.5, attackStyle="blunderbuss", threatRole="damage", roles=["damage"],
         artFamily="pointed hat, green-and-dark long coat, arcane blunderbuss with glowing blue orbs, spectral arcane blade",
         actives=["arcane_blunderbuss", "spirit_lantern", "purge", "banishment", "witchfinders_mark", "spectral_blade"],
         passive="witchbane", ultimate="hexbane_judgment"),
    dict(id="huntress", displayName="Huntress", familyId="huntress", race="sylvan", variant="Glaive Thrower", classType="Glaive Huntress", difficulty=2,
         lore="Masked and hooded, she stalks the treeline with the great sabercat Ashfang at her side, and her glaives come back for more.",
         quote="Track. Strike. Survive.",
         description="A ranged hunter on foot with a companion: glaives that bounce between foes, the sabercat Ashfang who pounces, mauls and roars on command, and an owl that finds prey.",
         runtimeArchetype=1, primaryStat="agility", basicAttackRange=1300, attackSeconds=1.5, attackStyle="glaive", threatRole="damage", roles=["damage"],
         artFamily="masked hooded huntress in leather and red cloth on foot beside a great sabercat, glaive launcher, bouncing glaives",
         actives=["bouncing_glaive", "sabercat_pounce", "owl_scout", "moonlit_sprint", "crescent_volley", "sabercat_maul"],
         passive="moon_glaive", ultimate="glaive_storm"),
    dict(id="aetheri_artificer", displayName="Aetheri Artificer", familyId="aetheri", race="aetheri", variant="Forge-Caller", classType="Construct Artificer", difficulty=3,
         lore="Aetheri artificers speak to machines in light; turrets rise where they point.",
         quote="Build. Deploy. Endure.",
         description="An Aetheri engineer who fights through Constructs: photon turrets, skitter bombs, arc mines and a disruption pylon.",
         runtimeArchetype=4, primaryStat="intelligence", basicAttackRange=1200, attackSeconds=1.5, attackStyle="arcane", threatRole="damage", roles=["damage"],
         artFamily="luminous crystalline being in gold and white alloy plate with blue-violet energy seams, floating focus crystal",
         actives=["photon_turret", "skitter_swarm", "arc_mine", "disruption_pylon", "phase_lance", "overcharge"],
         passive="aether_engineering", ultimate="warp_obelisk"),
    dict(id="aetheri_warden", displayName="Aetheri Warden", familyId="aetheri", race="aetheri", variant="Pylon Warden", classType="Pylon Warden", difficulty=2,
         lore="Where a Warden stands, pylons hum and the line does not break.",
         quote="The lattice holds.",
         description="An Aetheri guardian who shapes the battlefield with pylons: shield-regen, haste and gravity fields, stasis snares and a Nexus.",
         runtimeArchetype=0, primaryStat="intelligence", basicAttackRange=220, attackSeconds=1.5, attackStyle="staff", threatRole="healer", roles=["healer", "support", "tank"],
         artFamily="tall armoured Aetheri warden in white-gold alloy, blue-violet psionic glow, energy halberd",
         actives=["aegis_pylon", "haste_pylon", "gravity_pylon", "stasis_snare", "aether_mend", "repulsor_pulse"],
         passive="resonant_lattice", ultimate="aether_nexus"),
]


def skill(sid: str) -> dict:
    row = DB.NEW_CHAMPION_SKILLS[sid]
    name, school, effect, desc = row[0], row[3], row[9], row[14]
    return {"id": sid, "displayName": name, "status": "implemented", "mechanic": desc.replace("{effect}", f"{effect:g}"),
            "vfxFamily": VFX.get(school, "arcane"), "delivery": DELIVERY[sid]}


def profile(c: dict) -> dict:
    stats = {k: 10 for k in ("strength", "agility", "intelligence")}
    stats[c["primaryStat"]] = 20
    return {"id": c["id"], "displayName": c["displayName"], "familyId": c["familyId"], "race": c["race"], "variant": c["variant"],
            "classType": c["classType"], "difficulty": c["difficulty"], "lore": c["lore"], "quote": c["quote"], "description": c["description"],
            "runtimeArchetype": c["runtimeArchetype"], "primaryStat": c["primaryStat"], **stats, "basicAttackRange": c["basicAttackRange"],
            "attackSeconds": c["attackSeconds"], "attackStyle": c["attackStyle"], "threatRole": c["threatRole"], "roles": c["roles"],
            "artFamily": c["artFamily"], "artStatus": "prototype_fallback", "artProvenance": PROVENANCE, "startsWithSkills": [],
            "actives": [skill(s) for s in c["actives"]], "passive": skill(c["passive"]), "ultimate": skill(c["ultimate"])}


# ---------------------------------------------------------------------------------------------- art
TRIPO = "/Game/Tripo/Monsters/BarbedHunterB"
BINDINGS = [
    {"profileId": "gunblade", "status": "custom_ready", "motion": "monster_native",
     "mesh": f"{TRIPO}/CTS_Monster_BarbedHunterB.CTS_Monster_BarbedHunterB", "heightCm": 182, "yaw": -90,
     "animations": {r: f"{TRIPO}/Animations/CTS_Monster_BarbedHunterB_{c}.CTS_Monster_BarbedHunterB_{c}" for r, c in
                    (("idle", "idle"), ("walk", "walk"), ("run", "run"), ("attack", "attack_crossbow"), ("hit", "hit_to_body_01"), ("death", "fall"))},
     "props": [{"asset": "/Game/Art/NewChampions01/Props/SM_Falchion.SM_Falchion", "bone": "hand_l"},
               {"asset": "/Game/Art/NewChampions01/Props/SM_Flintlock.SM_Flintlock", "bone": "pelvis", "offsetCm": [4, 16, -6], "rotation": [100, 0, 0]}],
     "tripoSlot": {"unit": "gunblade", "skeleton": "Tripo humanoid biped (61-bone champion batch rig: pelvis, spine_01..03, thigh/calf/foot, hand_l/hand_r)",
                   "sheet": "Saved/Reference/gunblade-sheet.png", "props": ["flintlock pistol (hand_r)", "falchion (hand_l)", "powder flask, bounty ledger (belt)"],
                   "bindAs": "status ready: mesh + BS_Idle_Walk_Run + A_*_Attack (see the lancer row)"},
     "note": "Temporary body: the scarecrow-hatted BarbedHunterB monster (its crossbow is fused into the right hand and reads as the gun); falchion on the off hand."},
    {"profileId": "witch_slayer", "status": "ready",
     "mesh": "/Game/Art/Characters/TripoBatch/Batch01/Bodies/summoner/SK_summoner.SK_summoner",
     "locomotion": "/Game/Art/Characters/TripoBatch/Batch01/Locomotion/summoner/Animations/BS_Idle_Walk_Run_summoner.BS_Idle_Walk_Run_summoner",
     "attack": "/Game/Art/Characters/TripoBatch/Batch01/Attacks/summoner/A_summoner_Attack.A_summoner_Attack", "heightCm": 180,
     "tint": {"base": [0.06, 0.22, 0.14], "accent": [0.25, 0.45, 1.0], "strength": 0.7, "rim": [0.3, 0.55, 1.6]},
     "tripoSlot": {"unit": "witch_slayer", "skeleton": "Tripo humanoid biped (champion batch rig)", "sheet": "Saved/Reference/witchslayer-sheet.png",
                   "props": ["arcane blunderbuss with blue orb drum (hand_l)", "spectral arcane blade (hand_r / back)", "spirit lantern (belt)"]},
     "note": "Temporary body: the Rift Summoner rig re-tinted dark green with blue arcane accents."},
    {"profileId": "huntress", "status": "ready",
     "mesh": "/Game/TripoModels/armored_archer_3d_model/armored_archer_3d_model.armored_archer_3d_model",
     "locomotion": "/Game/Art/Characters/TripoRetarget/Preview02/Ranger/Animations/BS_Idle_Walk_Run_Ranger.BS_Idle_Walk_Run_Ranger",
     "attack": "/Game/Art/Characters/CombatPrototype01/Ranger/A_Ranger_Attack.A_Ranger_Attack", "heightCm": 172,
     "tripoSlot": {"unit": "huntress", "skeleton": "Tripo humanoid biped (champion batch rig)",
                   "sheet": "Saved/Reference/huntress-sheet.png", "props": ["glaive launcher (hand_l)", "glaive (hand_r)", "glaive ammo (back)"]},
     "note": "Temporary body: the Ash Ranger rig on foot. Her sabercat Ashfang is a companion pet (Content/Data/Pets.json), not a mount."},
    {"profileId": "aetheri_artificer", "status": "ready",
     "mesh": "/Game/Art/Characters/TripoBatch/Batch01/Bodies/wizard/SK_wizard.SK_wizard",
     "locomotion": "/Game/Art/Characters/TripoBatch/Batch01/Locomotion/wizard/Animations/BS_Idle_Walk_Run_wizard.BS_Idle_Walk_Run_wizard",
     "attack": "/Game/Art/Characters/TripoBatch/Batch01/Attacks/wizard/A_wizard_Attack.A_wizard_Attack", "heightCm": 184,
     "tint": {"base": [0.85, 0.72, 0.42], "accent": [0.45, 0.35, 1.2], "strength": 0.75, "rim": [0.5, 0.4, 2.2]},
     "tripoSlot": {"unit": "aetheri_artificer", "skeleton": "Tripo humanoid biped (champion batch rig)", "sheet": "(none yet: gold/white alloy, blue-violet energy)",
                   "props": ["floating focus crystal / energy staff (hand_l)"]},
     "note": "Temporary body: the Cinder Arcanist rig re-tinted Aetheri gold-white with violet energy."},
    {"profileId": "aetheri_warden", "status": "ready",
     "mesh": "/Game/Art/Characters/TripoBatch/Batch01/Bodies/keeper_of_light/SK_keeper_of_light.SK_keeper_of_light",
     "locomotion": "/Game/Art/Characters/TripoBatch/Batch01/Locomotion/keeper_of_light/Animations/BS_Idle_Walk_Run_keeper_of_light.BS_Idle_Walk_Run_keeper_of_light",
     "attack": "/Game/Art/Characters/TripoBatch/Batch01/Attacks/keeper_of_light/A_keeper_of_light_Attack.A_keeper_of_light_Attack", "heightCm": 192,
     "tint": {"base": [0.9, 0.84, 0.62], "accent": [0.3, 0.45, 1.3], "strength": 0.6, "rim": [0.35, 0.5, 2.2]},
     "tripoSlot": {"unit": "aetheri_warden", "skeleton": "Tripo humanoid biped (champion batch rig)", "sheet": "(none yet: white-gold alloy warden, psionic glow)",
                   "props": ["energy halberd (hand_r)"]},
     "note": "Temporary body: the Keeper of the Light rig re-tinted white-gold with blue-violet psionic accents."},
]

LOADOUT_PRESETS = {
    "gunblade": '{"motion": "melee", "parts": [\n      {"asset": "hunter/Falchion", "bone": "hand_r", "role": "primary"},\n      {"asset": "hunter/Flintlock", "bone": "hand_l"}]}',
    "witch_slayer": '{"motion": "cast", "parts": [\n      {"asset": "hunter/ArcaneBlunderbuss", "bone": "hand_l", "role": "primary"},\n      {"asset": "hunter/SpectralBlade", "bone": "pelvis", "offsetCm": [0, -18, -4], "rotation": [160, 0, 0], "scale": 0.9}]}',
    "huntress": '{"motion": "throw", "parts": [\n      {"asset": "hunter/Glaive", "bone": "hand_r", "role": "primary", "hideOnRelease": true},\n      {"asset": "hunter/GlaiveLauncher", "bone": "hand_l"}]}',
    "artificer": '{"motion": "cast", "parts": [\n      {"asset": "hunter/AetherStaff", "bone": "hand_l", "role": "primary"}]}',
    "aetheri_warden": '{"motion": "melee", "parts": [\n      {"asset": "hunter/AetherHalberd", "bone": "hand_r", "role": "primary"}]}',
}
LOADOUT_PROFILES = {"gunblade": "gunblade", "witch_slayer": "witch_slayer", "huntress": "huntress", "aetheri_artificer": "artificer", "aetheri_warden": "aetheri_warden"}
GRIPS = {
    "SM_Falchion": {"handle": [0, 0, -3], "axis": [0, 0, 1], "edge": [1, 0, 0], "radiusCm": 1.6, "tilt": 25},
    "SM_Flintlock": {"handle": [0, 0, -2], "axis": [0, 0, 1], "edge": [1, 0, 0], "radiusCm": 1.9},
    "SM_ArcaneBlunderbuss": {"handle": [0, 0, -2], "axis": [0, 0, 1], "edge": [1, 0, 0], "radiusCm": 2.1, "offHand": [16, 0, 1], "offAxis": [1, 0, 0], "carry": True, "carryAt": [0.45, 0.34, 0.42]},
    "SM_SpectralBlade": {"handle": [0, 0, -3], "axis": [0, 0, 1], "edge": [1, 0, 0], "radiusCm": 1.5, "tilt": 20},
    "SM_Glaive": {"handle": [0, 0, 0], "axis": [0, 0, 1], "edge": [1, 0, 0], "radiusCm": 1.8},
    "SM_GlaiveLauncher": {"handle": [0, 0, -2], "axis": [0, 0, 1], "edge": [1, 0, 0], "radiusCm": 2.2, "offHand": [18, 0, 1], "offAxis": [1, 0, 0], "carry": True, "carryAt": [0.45, 0.34, 0.42]},
    "SM_AetherStaff": {"handle": [0, 0, 0], "axis": [0, 0, 1], "edge": [1, 0, 0], "radiusCm": 2.2, "offHand": [0, 0, 22], "offAxis": [0, 0, 1], "carry": True},
    "SM_AetherHalberd": {"handle": [0, 0, 0], "axis": [0, 0, 1], "edge": [1, 0, 0], "radiusCm": 2.2, "offHand": [0, 0, 28], "offAxis": [0, 0, 1], "carry": True},
}
SWAP_HAND = ["witch_slayer", "artificer"]

# Painted champion-select backgrounds (the draft-layout agent paints Art/DraftBackgrounds/<id>.png ->
# /Game/UI/Draft/Backgrounds/T_DraftBg_<id>). Until a painting exists the draft screen uses the fallback,
# a role-themed scene of an existing champion. Prompts describe the painting ("status": painted once
# Art/DraftBackgrounds/<background>.png exists and is imported; all five painted by art-2d, 2026-09-25).
BACKGROUNDS = {
    "gunblade": {"background": "gunblade", "fallback": "ranger", "mood": "candle",
                 "prompt": "Dusk over a gallows crossroads outside a burned village, bounty posters nailed to a leaning post, lantern light, crows, dark fantasy oil painting, no people"},
    "witch_slayer": {"background": "witch_slayer", "fallback": "summoner", "mood": "violet",
                     "prompt": "A witch's clearing at night, broken ritual circle of blue fire, a single spirit lantern hanging from a dead oak, mist, dark fantasy oil painting, no people"},
    "huntress": {"background": "huntress", "fallback": "dryad", "mood": "moon",
                 "prompt": "Moonlit ancient forest ridge, great cat tracks in silver frost, an owl on a broken branch, cold blue light, dark fantasy oil painting, no people"},
    "aetheri_artificer": {"background": "aetheri", "fallback": "ether_golem", "mood": "ether",
                          "prompt": "Aetheri workshop plaza: crystalline gold-and-white alloy spires under a violet sky, floating warp rings and a humming pylon, gears and crystal shards on wet flagstones, luminous blue energy, dark fantasy oil painting, no people"},
    "aetheri_warden": {"background": "aetheri_warden", "fallback": "ether_golem", "mood": "ether",
                       "prompt": "A quiet Aetheri sanctuary terrace on a cliff at night, white marble and gold-and-white crystal pylons casting an icy-blue warding field, floating rune rings, teal aurora over snowy peaks, a crystal beacon, dark fantasy oil painting, no people"},
}


# Buff/debuff visuals for the records these kits and the Aetheri constructs write (CireBuffs::KnownIds needs a row each).
def _vis(name, kind, school, primary, secondary, core, layers, cue, source):
    return {"name": name, "kind": kind, "school": school, "source": source,
            "palette": {"primary": primary, "secondary": secondary, "core": core}, "priority": 60,
            "layers": layers, "lifecycle": {"burstSeconds": 0.4, "fadeSeconds": 0.35}, "sound": {"start": cue}}


GLYPH_EYE = {"shape": "glyph", "attach": "overhead", "style": "eye", "size": 0.8}
# Every aura needs a distinct layer signature (CireAuraVisuals smoke), so each row mixes shapes/styles no other row uses.
VISUALS = {
    "bounty_mark": _vis("Bounty", "debuff", "shadow", [0.95, 0.25, 0.2], [0.35, 0.05, 0.05], [1.0, 0.8, 0.6],
                        [GLYPH_EYE, {"shape": "ring", "attach": "ground", "style": "runes", "count": 6, "speed": 0.4}], "npc.profane.start", "Gunblade Hex Mark: +damage taken, bounty gold on kill"),
    "witch_mark": _vis("Exposed", "debuff", "arcane", [0.3, 0.55, 1.0], [0.08, 0.15, 0.4], [0.85, 0.95, 1.0],
                       [{"shape": "glyph", "attach": "overhead", "style": "muted", "size": 0.9}, {"shape": "ring", "attach": "ground", "style": "dashed", "count": 10, "speed": 1.1},
                        {"shape": "motes", "attach": "overhead", "style": "glint", "count": 6, "speed": 0.8}], "npc.silence.start", "Witch Slayer Witchfinder's Mark: revealed, +damage taken (x3 while casting)"),
    "tracked": _vis("Tracked", "debuff", "nature", [0.6, 0.8, 0.45], [0.2, 0.3, 0.1], [0.95, 1.0, 0.85],
                    [GLYPH_EYE], "npc.feral.start", "Huntress Owl Scout: revealed, +damage taken from the Huntress"),
    "banished": _vis("Banished", "debuff", "void", [0.55, 0.3, 1.0], [0.1, 0.05, 0.25], [0.95, 0.85, 1.0],
                     [{"shape": "swirl", "attach": "body", "speed": 1.6}, {"shape": "ring", "attach": "ground", "style": "runes", "count": 8, "speed": 1.0}],
                     "npc.void.start", "Witch Slayer Banishment: exiled (cannot act, cannot be harmed) until it returns"),
    "hunters_stride": _vis("Hunter's Stride", "buff", "steel", [0.85, 0.8, 0.7], [0.3, 0.25, 0.2], [1.0, 0.95, 0.85],
                           [{"shape": "motes", "attach": "body", "style": "glint", "count": 8, "speed": 1.2}], "npc.cleave.start", "Gunblade: the next basic attack deals bonus damage"),
    "warding_talisman": _vis("Warding Talisman", "buff", "holy", [0.95, 0.85, 0.5], [0.4, 0.3, 0.1], [1.0, 1.0, 0.85],
                             [{"shape": "halo", "attach": "overhead", "count": 10, "speed": 0.6}, {"shape": "plates", "attach": "body", "count": 5, "speed": 0.5}],
                             "npc.ward.start", "Gunblade: damage reduction"),
    "moonlit_sprint": _vis("Moonlit Sprint", "buff", "arcane", [0.65, 0.75, 1.0], [0.15, 0.2, 0.45], [0.95, 0.97, 1.0],
                           [{"shape": "motes", "attach": "body", "style": "glint", "count": 12, "speed": 1.4}, {"shape": "ripple", "attach": "ground", "style": "shock", "speed": 1.2}],
                           "npc.rune.start", "Huntress: movement speed"),
    "overcharge": _vis("Overcharge", "buff", "arcane", [0.5, 0.45, 1.0], [0.15, 0.1, 0.45], [0.95, 0.9, 1.0],
                       [{"shape": "crystals", "attach": "body", "count": 6, "speed": 1.2}, {"shape": "tether", "attach": "link", "style": "beam", "burstOnly": True}],
                       "npc.rune.start", "Aetheri Artificer: constructs overcharged"),
    "aether_aegis": _vis("Aegis Field", "buff", "arcane", [0.3, 0.85, 1.0], [0.05, 0.25, 0.4], [0.85, 1.0, 1.0],
                         [{"shape": "plates", "attach": "body", "count": 6, "speed": 0.6}, {"shape": "ring", "attach": "ground", "style": "dashed", "count": 5, "speed": 0.5}],
                         "npc.ward.start", "Aetheri Aegis Pylon field: regeneration, less damage taken"),
    "aether_haste": _vis("Haste Field", "buff", "arcane", [1.0, 0.8, 0.35], [0.4, 0.28, 0.05], [1.0, 0.95, 0.75],
                         [{"shape": "flames", "attach": "body", "count": 5, "speed": 1.8}, {"shape": "ring", "attach": "ground", "style": "dashed", "count": 4, "speed": 1.6}],
                         "npc.rune.start", "Aetheri Haste Pylon field: move and attack speed"),
    "aether_weakened": _vis("Disrupted", "debuff", "arcane", [0.95, 0.35, 0.85], [0.35, 0.05, 0.3], [1.0, 0.8, 0.95],
                            [{"shape": "cracks", "attach": "ground", "speed": 0.8}, {"shape": "swirl", "attach": "overhead", "speed": 1.1}],
                            "npc.mind.start", "Aetheri Disruption Pylon field: less damage dealt"),
    "aether_nexus": _vis("Nexus", "buff", "arcane", [0.95, 0.95, 1.0], [0.35, 0.35, 0.6], [1.0, 1.0, 1.0],
                         [{"shape": "halo", "attach": "overhead", "count": 12, "speed": 0.8}, {"shape": "crystals", "attach": "body", "count": 4, "speed": 0.4},
                          {"shape": "ring", "attach": "ground", "style": "runes", "count": 12, "speed": 0.4}],
                         "npc.ward.start", "Aetheri Nexus: guard and regeneration"),
    "npc_aether_empowered": _vis("Empowered", "buff", "war", [1.0, 0.3, 0.35], [0.45, 0.05, 0.1], [1.0, 0.8, 0.8],
                                 [{"shape": "flames", "attach": "body", "count": 8, "speed": 1.2}, {"shape": "crystals", "attach": "overhead", "count": 3, "speed": 1.0}],
                                 "npc.warcry.start", "Aetheri monster Empowering Pylon field: more damage, less taken"),
}
# Modifier summary rows (BuffModifiers.json, Docs/BuffModifiers.md) for every id above plus the race's npc_aether.
MODIFIERS = {
    "bounty_mark": '{"type": "curse", "mods": [{"stat": "Damage taken", "value": 15, "unit": "%"}], "line": "A bounty is on this head: takes more damage; its killer is paid."}',
    "witch_mark": '{"type": "magic", "mods": [{"stat": "Damage taken", "value": 12, "unit": "%"}], "line": "Exposed: revealed and takes more damage, three times more while casting."}',
    "tracked": '{"type": "none", "mods": [{"stat": "Damage taken", "value": 10, "unit": "%"}], "line": "Tracked by the Huntress: revealed; her glaives hit harder."}',
    "banished": '{"type": "magic", "control": "stun", "mods": [], "line": "Exiled: cannot act, move or be harmed until it returns."}',
    "hunters_stride": '{"type": "none", "mods": [{"stat": "ATK", "value": 50, "unit": "%"}], "line": "The next basic attack deals more damage."}',
    "warding_talisman": '{"type": "magic", "mods": [{"stat": "DEF", "value": 30, "unit": "%"}], "line": "Takes less damage."}',
    "moonlit_sprint": '{"type": "magic", "mods": [{"stat": "Move", "value": 40, "unit": "%"}], "line": "Moves faster."}',
    "overcharge": '{"type": "none", "mods": [], "line": "Constructs overcharged: turrets fire twice as fast."}',
    "aether_aegis": '{"type": "magic", "mods": [{"stat": "DEF", "value": 15, "unit": "%"}], "line": "Inside an Aegis Pylon field: regenerates and takes less damage."}',
    "aether_haste": '{"type": "magic", "mods": [{"stat": "Move", "value": 25, "unit": "%"}, {"stat": "Attack speed", "value": 25, "unit": "%"}], "line": "Inside a Haste Pylon field: moves and attacks faster."}',
    "aether_weakened": '{"type": "curse", "mods": [{"stat": "ATK", "value": -25, "unit": "%"}], "line": "Inside a Disruption Pylon field: deals less damage."}',
    "aether_nexus": '{"type": "magic", "mods": [{"stat": "DEF", "value": 40, "unit": "%"}], "line": "Inside the Nexus: guarded and regenerating."}',
    "npc_aether_empowered": '{"type": "magic", "mods": [{"stat": "ATK", "value": 25, "unit": "%"}, {"stat": "DEF", "value": 15, "unit": "%"}], "line": "Empowered by an Aetheri pylon."}',
    "npc_aether": '{"type": "curse", "mods": [], "line": "Seared by Aetheri energy."}',
}
FOOTSTEPS = {"gunblade": '{ "class": "leather" }', "witch_slayer": '{ "class": "cloth", "pitch": 0.95 }', "huntress": '{ "class": "beast", "pitch": 0.9 }',
             "aetheri_artificer": '{ "class": "golem", "pitch": 1.1, "volume": 0.8 }', "aetheri_warden": '{ "class": "plate", "pitch": 1.05 }'}


def upsert_visuals(text: str) -> str:
    rows = {k: f'    "{k}": {json.dumps(v)},\n' for k, v in VISUALS.items()}
    out, _ = _insert(text, '"buffs": {', rows)
    json.loads(out)
    return out


def _insert(text: str, anchor: str, rows: dict[str, str]):
    lines = set(text.split("\n"))
    if all(row.rstrip("\n").rstrip(",") in lines or row.rstrip("\n") in lines for row in rows.values()):
        return text, False  # every managed row already present verbatim (order may differ after AuthorRaces runs)
    keep =[line for line in text.split("\n") if not any(line.lstrip().startswith(f'"{k}":') for k in rows)]
    text = "\n".join(keep)
    idx = text.index(anchor) + len(anchor)
    block = "".join(rows.values())
    return text[:idx] + "\n" + block.rstrip("\n").rstrip(",") + ("," if text[idx:].lstrip().startswith('"') else "") + text[idx:], True


def upsert_modifiers(text: str) -> str:
    rows = {k: f'    "{k}": {v},\n' for k, v in MODIFIERS.items()}
    out, _ = _insert(text, '"effects": {', rows)
    json.loads(out)
    return out


def upsert_footsteps(text: str) -> str:
    rows = {k: f'    "{k}": {v},\n' for k, v in FOOTSTEPS.items()}
    out, _ = _insert(text, '"profiles": {', rows)
    json.loads(out)
    return out


def upsert_roster(text: str) -> str:
    data = json.loads(text)
    by_id = {c["id"]: i for i, c in enumerate(data["champions"])}
    for c in CHAMPIONS:
        p = profile(c)
        if c["id"] in by_id:
            data["champions"][by_id[c["id"]]] = p
        else:
            data["champions"].append(p)
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


TRIPO = DATA / "ChampionArt.tripo.json"
# The Tripo idle of these bodies keeps the arms near T-pose (the carry grips of the others hide it): the runtime lowers them.
RELAX_ARMS = {"gunblade", "huntress"}
BINDING_KEYS = ("profileId", "status", "mesh", "locomotion", "attack", "heightCm")


def tripo_rows() -> dict:
    """The Tripo agent's finished champion bodies (Docs/ArtIntegration.md, "Tripo champions"), when delivered."""
    if not TRIPO.is_file():
        return {}
    return {c["profileId"]: c for c in json.loads(TRIPO.read_text(encoding="utf-8"))["champions"] if c.get("status") == "ready"}


def final_bindings() -> list:
    """Tripo bodies replace the temporary ones (status ready: attack clips + grips, no tint). Every champion fights on foot:
    the Huntress uses her Tripo body with normal locomotion and her sabercat is a companion pet (Content/Data/Pets.json)."""
    tripo = tripo_rows()
    out = []
    for row in copy.deepcopy(BINDINGS):
        t = tripo.get(row["profileId"])
        if not t:
            out.append(row)
            continue
        bound = {k: t[k] for k in BINDING_KEYS}
        if row["profileId"] in RELAX_ARMS:
            bound["relaxArms"] = True
        bound["note"] = "Tripo model (tripo-races). Temporary body before it: " + row.get("note", "")
        bound["tripoSlot"] = dict(row["tripoSlot"], delivered=t["mesh"])
        out.append(bound)
    return out


def upsert_bindings(text: str) -> str:
    data = json.loads(text)
    rows = [b for b in data["bindings"] if b["profileId"] not in {x["profileId"] for x in BINDINGS}]
    data["bindings"] = rows + final_bindings()
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


def loadout_plan() -> tuple[dict, dict, dict]:
    """Presets, profile mapping and preview options: the Tripo presets become the defaults once delivered."""
    presets = dict(LOADOUT_PRESETS)
    profiles = dict(LOADOUT_PROFILES)
    options = {}
    if TRIPO.is_file():
        data = json.loads(TRIPO.read_text(encoding="utf-8"))
        for name, row in data.get("loadoutPresets", {}).items():
            parts = ",\n      ".join(json.dumps(p) for p in row["parts"])
            presets[name] = '{"motion": "%s", "parts": [\n      %s]}' % (row["motion"], parts)
        for champion in data["champions"]:
            preset = champion.get("loadoutPreset")
            if champion.get("status") == "ready" and preset in presets and champion["profileId"] in profiles:
                if preset != profiles[champion["profileId"]]:
                    options[champion["profileId"]] = [preset, profiles[champion["profileId"]]]
                profiles[champion["profileId"]] = preset
    return presets, profiles, options


def upsert_loadouts(text: str) -> str:
    # Keep the file's hand-formatted layout: insert missing preset lines and profile mappings textually.
    presets, profiles, options = loadout_plan()
    for name, row in presets.items():
        if f'"{name}": {{"motion"' in text:
            text = re.sub(rf'    "{name}": \{{"motion".*?\]\}}', lambda m: f'    "{name}": {row}', text, count=1, flags=re.S)
        else:
            text = text.replace('  "presets": {\n', f'  "presets": {{\n    "{name}": {row},\n', 1)
    for profile_id, preset in profiles.items():
        body = text.split('"profiles"')[1]
        if f'"{profile_id}": "' in body:
            head, tail = text.split('"profiles"', 1)
            tail = re.sub(rf'"{profile_id}": "[a-z_]+"', f'"{profile_id}": "{preset}"', tail, count=1)
            text = head + '"profiles"' + tail
            continue
        head, tail = text.rsplit("\n  }\n}", 1)
        text = head + f',\n    "{profile_id}": "{preset}"' + "\n  }\n}" + tail
    # Preview options (development HUD cycling): the Tripo preset first, the prototype props second.
    for profile_id, names in options.items():
        line = f'    "{profile_id}": {json.dumps(names)}'
        head, tail = text.split('"previewOptions": {', 1)
        tail = re.sub(rf'\n    "{profile_id}": \[[^\]]*\],?', '', tail, count=1)
        text = head + '"previewOptions": {\n' + line + ',' + tail if not tail.startswith('\n') else head + '"previewOptions": {\n' + line + ',' + tail
    text = text.replace(',,', ',')
    json.loads(text)
    return text


def upsert_grips(text: str) -> str:
    data = json.loads(text)
    if all(data["weapons"].get(k) == v for k, v in GRIPS.items()) and all(n in data["swapHandPresets"] for n in SWAP_HAND):
        return text  # up to date: keep other agents' hand formatting
    if text.split('"weapons": {', 1)[1].startswith('\n    "') and '{"handle"' in text:
        # Compact one-row-per-weapon layout (tripo-races): insert/replace our rows textually.
        rows = {k: f'    "{k}": {json.dumps(v)},\n' for k, v in GRIPS.items()}
        text, _ = _insert(text, '"weapons": {', rows)
        missing = [n for n in SWAP_HAND if n not in json.loads(text)["swapHandPresets"]]
        if missing:
            text = re.sub(r'"swapHandPresets": \[([^\]]*)\]', lambda m: '"swapHandPresets": [' + m.group(1).rstrip() + "".join(f', "{n}"' for n in missing) + "]", text, count=1)
        json.loads(text)
        return text
    data["weapons"].update(copy.deepcopy(GRIPS))
    for name in SWAP_HAND:
        if name not in data["swapHandPresets"]:
            data["swapHandPresets"].append(name)
    return json.dumps(data, indent=2, ensure_ascii=False) + "\n"


def backgrounds() -> str:
    return json.dumps({"schemaVersion": 1, "generator": "Tools/AuthorNewChampions.py",
                       "notes": "Champion-select painted backgrounds for champions without one yet. background = /Game/UI/Draft/Backgrounds/T_DraftBg_<background>; "
                                "until that texture exists the draft screen shows fallback's painting (a role-themed scene). mood picks the stage lighting. "
                                "status: painted when Art/DraftBackgrounds/<background>.png exists (imported by Tools/BuildDraftSelectContent.py), else fallback.",
                       "champions": {cid: dict(row, status="painted" if (ROOT / "Art/DraftBackgrounds" / (row["background"] + ".png")).is_file() else "fallback")
                                     for cid, row in BACKGROUNDS.items()}}, indent=2) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    outputs = {}
    for name, fn in (("ChampionRoster.json", upsert_roster), ("ChampionArtBindings.json", upsert_bindings),
                     ("WeaponLoadouts.json", upsert_loadouts), ("WeaponGrips.json", upsert_grips),
                     ("BuffVisuals.json", upsert_visuals), ("AudioFootsteps.json", upsert_footsteps), ("BuffModifiers.json", upsert_modifiers)):
        path = DATA / name
        outputs[path] = fn(path.read_text(encoding="utf-8"))
    outputs[DATA / "DraftBackgrounds.json"] = backgrounds()
    stale = [p for p, t in outputs.items() if not p.exists() or p.read_text(encoding="utf-8") != t]
    if not args.check:
        for p in stale:
            p.write_text(outputs[p], encoding="utf-8", newline="\n")
    print("AUTHOR_NEW_CHAMPIONS champions=%d %s" % (len(CHAMPIONS), ("STALE " + ",".join(p.name for p in stale)) if stale else "up-to-date"))
    return 1 if args.check and stale else 0


if __name__ == "__main__":
    raise SystemExit(main())
