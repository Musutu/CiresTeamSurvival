"""ability-expansion: build the expansion ability pool (Docs/Abilities.md "Expansion pool").

Writes, from the tables below:
  Content/Data/AbilitiesExpansion.json   Ability DB rows (Abilities.json format + "recipe"), merged additively by CireAbilityDB
  Content/Data/FabVFX.expansion.json     per-ability Fab Niagara signatures (FabVFX.json "abilities" format)
  Content/Data/AudioEvents.expansion.json  sound rows (AudioEvents.json "abilities" format)
  Content/Data/AbilityIcons.expansion.json icon school / glyph / accent rows (AbilityIcons.json format)
  Content/Data/SummonArt.expansion.json  bodies for the expansion summons (SummonArt.json format)
  Docs/Abilities.md                      the "Expansion pool" appendix (between AUTO markers)

Every ability: role types, a Skill Shop section, PRIMARY-stat scaling (or potency for utility), a level-15 bonus (actives)
or team aura (passives), a delivery recipe with a true telegraph shape (CireAbilityExpansion::DescribeShape) and an icon.
Every purchased-pack Niagara system that no ability or monster used yet (and that the pack-usage-3 rating grades A/B, or
leaves unrated as a moving projectile / line) is the signature of one active; the other roles take A/B systems of the same
school (recoloured to the school hue when reused).

    python Tools/BuildAbilityExpansion.py [--inventory PATH] [--check]
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "Tools"))
import BuildAbilityDB as DB  # noqa: E402  (CURVES, SCHOOLS)
import BuildAbilityIcons as ICONS  # noqa: E402  (PALETTES, GLYPHS; Pillow is only needed for painting)

# ------------------------------------------------------------------------------------------------ numbers per delivery
# (effect, coef, mana, energy, cooldown, range, radius, duration, component, effectLabel)
T = {
    "bolt":      (70, 1.3, 35, 0, 7, 1100, 30, 0, "damage", "damage"),
    "pierce":    (60, 1.2, 40, 0, 9, 1200, 34, 0, "damage", "damage per target"),
    "line":      (75, 1.3, 40, 0, 9, 900, 90, 0, "damage", "damage"),
    "cone":      (65, 1.2, 35, 0, 8, 0, 450, 0, "damage", "damage"),
    "circle":    (80, 1.4, 45, 0, 10, 900, 260, 0, "damage", "impact damage"),
    "zone":      (22, .45, 50, 0, 14, 900, 320, 5, "damage", "damage per second"),
    "nova":      (60, 1.1, 35, 0, 10, 0, 380, 0, "damage", "damage"),
    "chain":     (55, 1.0, 40, 0, 9, 900, 500, 0, "damage", "damage per bounce"),
    "strike":    (90, 1.6, 0, 30, 7, 280, 0, 0, "damage", "damage"),
    "leap":      (70, 1.2, 0, 35, 12, 700, 280, 0, "damage", "damage"),
    "dash":      (65, 1.1, 0, 30, 11, 650, 90, 0, "damage", "damage"),
    "heal":      (110, 3.0, 50, 0, 8, 1100, 0, 0, "heal", "healing"),
    "healZone":  (30, .6, 70, 0, 16, 900, 350, 6, "heal", "healing per second"),
    "barrier":   (140, 3.0, 50, 0, 14, 1000, 0, 6, "shield", "barrier health"),
    "selfBuff":  (25, 0, 30, 0, 18, 0, 0, 8, "potency", "%"),
    "partyBuff": (20, 0, 50, 0, 22, 0, 700, 8, "potency", "%"),
    "summon":    (16, .4, 60, 0, 24, 800, 0, 20, "summon", "damage per hit"),
    "construct": (220, .35, 55, 0, 16, 800, 0, 20, "construct", "construct health"),
    "wall":      (400, 8.0, 45, 0, 18, 700, 220, 8, "shield", "wall health"),
    "barrage":   (70, 1.2, 130, 0, 90, 1000, 600, 0, "damage", "damage per strike"),
}
PHYSICAL_ENERGY = {"strike", "leap", "dash", "cone", "nova"}  # physical versions of these spend energy, not mana
HIT = {"bolt", "pierce", "line", "cone", "circle", "zone", "nova", "chain", "strike", "leap", "dash", "barrage"}
SECTION_OF = {"heal": "defensive", "healZone": "defensive", "barrier": "defensive", "selfBuff": "defensive", "partyBuff": "defensive",
              "wall": "defensive", "summon": "summon", "construct": "construct", "passive": "passive"}
CATEGORY_OF = {"spell": "Offensive", "attack": "Offensive", "defensive": "Defensive", "control": "Crowd Control", "summon": "Summons",
               "construct": "Constructs", "passive": "Passives", "ultimate": "Ultimates"}
TARGETING = {"bolt": "aim", "pierce": "aim", "line": "aim", "cone": "aim", "circle": "aim", "zone": "aim", "nova": "self", "chain": "enemy",
             "strike": "enemy", "leap": "aim", "dash": "aim", "heal": "ally", "healZone": "aim", "barrier": "ally", "selfBuff": "self",
             "partyBuff": "self", "summon": "aim", "construct": "aim", "wall": "aim", "barrage": "aim", "passive": "passive"}
CC_TYPES = {"stun", "slow", "root", "silence", "taunt", "interrupt"}

# ------------------------------------------------------------------------------------------------ helpers
def fx(kind, duration=0.0, magnitude=0.0, zone="target", label=""):
    e = dict(type=kind, zone=zone, duration=duration, magnitude=magnitude, radius=0.0)
    if label:
        e["label"] = label
    return e

SLOW = lambda s=2.5, m=.35: fx("slow", s, m, label=f"Move -{int(m * 100)}%")
STUN = lambda s=.75: fx("stun", s, label="Stunned")
ROOTS = lambda s=1.5: fx("root", s, label="Rooted")
SILENCE = lambda s=1.5: fx("silence", s, label="Silenced")
TAUNT = lambda s=2.5: fx("taunt", s)
HEALCUT = lambda s=5, m=.4: fx("healCut", s, m, label=f"Healing -{int(m * 100)}%")
ARMOR = lambda s=5: fx("armorBreak", s, .5, label="Armor -50%")
WEAKEN = lambda s=4, m=.2: fx("weaken", s, m, label=f"Damage dealt -{int(m * 100)}%")
MARK = lambda s=5, m=.12: fx("mark", s, m, label=f"Damage taken +{int(m * 100)}%")
PURGE = lambda: fx("purge")
INTERRUPT = lambda: dict(type="interrupt", zone="target", duration=0.0, magnitude=0.0, radius=0.0, lockoutSeconds=1.5, label="Interrupted")


def A(id_, name, delivery, school, types, desc, *, vfx=None, eff=None, rec=None, l15="damageAmp", tags=None, section=None, glyph=None,
      over=None, kind="active", upgrade=None, champs_extra=None):
    """One expansion ability. vfx: {role: stem or [stem, scale]} (the first role's stem is the pack signature)."""
    return dict(id=id_, name=name, delivery=delivery, school=school, types=types, desc=desc, vfx=vfx or {}, effects=eff or [],
                recipe=rec or {}, l15=l15, tags=tags, section=section, glyph=glyph, over=over or {}, kind=kind, upgrade=upgrade)


D, TK, HL = "DPS", "TANK", "HEAL"

# ------------------------------------------------------------------------------------------------ the pool
# ---- A. Pack signatures: every unused A/B (or unrated moving) Niagara system from the purchased packs becomes an active.
SIGNATURES = [
    # Frost (cold)
    A("glacial_eruption", "Glacial Eruption", "circle", "cold", [D], "Ice erupts under the target area after 0.6s: {effect} damage and a 35% slow for 2.5s.",
      vfx={"area": "Ice_Magic_IceSpike2"}, eff=[SLOW()], rec={"warning": .6}, l15="stun", glyph="snowflake"),
    A("rime_spike", "Rime Spike", "circle", "cold", [D, TK], "A single spike of rime pins the spot: {effect} damage and roots enemies for 1.2s.",
      vfx={"impact": "Ice_Magic_IceSpike"}, eff=[ROOTS(1.2)], rec={"warning": .4}, over=dict(radius=180, effect=60, coef=1.1), l15="slow", section="control", glyph="thorn_vine"),
    A("frostcleave", "Frostcleave", "cone", "cold", [TK, D], "A frozen arc in front of you: {effect} damage and a 30% slow for 2s.",
      vfx={"cast": "Ice_Magic_Slash1"}, eff=[SLOW(2, .3)], rec={"angle": 80, "warning": .1}, over=dict(radius=380), l15="vulnerability", section="attack", glyph="cleave"),
    A("frost_pirouette", "Frost Pirouette", "nova", "cold", [TK, D], "Spin in a ring of frost: {effect} damage to every enemy around you and a 35% slow.",
      vfx={"cast": "Ice_Magic_Slash2"}, eff=[SLOW()], over=dict(radius=320), l15="stun", section="attack", glyph="spiral"),
    A("blizzard", "Blizzard", "zone", "cold", [D, HL], "A blizzard rages for 5s: {effect} damage per second and a 35% slow inside.",
      vfx={"area": "Ice_Magic_Snowstorm1"}, eff=[SLOW(1, .35)], over=dict(radius=360), l15="slow", section="control", glyph="snowflake"),
    A("glacier_wall", "Glacier Wall", "wall", "cold", [TK, HL], "Raise a wall of glacier ice ({effect} health) across the aim for 8s: it blocks units and projectiles.",
      vfx={"cast": "Ice_Magic_Wall"}, l15="slow", glyph="wall"),
    # Blood (physical bleeds)
    A("crimson_crystals", "Crimson Crystals", "circle", "physical", [D], "Blood crystals burst from the ground: {effect} damage, the victims bleed for 40% more over 3s and receive 40% less healing.",
      vfx={"area": "Blood_Magic_Crystal5"}, eff=[HEALCUT()], rec={"warning": .5, "bleed": .4}, l15="dot", section="spell", glyph="sigil", tags=["Damage", "Bleed", "Heal Cut"]),
    A("sanguine_lash", "Sanguine Lash", "strike", "physical", [D, TK], "Lash a foe within 7m with a tendril of blood: {effect} damage, and you heal for 35% of it.",
      vfx={"impact": "Blood_Magic_Beam2"}, rec={"lifesteal": .35}, over=dict(range=700, mana=35, energy=0, cooldown=9, effect=70, coef=1.3), l15="healCut", section="spell", glyph="chains", tags=["Damage", "Lifesteal"]),
    A("blood_bolt", "Blood Bolt", "bolt", "physical", [D], "Hurl a bolt of blood: {effect} damage and a bleed for 40% more over 3s.",
      vfx={"projectile": "Blood_Magic_Projectile2"}, rec={"bleed": .4}, l15="dot", section="spell", glyph="drop", tags=["Damage", "Bleed"]),
    A("haemic_orb", "Haemic Orb", "pierce", "physical", [D, HL], "A slow orb of blood pierces through up to 4 enemies for {effect} damage each; you heal for 15% of the damage.",
      vfx={"projectile": "Blood_Magic_Projectile3"}, rec={"hits": 4, "speed": 1500, "lifesteal": .15}, l15="healCut", section="spell", glyph="orb", tags=["Damage", "Lifesteal"]),
    A("hemorrhage", "Hemorrhage", "strike", "physical", [D], "A savage melee strike: {effect} damage and a heavy bleed for 80% more over 3s.",
      vfx={"impact": "BloodBurst_Extreme"}, rec={"bleed": .8}, l15="healCut", section="attack", glyph="claw", tags=["Damage", "Bleed"]),
    A("arterial_slash", "Arterial Slash", "cone", "physical", [D, TK], "Slash in front of you: {effect} damage and every victim bleeds for 30% more over 3s.",
      vfx={"impact": "BloodSplash_High"}, rec={"angle": 90, "warning": .05, "bleed": .3}, over=dict(radius=330), l15="dot", section="attack", glyph="sword", tags=["Damage", "Bleed"]),
    A("crimson_volley", "Crimson Volley", "pierce", "physical", [D], "Loose a piercing shot through up to 3 enemies: {effect} damage each.",
      vfx={"impact": "BulletHit_Sample"}, rec={"hits": 3, "speed": 3000}, over=dict(mana=0, energy=30), l15="vulnerability", section="attack", glyph="arrow"),
    A("blood_pool", "Blood Pool", "zone", "physical", [D], "Spill a pool of blood for 5s: {effect} damage per second; enemies inside receive 40% less healing.",
      vfx={"area": "SphericalDecalSplatter_Extreme"}, eff=[HEALCUT(2)], over=dict(radius=300), l15="healCut", section="control", glyph="drop", tags=["Damage", "Heal Cut"]),
    A("bloodletting", "Bloodletting", "selfBuff", "physical", [D, TK], "Open your veins: deal {effect}% more damage for 8s.",
      vfx={"cast": "Dripping_High"}, rec={"buff": "xp_empowered"}, over=dict(effect=20), l15="dot", glyph="skull", tags=["Empower"]),
    A("lacerate", "Lacerate", "strike", "physical", [D, TK], "Rip into a foe: {effect} damage and a bleed for 60% more over 3s.",
      vfx={"impact": "Dripping_Splash_High"}, rec={"bleed": .6}, over=dict(effect=70, coef=1.3, cooldown=6), l15="dot", section="attack", glyph="claw", tags=["Damage", "Bleed"]),
    A("rending_leap", "Rending Leap", "leap", "physical", [D, TK], "Leap to the spot and rend everything there: {effect} damage and a 25% bleed.",
      vfx={"impact": "Splatter_Omni_02_Decal"}, rec={"bleed": .25}, l15="slow", section="attack", glyph="dash", tags=["Damage", "Gap Closer", "Bleed"]),
    A("savage_cleave", "Savage Cleave", "nova", "physical", [TK, D], "Cleave all around you: {effect} damage; monsters hit attack you for 2s.",
      vfx={"impact": "Splatter_Omni_03_Decal"}, eff=[TAUNT(2)], over=dict(radius=300), l15="vulnerability", section="attack", glyph="axe", tags=["Damage", "Taunt"]),
    A("gore_charge", "Gore Charge", "dash", "physical", [TK, D], "Charge down a lane: {effect} damage to everyone in the way, knocked aside.",
      vfx={"impact": "Splatter_Directional_02_Decal"}, rec={"knockback": 260}, l15="stun", section="attack", glyph="horn", tags=["Damage", "Gap Closer", "Knockback"]),
    A("flay", "Flay", "chain", "physical", [D], "A barbed chain lashes up to 4 enemies: {effect} damage, -20% per bounce, each left bleeding for 20%.",
      vfx={"impact": "Splatter_Omni_04_Decal"}, rec={"hits": 4, "bleed": .2}, l15="healCut", section="attack", glyph="chains", tags=["Damage", "Bleed"]),
    # Poison
    A("toxic_bubble", "Toxic Bubble", "nova", "poison", [TK, D], "Burst a bubble of toxins around you: {effect} damage; victims deal 20% less damage for 4s.",
      vfx={"impact": "Posion_Magic_Shild_Splash"}, eff=[WEAKEN()], over=dict(radius=400), l15="healCut", section="control", glyph="orb", tags=["Damage", "Weaken"]),
    A("venom_dart", "Venom Dart", "bolt", "poison", [D], "A venom dart: {effect} damage and poison for 50% more over 3s.",
      vfx={"projectile": "Posion_Magic_Projectile3"}, rec={"bleed": .5, "speed": 3200}, over=dict(radius=20), l15="dot", section="spell", glyph="venom", tags=["Damage", "Poison"]),
    A("plague_orb", "Plague Orb", "bolt", "poison", [D, HL], "A plague orb: {effect} damage; the victim receives 50% less healing for 6s.",
      vfx={"projectile": "Posion_Magic_Projectile4"}, eff=[HEALCUT(6, .5)], l15="dot", section="spell", glyph="orb", tags=["Damage", "Heal Cut"]),
    # Tide
    A("riptide_bolt", "Riptide Bolt", "bolt", "tide", [D, HL], "A crashing water bolt: {effect} damage and knocks the victim back.",
      vfx={"projectile": "Water_Magic_Projectile2"}, rec={"knockback": 280}, l15="slow", section="spell", glyph="drop", tags=["Damage", "Knockback"]),
    A("tidal_lance", "Tidal Lance", "pierce", "tide", [D], "A lance of pressurised water pierces up to 5 enemies: {effect} damage each.",
      vfx={"projectile": "Water_Magic_Projectile3"}, rec={"hits": 5, "speed": 2800}, l15="vulnerability", section="spell", glyph="javelin"),
    A("maelstrom", "Maelstrom", "zone", "tide", [D, HL], "A maelstrom churns for 5s: {effect} damage per second; enemies inside are slowed 40%.",
      vfx={"area": ["P_ky_aquaStorm", .9]}, eff=[SLOW(1, .4)], over=dict(radius=340), l15="slow", section="control", glyph="spiral", tags=["Damage", "Slow"]),
    # Void / dark
    A("umbral_ward", "Umbral Ward", "barrier", "void", [TK, HL], "Wrap an ally (or yourself) in a void shell absorbing {effect} damage for 6s.",
      vfx={"cast": "Dark_Magic_Shield_Splash"}, l15="purge", glyph="dome", tags=["Shield"]),
    A("void_pyre", "Void Pyre", "zone", "void", [D], "Light a void pyre for 5s: {effect} damage per second to enemies in it.",
      vfx={"area": "Dark_Magic_Dark_Flame"}, over=dict(radius=280, effect=26), l15="dot", section="spell", glyph="flame"),
    A("abyssal_pillar", "Abyssal Pillar", "circle", "void", [D, TK], "A pillar of void fire erupts after 0.7s: {effect} damage and a 1s stun.",
      vfx={"area": "Dark_Magic_Wall1"}, eff=[STUN(1)], rec={"warning": .7}, over=dict(radius=220, cooldown=12), l15="vulnerability", section="control", glyph="tower", tags=["Damage", "Stun"]),
    A("ring_of_ruin", "Ring of Ruin", "nova", "void", [TK, D], "A ring of void flame rises around you: {effect} damage and roots enemies for 1.5s.",
      vfx={"area": "Dark_Magic_Wall2"}, eff=[ROOTS(1.5)], rec={"warning": .2}, over=dict(radius=420, cooldown=14), l15="stun", section="control", glyph="rune_circle", tags=["Damage", "Root"]),
    A("void_bolt", "Void Bolt", "bolt", "void", [D], "A bolt of void: {effect} damage; the victim takes 12% more damage for 5s.",
      vfx={"projectile": "Dark_Magic_Projectile2"}, eff=[MARK()], l15="vulnerability", section="spell", glyph="orb", tags=["Damage", "Mark"]),
    A("rift_line", "Rift Line", "line", "void", [D], "Tear a rift along the ground: {effect} damage to everyone on the line after 0.35s.",
      vfx={"cast": "Dark_Magic_Line_Splash1"}, rec={"warning": .35}, l15="damageAmp", section="spell", glyph="grave_line"),
    # Holy
    A("sunlance", "Sunlance", "bolt", "holy", [D, HL], "A lance of sunlight: {effect} damage.",
      vfx={"projectile": "Light_Magic_Projectile1_Circle"}, l15="purge", section="spell", glyph="beam"),
    A("radiant_orb", "Radiant Orb", "pierce", "holy", [HL, D], "A radiant orb pierces up to 3 enemies for {effect} damage each and strips their buffs.",
      vfx={"projectile": "Light_Magic_Projectile2_Circle"}, eff=[PURGE()], rec={"hits": 3, "speed": 1800}, l15="healCut", section="spell", glyph="sun", tags=["Damage", "Purge"]),
    A("blade_of_judgment", "Blade of Judgment", "line", "holy", [TK, D], "A holy blade falls along the line after 0.4s: {effect} damage and armor broken for 5s.",
      vfx={"cast": "Light_Magic_Sword_Line2"}, eff=[ARMOR()], rec={"warning": .4}, over=dict(radius=70), l15="stun", section="attack", glyph="sword", tags=["Damage", "Armor Break"]),
    # Storm
    A("thunderclap", "Thunderclap", "circle", "storm", [D], "Call a lightning strike on the spot after 0.5s: {effect} damage and a 0.6s stun.",
      vfx={"impact": "P_ky_lightning1"}, eff=[STUN(.6)], rec={"warning": .5}, over=dict(radius=220), l15="vulnerability", section="control", glyph="bolt", tags=["Damage", "Stun"]),
    A("storm_call", "Storm Call", "circle", "storm", [D, HL], "A forked bolt hammers the area after 0.6s: {effect} damage.",
      vfx={"impact": "P_ky_lightning2"}, rec={"warning": .6}, over=dict(radius=300, effect=90, coef=1.5), l15="damageAmp", section="spell", glyph="bolt"),
    A("ground_surge", "Ground Surge", "nova", "storm", [TK, D], "Discharge into the ground around you: {effect} damage; casts are interrupted.",
      vfx={"area": "P_ky_lightning3"}, eff=[INTERRUPT()], over=dict(radius=360), l15="stun", section="control", glyph="seismic", tags=["Damage", "Interrupt"]),
    A("tempest_line", "Tempest Line", "line", "storm", [D], "Send a tornado down the lane: {effect} damage and knocks enemies aside.",
      vfx={"cast": "Lightning_Magic_Tornado_Line"}, rec={"warning": .25, "knockback": 220}, over=dict(radius=110), l15="slow", section="spell", glyph="wind", tags=["Damage", "Knockback"]),
    A("thunder_line", "Thunder Line", "line", "storm", [D], "Thunder rolls along a line: {effect} damage and a 35% slow.",
      vfx={"cast": "Lightning_Magic_Tunder_Line"}, eff=[SLOW()], rec={"warning": .3}, l15="stun", section="spell", glyph="bolt"),
    A("static_arc", "Static Arc", "chain", "storm", [D, HL], "Static arcs between up to 5 enemies: {effect} damage, -20% per bounce.",
      vfx={"impact": "Lightning_Magic_Lightning2"}, rec={"hits": 5}, l15="stun", section="spell", glyph="chain_spark"),
    A("storm_slash", "Storm Slash", "cone", "storm", [TK, D], "A crackling slash in front of you: {effect} damage and silences for 1s.",
      vfx={"cast": "Lightning_Magic_Slash1"}, eff=[SILENCE(1)], rec={"angle": 75, "warning": .05}, over=dict(radius=340), l15="vulnerability", section="attack", glyph="cleave", tags=["Damage", "Silence"]),
    # Shadow
    A("shade_wave", "Shade Wave", "line", "shadow", [D], "A wave of shades rolls down the lane: {effect} damage.",
      vfx={"cast": "Shadow_Magic_Area_Line_Attack2"}, rec={"warning": .3}, over=dict(radius=120), l15="dot", section="spell", glyph="grave_line"),
    A("night_spear", "Night Spear", "line", "shadow", [D], "A narrow spear of night: {effect} damage and silences for 1.5s.",
      vfx={"cast": "Shadow_Magic_Line_Attack3"}, eff=[SILENCE()], rec={"warning": .25}, over=dict(radius=55, range=1000), l15="damageAmp", section="control", glyph="javelin", tags=["Damage", "Silence"]),
    A("dusk_orb", "Dusk Orb", "bolt", "shadow", [D, HL], "A dusk orb: {effect} damage; the victim deals 20% less damage for 4s.",
      vfx={"projectile": "Shadow_Magic_Orb2"}, eff=[WEAKEN()], l15="healCut", section="spell", glyph="moon", tags=["Damage", "Weaken"]),
    A("crystal_prison", "Crystal Prison", "nova", "shadow", [TK, HL], "Shadow crystals ring you: {effect} damage and roots enemies for 2s.",
      vfx={"area": "Shadow_Magic_Wall1"}, eff=[ROOTS(2)], rec={"warning": .25}, over=dict(radius=450, cooldown=16), l15="stun", section="control", glyph="rune_circle", tags=["Damage", "Root"]),
    A("shadow_palisade", "Shadow Palisade", "wall", "shadow", [TK], "Raise a spiked shadow palisade ({effect} health) for 8s: it blocks units and projectiles.",
      vfx={"cast": "Shadow_Magic_Wall2"}, l15="slow", glyph="wall"),
    A("shadow_rend", "Shadow Rend", "line", "shadow", [D, TK], "Rend the shadows along a line: {effect} damage; victims take 12% more damage for 5s.",
      vfx={"cast": "Shadow_Magic_Area_Line_Attack3"}, eff=[MARK()], rec={"warning": .3}, l15="vulnerability", section="spell", glyph="claw", tags=["Damage", "Mark"]),
    A("umbral_scythe", "Umbral Scythe", "line", "shadow", [D], "A scythe of darkness reaps a line: {effect} damage; victims deal 20% less damage for 4s.",
      vfx={"cast": "Shadow_Magic_Area_Line_Attack4"}, eff=[WEAKEN()], rec={"warning": .3}, over=dict(radius=100), l15="healCut", section="spell", glyph="cleave", tags=["Damage", "Weaken"]),
    A("gloom_lance", "Gloom Lance", "line", "shadow", [D], "A lance of gloom pierces the line after 0.2s: {effect} damage.",
      vfx={"cast": "Shadow_Magic_Line_Attack4"}, rec={"warning": .2}, over=dict(radius=60, range=1100, effect=85, coef=1.45), l15="damageAmp", section="spell", glyph="beam"),
    A("shadow_dart", "Shadow Dart", "bolt", "shadow", [D], "A fast shadow dart: {effect} damage.",
      vfx={"projectile": "Shadow_Magic_Projectile3"}, rec={"speed": 3400}, over=dict(cooldown=5, effect=55, coef=1.1, mana=25), l15="dot", section="spell", glyph="feather"),
    # Earth
    A("stone_spire", "Stone Spire", "circle", "earth", [TK, D], "A stone spire bursts up under the spot after 0.5s: {effect} damage and a 0.75s stun.",
      vfx={"area": "Earth_Magic_Spike6"}, eff=[STUN()], rec={"warning": .5}, over=dict(radius=200), l15="vulnerability", section="control", glyph="mountain", tags=["Damage", "Stun"]),
    A("magma_rift", "Magma Rift", "line", "earth", [D, TK], "Split the earth along a line: {effect} damage after 0.45s; victims' armor breaks for 5s.",
      vfx={"cast": "Earth_Spells_Area_Spike_Line3"}, eff=[ARMOR()], rec={"warning": .45}, over=dict(radius=110, effect=85, coef=1.45), l15="stun", section="spell", glyph="seismic", tags=["Damage", "Armor Break"]),
    A("stone_henge", "Stone Henge", "nova", "earth", [TK], "A ring of standing stones slams up around you: {effect} damage; monsters hit attack you for 3s.",
      vfx={"area": "Earth_Spells_Wall4"}, eff=[TAUNT(3)], rec={"warning": .2}, over=dict(radius=420, cooldown=14), l15="stun", section="control", glyph="totem", tags=["Damage", "Taunt"]),
    A("boulder_toss", "Boulder Toss", "bolt", "earth", [TK, D], "Hurl a boulder: {effect} damage and knocks the victim back.",
      vfx={"projectile": "Earth_Spells_Projectile4"}, rec={"knockback": 300, "speed": 2000}, over=dict(radius=45, mana=0, energy=30), l15="stun", section="attack", glyph="meteor", tags=["Damage", "Knockback"]),
    A("spikebreaker", "Spikebreaker", "line", "earth", [TK, D], "Drive a row of spikes along the ground: {effect} damage and a 35% slow.",
      vfx={"cast": "Earth_Spells_Area_Spike_Line4"}, eff=[SLOW()], rec={"warning": .35}, l15="stun", section="control", glyph="pick", tags=["Damage", "Slow"]),
    # Air (arcane)
    A("gale_arrow", "Gale Arrow", "pierce", "arcane", [D], "A gale-borne arrow pierces up to 3 enemies: {effect} damage and knocks them back.",
      vfx={"projectile": "Air_Magic_Arrow3"}, rec={"hits": 3, "speed": 3200, "knockback": 200}, over=dict(radius=24), l15="slow", section="attack", glyph="arrow", tags=["Damage", "Knockback"]),
    A("zephyr_arrow", "Zephyr Arrow", "bolt", "arcane", [D, HL], "A zephyr arrow: {effect} damage and a 35% slow for 2.5s.",
      vfx={"projectile": "Air_Magic_Arrow4"}, eff=[SLOW()], rec={"speed": 3400}, over=dict(radius=22), l15="vulnerability", section="attack", glyph="feather", tags=["Damage", "Slow"]),
    # Fire
    A("cinder_shot", "Cinder Shot", "bolt", "fire", [D], "A cinder shot: {effect} damage and a burn for 40% more over 3s.",
      vfx={"projectile": "Fire_Magic_Projectile5"}, rec={"bleed": .4}, l15="dot", section="spell", glyph="flame", tags=["Damage", "Burn"]),
]

# ---- B. Healing and defence (support playstyles).
SUPPORT = [
    A("mending_rain", "Mending Rain", "healZone", "tide", [HL], "A healing rain falls on the spot for 6s: allies inside heal {effect} per second.", l15="purge", glyph="drop", tags=["Heal"]),
    A("radiant_mend", "Radiant Mend", "heal", "holy", [HL], "Heal an ally (or yourself) for {effect}.", l15="purge", glyph="restoring", tags=["Heal"]),
    A("transfusion", "Transfusion", "heal", "physical", [HL, TK], "Heal an ally for {effect}; they also gain a barrier of 30% of it for 6s.", rec={"barrier": .3}, over=dict(effect=95), l15="healCut", glyph="heart", tags=["Heal", "Shield"]),
    A("verdant_renewal", "Verdant Renewal", "heal", "nature", [HL], "Heal an ally for {effect} and clear their slows and roots.", rec={"cleanse": True}, over=dict(effect=100, cooldown=10), l15="purge", glyph="leaf", tags=["Heal", "Cleanse"]),
    A("stoneguard", "Stoneguard", "barrier", "earth", [TK, HL], "Encase an ally (or yourself) in stone: a barrier absorbing {effect} damage for 6s.", l15="slow", glyph="stone_skin", tags=["Shield"]),
    A("adrenaline", "Adrenaline Surge", "selfBuff", "physical", [D, TK], "Move and attack {effect}% faster for 8s.", rec={"buff": "xp_hastened"}, over=dict(mana=0, energy=25), l15="slow", glyph="rhythm", tags=["Haste"]),
    A("iron_resolve", "Iron Resolve", "selfBuff", "earth", [TK], "Take {effect}% less damage for 8s; slows and roots are cleared.", rec={"buff": "xp_fortified", "cleanse": True}, over=dict(mana=0, energy=25), l15="stun", glyph="shield", tags=["Guard", "Cleanse"]),
    A("rallying_banner", "Rallying Cry", "partyBuff", "physical", [TK, HL], "Allies within 7m deal {effect}% more damage for 8s.", rec={"buff": "xp_empowered"}, over=dict(effect=15), l15="purge", glyph="banner", tags=["Empower"]),
    A("wind_ward", "Tailwind", "partyBuff", "arcane", [HL, D], "Allies within 7m move and attack {effect}% faster for 8s.", rec={"buff": "xp_hastened"}, over=dict(effect=15), l15="slow", glyph="wind", tags=["Haste"]),
]

# ---- C. Summons.
SUMMONS = [
    A("skeletal_warband", "Skeletal Warband", "summon", "shadow", [D], "Raise 3 skeletal warriors for 20s that hunt your target: {effect} damage per hit each.",
      rec={"count": 3, "health": 220, "healthPrimary": 4, "summonName": "Skeletal Warrior", "visual": 1}, l15="dot", glyph="skull", tags=["Summon"]),
    A("stone_sentinel", "Stone Sentinel", "summon", "earth", [TK], "Call a Stone Sentinel for 25s (commandable; it guards you): {effect} damage per hit.",
      rec={"count": 1, "health": 700, "healthPrimary": 12, "commandable": True, "summonName": "Stone Sentinel", "visual": 0}, over=dict(duration=25, effect=18), l15="stun", glyph="guardian", tags=["Summon", "Tank"]),
    A("spirit_wolves", "Spirit Wolves", "summon", "nature", [D, HL], "Two spirit wolves run down your target for 18s: {effect} damage per hit each.",
      rec={"count": 2, "health": 200, "healthPrimary": 4, "moveSpeed": 620, "summonName": "Spirit Wolf", "visual": 2}, over=dict(duration=18), l15="slow", glyph="wolf_head", tags=["Summon"]),
    A("blood_thralls", "Blood Thralls", "summon", "physical", [D, TK], "Two blood thralls rise for 20s and maul your target: {effect} damage per hit each.",
      rec={"count": 2, "health": 260, "healthPrimary": 5, "summonName": "Blood Thrall", "visual": 1}, l15="healCut", glyph="bear_head", tags=["Summon"]),
    A("frost_wraith", "Frost Wraith", "summon", "cold", [D, HL], "A frost wraith haunts your target for 20s from 7m away: {effect} damage per bolt.",
      rec={"count": 1, "health": 260, "healthPrimary": 5, "attackRange": 700, "summonName": "Frost Wraith", "visual": 3}, over=dict(effect=22, coef=.5), l15="slow", glyph="eye", tags=["Summon", "Ranged"]),
    A("radiant_guardian", "Radiant Guardian", "summon", "holy", [HL, TK], "A radiant guardian stands with you for 25s (commandable): {effect} damage per hit.",
      rec={"count": 1, "health": 600, "healthPrimary": 10, "commandable": True, "summonName": "Radiant Guardian", "visual": 0}, over=dict(duration=25), l15="purge", glyph="crown", tags=["Summon"]),
    A("storm_elemental", "Storm Elemental", "summon", "storm", [D], "A storm elemental blasts your target from 8m for 18s: {effect} damage per bolt.",
      rec={"count": 1, "health": 240, "healthPrimary": 4, "attackRange": 800, "summonName": "Storm Elemental", "visual": 3}, over=dict(effect=24, coef=.55, duration=18), l15="stun", glyph="bolt", tags=["Summon", "Ranged"]),
    A("clockwork_knight", "Clockwork Knight", "summon", "arcane", [TK, D], "Wind up a clockwork knight for 25s (commandable): {effect} damage per hit.",
      rec={"count": 1, "health": 650, "healthPrimary": 11, "commandable": True, "summonName": "Clockwork Knight", "visual": 0}, over=dict(duration=25, effect=18), l15="vulnerability", glyph="gear", tags=["Summon", "Tank"]),
]

# ---- D. Constructs (CireTechConstructs recipes).
def C(kind, **kw):
    return {"construct": dict(kind=kind, **kw)}
CONSTRUCTS = [
    A("frost_sentry", "Frost Sentry", "construct", "cold", [D, HL], "Build a frost sentry for 20s ({effect} health, up to 2) that fires ice shards at enemies within 9m.",
      rec=C("turret", health=220, lifetime=20, footprint=60, height=160, range=900, interval=.9, damage=14, limit=2, color="#8fd4ff"), over=dict(effect=220, coef=.35), l15="slow", glyph="turret", tags=["Construct", "Slow"]),
    A("siege_ballista", "Siege Ballista", "construct", "physical", [D], "Assemble a siege ballista for 20s ({effect} health) that fires heavy bolts at enemies within 14m, splashing 1.5m.",
      rec=C("turret", health=300, lifetime=20, footprint=90, height=170, range=1400, interval=1.6, damage=40, splash=150, limit=1, color="#c8a070"), over=dict(effect=300, coef=.8, cooldown=20), l15="vulnerability", glyph="piercing", tags=["Construct"]),
    A("venom_totem", "Venom Totem", "construct", "poison", [D, HL], "Plant a venom totem for 15s ({effect} health): enemies within 4.5m deal 20% less damage.",
      rec=C("pylon", effect="weaken", health=200, lifetime=15, footprint=55, height=190, interval=.5, radius=450, magnitude=.2, limit=1, color="#90e040"), over=dict(effect=200, coef=.3), l15="healCut", glyph="totem", tags=["Construct", "Weaken"]),
    A("warding_obelisk", "Warding Obelisk", "construct", "holy", [HL, TK], "Raise a warding obelisk for 15s ({effect} health): allies within 4.5m regenerate and take less damage.",
      rec=C("pylon", effect="shield", health=240, lifetime=15, footprint=60, height=210, interval=.5, radius=450, magnitude=.02, limit=1, color="#ffe08c"), over=dict(effect=240, coef=.3), l15="purge", glyph="tower", tags=["Construct", "Guard"]),
    A("war_drum", "War Drum", "construct", "physical", [TK, D], "Set down a war drum for 15s ({effect} health): allies within 5m are rallied (+damage).",
      rec=C("pylon", effect="rally", health=260, lifetime=15, footprint=60, height=150, interval=.5, radius=500, magnitude=.12, limit=1, color="#f05a3c"), over=dict(effect=260, coef=.3), l15="stun", glyph="banner", tags=["Construct", "Empower"]),
    A("thunder_coil", "Thunder Coil", "construct", "storm", [D], "Hide a thunder coil for 30s (up to 2): the first enemy near it sets off a {effect}-health coil blasting 2.6m.",
      rec=C("trap", effect="mine", health=60, lifetime=30, footprint=45, height=30, trigger=150, radius=260, damage=80, limit=2, color="#ffe066"), over=dict(effect=60, coef=1.0, cooldown=12), l15="stun", glyph="bolt", tags=["Construct", "Trap"]),
    A("frost_snare", "Frost Snare", "construct", "cold", [TK, D], "Set a frost snare for 30s (up to 3): it freezes the first enemy in place for 1.5s.",
      rec=C("trap", effect="stasis", health=60, lifetime=30, footprint=45, height=20, trigger=150, radius=160, damage=25, magnitude=1.5, limit=3, color="#a8e8ff"), over=dict(effect=60, coef=.3, cooldown=10), l15="slow", glyph="hourglass", tags=["Construct", "Trap", "Stun"]),
    A("clockwork_scarabs", "Clockwork Scarabs", "construct", "arcane", [D], "Release 3 clockwork scarabs that scuttle to enemies and explode for their damage (up to 3 live).",
      rec=C("skitter", effect="mine", health=30, lifetime=12, footprint=40, height=40, range=1400, trigger=110, radius=200, damage=40, speed=540, limit=3, count=3, color="#c0b0ff"), over=dict(effect=30, coef=.55, cooldown=14), l15="damageAmp", glyph="skitter", tags=["Construct"]),
    A("hex_lantern", "Hex Lantern", "construct", "shadow", [HL, D], "Hang a hex lantern for 20s (up to 2): the first enemy near it is silenced for 2s and burned.",
      rec=C("trap", effect="silence", health=60, lifetime=20, footprint=40, height=80, trigger=150, radius=260, damage=50, magnitude=2, limit=2, color="#b070ff"), over=dict(effect=60, coef=.8, cooldown=14), l15="purge", glyph="lantern", tags=["Construct", "Trap", "Silence"]),
]

# ---- E. Passives (CireAbilityExpansion hooks; value = the row's scaled effect %).
def P(id_, name, school, types, hook, effect, aura, desc, glyph, tags, **rec):
    return A(id_, name, "passive", school, types, desc, rec=dict(hook=hook, **rec), l15=None, glyph=glyph, tags=tags,
             over=dict(effect=effect, aura=aura), kind="passive")
PASSIVES = [
    P("reapers_instinct", "Reaper's Instinct", "shadow", [D], "executioner", 20, "crit", "Deal {effect}% more damage to targets below 30% health.", "skull", ["Execute"], threshold=.3),
    P("opportunist", "Opportunist", "physical", [D, TK], "opportunist", 15, "doubleAttack", "Deal {effect}% more damage to stunned, rooted or slowed targets.", "eye", ["Damage"]),
    P("first_blood", "First Blood", "physical", [D], "firstStrike", 15, "rangedDamage", "Deal {effect}% more damage to targets above 80% health.", "arrow", ["Damage"], threshold=.8),
    P("iron_hide", "Iron Hide", "earth", [TK], "thickSkin", 8, "armor", "Take {effect}% less damage.", "stone_skin", ["Guard"]),
    P("last_bastion", "Last Bastion", "holy", [TK, HL], "lastBastion", 25, "stunIgnore", "Below 35% health, take {effect}% less damage.", "last_stand", ["Guard"], threshold=.35),
    P("legions_bond", "Legion's Bond", "arcane", [D, TK, HL], "bond", 20, "aoeResist", "Your summons and constructs deal {effect}% more damage.", "constellation", ["Summon", "Construct"]),
    P("arcane_echo", "Arcane Echo", "arcane", [D, HL], "echo", 50, "magicLifesteal", "Your skills have a 15% chance to hit again for {effect}% of the damage.", "spiral", ["Damage"], chance=.15),
    P("serrated_edge", "Serrated Edge", "physical", [D, TK], "bleedEdge", 40, "physicalLifesteal", "Your skill hits have a 20% chance to bleed for {effect}% of the hit over 3s.", "claw", ["Bleed"], chance=.2),
    P("sanguine_pact", "Sanguine Pact", "physical", [D, TK], "vampiric", 8, "magicResist", "Heal for {effect}% of the damage your skills deal.", "drop", ["Lifesteal"]),
    P("fleetfoot", "Fleetfoot", "nature", [D, TK, HL], "fleet", 8, "attackSpeed", "Move {effect}% faster.", "feather", ["Move Speed"]),
    P("battle_trance", "Battle Trance", "physical", [D, TK], "trance", 12, "stunOnHit", "Attack {effect}% faster.", "rhythm", ["Haste"]),
    P("mana_font", "Mana Font", "arcane", [HL, D], "manaFont", 4, "magicResist", "Each kill restores {effect}% of your max mana.", "wellspring", ["Mana"]),
    P("soul_harvest", "Soul Harvest", "shadow", [TK, D], "soulHarvest", 3, "armor", "Each kill heals you for {effect}% of your max health.", "orb", ["Heal"]),
    P("frostbite", "Frostbite", "cold", [D, TK, HL], "frostbite", 20, "aoeResist", "Your skill hits have a {effect}% chance to slow for 2s.", "snowflake", ["Slow"]),
]

# ---- F. Ultimates.
def U(id_, name, delivery, school, types, desc, upgrade, **kw):
    over = kw.pop("over", {})
    base = dict(effect=200, coef=3.0, mana=130, energy=0, cooldown=90)
    base.update(over)
    return A(id_, name, delivery, school, types, desc, kind="ultimate", upgrade=upgrade, over=base, **kw)
UP = lambda name, text, effects, center="self", delay=0.0: dict(name=name, text=text, center=center, delay=delay, effects=effects)
UX = lambda type_, **kw: dict(type=type_, **kw)
ULTIMATES = [
    U("eye_of_the_void", "Eye of the Void", "zone", "void", [D], "A void storm tears at the area for 6s: {effect} damage per second and a 40% slow.",
      UP("Event Horizon", "Enemies in the storm are also silenced for 2 s.", [UX("silence", radius=500, duration=2)], center="target"),
      vfx={"area": ["P_ky_darkStorm", 1.2]}, eff=[SLOW(1, .4)], over=dict(effect=55, coef=1.1, radius=500, duration=6), l15="vulnerability", glyph="spiral", tags=["Damage", "Slow"]),
    U("heavens_wrath", "Heaven's Wrath", "barrage", "storm", [D], "Call down 6 waves of lightning over a 6m area: {effect} damage per strike.",
      UP("Stormcrown", "You gain +25% attack speed and +15% move speed for 8 s.", [UX("partyBuff", radius=0, duration=8, stats=dict(attackSpeed=25, moveSpeed=15))]),
      vfx={"area": ["P_ky_thunderStorm", 1.0], "impact": "P_ky_lightning2"}, rec={"waves": 6, "hits": 2, "interval": .45, "subRadius": 220, "warning": .5}, over=dict(effect=75, coef=1.2), l15="stun", glyph="starfall", tags=["Damage"]),
    U("meteor_rain", "Meteor Rain", "barrage", "fire", [D, TK], "Meteors rain on a 6m area in 5 waves: {effect} damage per meteor.",
      UP("Scorched Earth", "Enemies within 6 m of the target are also armor-broken for 5 s.", [UX("armorBreak", radius=600, duration=5)], center="target", delay=1.0),
      rec={"waves": 5, "hits": 3, "interval": .5, "subRadius": 200, "warning": .6}, over=dict(effect=70, coef=1.15), l15="dot", glyph="meteor", tags=["Damage"]),
    U("frozen_eternity", "Frozen Eternity", "nova", "cold", [D, TK], "Freeze everything within 6m: {effect} damage and a 2s stun.",
      UP("Permafrost", "Enemies within 6 m are also slowed 35% for 4 s.", [UX("slow", radius=600, duration=4)]),
      eff=[STUN(2)], rec={"warning": .3}, over=dict(radius=600), l15="slow", glyph="snowflake", tags=["Damage", "Stun"]),
    U("sanguine_ascension", "Sanguine Ascension", "selfBuff", "physical", [D, TK], "Ascend in blood: deal {effect}% more damage for 10s and gain a barrier of 200 + 3x Primary.",
      UP("Crimson Tide", "Allies within 8 m also gain +15% attack speed for 8 s.", [UX("partyBuff", radius=800, duration=8, stats=dict(attackSpeed=15))]),
      rec={"buff": "xp_empowered", "barrier": 1.0, "buffValue": 35}, over=dict(effect=200, coef=3.0, duration=10), l15="healCut", glyph="blood_moon" if False else "moon", tags=["Empower", "Shield"]),
    U("call_of_the_legion", "Call of the Legion", "summon", "shadow", [D, TK], "Summon 4 spectral legionnaires for 20s: {effect} damage per hit each.",
      UP("Legion's Might", "You gain +12 primary stat for 12 s.", [UX("partyBuff", radius=0, duration=12, stats=dict(primaryStat=12))]),
      rec={"count": 4, "health": 320, "healthPrimary": 6, "summonName": "Spectral Legionnaire", "visual": 1}, over=dict(effect=26, coef=.6, duration=20), l15="dot", glyph="pack", tags=["Summon"]),
    U("awaken_the_colossus", "Awaken the Colossus", "summon", "earth", [TK], "Awaken a stone colossus for 30s (commandable): {effect} damage per hit, huge health.",
      UP("Mountain's Heart", "Allies within 8 m gain +40 armor for 8 s.", [UX("partyBuff", radius=800, duration=8, stats=dict(armor=40))]),
      rec={"count": 1, "health": 1800, "healthPrimary": 25, "commandable": True, "summonName": "Stone Colossus", "visual": 0}, over=dict(effect=40, coef=.9, duration=30), l15="stun", glyph="mountain", tags=["Summon", "Tank"]),
    U("sanctified_ground", "Sanctified Ground", "healZone", "holy", [HL], "Sanctify a 5m area for 8s: allies inside heal {effect} per second.",
      UP("Hallowed", "Allies within 9 m also gain a shield absorbing 150 + 2x primary stat for 8 s.", [UX("barrier", radius=900, duration=8, amount=150, scaling=2)]),
      over=dict(effect=60, coef=1.2, radius=500, duration=8, cooldown=80), l15="purge", glyph="sanctuary", tags=["Heal"]),
    U("aegis_of_ages", "Aegis of Ages", "partyBuff", "earth", [TK, HL], "Allies within 8m gain a barrier of {effect} and take 20% less damage for 8s.",
      UP("Unbroken", "Allies within 8 m also clear slows and restore 15% of their max mana.", [UX("cleanse", radius=800), UX("restore", radius=800, magnitude=.15)]),
      rec={"buff": "xp_fortified", "buffValue": 20, "barrier": 1.0}, over=dict(effect=180, coef=2.5, radius=800, component="shield", label="barrier health"), l15="purge", glyph="aegis", tags=["Shield", "Guard"]),
    U("warlords_anthem", "Warlord's Anthem", "partyBuff", "physical", [TK, D], "Allies within 8m move and attack 30% faster for 8s and gain a barrier of {effect}.",
      UP("Encore", "Your other cooldowns shrink by 30%.", [UX("cooldownRefund", magnitude=.3)]),
      rec={"buff": "xp_hastened", "buffValue": 30, "barrier": 1.0}, over=dict(effect=120, coef=2.0, radius=800, component="shield", label="barrier health"), l15="slow", glyph="horn", tags=["Haste", "Shield"]),
    U("earthshatter", "Earthshatter", "line", "earth", [TK, D], "Shatter the earth along a 12m line after 0.6s: {effect} damage and a 1.5s stun.",
      UP("Aftershock", "Enemies within 5 m of you are also armor-broken for 6 s.", [UX("armorBreak", radius=500, duration=6)], delay=.6),
      vfx={"cast": ["Earth_Spells_Area_Spike_Line3", 1.3]}, eff=[STUN(1.5)], rec={"warning": .6}, over=dict(range=1200, radius=160), l15="vulnerability", glyph="seismic", tags=["Damage", "Stun"]),
    U("tidal_cataclysm", "Tidal Cataclysm", "zone", "tide", [D, HL], "A tidal vortex rages for 6s: {effect} damage per second and a 50% slow.",
      UP("Undertow", "You and allies within 9 m heal 150.", [UX("heal", radius=900, amount=150)]),
      vfx={"area": ["P_ky_aquaStorm", 1.3]}, eff=[SLOW(1, .5)], over=dict(effect=50, coef=1.0, radius=520, duration=6), l15="slow", glyph="spiral", tags=["Damage", "Slow"]),
]

POOL = SIGNATURES + SUPPORT + SUMMONS + CONSTRUCTS + PASSIVES + ULTIMATES

# ------------------------------------------------------------------------------------------------ Fab VFX picks
SCHOOL_TO_INV = {"cold": ["frost"], "physical": ["blood", "physical"], "poison": ["poison"], "tide": ["tide"], "void": ["void"], "holy": ["holy"],
                 "storm": ["storm"], "shadow": ["shadow"], "earth": ["earth"], "arcane": ["arcane"], "fire": ["fire"], "nature": ["nature", "poison"]}
SCHOOL_TINT = {"cold": [.45, .8, 1.0], "physical": [.8, .08, .08], "poison": [.5, 1.0, .2], "tide": [.2, .6, 1.0], "void": [.6, .2, 1.0],
               "holy": [1.0, .85, .4], "storm": [.55, .75, 1.0], "shadow": [.45, .3, .8], "earth": [1.0, .6, .25], "arcane": [.5, .6, 1.0],
               "fire": [1.0, .45, .1], "nature": [.4, 1.0, .35]}
ROLE_FOR = {"bolt": ["projectile", "impact"], "pierce": ["projectile", "impact"], "line": ["cast", "impact"], "cone": ["cast", "impact"],
            "circle": ["area", "impact"], "zone": ["area"], "nova": ["area", "cast"], "chain": ["impact"], "strike": ["impact"], "leap": ["impact", "area"],
            "dash": ["cast", "impact"], "heal": ["cast"], "healZone": ["area"], "barrier": ["cast"], "selfBuff": ["cast"], "partyBuff": ["cast"],
            "summon": ["cast"], "construct": ["cast"], "wall": ["cast"], "barrage": ["area", "impact"]}
INV_ROLE = {"projectile": {"projectile"}, "impact": {"impact", "cast"}, "cast": {"cast", "impact"}, "area": {"area"}}


ALT_GLYPHS = {
    "bolt": ["orb", "beam", "javelin", "meteor", "arrow", "feather"], "pierce": ["javelin", "piercing", "beam", "arrow"],
    "line": ["grave_line", "beam", "seismic", "trail", "dash", "javelin"], "cone": ["ground_cone", "cinder_cone", "cleave", "wind", "claw"],
    "circle": ["sigil", "rune_circle", "starfall", "cataclysm", "seismic", "meteor"], "zone": ["venom", "sigil", "spiral", "sanctuary", "wellspring", "cataclysm"],
    "nova": ["rune_circle", "seismic", "sun", "aegis", "challenge"], "chain": ["chains", "chain_spark", "bolt", "hook"],
    "strike": ["claw", "sword", "fist", "axe", "cleave", "verdict"], "leap": ["dash", "wing", "hooves"], "dash": ["dash", "horn", "hooves", "trail"],
    "heal": ["restoring", "heart", "renewal", "seed"], "healZone": ["wellspring", "sanctuary", "renewal"], "barrier": ["dome", "aegis", "shield", "bastion"],
    "selfBuff": ["rhythm", "crown", "helm", "fist"], "partyBuff": ["banner", "horn", "crown", "war_cry"], "summon": ["pack", "guardian", "hunt", "wolf_head"],
    "construct": ["turret", "pylon", "tower", "caltrop", "lantern"], "wall": ["wall", "square_ward", "tower"], "barrage": ["starfall", "meteor", "cataclysm"],
    "passive": ["constellation", "eye", "crown", "rune_circle", "helm"]}
ALT_ANY = ["sigil", "rune_circle", "orb", "spiral", "constellation", "eye", "sun", "moon", "crown", "star" ]


def load_inventory(path: Path):
    inv = json.loads(path.read_text(encoding="utf-8"))["systems"]
    return {s["stem"]: s for s in inv}


def pick(inv, school, role, used, index):
    """An A/B, non-swirl system of the school for a secondary role; the least-reused first (recoloured on reuse)."""
    pool = []
    for s in inv.values():
        if s.get("base") or s.get("quality") not in ("A", "B") or "swirl" in (s.get("tags") or []):
            continue
        if s.get("school") not in SCHOOL_TO_INV.get(school, [school]) or s.get("role") not in INV_ROLE[role]:
            continue
        pool.append(s)
    if not pool:
        return None
    pool.sort(key=lambda s: (s.get("quality") != "A", used.get(s["stem"], 0), s["stem"]))
    top = pool[: max(1, min(4, len(pool)))]
    return top[index % len(top)]


def vfx_entries(ab, inv, used, index):
    out = {}
    roles = ROLE_FOR.get(ab["delivery"], [])
    for role, spec in ab["vfx"].items():
        stem, scale = (spec, 1.0) if isinstance(spec, str) else (spec[0], spec[1])
        s = inv.get(stem)
        if not s:
            raise SystemExit(f"{ab['id']}: unknown VFX stem {stem}")
        e = dict(paths=[s["path"]], scale=scale)
        if used.get(stem, 0) > 0 or ab["kind"] == "ultimate" and stem in ("P_ky_aquaStorm", "Earth_Spells_Area_Spike_Line3"):
            e.update(tint=SCHOOL_TINT[ab["school"]], tintStrength=.7)
        used[stem] = used.get(stem, 0) + 1
        out[role] = e
    for role in roles:
        if role in out:
            continue
        s = pick(inv, ab["school"], role, used, index)
        if not s:
            continue
        e = dict(paths=[s["path"]], scale=1.0)
        if used.get(s["stem"], 0) > 0:
            e.update(tint=SCHOOL_TINT[ab["school"]], tintStrength=.7)
        used[s["stem"]] = used.get(s["stem"], 0) + 1
        out[role] = e
    return out

# ------------------------------------------------------------------------------------------------ rows
AUDIO_ELEMENT = {"cold": "frost", "physical": "physical", "poison": "nature", "tide": "water", "void": "shadow", "holy": "holy", "storm": "lightning",
                 "shadow": "shadow", "earth": "earth", "arcane": "arcane", "fire": "fire", "nature": "nature"}
AUDIO_KIND = {"heal": "heal", "healZone": "heal", "barrier": "guard", "selfBuff": "buff", "partyBuff": "shout", "summon": "summon", "construct": "summon",
              "wall": "guard", "passive": "passive", "strike": "melee", "cone": "melee", "nova": "melee", "leap": "melee", "dash": "melee"}
ICON_PALETTE = {"cold": "frost", "physical": "blood", "poison": "venom", "tide": "spectral", "void": "shadow", "holy": "holy", "storm": "ether",
                "shadow": "shadow", "earth": "earth", "arcane": "arcane", "fire": "fire", "nature": "nature"}
SUMMON_BODY = {0: "oathbound_guardian", 1: "spectral_companion", 2: "spectral_companion", 3: "spectral_companion"}
SUMMON_BODY_BY_ID = {"stone_sentinel": "oathbound_guardian", "radiant_guardian": "oathbound_guardian", "clockwork_knight": "mechanical_tank",
                     "awaken_the_colossus": "mechanical_tank"}


def label15(bonus, labels):
    return f"Lv 15: +{labels[bonus]}"


def build_row(ab, base_db):
    d = ab["delivery"]
    kind = ab["kind"]
    tmpl = T.get(d) if d != "passive" else (10, 0, 0, 0, 0, 0, 0, 0, "potency", "%")
    effect, coef, mana, energy, cooldown, rng, radius, duration, component, elabel = tmpl
    o = dict(ab["over"])
    effect = o.get("effect", effect); coef = o.get("coef", coef); mana = o.get("mana", mana); energy = o.get("energy", energy)
    cooldown = o.get("cooldown", cooldown); rng = o.get("range", rng); radius = o.get("radius", radius); duration = o.get("duration", duration)
    component = o.get("component", component); elabel = o.get("label", elabel)
    if ab["school"] == "physical" and d in PHYSICAL_ENERGY and "mana" not in o and kind != "ultimate":
        mana, energy = 0, max(energy, 30)
    if d == "passive":
        mana = energy = cooldown = 0
        elabel = "% " + {"executioner": "damage", "opportunist": "damage", "firstStrike": "damage", "thickSkin": "damage reduction", "lastBastion": "damage reduction",
                         "bond": "unit damage", "echo": "echo damage", "bleedEdge": "bleed", "vampiric": "lifesteal", "fleet": "move speed", "trance": "attack speed",
                         "manaFont": "max mana per kill", "soulHarvest": "max health per kill", "frostbite": "slow chance"}[ab["recipe"]["hook"]]
    if kind == "ultimate" and d in ("selfBuff", "partyBuff") and "component" not in o:
        component = "shield"; elabel = "barrier health"
    cast = o.get("cast", 0.0)
    curve = dict(DB.CURVES[kind])
    if d == "passive":
        curve["effectCap"] = effect * 2
    row = dict(id=ab["id"], name=ab["name"], icon=f"/Game/UI/Abilities/T_{ab['id']}", types=ab["types"], kind=kind, school=ab["school"],
               targeting=TARGETING[d], castTime=cast,
               base=dict(effect=effect, manaCost=mana, energyCost=energy, cooldown=cooldown, castTime=cast, range=rng, radius=radius, duration=duration),
               effectLabel=elabel, curve=curve, status="implemented", description=ab["desc"], effects=ab["effects"], champions=[], signatureOf=[])
    if component in ("potency",):
        row["scaling"] = dict(component="potency", base=0, primary=0, potency=0.4, potencyCap=40)
    else:
        row["scaling"] = dict(component=component, base=effect, primary=coef)
    labels = base_db["level15Labels"]
    if kind == "passive":
        row["aura15"] = dict(aura=o["aura"], label=f"Lv 15: +{base_db['auraLabels'][o['aura']]}")
    else:
        trig = "hit" if d in HIT or (d == "summon") else "pulse"
        row["level15"] = dict(bonus=ab["l15"], label=label15(ab["l15"], labels), trigger=trig)
    if d == "construct":
        row["category"] = "construct"
    if kind == "ultimate":
        row["section"] = "ultimate"
    elif ab["section"]:
        row["section"] = ab["section"]
    elif d in SECTION_OF:
        row["section"] = SECTION_OF[d]
    else:
        cc = any(e["type"] in CC_TYPES for e in ab["effects"])
        row["section"] = "control" if cc else ("attack" if ab["school"] == "physical" else "spell")
    tags = ab["tags"] or (["Damage"] if d in HIT else [])
    row["effectTags"] = tags[:4]
    row["categories"] = [CATEGORY_OF[row["section"]]]
    if any(e["type"] in CC_TYPES for e in ab["effects"]) and "Crowd Control" not in row["categories"]:
        row["categories"].append("Crowd Control")
    if kind == "ultimate":
        row["ultimateUpgrade"] = ab["upgrade"]
    rec = dict(delivery=d, **ab["recipe"])
    if d == "construct":
        rec["construct"] = dict(ab["recipe"]["construct"])
    row["recipe"] = rec
    return row


MAX_PER_CHAMPION = 30


def assign_champions(rows, base_db):
    """Role-matched champions whose own kit leans on the ability's school (affinity), so each pool grows by a readable amount."""
    champs = base_db["champions"]
    abilities = base_db["abilities"]
    affinity = {}
    for cid, c in champs.items():
        counts = {}
        for sid in c["purchasable"]:
            s = abilities[sid]["school"]
            counts[s] = counts.get(s, 0) + 1
        affinity[cid] = counts
    per_champ = {cid: [] for cid in champs}
    for row in rows:
        types = set(row["types"])
        eligible = [cid for cid, c in champs.items() if types & set(c["roles"])]
        broad = row["kind"] in ("passive", "ultimate") or row["recipe"]["delivery"] in ("summon", "construct")
        ranked = sorted(eligible, key=lambda cid: (-affinity[cid].get(row["school"], 0), cid))
        chosen = [cid for cid in ranked if affinity[cid].get(row["school"], 0) >= (1 if broad else 2)]
        if len(chosen) < 4:
            chosen = ranked[: max(4, len(chosen))]
        # Keep each champion's shop readable: an ability goes to its best-matching champions first, lightly loaded ones win ties.
        cap = 10 if broad else 7
        chosen = sorted(chosen, key=lambda cid: (-affinity[cid].get(row["school"], 0), len(per_champ[cid]), cid))
        roomy = [cid for cid in chosen if len(per_champ[cid]) < MAX_PER_CHAMPION]
        if len(roomy) < 3:  # every row stays purchasable by at least 3 champions
            roomy += [cid for cid in sorted(eligible, key=lambda c: (len(per_champ[c]), c)) if cid not in roomy][: 3 - len(roomy)]
        chosen = roomy[:cap]
        row["champions"] = sorted(chosen)
        for cid in chosen:
            per_champ[cid].append(row["id"])
    # Every champion gets at least 12 expansion skills (role-matched, most-affine first).
    for cid, c in champs.items():
        for row in sorted(rows, key=lambda r: -affinity[cid].get(r["school"], 0)):
            if len(per_champ[cid]) >= 12:
                break
            if cid not in row["champions"] and set(row["types"]) & set(c["roles"]):
                row["champions"] = sorted(row["champions"] + [cid]); per_champ[cid].append(row["id"])
    return per_champ


BUFF_MODIFIERS = {
    "xp_empowered": {"type": "magic", "kind": "buff", "name": "Empowered", "mods": [{"stat": "Damage dealt", "value": 20, "unit": "%"}], "line": "Deals more damage."},
    "xp_fortified": {"type": "magic", "kind": "buff", "name": "Fortified", "mods": [{"stat": "Damage taken", "value": -20, "unit": "%"}], "line": "Takes less damage."},
    "xp_hastened": {"type": "magic", "kind": "buff", "name": "Hastened", "mods": [{"stat": "Move", "value": 20, "unit": "%"}, {"stat": "Attack speed", "value": 20, "unit": "%"}], "line": "Moves and attacks faster."},
    "xp_exposed": {"type": "curse", "name": "Exposed", "mods": [{"stat": "Damage taken", "value": 12, "unit": "%"}], "line": "Takes more damage."},
    "xp_bleeding": {"type": "physical", "name": "Bleeding", "mods": [{"stat": "HP", "value": 0, "unit": ""}], "line": "Bleeding: takes damage over time."},
}


def _pal(p, s, c):
    return dict(primary=p, secondary=s, core=c)
BUFF_VISUALS = {
    "xp_empowered": dict(name="Empowered", kind="buff", school="blood", source="ability-expansion: Bloodletting / Rallying Cry / Sanguine Ascension (+damage dealt)",
                         palette=_pal([1.0, .3, .2], [.4, .08, .05], [1.0, .8, .6]), priority=61,
                         layers=[dict(shape="flames", attach="hands", count=4, speed=1.4), dict(shape="glyph", attach="overhead", style="skull", speed=.8)],
                         lifecycle=dict(burstSeconds=.4, fadeSeconds=.35), sound={"start": "npc.feral.start", "end": "buff.expire"}),
    "xp_fortified": dict(name="Fortified", kind="buff", school="earth", source="ability-expansion: Iron Resolve / Aegis of Ages (-damage taken)",
                         palette=_pal([.85, .7, .45], [.35, .25, .12], [1.0, .95, .8]), priority=61,
                         layers=[dict(shape="plates", attach="body", count=6, speed=.5), dict(shape="crystals", attach="ground", count=6, speed=.4), dict(shape="glyph", attach="overhead", style="shield", speed=.6)],
                         lifecycle=dict(burstSeconds=.4, fadeSeconds=.35), sound={"start": "npc.ward.start", "end": "buff.expire"}),
    "xp_hastened": dict(name="Hastened", kind="buff", school="arcane", source="ability-expansion: Adrenaline Surge / Tailwind / Warlord's Anthem (+move and attack speed)",
                        palette=_pal([.6, .85, 1.0], [.12, .25, .45], [.95, 1.0, 1.0]), priority=60,
                        layers=[dict(shape="motes", attach="body", style="chevron_up", count=10, speed=2.0), dict(shape="swirl", attach="ground", speed=1.8)],
                        lifecycle=dict(burstSeconds=.4, fadeSeconds=.35), sound={"start": "npc.rune.start", "end": "buff.expire"}),
    "xp_exposed": dict(name="Exposed", kind="debuff", school="void", source="ability-expansion: Void Bolt / Shadow Rend marks (+damage taken)",
                       palette=_pal([.75, .35, 1.0], [.25, .08, .4], [1.0, .85, 1.0]), priority=60,
                       layers=[dict(shape="glyph", attach="overhead", style="eye", speed=.8), dict(shape="ring", attach="ground", style="spikes", count=8, speed=.9)],
                       lifecycle=dict(burstSeconds=.4, fadeSeconds=.35), sound={"start": "npc.silence.start", "end": "buff.expire"}),
    "xp_bleeding": dict(name="Bleeding", kind="debuff", school="blood", source="ability-expansion: bleeds (Hemorrhage, Lacerate, Serrated Edge...)",
                        palette=_pal([.8, .05, .05], [.35, .02, .02], [1.0, .5, .45]), priority=59,
                        layers=[dict(shape="drips", attach="body", style="blood", count=10, speed=1.4), dict(shape="pool", attach="ground", style="blood", speed=.6)],
                        lifecycle=dict(burstSeconds=.4, fadeSeconds=.35), sound={"start": "debuff.poisoned.start", "end": "buff.expire"}),
}


def silhouette(row):
    return tuple(sorted((l["shape"], l.get("style", ""), l["attach"]) for l in row["layers"]))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--inventory", type=Path, default=ROOT / "Art/Fab/FabVFXInventory.json", help="rated Fab VFX inventory (pack-usage-3)")
    ap.add_argument("--check", action="store_true", help="validate only")
    args = ap.parse_args()
    base_db = json.loads((ROOT / "Content/Data/Abilities.json").read_text(encoding="utf-8"))
    inv = load_inventory(args.inventory)
    errors = []
    ids = [a["id"] for a in POOL]
    if len(ids) != len(set(ids)):
        errors.append("duplicate expansion ids")
    for a in POOL:
        if a["id"] in base_db["abilities"]:
            errors.append(f"{a['id']} collides with a base ability")
        if a["school"] not in DB.SCHOOLS:
            errors.append(f"{a['id']}: school {a['school']}")
        if a["glyph"] and a["glyph"] not in ICONS.GLYPHS:
            errors.append(f"{a['id']}: glyph {a['glyph']}")
        for role, spec in a["vfx"].items():
            stem = spec if isinstance(spec, str) else spec[0]
            s = inv.get(stem)
            if not s:
                errors.append(f"{a['id']}: vfx {stem} missing")
            elif s.get("quality") == "C":
                errors.append(f"{a['id']}: vfx {stem} is C-rated")
    rows = [build_row(a, base_db) for a in POOL]
    per_champ = assign_champions(rows, base_db)
    used = {}
    vfx = {}
    for i, a in enumerate(POOL):
        if a["kind"] == "passive":
            continue
        entries = vfx_entries(a, inv, used, i)
        if entries:
            vfx[a["id"]] = entries
    if errors:
        print("\n".join(errors))
        return 1
    if args.check:
        print(f"OK: {len(rows)} expansion abilities")
        return 0
    out = dict(schemaVersion=1, generator="Tools/BuildAbilityExpansion.py",
               notes="ability-expansion: merged additively into the Ability Database by CireAbilityDB::Reload; 'recipe' is read by CireAbilityExpansion.",
               schools=DB.SCHOOLS, abilities={r["id"]: r for r in rows},
               champions={cid: dict(name=base_db["champions"][cid]["name"], primaryRole=base_db["champions"][cid]["primaryRole"], roles=base_db["champions"][cid]["roles"],
                                    signature=[], purchasable=ids_, purchasableImplemented=ids_) for cid, ids_ in per_champ.items() if ids_},
               buffModifiers=BUFF_MODIFIERS)
    write(ROOT / "Content/Data/AbilitiesExpansion.json", out)
    visuals = json.loads((ROOT / "Content/Data/BuffVisuals.json").read_text(encoding="utf-8"))
    taken = {silhouette(v) for v in visuals["buffs"].values()}
    for k, v in BUFF_VISUALS.items():
        if silhouette(v) in taken:
            raise SystemExit(f"buff visual {k} repeats an existing silhouette")
        taken.add(silhouette(v))
    write(ROOT / "Content/Data/BuffVisuals.expansion.json", dict(schemaVersion=1, notes="ability-expansion: buff visuals of the expansion pool (BuffVisuals.json schema; BuffVisuals.json wins on a clash). Written by Tools/BuildAbilityExpansion.py.",
          limits=visuals.get("limits", {}), buffs=BUFF_VISUALS))
    write(ROOT / "Content/Data/FabVFX.expansion.json", dict(schemaVersion=1, generator="Tools/BuildAbilityExpansion.py",
          notes="ability-expansion: per-ability Fab Niagara signatures of the expansion pool (FabVFX.json 'abilities' format; FabVFX.json wins on a clash).", abilities=vfx))
    audio = {}
    for a in POOL:
        d = a["delivery"]
        kind = AUDIO_KIND.get(d, "spell")
        if a["kind"] == "ultimate" and kind in ("buff", "guard"):
            kind = "shout"
        row = dict(name=a["name"], element=AUDIO_ELEMENT[a["school"]], kind=kind)
        if kind == "melee":
            row["weapon"] = "caster"
        if d in ("bolt", "pierce") and a["school"] == "physical":
            row.update(kind="shot", weapon="bow")
        audio[a["id"]] = row
    write(ROOT / "Content/Data/AudioEvents.expansion.json", dict(schemaVersion=1, generator="Tools/BuildAbilityExpansion.py", abilities=audio))
    icons = {}
    seen = set()
    for a in POOL:
        pal = ICON_PALETTE[a["school"]]
        glow = ICONS.PALETTES[pal][3]
        glyph = a["glyph"] or "rune_circle"
        if (glyph, pal) in seen:  # every expansion icon reads distinctly: same school, next glyph for the delivery
            glyph = next((g for g in ALT_GLYPHS.get(a["delivery"], []) + ALT_ANY if (g, pal) not in seen and g in ICONS.GLYPHS), glyph)
        seen.add((glyph, pal))
        icons[a["id"]] = dict(school=pal, glyph=glyph, accent="#%02x%02x%02x" % glow, pool=False)
    write(ROOT / "Content/Data/AbilityIcons.expansion.json", dict(schemaVersion=1, generator="Tools/BuildAbilityExpansion.py", texturePath="/Game/UI/Abilities/T_<id>", icons=icons))
    summon_art = json.loads((ROOT / "Content/Data/SummonArt.json").read_text(encoding="utf-8"))
    bodies = {s["id"]: s for s in summon_art["summons"]}
    rows_art = []
    for a in POOL:
        if a["delivery"] != "summon":
            continue
        name = a["recipe"]["summonName"]
        slug = re.sub(r"[ -]", "_", name.lower())
        body = bodies.get(SUMMON_BODY_BY_ID.get(a["id"], SUMMON_BODY[a["recipe"].get("visual", 0)]))
        if not body:
            continue
        r = {k: body[k] for k in ("status", "mesh", "locomotion", "attack", "heightCm", "yaw") if k in body}
        r = dict(id=slug, **r, note=f"ability-expansion: {a['name']} reuses the {body['id']} HQ summon body")
        if a["id"] in ("awaken_the_colossus",):
            r["heightCm"] = 260
        rows_art.append(r)
    write(ROOT / "Content/Data/SummonArt.expansion.json", dict(schemaVersion=1, description="ability-expansion: bodies for the expansion summons (SummonArt.json row shape; SummonArt.json wins on a clash). Written by Tools/BuildAbilityExpansion.py.", summons=rows_art))
    write_docs(rows, per_champ, vfx, inv)
    print(f"wrote {len(rows)} expansion abilities; {sum(len(v) for v in per_champ.values())} champion grants; {len(vfx)} with Fab VFX")
    return 0


def write(path: Path, data):
    path.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")


def write_docs(rows, per_champ, vfx, inv):
    by_path = {s["path"]: s for s in inv.values()}
    lines = ["<!-- AUTO:expansion -->", "## Expansion pool (ability-expansion)", "",
             f"{len(rows)} abilities from `Content/Data/AbilitiesExpansion.json` (written by `Tools/BuildAbilityExpansion.py`), merged additively into the",
             "Ability Database by `CireAbilityDB::Reload`; gameplay in `CireAbilityExpansion.cpp` (generic delivery recipes routed from `CireSignatureSkills`).",
             "Every row scales off the PRIMARY stat (damage / heal / barrier / summon and construct hits: `base + coef x Primary`; buffs and passives: potency",
             "+0.4% per Primary, max +40%), carries a level-15 bonus (actives) or team aura (passives), a Skill Shop section, role types and a true telegraph.",
             "Signature Fab systems (`FabVFX.expansion.json`) come from the purchased packs' previously unused A/B-rated (or unrated moving) systems.", "",
             "| Ability | Kind | Delivery | School | Roles | Section | Base (+coef x Primary) | Lv 15 | Signature VFX | Champions |", "|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        sc = r["scaling"]
        num = f"{sc['base']:g} + {sc['primary']:g}x" if sc["component"] != "potency" else f"{r['base']['effect']:g}% (potency)"
        l15 = (r.get("level15") or {}).get("bonus") or (r.get("aura15") or {}).get("aura", "")
        sig = ""
        for role, e in (vfx.get(r["id"]) or {}).items():
            s = by_path.get(e["paths"][0])
            sig = f"{role}: {s['stem'] if s else e['paths'][0].rsplit('.', 1)[-1]}{' (tinted)' if 'tint' in e else ''}"
            break
        lines.append(f"| {r['name']} (`{r['id']}`) | {r['kind']} | {r['recipe']['delivery']} | {r['school']} | {'/'.join(r['types'])} | {r['section']} | {num} {r['effectLabel']} | {l15} | {sig} | {len(r['champions'])} |")
    lines += ["", "Champion pool growth: " + ", ".join(f"{cid} +{len(v)}" for cid, v in sorted(per_champ.items())), "<!-- /AUTO:expansion -->"]
    doc = ROOT / "Docs/Abilities.md"
    text = doc.read_text(encoding="utf-8")
    block = "\n".join(lines)
    if "<!-- AUTO:expansion -->" in text:
        text = re.sub(r"<!-- AUTO:expansion -->.*?<!-- /AUTO:expansion -->", lambda m: block, text, flags=re.S)
    else:
        text = text.rstrip() + "\n\n" + block + "\n"
    doc.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
