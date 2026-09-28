"""Author Content/Data/ParagonChampions.json: Epic's Paragon heroes as playable champions (Docs/ParagonChampions.md).

Input: Saved/ParagonInspect.json (Tools/InspectParagon.py, a UE commandlet over the LOCAL Paragon packs) and the kit
table below (HEROES). Output: one JSON file that only references /Game/Paragon* paths; the packs themselves are
Epic-licensed, gitignored (Content/Paragon*) and never committed.

Every hero: role(s), primary stat, attack style/range, its own Paragon abilities (RMB, Q, E as actives, R as the
ultimate) with the hero's own clip and Cascade/Niagara FX, retuned to this game's numbers (base + coefficient x
PRIMARY, Ability DB curves, Skill Shop sections), plus one passive. Actives 4-6 of the roster draft example come from
the role template champion. The own actives also join the Skill Shop pool of every champion of the same type.

Usage: python Tools/AuthorParagonChampions.py [--check]   (--check exits 1 when the JSON is stale)
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INSPECT = ROOT / "Saved/ParagonInspect.json"
OUT = ROOT / "Content/Data/ParagonChampions.json"
ROSTER = ROOT / "Content/Data/ChampionRoster.json"
ABILITIES = ROOT / "Content/Data/Abilities.json"

# ---------------------------------------------------------------------------------------------------------------
# Kit table. Ability tuple: (slot, name, delivery, school, clip, fx_group, overrides)
#   slot: rmb | q | e | r (ultimate)
#   delivery: projectile pierce cone circle line self_burst strike lunge leap dash pull ally_heal party_heal self_buff storm mark
#   overrides (all optional): effect, coef, cd, cost, range, radius, dur, cast, cc=[(type, dur, mag)], buff, buffPct,
#     angle, warning, hits, speed, style, atAim, taunt, types, section, text
# Passive: (name, kind, effect, text)   kind: guard | lifesteal | haste | frenzy | power
# ---------------------------------------------------------------------------------------------------------------
def A(slot, name, delivery, school, clip, fx, **o):
    return dict(slot=slot, name=name, delivery=delivery, school=school, clip=clip, fx=fx, o=o)


def P(name, kind, effect, text):
    return dict(name=name, kind=kind, effect=effect, text=text)


HEROES = [
    # ------------------------------------------------------------------ batch 1: melee fighters and tanks
    dict(id="pg_greystone", pack="ParagonGreystone", folder="Greystone", name="Greystone", cls="Stoneguard Knight", roles=["tank", "damage"],
         threat="tank", primary="strength", style="sword", range=230, race="human", diff=1,
         lore="A warrior who refuses to fall; stone answers his oath.", quote="I will not break.",
         attack=["Attack_PrimaryA", "Attack_PrimaryB"],
         kit=[A("rmb", "Leap Smash", "leap", "earth", "Attack_RMB", "LeapAOE", effect=120, coef=1.4, cd=11, range=800, radius=300, cc=[("slow", 2, .3)]),
              A("q", "Clear a Path", "line", "physical", "Ability_Q", "ClearAPath", effect=110, coef=1.5, cd=9, range=700, radius=140),
              A("e", "Deflect", "self_buff", "earth", "Ability_E", "Deflect", effect=35, cd=14, dur=3, buff="pg_guard", taunt=True, types=["TANK"], section="defensive",
                text="Raise your guard for 3s: take {effect}% less damage; nearby monsters turn on you."),
              A("r", "Hard to Kill", "self_burst", "holy", "Ability_Ultimate", "Ultimate", effect=240, coef=2.6, cd=75, radius=450, warning=.3, cc=[("stun", 1.2, 0)])],
         passive=P("Stone Oath", "guard", 8, "Take {effect}% less damage from every source.")),
    dict(id="pg_aurora", pack="ParagonAurora", folder="Aurora", name="Aurora", cls="Frost Duelist", roles=["damage", "tank"],
         threat="damage", primary="agility", style="sword", range=230, race="human", diff=2,
         lore="Ice queen of the high passes; the cold is her blade.", quote="Stay frozen.",
         attack=["Primary_Attack_A", "Primary_Attack_B"],
         kit=[A("rmb", "Glacial Charge", "dash", "cold", "Ability_RMB_Fwd", "Dash", cd=10, range=750, buff="pg_haste", buffPct=25, dur=2.5, types=["DPS"], section="defensive",
                text="Skate to the aimed spot on a ribbon of ice and gain 25% move speed for 2.5s."),
              A("q", "Hoarfrost", "self_burst", "cold", "Ability_Q", "Freeze", effect=120, coef=1.4, cd=11, radius=350, warning=.2, cc=[("stun", 1, 0)]),
              A("e", "Frozen Simulacrum", "self_buff", "cold", "Ability_E", "Leap", effect=25, cd=16, dur=4, buff="pg_empower", section="defensive",
                text="An ice double shatters out of you: +{effect}% damage for 4s."),
              A("r", "Cryoseism", "circle", "cold", "Ability_R", "Ultimate", effect=230, coef=2.8, cd=75, range=600, radius=500, warning=.6, cc=[("stun", 1.5, 0)])],
         passive=P("Winter's Edge", "frenzy", 10, "+{effect}% attack speed.")),
    dict(id="pg_crunch", pack="ParagonCrunch", folder="Crunch", name="Crunch", cls="Cybernetic Brawler", roles=["damage", "tank"],
         threat="damage", primary="strength", style="claws", range=220, race="human", diff=1,
         lore="Built for the pit, rebuilt for war.", quote="Next!",
         attack=["Ability_Combo_01", "Ability_Combo_02"],
         kit=[A("rmb", "Dashing Cross", "lunge", "physical", "Ability_DashingCross", "Cross", effect=110, coef=1.5, cd=9, range=650),
              A("q", "Uppercut", "strike", "physical", "Ability_Uppercut", "Uppercut", effect=90, coef=1.3, cd=10, range=260, cc=[("stun", 1, 0)]),
              A("e", "Gut Punch", "cone", "physical", "Ability_GutPunch", "Hook", effect=120, coef=1.5, cd=8, radius=320, angle=70, cc=[("slow", 2, .35)]),
              A("r", "Hook Empowered", "strike", "physical", "Ability_Hook_Empowered", "Ultimate", effect=320, coef=3.2, cd=70, range=300)],
         passive=P("Pit Fighter", "lifesteal", 8, "Heal for {effect}% of your ability damage.")),
    dict(id="pg_fengmao", pack="ParagonFengMao", folder="FengMao", name="Feng Mao", cls="Blade Samurai", roles=["damage", "tank"],
         threat="damage", primary="strength", style="sword", range=230, race="human", diff=2,
         lore="A disgraced samurai who fights to earn his name back.", quote="Steel remembers.",
         attack=["Primary_A", "Primary_B"],
         kit=[A("rmb", "Dash Strike", "lunge", "physical", "Dash_Ability", "Dash", effect=100, coef=1.4, cd=8, range=700),
              A("q", "Tranquility", "self_buff", "arcane", "Shield", "Block", effect=30, cd=14, dur=3, buff="pg_guard", types=["TANK", "DPS"], section="defensive",
                text="Block with the flat of the blade: take {effect}% less damage for 3s."),
              A("e", "Slowing Strike", "cone", "cold", "SlowAbility", "SlowShield", effect=100, coef=1.3, cd=10, radius=350, angle=80, cc=[("slow", 2.5, .4)]),
              A("r", "Blade Rush", "line", "physical", "Ultimate", "Ultimate", effect=260, coef=3.0, cd=70, range=1000, radius=180, warning=.3)],
         passive=P("Ronin's Resolve", "power", 8, "+{effect}% ability damage.")),
    dict(id="pg_grux", pack="ParagonGrux", folder="Grux", name="Grux", cls="Warlord", roles=["tank", "damage"],
         threat="tank", primary="strength", style="axes", range=240, race="orc", diff=1,
         lore="A two-fisted warlord who settles every argument the same way.", quote="Grux smash!",
         attack=["PrimaryAttack_LA", "PrimaryAttack_RA"],
         kit=[A("rmb", "Double Pain", "cone", "physical", "DoublePain", "HardKnocks", effect=120, coef=1.5, cd=8, radius=320, angle=90),
              A("q", "Stampede", "leap", "earth", "Stampede", "Stampede", effect=100, coef=1.3, cd=12, range=850, radius=280, cc=[("slow", 2, .35)]),
              A("e", "Rippling Smash", "self_burst", "earth", "PrimaryAttack_FourStrikes", "RipplingSmash", effect=110, coef=1.4, cd=10, radius=380, warning=.25, taunt=True, types=["TANK"]),
              A("r", "Warlord's Challenge", "self_burst", "physical", "Ultimate_Roar", "Ultimate", effect=200, coef=2.6, cd=75, radius=550, warning=.2, taunt=True, cc=[("stun", 1.5, 0)], types=["TANK"])],
         passive=P("Thick Hide", "guard", 10, "Take {effect}% less damage from every source.")),
    dict(id="pg_steel", pack="ParagonSteel", folder="Steel", name="Steel", cls="Bulwark", roles=["tank", "support"],
         threat="tank", primary="strength", style="flail", range=230, race="human", diff=1,
         lore="A walking fortress; his shield has never been lowered.", quote="Behind me.",
         idle="Steel_Idle_Combat", run="Steel_Run_Fwd_Combat", walk="Steel_Walk_Fwd_Combat",
         attack=["Steel_Attack_Melee_A", "Steel_Attack_Melee_B"], death="Steel_Death_A",
         kit=[A("rmb", "Shield Bash", "strike", "physical", "Steel_Ability_Bash", "Bash", effect=90, coef=1.2, cd=9, range=280, cc=[("stun", 1, 0)], taunt=True, types=["TANK"]),
              A("q", "Cow Catcher", "dash", "physical", "Steel_Ability_CowCatcher_Charge", "Primary", cd=12, range=800, buff="pg_guard", buffPct=20, dur=2, types=["TANK"], section="defensive",
                text="Charge to the aimed spot behind your shield; take 20% less damage for 2s."),
              A("e", "Shield Wall", "self_buff", "holy", "Steel_Ability_ShieldBlock_Deploy", "ShieldBlock", effect=40, cd=16, dur=4, buff="pg_guard", taunt=True, types=["TANK"], section="defensive",
                text="Plant the tower shield: take {effect}% less damage for 4s; nearby monsters turn on you."),
              A("r", "Ground Smash", "self_burst", "earth", "Steel_Ability_GroundSmash_End", "Ultimate", effect=220, coef=2.6, cd=75, radius=500, warning=.4, cc=[("stun", 1.5, 0)], types=["TANK"])],
         passive=P("Ability Armor", "guard", 12, "Take {effect}% less damage from every source.")),
    dict(id="pg_terra", pack="ParagonTerra", folder="Terra", name="Terra", cls="Mountain Warden", roles=["tank", "damage"],
         threat="tank", primary="strength", style="axes", range=240, race="human", diff=2,
         lore="The mountains sent her down to settle a debt.", quote="The earth remembers.",
         attack=["Primary_Melee_A_slow", "Primary_Melee_B_slow"],
         kit=[A("rmb", "Shield Charge", "dash", "earth", "RMB_melee", "Shield", cd=11, range=750, buff="pg_guard", buffPct=20, dur=2.5, types=["TANK"], section="defensive",
                text="Charge behind your shield to the aimed spot; take 20% less damage for 2.5s."),
              A("q", "Double Sweep", "cone", "physical", "Q_swing_B", "DoubleSweep", effect=120, coef=1.5, cd=8, radius=340, angle=110),
              A("e", "Balance", "self_burst", "earth", "E_ability", "Balance", effect=100, coef=1.3, cd=12, radius=380, taunt=True, cc=[("slow", 2, .3)], types=["TANK"]),
              A("r", "Wrath of the Mountain", "self_burst", "earth", "R_Intro", "Ultimate", effect=230, coef=2.7, cd=75, radius=500, warning=.4, cc=[("stun", 1.2, 0)])],
         passive=P("Stone Skin", "guard", 10, "Take {effect}% less damage from every source.")),
    dict(id="pg_sevarog", pack="ParagonSevarog", folder="Sevarog", name="Sevarog", cls="Soul Reaper", roles=["tank", "damage"],
         threat="tank", primary="strength", style="axes", range=250, race="undead", diff=2,
         lore="Lord of the harvest; every soul he takes makes him larger.", quote="Your soul is mine.",
         attack=["Swing1_Medium", "Swing2_Medium"],
         kit=[A("rmb", "Soul Siphon", "strike", "shadow", "Soul_Siphon", "SoulSiphon", effect=110, coef=1.4, cd=9, range=600, cc=[("slow", 2, .3)]),
              A("q", "Relentless", "self_buff", "shadow", "Speedburst", "Relentless", effect=30, cd=14, dur=4, buff="pg_haste", section="defensive",
                text="Surge forward: +{effect}% move speed for 4s."),
              A("e", "Stage of Harvest", "cone", "shadow", "Swing3_Slow", "SoulStackPassive", effect=120, coef=1.5, cd=8, radius=340, angle=90),
              A("r", "Subjugate", "circle", "shadow", "Subjugation", "Subjugate", effect=260, coef=3.0, cd=75, range=500, radius=350, warning=.5, cc=[("stun", 1.2, 0)])],
         passive=P("Soul Eater", "lifesteal", 10, "Heal for {effect}% of your ability damage.")),
    dict(id="pg_rampage", pack="ParagonRampage", folder="Rampage", name="Rampage", cls="Jungle King", roles=["tank", "damage"],
         threat="tank", primary="strength", style="claws", range=250, race="beast", diff=1,
         lore="The apex of the jungle; everything else is prey.", quote="RAAARGH!",
         attack=["Attack_Biped_Melee_A", "Attack_Biped_Melee_B"],
         kit=[A("rmb", "Ground Smash", "leap", "earth", "Ability_RMB_Smash", "Lunge", effect=110, coef=1.4, cd=11, range=750, radius=300, cc=[("slow", 2, .3)]),
              A("q", "Rip and Toss", "circle", "earth", "Ability_RipNToss_Toss", "RipNToss", effect=130, coef=1.6, cd=10, range=1000, radius=260, warning=.5, cc=[("stun", .8, 0)]),
              A("e", "Energize", "self_buff", "nature", "Ability_Energize", "Roar", effect=30, cd=15, dur=5, buff="pg_frenzy", section="defensive",
                text="Energize: +{effect}% attack speed for 5s."),
              A("r", "Enrage", "self_burst", "physical", "Ability_Enrage_End", "Ultimate", effect=220, coef=2.6, cd=70, radius=450, warning=.3, taunt=True)],
         passive=P("King of the Jungle", "lifesteal", 8, "Heal for {effect}% of your ability damage.")),
    dict(id="pg_kwang", pack="ParagonKwang", folder="Kwang", name="Kwang", cls="Heaven's Blade", roles=["damage", "tank"],
         threat="damage", primary="strength", style="sword", range=240, race="human", diff=2,
         lore="Chosen by a sword of light that will not let him rest.", quote="By the light.",
         attack=["PrimaryAttack_A_Slow", "PrimaryAttack_B_Slow"],
         kit=[A("rmb", "Heaven's Bolt", "circle", "holy", "Ability_RMB", "LightStrike", effect=120, coef=1.5, cd=9, range=900, radius=240, warning=.5),
              A("q", "Sword of Light", "pierce", "holy", "Ability_Q_Throw", "Sword", effect=110, coef=1.4, cd=10, range=1100, radius=40, hits=4, style="holy"),
              A("e", "Hallowed Guard", "self_buff", "holy", "Ability_Q_Catch", "Primary", effect=30, cd=15, dur=3, buff="pg_guard", section="defensive",
                text="Catch the returning light: take {effect}% less damage for 3s."),
              A("r", "Judgement of Heaven", "self_burst", "holy", "Ability_R", "Ultimate", effect=250, coef=2.9, cd=75, radius=450, warning=.5, cc=[("stun", 1, 0)])],
         passive=P("Chosen of the Blade", "power", 8, "+{effect}% ability damage.")),
    # ------------------------------------------------------------------ batch 2: assassins and melee DPS
    dict(id="pg_kallari", pack="ParagonKallari", folder="Kallari", name="Kallari", cls="Shadow Assassin", roles=["damage"],
         threat="damage", primary="agility", style="claws", range=220, race="human", diff=3,
         lore="Born in the shadow plane; she only visits.", quote="You never saw me.",
         attack=["Attack_Melee_A", "Attack_Melee_B"], death="Death_A",
         kit=[A("rmb", "Shadow Strike", "lunge", "shadow", "Shadowstrike_Mid", "ShadowPlane", effect=110, coef=1.5, cd=8, range=700),
              A("q", "Dagger Throw", "projectile", "shadow", "Attack01", "DaggerThrow", effect=100, coef=1.4, cd=6, range=1100, style="arrow", cc=[("slow", 1.5, .3)]),
              A("e", "Marked for Death", "mark", "shadow", "Ability_MarkedForDeath_Cast", "Passive", effect=15, cd=12, range=900, dur=6, section="control",
                text="Mark a target for 6s: it takes {effect}% more damage from everyone."),
              A("r", "Death Blossom", "self_burst", "shadow", "Ability_Ultimate", "Ultimate", effect=280, coef=3.2, cd=70, radius=380, warning=.1)],
         passive=P("Shadow Plane", "haste", 8, "+{effect}% move speed.")),
    dict(id="pg_countess", pack="ParagonCountess", folder="Countess", name="Countess", cls="Blood Countess", roles=["damage"],
         threat="damage", primary="intelligence", style="claws", range=220, race="undead", diff=3,
         lore="She dines on the living and never grows old.", quote="Delicious.",
         idle="Idle_Relaxed", attack=["Primary_Attack_A_Normal", "Primary_Attack_B_Normal"],
         kit=[A("rmb", "Blink Strike", "lunge", "shadow", "Ability_RMB", "BlinkStrike", effect=110, coef=1.5, cd=8, range=750),
              A("q", "Blade Siphon", "cone", "shadow", "Ability_Q", "BladeSiphon", effect=120, coef=1.6, cd=9, radius=380, angle=80),
              A("e", "Rolling Dark", "line", "shadow", "Ability_E", "RollingDark", effect=100, coef=1.3, cd=11, range=900, radius=150, cc=[("silence", 1.2, 0)]),
              A("r", "Feast", "self_burst", "shadow", "Ability_Ultimate", "Ultimate", effect=260, coef=3.0, cd=70, radius=450, warning=.2, cc=[("slow", 2, .4)])],
         passive=P("Bloodthirst", "lifesteal", 12, "Heal for {effect}% of your ability damage.")),
    dict(id="pg_shinbi", pack="ParagonShinbi", folder="Shinbi", name="Shinbi", cls="Wolf Dancer", roles=["damage"],
         threat="damage", primary="agility", style="sword", range=220, race="human", diff=2,
         lore="Her blades dance; her wolves finish the song.", quote="Dance with me.",
         attack=["PrimaryMelee_B_Slow", "PrimaryMelee_C_Slow"], death="Death_A",
         kit=[A("rmb", "Rending Wolves", "pierce", "physical", "Ability_AttackWolves_Cast_L", "AttackWolves", effect=100, coef=1.4, cd=7, range=1000, radius=60, hits=5, style="arrow"),
              A("q", "Circling Wolves", "storm", "physical", "Ability_CirclingWolves", "CirclingWolves", effect=140, coef=1.6, cd=12, radius=300, dur=3),
              A("e", "Wolf Dash", "dash", "physical", "Ability_Dash", "Dash", cd=9, range=700, buff="pg_empower", buffPct=15, dur=3, section="defensive",
                text="Dash to the aimed spot; +15% damage for 3s."),
              A("r", "Dance of Death", "self_burst", "physical", "Ability_R_PeaceOut", "Ultimate", effect=280, coef=3.2, cd=70, radius=420, warning=.3)],
         passive=P("Wolf Pack", "frenzy", 10, "+{effect}% attack speed.")),
    dict(id="pg_khaimera", pack="ParagonKhaimera", folder="Khaimera", name="Khaimera", cls="Bloodhound", roles=["damage"],
         threat="damage", primary="strength", style="claws", range=230, race="beast", diff=1,
         lore="A hunter with a bottomless appetite for the chase.", quote="I smell fear.",
         attack=["Melee_A", "Melee_B"], death="Death_A",
         kit=[A("rmb", "Leap", "leap", "physical", "RMB_60fps", "", effect=100, coef=1.4, cd=10, range=800, radius=260),
              A("q", "Glaive Throw", "projectile", "physical", "E_Ability_Throw", "", effect=110, coef=1.5, cd=7, range=1000, style="arrow", cc=[("slow", 1.5, .3)]),
              A("e", "Bloodlust", "self_buff", "physical", "Melee_C", "", effect=30, cd=14, dur=5, buff="pg_frenzy", section="defensive",
                text="Frenzy: +{effect}% attack speed for 5s."),
              A("r", "Dash of Blood", "lunge", "physical", "R_Ability", "", effect=300, coef=3.2, cd=65, range=900)],
         passive=P("Hunter's Appetite", "lifesteal", 10, "Heal for {effect}% of your ability damage.")),
    dict(id="pg_serath", pack="ParagonSerath", folder="Serath", name="Serath", cls="Fallen Seraph", roles=["damage"],
         threat="damage", primary="strength", style="flail", range=230, race="spirit", diff=2,
         lore="Cast down from the heavens, she takes vengeance on both.", quote="Heaven falls.",
         attack=["Primary_Attack_A_Medium", "Primary_Attack_B_Medium"],
         kit=[A("rmb", "Ascend", "leap", "holy", "RMB_Dive", "Ascend", effect=120, coef=1.5, cd=12, range=900, radius=280),
              A("q", "Heaven's Fury", "cone", "holy", "Q_Ability_Attack_A", "Fury", effect=120, coef=1.5, cd=8, radius=360, angle=80),
              A("e", "Chastise", "self_burst", "holy", "E_Ability_Wing_Flap", "Chastise", effect=90, coef=1.2, cd=11, radius=350, cc=[("slow", 2, .4)]),
              A("r", "Retribution", "circle", "holy", "R_Ability_Loop", "Ultimate", effect=260, coef=3.0, cd=75, range=700, radius=450, warning=.6)],
         passive=P("Wings of Wrath", "power", 8, "+{effect}% ability damage.")),
    dict(id="pg_wukong", pack="ParagonSunWukong", folder="Wukong", name="Sun Wukong", cls="Monkey King", roles=["damage"],
         threat="damage", primary="agility", style="lance", range=250, race="beast", diff=2,
         lore="The trickster king of the mountain; his staff grows with his mood.", quote="Catch me!",
         attack=["Primary_Melee_A_Slow", "Primary_Melee_B_Slow"],
         kit=[A("rmb", "Staff Poke", "line", "physical", "RMB_Push", "Poke", effect=110, coef=1.4, cd=8, range=800, radius=120),
              A("q", "Cloud Flip", "leap", "storm", "Q_Slam", "DoubleJump", effect=100, coef=1.3, cd=10, range=750, radius=250, cc=[("slow", 1.5, .3)]),
              A("e", "Mischief", "self_buff", "arcane", "RMB_Hit", "Toggle", effect=30, cd=14, dur=4, buff="pg_haste", section="defensive",
                text="Tumble clear: +{effect}% move speed for 4s."),
              A("r", "Staff of Heaven", "self_burst", "storm", "RMB_Hit_Down", "Ultimate", effect=270, coef=3.1, cd=70, radius=450, warning=.3, cc=[("stun", 1, 0)])],
         passive=P("Nimble Trickster", "haste", 8, "+{effect}% move speed.")),
    dict(id="pg_yin", pack="ParagonYin", folder="Yin", name="Yin", cls="Lash Dancer", roles=["damage"],
         threat="damage", primary="agility", style="sword", range=230, race="human", diff=2,
         lore="Her whip speaks before her blade does.", quote="Pain is a lesson.",
         attack=["Primary_Attack_A_Slow", "Primary_Attack_B_Slow"],
         kit=[A("rmb", "Whip Crack", "cone", "physical", "RMB", "WhipCrack", effect=110, coef=1.4, cd=7, radius=450, angle=40),
              A("q", "Reaching Pain", "pull", "physical", "Q_Pull_Kick", "ReachingPain", effect=100, coef=1.3, cd=12, range=900, radius=70, cc=[("stun", .6, 0)]),
              A("e", "Lash Kick", "strike", "physical", "E_Ability_Attack_A", "LashKick", effect=130, coef=1.6, cd=7, range=300),
              A("r", "Thousand Cuts", "self_burst", "physical", "R_Ability", "Ultimate", effect=270, coef=3.1, cd=70, radius=420, warning=.2, cc=[("slow", 2, .4)])],
         passive=P("Blade Rhythm", "frenzy", 10, "+{effect}% attack speed.")),
    dict(id="pg_boris", pack="ParagonBoris", folder="Boris", name="Boris", cls="Serum Beast", roles=["damage", "tank"],
         threat="damage", primary="strength", style="claws", range=230, race="beast", diff=2,
         lore="A soldier turned beast by the serum he still injects.", quote="Another dose.",
         attack=["Primary_A", "Primary_B"],
         kit=[A("rmb", "Swipe", "cone", "physical", "RMB", "Swipe", effect=120, coef=1.5, cd=7, radius=330, angle=100),
              A("q", "Tracker", "lunge", "nature", "Q_Tracker_Ability_Success", "Pull", effect=100, coef=1.4, cd=10, range=850, cc=[("slow", 2, .3)]),
              A("e", "Inject", "self_buff", "poison", "E_Inject_Ability", "Forage", effect=30, cd=15, dur=5, buff="pg_frenzy", section="defensive",
                text="Inject the serum: +{effect}% attack speed for 5s."),
              A("r", "Unleashed", "self_burst", "nature", "Ultimate", "Ultimate", effect=240, coef=2.8, cd=70, radius=450, warning=.3, taunt=True)],
         passive=P("Serum Rush", "lifesteal", 10, "Heal for {effect}% of your ability damage.")),
    dict(id="pg_riktor", pack="ParagonRiktor", folder="Riktor", name="Riktor", cls="Chain Warden", roles=["tank", "support"],
         threat="tank", primary="strength", style="flail", range=230, race="human", diff=2,
         lore="He drags criminals to justice, one chain at a time.", quote="Get over here.",
         attack=["PrimaryAttack_A_Slow", "PrimaryAttack_B_Slow"],
         kit=[A("rmb", "Hook", "pull", "storm", "Ability_Hook_Cast", "Hook", effect=100, coef=1.3, cd=12, range=1000, radius=70, types=["TANK"], cc=[("stun", .5, 0)]),
              A("q", "Shocking Punch", "strike", "storm", "Ability_ShockingPunch", "ShockingPunch", effect=110, coef=1.4, cd=9, range=280, cc=[("stun", 1, 0)], types=["TANK"]),
              A("e", "Lockdown", "self_burst", "storm", "Ability_Lockdown", "ShockingGround", effect=90, coef=1.2, cd=12, radius=380, taunt=True, cc=[("slow", 2.5, .4)], types=["TANK"]),
              A("r", "Electro Chain", "self_burst", "storm", "Ability_Ultimate", "Ultimate", effect=220, coef=2.6, cd=75, radius=550, warning=.4, cc=[("stun", 1.5, 0)], types=["TANK"])],
         passive=P("Warden's Plate", "guard", 10, "Take {effect}% less damage from every source.")),
    # ------------------------------------------------------------------ batch 3: marksmen
    dict(id="pg_sparrow", pack="ParagonSparrow", folder="Sparrow", name="Sparrow", cls="Warrior Archer", roles=["damage"],
         threat="damage", primary="agility", style="bow", range=1400, race="human", diff=1,
         lore="The warrior princess who never misses twice.", quote="Draw. Loose.",
         idle="idle", attack=["Primary_Fire_Med", "Primary_Fire_Slow"],
         kit=[A("rmb", "Draw a Bead", "pierce", "physical", "RMB_Fire", "DrawABead", effect=140, coef=1.7, cd=9, range=1500, radius=35, hits=3, style="arrow", speed=3200),
              A("q", "Rain of Arrows", "circle", "physical", "Q_Ability", "RainOfArrows", effect=120, coef=1.5, cd=11, range=1200, radius=320, warning=.6, cc=[("slow", 2, .35)]),
              A("e", "Inner Fire", "self_buff", "fire", "R_Ability_Med_Fire", "Primary", effect=30, cd=15, dur=5, buff="pg_frenzy", section="defensive",
                text="Burn bright: +{effect}% attack speed for 5s."),
              A("r", "Arrow Storm", "storm", "fire", "R_Ability_Fast_Fire", "Ultimate", effect=320, coef=3.2, cd=75, range=1200, radius=450, dur=4, atAim=True)],
         passive=P("Keen Eye", "power", 8, "+{effect}% ability damage.")),
    dict(id="pg_twinblast", pack="ParagonTwinblast", folder="TwinBlast", name="TwinBlast", cls="Double Gunslinger", roles=["damage"],
         threat="damage", primary="agility", style="blunderbuss", range=1200, race="human", diff=1,
         lore="Two pistols, one ego, no plan.", quote="Double trouble!",
         attack=["Primary_Fire_Med_A", "Primary_Fire_Med_B"],
         kit=[A("rmb", "Nitro Dash", "dash", "fire", "Ability_NitroV2", "Nitro", cd=8, range=700, buff="pg_haste", buffPct=25, dur=2, section="defensive",
                text="Boost to the aimed spot; +25% move speed for 2s."),
              A("q", "Vortex Grenade", "circle", "fire", "Ability_VortexGrenade_Fire", "VortexGrenade", effect=120, coef=1.5, cd=10, range=1000, radius=300, warning=.5, cc=[("slow", 2, .4)]),
              A("e", "Charge Blast", "projectile", "fire", "Ability_ChargeBlast_Fire", "Primary", effect=140, coef=1.7, cd=8, range=1200, radius=45, style="arcane"),
              A("r", "Double Trouble", "cone", "fire", "Ability_Ultimate_Fire", "Ultimate", effect=300, coef=3.1, cd=70, radius=900, angle=45, warning=.1)],
         passive=P("Showboat", "frenzy", 12, "+{effect}% attack speed.")),
    dict(id="pg_murdock", pack="ParagonMurdock", folder="Murdock", name="Murdock", cls="Bounty Marshal", roles=["damage"],
         threat="damage", primary="agility", style="blunderbuss", range=1000, race="human", diff=1,
         lore="The law's last shotgun on the frontier.", quote="Justice is served.",
         attack=["Fire_Fast", "Moving_Fire_Fast"],
         kit=[A("rmb", "Spread Shot", "cone", "physical", "SpreadShot", "SpreadShot", effect=130, coef=1.6, cd=7, radius=700, angle=45),
              A("q", "Tazer Trap", "circle", "storm", "TazerTrap", "TazerTrap", effect=80, coef=1.0, cd=12, range=900, radius=240, warning=.4, cc=[("stun", 1.2, 0)]),
              A("e", "Gun Shield", "self_buff", "storm", "Shield", "GunShield", effect=35, cd=15, dur=3, buff="pg_guard", section="defensive",
                text="Deploy the gun shield: take {effect}% less damage for 3s."),
              A("r", "The Eleven", "pierce", "storm", "TheEleven", "Ultimate", effect=380, coef=3.5, cd=80, range=1900, radius=90, hits=8, style="arcane", speed=5000)],
         passive=P("Marshal's Grit", "power", 8, "+{effect}% ability damage.")),
    dict(id="pg_revenant", pack="ParagonRevenant", folder="Revenant", name="Revenant", cls="Revenant Gunslinger", roles=["damage"],
         threat="damage", primary="agility", style="blunderbuss", range=1300, race="undead", diff=2,
         lore="Death sent him back to collect.", quote="Your time's up.",
         attack=["Primary_Fire_Med", "Primary_Fire_Slow"], death="Death_Forward",
         kit=[A("rmb", "Haunting Shot", "projectile", "shadow", "RMB", "Primary", effect=150, coef=1.8, cd=8, range=1400, radius=40, style="arcane", speed=3500),
              A("q", "Mark of the Revenant", "mark", "shadow", "Q_Ability", "Mark", effect=15, cd=12, range=1300, dur=6, section="control",
                text="Mark a target for 6s: it takes {effect}% more damage from everyone."),
              A("e", "Obliterate", "circle", "shadow", "E_Ability", "Obliterate", effect=130, coef=1.6, cd=11, range=1100, radius=300, warning=.5, cc=[("silence", 1.2, 0)]),
              A("r", "Death Sentence", "pierce", "shadow", "R_Ability", "Ultimate", effect=360, coef=3.4, cd=75, range=2000, radius=70, hits=5, style="arcane", speed=4000)],
         passive=P("Undying Hunt", "lifesteal", 8, "Heal for {effect}% of your ability damage.")),
    dict(id="pg_wraith", pack="ParagonWraith", folder="Wraith", name="Wraith", cls="Shadow Sniper", roles=["damage"],
         threat="damage", primary="agility", style="blunderbuss", range=1500, race="human", diff=3,
         lore="A sniper who works from a place the eye can't follow.", quote="One shot.",
         attack=["Fire_A_Slow", "Fire_A_Fast_V1"], death="Death_Forward",
         kit=[A("rmb", "Scoped Shot", "projectile", "shadow", "Ability_RMB_Loop", "ScopedShot", effect=170, coef=1.9, cd=9, range=1800, radius=35, style="arcane", speed=5000),
              A("q", "Shadow Canon", "circle", "shadow", "Ability_Q_Fire", "Drone", effect=110, coef=1.4, cd=11, range=1200, radius=280, warning=.4, cc=[("silence", 1, 0)]),
              A("e", "Shadow Step", "dash", "shadow", "Ability_E", "TeleportTarget", cd=10, range=900, buff="pg_empower", buffPct=15, dur=3, section="defensive",
                text="Blink to the aimed spot; +15% damage for 3s."),
              A("r", "Deadeye", "pierce", "shadow", "Ability_R", "Ultimate", effect=400, coef=3.6, cd=80, range=2000, radius=60, hits=4, style="arcane", speed=6000)],
         passive=P("Patient Hunter", "power", 10, "+{effect}% ability damage.")),
    dict(id="pg_grim", pack="ParagonGRIMexe", folder="GRIM", name="GRIM.exe", cls="Salvage Gunner", roles=["damage"],
         threat="damage", primary="agility", style="blunderbuss", range=1200, race="ether-construct", diff=2,
         lore="A scrap-built war machine running a stolen conscience.", quote="Target acquired.",
         attack=["Primary_Fire_Med_A", "Primary_Fire_Slow_A"],
         kit=[A("rmb", "Deathbolt", "projectile", "storm", "Ability_Deathbolt_Fire", "BFG", effect=150, coef=1.8, cd=9, range=1300, radius=55, style="arcane"),
              A("q", "Sap Zap", "circle", "storm", "Ability_SapZap_Fire", "SlowOnHit", effect=110, coef=1.4, cd=10, range=1000, radius=280, warning=.4, cc=[("slow", 2.5, .4)]),
              A("e", "Energy Shield", "self_buff", "storm", "Ability_Q_Deploy_Fire", "EnergyShield", effect=35, cd=15, dur=3, buff="pg_guard", section="defensive",
                text="Project an energy shield: take {effect}% less damage for 3s."),
              A("r", "Overclock", "storm", "storm", "Ability_R_Fire", "Ultimate", effect=300, coef=3.0, cd=75, range=1100, radius=420, dur=4, atAim=True)],
         passive=P("Scrap Plating", "guard", 8, "Take {effect}% less damage from every source.")),
    dict(id="pg_drongo", pack="ParagonDrongo", folder="Drongo", name="Drongo", cls="Boomerang Raider", roles=["damage"],
         threat="damage", primary="agility", style="blunderbuss", range=1200, race="human", diff=2,
         lore="A wasteland raider with too many toys and no regrets.", quote="Crikey!",
         attack=["Primary_Fire"], idle="Idle",
         kit=[A("rmb", "Boomerang", "pierce", "physical", "Ability_Boomerang_Throw", "Boomerang", effect=120, coef=1.5, cd=7, range=1000, radius=60, hits=5, style="arrow"),
              A("q", "Pyro Grenade", "circle", "fire", "Ability_Grenade_Throw", "Grenade", effect=130, coef=1.6, cd=10, range=1000, radius=300, warning=.5),
              A("e", "Drongo Rounds", "self_buff", "fire", "Ability_DrongoRounds", "Shards", effect=30, cd=15, dur=5, buff="pg_frenzy", section="defensive",
                text="Load special rounds: +{effect}% attack speed for 5s."),
              A("r", "Bazooka", "projectile", "fire", "Ability_BazookaFire", "Ultimate", effect=380, coef=3.4, cd=75, range=1600, radius=90, style="arcane")],
         passive=P("Scavenger", "power", 8, "+{effect}% ability damage.")),
    # ------------------------------------------------------------------ batch 4: mages
    dict(id="pg_gideon", pack="ParagonGideon", folder="Gideon", name="Gideon", cls="Cosmic Sorcerer", roles=["damage"],
         threat="damage", primary="intelligence", style="arcane", range=1100, race="human", diff=2,
         lore="He tore a hole in the sky once. He'd like to do it again.", quote="Look up.",
         attack=["Primary_Attack_A_Medium", "Primary_Attack_B_Medium"], death="Death_Fwd",
         kit=[A("rmb", "Cosmic Rift", "circle", "void", "CosmicRift_Execute", "Meteor", effect=140, coef=1.8, cd=9, range=1100, radius=300, warning=.7),
              A("q", "Burden", "circle", "void", "Burden", "Burden", effect=100, coef=1.3, cd=12, range=900, radius=280, warning=.8, cc=[("stun", 1.2, 0)]),
              A("e", "Torn Space", "dash", "void", "Torn_Space", "Portal", cd=12, range=900, buff="pg_empower", buffPct=15, dur=3, section="defensive",
                text="Tear open a portal to the aimed spot; +15% damage for 3s."),
              A("r", "Black Hole", "storm", "void", "BlackHole_Loop", "Ultimate", effect=320, coef=3.2, cd=80, range=1000, radius=450, dur=3, atAim=True, cc=[("slow", 3, .5)])],
         passive=P("Cosmic Insight", "power", 10, "+{effect}% ability damage.")),
    dict(id="pg_howitzer", pack="ParagonHowitzer", folder="Howitzer", name="Howitzer", cls="Artillery Mech", roles=["damage"],
         threat="damage", primary="intelligence", style="arcane", range=1100, race="ether-construct", diff=2,
         lore="A tiny pilot in a very large argument.", quote="Incoming!",
         attack=["Primary_Fire", "Primary_Fire_V1"],
         kit=[A("rmb", "Missile Swarm", "circle", "fire", "Missile_Swarm_Fire", "MissileSwarm", effect=130, coef=1.7, cd=9, range=1200, radius=320, warning=.7),
              A("q", "Det Charge", "circle", "fire", "Det_Charge_Fire", "DetCharge", effect=120, coef=1.5, cd=11, range=900, radius=280, warning=1.0, cc=[("slow", 2, .4)]),
              A("e", "Slow Grenade", "projectile", "cold", "Secondary_Fire", "SlowGrenade", effect=90, coef=1.2, cd=10, range=1000, radius=60, style="arcane", cc=[("slow", 2.5, .45)]),
              A("r", "LRM Barrage", "storm", "fire", "LRM_Fire_V2", "LRM", effect=340, coef=3.3, cd=80, range=1500, radius=500, dur=4, atAim=True)],
         passive=P("Payload", "power", 10, "+{effect}% ability damage.")),
    dict(id="pg_iggy", pack="ParagonIggyScorch", folder="IggyScorch", name="Iggy & Scorch", cls="Pyro Duo", roles=["damage"],
         threat="damage", primary="intelligence", style="arcane", range=1000, race="human", diff=2,
         lore="A kid, a lizard, and a flamethrower. What could go wrong?", quote="Burn it all!",
         idle="IggyScorch_Idle", attack=["Primary_Med_Fire", "Primary_Fast_Fire"], death="Death_Front",
         kit=[A("rmb", "Scorch Breath", "cone", "fire", "RMB_Ability", "Primary", effect=130, coef=1.7, cd=8, radius=550, angle=45),
              A("q", "Flame Turret", "storm", "fire", "Q_Ability_Throw", "Turret", effect=150, coef=1.6, cd=12, range=900, radius=320, dur=5, atAim=True),
              A("e", "Oil Slick", "circle", "fire", "E_Ability", "OilTrail", effect=90, coef=1.1, cd=11, range=900, radius=320, warning=.3, cc=[("slow", 3, .45)]),
              A("r", "Fire Storm", "storm", "fire", "IggyScorch_Ultimate", "Ultimate", effect=340, coef=3.3, cd=80, radius=500, dur=4)],
         passive=P("Pyromania", "power", 10, "+{effect}% ability damage.")),
    dict(id="pg_belica", pack="ParagonLtBelica", folder="Belica", name="Lt. Belica", cls="Mana Tactician", roles=["damage", "support"],
         threat="damage", primary="intelligence", style="arcane", range=1100, race="human", diff=2,
         lore="A lieutenant with a drone for every problem.", quote="Drone deployed.",
         attack=["Primary_Fire_Med", "Primary_Fire_Slow"], death="Death_A",
         kit=[A("rmb", "Neural Disruptor", "circle", "storm", "RMB", "TeslaConduit", effect=110, coef=1.4, cd=11, range=1000, radius=280, warning=.8, cc=[("stun", 1.2, 0)]),
              A("q", "Mana Bomb", "circle", "arcane", "Q_Ability", "ManaBomb", effect=130, coef=1.7, cd=9, range=1100, radius=300, warning=.4),
              A("e", "Eruption Beam", "line", "storm", "E_Ability", "EruptionBeam", effect=120, coef=1.5, cd=10, range=1100, radius=120, cc=[("silence", 1, 0)]),
              A("r", "Neural Detonator", "strike", "arcane", "R_Ability", "Ultimate", effect=360, coef=3.5, cd=75, range=1100, cc=[("silence", 1.5, 0)])],
         passive=P("Mana Siphon", "power", 8, "+{effect}% ability damage.")),
    dict(id="pg_morigesh", pack="ParagonMorigesh", folder="Morigesh", name="Morigesh", cls="Hex Witch", roles=["damage"],
         threat="damage", primary="intelligence", style="arcane", range=1100, race="human", diff=3,
         lore="Every doll she stitches has a name.", quote="Hold still.",
         attack=["PrimaryAttack_A_Slow", "PrimaryAttack_B_Slow"],
         kit=[A("rmb", "Swarm of Insects", "projectile", "poison", "Ability_RMB", "SkillshotAOE", effect=130, coef=1.7, cd=8, range=1200, radius=60, style="arcane", cc=[("slow", 2, .35)]),
              A("q", "Stab the Doll", "mark", "shadow", "Ability_Q", "StabDoll", effect=18, cd=12, range=1000, dur=6, section="control",
                text="Stitch the target into a doll for 6s: it takes {effect}% more damage from everyone."),
              A("e", "Life Drain", "strike", "shadow", "Ability_E", "LifeDrain", effect=140, coef=1.8, cd=9, range=900),
              A("r", "Blood Price", "storm", "shadow", "Ability_R", "Ultimate", effect=320, coef=3.2, cd=80, range=1000, radius=450, dur=4, atAim=True)],
         passive=P("Hexweaver", "lifesteal", 10, "Heal for {effect}% of your ability damage.")),
    dict(id="pg_gadget", pack="ParagonGadget", folder="Gadget", name="Gadget", cls="Gadgeteer", roles=["damage"],
         threat="damage", primary="intelligence", style="arcane", range=1100, race="human", diff=2,
         lore="A genius inventor whose drones do the heavy lifting.", quote="Science!",
         attack=["LMB_Fire_A", "LMB_Fire_B"], death="Gadget_Death_A",
         kit=[A("rmb", "Power Mine", "circle", "storm", "Throw_Ready", "", effect=130, coef=1.6, cd=10, range=1000, radius=280, warning=.8, cc=[("slow", 2, .4)]),
              A("q", "Charge Shot", "pierce", "storm", "Ability_Q", "", effect=140, coef=1.8, cd=9, range=1300, radius=60, hits=4, style="arcane"),
              A("e", "Overdrive", "self_buff", "arcane", "Cast", "", effect=25, cd=15, dur=5, buff="pg_empower", section="defensive",
                text="Overclock your gadgets: +{effect}% damage for 5s."),
              A("r", "Speed Gate", "storm", "storm", "Cast", "", effect=300, coef=3.0, cd=80, range=1000, radius=450, dur=4, atAim=True)],
         passive=P("Tinkerer", "power", 8, "+{effect}% ability damage.")),
    dict(id="pg_zinx", pack="ParagonZinx", folder="Zinx", name="Zinx", cls="Plague Medic", roles=["healer", "support"],
         threat="healer", primary="intelligence", style="arcane", range=1100, race="human", diff=2,
         lore="She'll cure you. Or she'll cure the world of you.", quote="Doctor's orders.",
         attack=["Primary_Fire_Med", "Primary_Fire_Fast"], death="Death_Front",
         kit=[A("rmb", "Heal Shot", "ally_heal", "nature", "Ability_RMB", "HealShot", effect=90, coef=2.4, cd=5, range=1200, cast=0, types=["HEAL"], section="defensive",
                text="Fire a restorative dart: heal an ally (or yourself) for {effect}."),
              A("q", "Toxin Shot", "projectile", "poison", "Ability_DoT", "DotShot", effect=110, coef=1.4, cd=8, range=1200, radius=50, style="arcane", cc=[("slow", 2, .3)]),
              A("e", "Stun Shot", "projectile", "storm", "Ability_LMB", "StunShot", effect=70, coef=1.0, cd=12, range=1100, radius=50, style="arcane", cc=[("stun", 1.2, 0)]),
              A("r", "Mass Revival", "party_heal", "nature", "Ability_Ultimate", "Ultimate", effect=220, coef=3.0, cd=80, radius=900, types=["HEAL"], section="ultimate",
                text="Flood the area with nanites: heal every ally within 9m for {effect}.")],
         passive=P("Field Medicine", "haste", 8, "+{effect}% move speed.")),
    dict(id="pg_fey", pack="ParagonFey", folder="Fey", name="The Fey", cls="Grove Mother", roles=["support", "healer", "damage"],
         threat="healer", primary="intelligence", style="staff", range=1100, race="sylvan", diff=2,
         lore="The forest's last mother; the brambles answer her.", quote="Grow.",
         attack=["Ability_LMB_A_Medium", "Ability_LMB_B_Medium"], death="Death_Fwd",
         kit=[A("rmb", "Nettles", "circle", "nature", "Ability_RMB", "Nettles", effect=110, coef=1.5, cd=9, range=1000, radius=300, warning=.5, cc=[("slow", 2, .35)]),
              A("q", "Brambles", "line", "nature", "Ability_Q", "Brambles", effect=100, coef=1.3, cd=11, range=1000, radius=130, cc=[("stun", 1, 0)]),
              A("e", "Life Bloom", "party_heal", "nature", "Ability_E", "Growth", effect=90, coef=2.2, cd=10, radius=600, types=["HEAL"], section="defensive",
                text="Bloom: heal every ally within 6m for {effect}."),
              A("r", "Mother of the Grove", "storm", "nature", "Ability_R", "Ultimate", effect=300, coef=3.0, cd=80, range=1000, radius=500, dur=4, atAim=True, cc=[("slow", 3, .5)])],
         passive=P("Verdant Heart", "power", 8, "+{effect}% ability damage.")),
    dict(id="pg_muriel", pack="ParagonMuriel", folder="Muriel", name="Muriel", cls="Guardian Angel", roles=["healer", "support"],
         threat="healer", primary="intelligence", style="staff", range=1100, race="spirit", diff=2,
         lore="A guardian who crosses the battlefield on wings of light.", quote="Rise.",
         attack=["Primary_Fire_A_Slow_V1", "Primary_Fire_B_Slow_V1"],
         kit=[A("rmb", "Reversal of Fortune", "ally_heal", "holy", "LifeLock_Fire", "LifeLock", effect=100, coef=2.5, cd=7, range=1100, types=["HEAL"], buff="pg_guard", buffPct=20, dur=3, section="defensive",
                text="Bless an ally (or yourself): heal {effect} and 20% less damage taken for 3s."),
              A("q", "Consecrated Ground", "circle", "holy", "ConsecratedGround_Cast", "ConsGround", effect=110, coef=1.4, cd=11, range=1000, radius=320, warning=.6, cc=[("stun", 1, 0)]),
              A("e", "Winged Boots", "self_buff", "holy", "Boots", "Boots", effect=30, cd=14, dur=4, buff="pg_haste", section="defensive",
                text="Take wing: +{effect}% move speed for 4s."),
              A("r", "Guardian Angel", "party_heal", "holy", "Ultimate_Flare", "Ultimate", effect=240, coef=3.2, cd=80, radius=900, types=["HEAL"], buff="pg_guard", buffPct=25, dur=4, section="ultimate",
                text="Descend in light: heal every ally within 9m for {effect}; they take 25% less damage for 4s.")],
         passive=P("Grace", "guard", 8, "Take {effect}% less damage from every source.")),
    dict(id="pg_narbash", pack="ParagonNarbash", folder="Narbash", name="Narbash", cls="War Drummer", roles=["support", "healer", "tank"],
         threat="healer", primary="intelligence", style="totem", range=230, race="orc", diff=1,
         lore="The loudest drummer of the war camps; his beat mends the fallen.", quote="Feel the beat!",
         attack=["Primary_Swing1_Medium", "Primary_Swing2_Medium"], death="Death_Fwd",
         kit=[A("rmb", "Lantern Toss", "projectile", "fire", "RMB_Ability_Throw", "Throw", effect=110, coef=1.4, cd=8, range=1000, radius=60, style="arcane", cc=[("slow", 2, .35)]),
              A("q", "Healing Drum", "party_heal", "nature", "Q_Ability_InPlace", "Shield", effect=80, coef=2.2, cd=9, radius=700, types=["HEAL"], section="defensive",
                text="Beat the healing drum: heal every ally within 7m for {effect}."),
              A("e", "Drum Rush", "self_buff", "nature", "E_ability_repeat", "Dash", effect=25, cd=14, dur=4, buff="pg_haste", section="defensive",
                text="A marching beat: +{effect}% move speed for 4s."),
              A("r", "Grand Finale", "self_burst", "physical", "R_Ability", "Ultimate", effect=220, coef=2.6, cd=75, radius=600, warning=.4, cc=[("stun", 1.5, 0)], types=["TANK", "HEAL"])],
         passive=P("Rhythm of War", "frenzy", 10, "+{effect}% attack speed.")),
    dict(id="pg_phase", pack="ParagonPhase", folder="Phase", name="Phase", cls="Psionic Linker", roles=["support", "healer"],
         threat="healer", primary="intelligence", style="arcane", range=1100, race="human", diff=3,
         lore="A psion who shares her mind, and her shields, with her allies.", quote="We are one.",
         attack=["Primary_Attack_A_Medium", "Primary_Attack_B_Medium"],
         kit=[A("rmb", "Psionic Link", "ally_heal", "arcane", "RMB_Throw", "Link", effect=80, coef=2.3, cd=6, range=1100, types=["HEAL"], buff="pg_haste", buffPct=15, dur=3, section="defensive",
                text="Link to an ally (or yourself): heal {effect} and +15% move speed for 3s."),
              A("q", "Clarity Beam", "line", "arcane", "Ability_Q", "Beam", effect=120, coef=1.6, cd=9, range=1100, radius=110),
              A("e", "Psionic Flash", "self_burst", "arcane", "Ability_E", "Flash", effect=80, coef=1.1, cd=12, radius=450, cc=[("stun", 1, 0)]),
              A("r", "Mind Storm", "storm", "arcane", "R_Ability_Loop", "Ultimate", effect=300, coef=3.0, cd=80, range=1000, radius=450, dur=4, atAim=True, cc=[("slow", 3, .45)])],
         passive=P("Shared Mind", "haste", 8, "+{effect}% move speed.")),
    dict(id="pg_dekker", pack="ParagonDekker", folder="Dekker", name="Dekker", cls="Stasis Engineer", roles=["support", "damage"],
         threat="damage", primary="intelligence", style="arcane", range=1100, race="human", diff=2,
         lore="An engineer who believes every problem can be put on hold.", quote="Hold it right there.",
         attack=["Fire_Med", "Fire_Slow"], death="Death_Fwd", run="Run_Fwd",
         kit=[A("rmb", "Stasis Bomb", "line", "arcane", "Ability_RMB_V2", "StasisBomb", effect=110, coef=1.4, cd=9, range=1000, radius=120, cc=[("slow", 2.5, .45)]),
              A("q", "Slow Field", "circle", "cold", "Q_Ability", "SlowField", effect=90, coef=1.2, cd=11, range=1000, radius=340, warning=.4, cc=[("slow", 3, .5)]),
              A("e", "Rocket Boots", "dash", "fire", "E_Ability", "RocketBoots", cd=10, range=750, buff="pg_haste", buffPct=25, dur=2.5, section="defensive",
                text="Rocket to the aimed spot; +25% move speed for 2.5s."),
              A("r", "Containment Fence", "circle", "arcane", "R_Ability", "Ultimate", effect=260, coef=2.8, cd=80, range=1000, radius=450, warning=.8, cc=[("stun", 1.5, 0)])],
         passive=P("Stasis Engineer", "power", 8, "+{effect}% ability damage.")),
]

# Champion-select painting per hero (an existing /Game/UI/Draft/Backgrounds/T_DraftBg_<id>, closest theme) until a
# dedicated painting exists.
BACKGROUNDS = {
    "pg_greystone": "knight", "pg_aurora": "huntress", "pg_crunch": "orc_chieftain", "pg_fengmao": "lancer", "pg_grux": "orc_chieftain",
    "pg_steel": "paladin", "pg_terra": "dwarf_miner", "pg_sevarog": "witch_slayer", "pg_rampage": "bear", "pg_kwang": "paladin_holy",
    "pg_kallari": "witch_slayer", "pg_countess": "summoner", "pg_shinbi": "huntress", "pg_khaimera": "troll_berserker", "pg_serath": "paladin_holy",
    "pg_wukong": "evergrove_centaur", "pg_yin": "lancer", "pg_boris": "bear", "pg_riktor": "drakish_footman", "pg_sparrow": "ranger",
    "pg_twinblast": "gunblade", "pg_murdock": "gunblade", "pg_revenant": "witch_slayer", "pg_wraith": "ranger", "pg_grim": "aetheri",
    "pg_drongo": "troll_berserker_ranged", "pg_gideon": "wizard", "pg_howitzer": "aetheri_warden", "pg_iggy": "summoner", "pg_belica": "aetheri",
    "pg_morigesh": "summoner", "pg_gadget": "aetheri", "pg_zinx": "dryad", "pg_fey": "dryad", "pg_muriel": "keeper_of_light",
    "pg_narbash": "totemic_behemoth", "pg_phase": "whisp", "pg_dekker": "ether_golem_support",
}


# Role template champions: fill the roster's six-active draft example and seed the purchasable pool.
def template_for(threat, stat, ranged):
    if threat == "healer":
        return "scholar"
    if threat == "tank":
        return "knight"
    if stat == "intelligence":
        return "wizard" if ranged else "troll_berserker_melee"
    return "ranger" if ranged else "troll_berserker_melee"



ROSTER_DELIVERY = {"projectile": "projectile", "pierce": "projectile", "cone": "ground_cone", "circle": "ground_circle", "leap": "ground_circle",
                   "dash": "ground_circle", "line": "ground_line", "pull": "ground_line", "self_burst": "self", "self_buff": "self", "storm": "self",
                   "party_heal": "self", "strike": "targeted", "lunge": "targeted", "mark": "targeted", "ally_heal": "ally"}
TARGETING = {"projectile": "aim", "pierce": "aim", "cone": "aim", "circle": "aim", "leap": "aim", "dash": "aim", "line": "aim", "pull": "aim",
             "self_burst": "self", "self_buff": "self", "storm": "self", "party_heal": "self", "strike": "enemy", "lunge": "enemy", "mark": "enemy",
             "ally_heal": "ally"}
CURVE = {"effectGrowth": 0.35, "effectHalfLevels": 4, "effectCap": 0, "costCapMultiplier": 1.5, "costRampLevels": 10,
         "cooldownFloorFraction": 0.6, "cooldownDecayLevels": 15, "minCooldownSeconds": 1.0}
BUFF_WORD = {"pg_guard": "less damage taken", "pg_haste": "move speed", "pg_frenzy": "attack speed", "pg_empower": "damage", "pg_marked": "damage taken"}
CC_LABEL = {"stun": "Stunned", "slow": "Move -{m}%", "silence": "Silenced"}


def pick(names, *candidates):
    """First existing clip (exact, then case-insensitive, then prefix) among candidates."""
    low = {n.lower(): n for n in names}
    for c in candidates:
        if not c:
            continue
        if c in names:
            return c
        if c.lower() in low:
            return low[c.lower()]
    for c in candidates:
        if not c:
            continue
        hits = sorted(n for n in names if n.lower().startswith(c.lower()) and "montage" not in n.lower() and "_ao" not in n.lower())
        if hits:
            return hits[0]
    return None


def fx_for(entry, group):
    """Cast / impact systems of an ability folder (skins excluded), at most two each."""
    systems = [f["path"] for f in entry.get("fx", {}).get(group, [])] if group else []
    systems = [s for s in systems if "/Skins/" not in s and "Camera" not in s.split("/")[-1]]
    name = lambda s: s.split("/")[-1].lower()
    impact_words = ("impact", "explo", "burst", "smash", "hit", "land", "ground", "aoe", "detonat", "blast", "end")
    cast_words = ("cast", "start", "muzzle", "launch", "activate", "swipe", "trail", "charge", "fire", "warmup", "intro", "spawn", "swing", "throw", "loop")
    impact = [s for s in systems if any(w in name(s) for w in impact_words)][:2]
    cast = [s for s in systems if s not in impact and any(w in name(s) for w in cast_words)][:2]
    if not cast and not impact and systems:
        cast = systems[:1]
    obj = lambda s: s + "." + s.split("/")[-1]
    return [obj(s) for s in cast], [obj(s) for s in impact]


# ---------------------------------------------------------------------------------------------------------------
# Skins (Eric, 2026-09-28): every alternate skin is a RESKIN (selectable in champion select, same kit), a NEW
# CHAMPION (own name / role / primary stat, kit built from the parent's Paragon clips + FX, recoloured by school), or
# a MONSTER VARIANT (an extra body of the closest race unit). Parts (drones, ult guns, VFX shells) and meshes on a
# skeleton without clips are recorded as not usable.
# ---------------------------------------------------------------------------------------------------------------
PART_WORDS = ("bot", "UltGun", "FuryFX", "GhostBeastVFX", "ForVariantsTest", "Antlers", "BadSantaBelly")
PASSIVE_TEXT = {"guard": "Take {effect}% less damage from every source.", "lifesteal": "Heal for {effect}% of your ability damage.",
                "haste": "+{effect}% move speed.", "frenzy": "+{effect}% attack speed.", "power": "+{effect}% ability damage."}

# (parent hero id, skin key): new identity. rename: slot -> ability name; school: recolour of every ability.
NEW_CHAMPIONS = {
    ("pg_greystone", "Dragonlord"): dict(name="Dragonlord", cls="Wyrm Knight", roles=["damage", "tank"], threat="damage", primary="strength", school="fire", passive=("Dragonblood", "lifesteal", 10),
                                          rename={"rmb": "Wyrmfall", "q": "Dragon Path", "e": "Scaled Ward", "r": "Wrath of the Wyrm"}, lore="He slew the dragon and took its fire.", quote="Burn with me."),
    ("pg_greystone", "Novaborn"): dict(name="Novaborn", cls="Star Paladin", roles=["tank", "support"], threat="tank", primary="intelligence", school="arcane", passive=("Starforged", "guard", 12),
                                        rename={"rmb": "Starfall", "q": "Comet Path", "e": "Nova Aegis", "r": "Supernova"}, lore="A knight reforged in a dying star.", quote="The stars hold the line."),
    ("pg_gideon", "Mephisto"): dict(name="Mephisto", cls="Hellfire Magus", roles=["damage"], threat="damage", primary="intelligence", school="fire", passive=("Infernal Pact", "power", 12),
                                     rename={"rmb": "Hellrift", "q": "Brimstone Weight", "e": "Infernal Door", "r": "Abyssal Maw"}, lore="He bargained with the pit and kept the change.", quote="Every soul has a price."),
    ("pg_gideon", "Inquisitor"): dict(name="Grand Inquisitor", cls="Zealot Mage", roles=["support", "healer"], threat="healer", primary="intelligence", school="holy", passive=("Zeal", "haste", 8),
                                       heal_slot="e", rename={"rmb": "Judgement Rift", "q": "Penance", "e": "Absolution", "r": "Holy Singularity"}, lore="He burns heresy and mends the faithful.", quote="Confess."),
    ("pg_fengmao", "RoyalGuard"): dict(name="Royal Guard", cls="Palace Sentinel", roles=["tank"], threat="tank", primary="strength", school="holy", passive=("Oath of the Throne", "guard", 12),
                                        rename={"rmb": "Guardian Charge", "q": "Throne Wall", "e": "Halberd Sweep", "r": "Royal Decree"}, lore="The last guard of a fallen throne.", quote="None shall pass."),
    ("pg_grux", "Qilin"): dict(name="Qilin", cls="Celestial Beast", roles=["damage", "tank"], threat="damage", primary="agility", school="storm", passive=("Heaven Stride", "haste", 10),
                                rename={"rmb": "Thunder Hooves", "q": "Sky Stampede", "e": "Storm Rend", "r": "Celestial Roar"}, lore="A storm spirit wearing a warrior's skin.", quote="The sky runs with me."),
    ("pg_grux", "Molten"): dict(name="Magmalord", cls="Molten Warlord", roles=["tank"], threat="tank", primary="strength", school="fire", passive=("Molten Core", "guard", 12),
                                 rename={"rmb": "Lava Fists", "q": "Eruption Stampede", "e": "Magma Ripple", "r": "Caldera Challenge"}, lore="Born in a volcano, raised by anger.", quote="Melt."),
    ("pg_iggy", "MechaTerror"): dict(name="Mecha Terror", cls="War Engineer", roles=["damage", "tank"], threat="damage", primary="intelligence", school="storm", passive=("Overclocked", "frenzy", 12),
                                      rename={"rmb": "Arc Breath", "q": "Tesla Turret", "e": "Coolant Slick", "r": "Meltdown"}, lore="Iggy built a bigger friend.", quote="Upgrade complete."),
    ("pg_iggy", "Phoenix"): dict(name="Phoenix Rider", cls="Firebird Caller", roles=["damage", "support"], threat="damage", primary="intelligence", school="holy", passive=("Rebirth Flame", "lifesteal", 10),
                                  rename={"rmb": "Phoenix Breath", "q": "Ember Nest", "e": "Ash Trail", "r": "Rebirth Storm"}, lore="A rider on a bird that refuses to stay dead.", quote="From the ashes!"),
    ("pg_belica", "SpiderWitch"): dict(name="Spider Witch", cls="Venom Matriarch", roles=["damage"], threat="damage", primary="intelligence", school="poison", passive=("Brood Venom", "power", 10),
                                        rename={"rmb": "Web Snare", "q": "Venom Sac", "e": "Silk Lash", "r": "Brood Queen Bite"}, lore="Her drones have eight legs now.", quote="Come closer."),
    ("pg_belica", "HeavyArmor"): dict(name="Siege Lieutenant", cls="Armoured Tactician", roles=["tank", "support"], threat="tank", primary="strength", school="storm", passive=("Plated Command", "guard", 12),
                                       rename={"rmb": "Suppression Drone", "q": "Shock Mortar", "e": "Breach Beam", "r": "Command Override"}, lore="Belica stopped dodging and started tanking.", quote="Hold formation."),
    ("pg_morigesh", "NorthernMystic"): dict(name="Northern Mystic", cls="Frost Shaman", roles=["healer", "support"], threat="healer", primary="intelligence", school="cold", passive=("Aurora Blessing", "haste", 8),
                                             heal_slot="e", rename={"rmb": "Frost Swarm", "q": "Rime Doll", "e": "Aurora Mend", "r": "Northern Lights"}, lore="She sings to the ice and the ice sings back.", quote="Breathe the cold."),
    ("pg_murdock", "Executioner"): dict(name="The Executioner", cls="Headsman", roles=["damage"], threat="damage", primary="agility", school="shadow", passive=("Final Verdict", "power", 12),
                                         rename={"rmb": "Grim Spread", "q": "Shackle Trap", "e": "Iron Hood", "r": "The Sentence"}, lore="Justice, delivered one shell at a time.", quote="Kneel."),
    ("pg_revenant", "ChronoBoss"): dict(name="Chronoboss", cls="Time Enforcer", roles=["damage"], threat="damage", primary="agility", school="arcane", passive=("Borrowed Seconds", "frenzy", 12),
                                         rename={"rmb": "Paradox Round", "q": "Time Mark", "e": "Temporal Rupture", "r": "End of Time"}, lore="He collects the seconds you owe.", quote="Time is up."),
    ("pg_revenant", "FrostKing"): dict(name="Frost King", cls="Glacial Tyrant", roles=["tank", "damage"], threat="tank", primary="strength", school="cold", passive=("Permafrost", "guard", 12),
                                        rename={"rmb": "Frozen Bolt", "q": "King Mark", "e": "Glacier Break", "r": "Eternal Winter"}, lore="The king under the ice woke hungry.", quote="Kneel before winter."),
    ("pg_sevarog", "MaskedReaper"): dict(name="Masked Reaper", cls="Harvest Shade", roles=["damage"], threat="damage", primary="agility", school="shadow", passive=("Reaper Due", "lifesteal", 12),
                                          rename={"rmb": "Soul Hook", "q": "Shade Rush", "e": "Harvest Arc", "r": "Final Harvest"}, lore="Behind the mask there is only the scythe.", quote="Your harvest is due."),
    ("pg_sparrow", "Raven"): dict(name="Raven Queen", cls="Shadow Archer", roles=["damage"], threat="damage", primary="agility", school="shadow", passive=("Murder of Crows", "power", 10),
                                   rename={"rmb": "Raven Shot", "q": "Crow Rain", "e": "Night Feathers", "r": "Storm of Crows"}, lore="Her arrows are feathers of the dark.", quote="The ravens are hungry."),
    ("pg_steel", "Doomsday"): dict(name="Doomsday", cls="Apocalypse Engine", roles=["tank", "damage"], threat="tank", primary="strength", school="fire", passive=("Reactor Plating", "guard", 14),
                                    rename={"rmb": "Doom Bash", "q": "Ram Engine", "e": "Reactor Wall", "r": "Doomsday Impact"}, lore="Built to end sieges, and cities.", quote="Doomsday has arrived."),
    ("pg_wukong", "GreatSage"): dict(name="Great Sage", cls="Cloud Master", roles=["support", "damage"], threat="damage", primary="intelligence", school="storm", passive=("Enlightenment", "haste", 10),
                                      rename={"rmb": "Heaven Staff", "q": "Nimbus Slam", "e": "Seventy-Two Forms", "r": "Equal of Heaven"}, lore="The monkey became a sage and kept the staff.", quote="Heaven listens."),
    ("pg_terra", "GryphonKnight"): dict(name="Gryphon Knight", cls="Sky Lancer", roles=["damage", "tank"], threat="damage", primary="strength", school="storm", passive=("Talon Guard", "frenzy", 10),
                                         rename={"rmb": "Gryphon Dive", "q": "Talon Sweep", "e": "Wing Gust", "r": "Sky Judgement"}, lore="She rides the storm and falls with it.", quote="From above!"),
    ("pg_zinx", "StarQueen"): dict(name="Star Queen", cls="Astral Sovereign", roles=["damage", "healer"], threat="damage", primary="intelligence", school="arcane", passive=("Stellar Court", "power", 10),
                                    heal_slot="rmb", rename={"rmb": "Starlight Mend", "q": "Astral Needle", "e": "Gravity Spike", "r": "Constellation"}, lore="Queen of a court of stars.", quote="Bow to the heavens."),
    ("pg_crunch", "CrashSite"): dict(name="Crash Site", cls="Wreck Brawler", roles=["tank", "damage"], threat="tank", primary="strength", school="storm", passive=("Scrap Armor", "guard", 12),
                                      rename={"rmb": "Crash Cross", "q": "Impact Uppercut", "e": "Shrapnel Punch", "r": "Meteor Hook"}, lore="Pulled from a crater, still swinging.", quote="Impact!"),
    ("pg_narbash", "BashOLantern"): dict(name="Bash-O-Lantern", cls="Harvest Drummer", roles=["damage", "support"], threat="damage", primary="intelligence", school="fire", passive=("Hollow Beat", "power", 10),
                                          rename={"rmb": "Pumpkin Toss", "q": "Hollow Drum", "e": "Harvest March", "r": "Night of Drums"}, lore="The drummer of the last harvest night.", quote="Trick or beat!"),
    ("pg_rampage", "Elemental"): dict(name="Stone Colossus", cls="Earth Elemental", roles=["tank"], threat="tank", primary="strength", school="earth", passive=("Living Rock", "guard", 14),
                                       rename={"rmb": "Quake Smash", "q": "Boulder Toss", "e": "Earthen Surge", "r": "Tectonic Rage"}, lore="A mountain that learned to walk.", quote="The earth rises."),
}

# (parent hero id, skin key): (race, unit, reason)
MONSTER_VARIANTS = {
    ("pg_khaimera", "GruxPelt"): ("feral_kin", "wild_outrider", "wears a beast pelt: a feral raider"),
    ("pg_khaimera", "Halloween"): ("voidborn", "rift_stalker", "spectral hunter silhouette"),
    ("pg_grux", "Halloween"): ("hollow", "ironbound_bruiser", "undead brute look"),
    ("pg_grux", "BeetleRed"): ("drakkari", "drakkari_scalebreaker", "red carapace reads as scaled"),
    ("pg_sevarog", "Chronos"): ("stoneborn", "stoneborn_forgelord", "clockwork titan: a forge boss"),
    ("pg_sevarog", "Bloodred"): ("fallen_order", "dread_knight", "blood-armour reaper knight"),
    ("pg_revenant", "RavenQuill"): ("fallen_order", "fallen_inquisitor_crossbow", "plague-doctor gunman: ranged fallen"),
    ("pg_rampage", "Redneck"): ("feral_kin", "werebear_mauler", "hulking beast brawler"),
    ("pg_crunch", "BlackSite"): ("aetheri", "aetheri_warframe", "black-ops cyborg"),
    ("pg_grim", "Wasteland"): ("ironhide", "redmoon_axethrower", "scrap raider with a ranged weapon"),
    ("pg_howitzer", "Domed"): ("stoneborn", "crystal_ballista", "domed artillery mech"),
    ("pg_drongo", "AlienInvader"): ("voidborn", "rift_gazer", "alien gunner from beyond"),
    ("pg_kallari", "DeathLotus"): ("voidborn", "void_ravager", "shadow assassin"),
    ("pg_fey", "Nightshade"): ("blightwood", "rotbloom_shaman", "poisoned grove caster"),
    ("pg_yin", "CryptGoddess"): ("hollow", "barbed_hunter", "crypt huntress"),
    ("pg_gideon", "Undertow"): ("drowned_deep", "tidecaller", "drowned sorcerer"),
    ("pg_countess", "Carnivale"): ("fallen_order", "flagellant", "masked carnival zealot"),
}

# ParagonMinions -> race units (the lane minions are Paragon's robots: the Aetheri; jungle buffs by colour).
MINION_UNITS = [
    # (inventory unit, mesh name, race, unit, reason)
    ("Minions/Down_Minions", "Minion_Lane_Melee_Dawn", "aetheri", "aetheri_phaseblade", "lane melee robot (Dawn)"),
    ("Minions/Dusk_Minions", "Minion_Lane_Melee_Dusk", "aetheri", "aetheri_phaseblade", "lane melee robot (Dusk)"),
    ("Minions/Down_Minions", "Minion_Lane_Ranged_Dawn", "aetheri", "aetheri_lancer", "lane ranged robot (Dawn)"),
    ("Minions/Dusk_Minions", "Minion_Lane_Ranged_Dusk", "aetheri", "aetheri_lancer", "lane ranged robot (Dusk)"),
    ("Minions/Down_Minions", "Minion_Lane_Siege_Dawn", "aetheri", "aetheri_warframe", "siege robot (Dawn)"),
    ("Minions/Dusk_Minions", "Minion_Lane_Siege_Dusk", "aetheri", "aetheri_warframe", "siege robot (Dusk)"),
    ("Minions/Down_Minions", "Minion_Lane_Super_Dawn", "aetheri", "aetheri_bulwark", "super minion (Dawn)"),
    ("Minions/Dusk_Minions", "Minion_Lane_Super_Dusk", "aetheri", "aetheri_bulwark", "super minion (Dusk)"),
    ("Buff/Buff_Black", "Buff_Black", "voidborn", "void_ravager", "black jungle buff beast"),
    ("Buff/Buff_Red", "Buff_Red", "drakkari", "drakkari_scalebreaker", "red jungle brute"),
    ("Buff/Buff_White", "Buff_White", "stoneborn", "granite_crusher", "white camp golem (minion rig)"),
    ("Buff/Buff_Blue", "Buff_Blue", "drowned_deep", "barbspitter", "floating blue caster (fly clips)"),
    ("Minions/Prime_Helix", "Prime_Helix", "aetheri", "aetheri_colossus", "Prime Helix guardian: a boss body"),
]


def skin_key(hero_folder, mesh_name):
    n = mesh_name[3:] if mesh_name.startswith("SM_") else mesh_name
    for pre in (hero_folder, "Khai", "Gadget", "gadget"):
        if n.lower().startswith(pre.lower()):
            n = n[len(pre):]
            break
    n = n.strip("_").replace("_GDC", "").replace("BadSanta_belly", "BadSantaBelly")
    return re.sub(r"[^A-Za-z0-9]", "", n)


def spaced(key):
    return re.sub(r"(?<=[a-z])(?=[A-Z])", " ", key).strip()


def derive_new_champions(inspect):
    """Synthetic HEROES rows for the new champions: the parent's kit (clips + FX) renamed, re-schooled and re-roled."""
    by_id = {h["id"]: h for h in HEROES}
    out = []
    for (parent_id, key), n in NEW_CHAMPIONS.items():
        p = by_id[parent_id]
        inv = inspect.get(p["pack"], {}).get("heroes", {}).get(p["folder"], {})
        skin = next((m for m in inv.get("meshes", []) if m.get("skin") and skin_key(p["folder"], m["path"].split("/")[-1]) == key), None)
        if not skin:
            continue
        h = copy.deepcopy(p)
        h.update(id=f"{parent_id}_{key.lower()}", name=n["name"], cls=n["cls"], roles=n["roles"], threat=n["threat"], primary=n["primary"],
                 lore=n["lore"], quote=n["quote"], diff=2, skin_mesh=skin["path"], parent=parent_id, skin_key=key, pool=False)
        for a in h["kit"]:
            a["name"] = n["rename"].get(a["slot"], a["name"])
            a["school"] = n["school"]
            o = a["o"]
            if a["slot"] == n.get("heal_slot") and a["delivery"] not in ("ally_heal", "party_heal"):
                a["delivery"] = "ally_heal"
                for k in ("cc", "angle", "warning", "hits", "speed", "style", "atAim", "taunt", "radius", "buff", "buffPct", "dur", "text"):
                    o.pop(k, None)
                o.update(effect=90, coef=2.4, types=["HEAL"], section="defensive", range=1100)
            elif n["threat"] == "tank" and a["delivery"] in ("self_burst", "strike", "self_buff"):
                o["taunt"] = True
                o["types"] = ["TANK"]
            elif "types" in o and "HEAL" not in o["types"]:
                o["types"] = ["TANK"] if n["threat"] == "tank" else ["DPS"]
        pn = n["passive"]
        h["passive"] = P(pn[0], pn[1], pn[2], PASSIVE_TEXT[pn[1]])
        out.append(h)
    return out


def classify_skins(h, hero_inv, base_skeleton):
    """Every alternate skin of a base hero -> (key, mesh, decision, detail)."""
    rows = []
    for m in hero_inv.get("meshes", []):
        if not m.get("skin"):
            continue
        key = skin_key(h["folder"], m["path"].split("/")[-1])
        if any(w.lower() in key.lower() for w in PART_WORDS):
            rows.append((key, m, "not usable", "a part of another skin (drone, ultimate gun, VFX shell, antlers), not a body"))
        elif m["skeleton"] != base_skeleton:
            rows.append((key, m, "not usable", "its own skeleton (" + m["skeleton"].split("/")[-1] + ") has no clips in the pack"))
        elif (h["id"], key) in NEW_CHAMPIONS:
            n = NEW_CHAMPIONS[(h["id"], key)]
            rows.append((key, m, "new champion", f"{n['name']} ({n['cls']}; {'/'.join(n['roles'])}; {n['primary'][:3].upper()}; {n['school']})"))
        elif (h["id"], key) in MONSTER_VARIANTS:
            race, unit, why = MONSTER_VARIANTS[(h["id"], key)]
            rows.append((key, m, "monster variant", f"{race} / {unit}: {why}"))
        else:
            rows.append((key, m, "reskin", "same silhouette and weapon: a cosmetic variant"))
    return rows


def markdown_table(table, abilities, heroes, bindings):
    """Per-hero summary for Docs/ParagonChampions.md (between the paragon-table markers)."""
    by_id = {h["id"]: h for h in heroes}
    bind = {b["profileId"]: b for b in bindings}
    stat = {"strength": "STR", "agility": "AGI", "intelligence": "INT"}
    lines = ["", "| Champion | Class | Roles | Primary | Basic attack | Own kit (RMB / Q / E / ultimate) | Passive | Paragon FX groups | Height |",
             "|---|---|---|---|---|---|---|---|---|"]
    for h in table:
        info = by_id.get(h["id"])
        if not info:
            lines.append(f"| {h['name']} | {h['cls']} | not built: see problems | | | | | | |")
            continue
        own = " / ".join(f"{abilities[i]['name']} ({i.rsplit('_', 1)[-1].upper()})" for i in info["own"])
        passive = abilities[h["id"] + "_passive"]["name"]
        fx = ", ".join(g for g in info["inventory"]["fxGroups"] if g not in ("misc",)) or "none in pack"
        attack = f"{h['style']} {'ranged' if h['range'] > 400 else 'melee'} ({h['range']})"
        lines.append(f"| {h['name']} | {h['cls']} | {', '.join(h['roles'])} | {stat[h['primary']]} | {attack} | {own} | {passive} | {fx} | "
                     f"{bind[h['id']]['heightCm']:.0f} cm (native {info['nativeHeightCm']:.0f}) |")
    return "\n".join(lines) + "\n\n"


def main() -> int:

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if not INSPECT.exists():
        print("Missing " + str(INSPECT) + ": run Tools/InspectParagon.py in UnrealEditor-Cmd first", file=sys.stderr)
        return 2
    inspect = json.loads(INSPECT.read_text(encoding="utf-8"))
    base_roster = json.loads(ROSTER.read_text(encoding="utf-8"))
    base_abilities = json.loads(ABILITIES.read_text(encoding="utf-8"))
    roster_by_id = {c["id"]: c for c in base_roster["champions"]}
    kits = base_abilities["champions"]
    problems, abilities, champions_kits, recipes, bindings, heroes, roster, audio = [], {}, {}, {}, [], [], [], {}
    base_audio = json.loads((ROOT / "Content/Data/AudioEvents.json").read_text(encoding="utf-8"))
    pool = {"DPS": [], "TANK": [], "HEAL": []}
    skin_table, units = [], {}
    for h in HEROES + derive_new_champions(inspect):
        pack = inspect.get(h["pack"], {})
        hero_inv = pack.get("heroes", {}).get(h["folder"], {})
        anims = hero_inv.get("anims", {})
        names = set(anims)
        base_meshes = [m for m in hero_inv.get("meshes", []) if not m.get("skin")]
        mesh = next((m for m in base_meshes if m["path"].split("/")[-1].lower() == h["folder"].lower()), base_meshes[0] if base_meshes else None)
        if h.get("skin_mesh"):
            mesh = next((m for m in hero_inv.get("meshes", []) if m["path"] == h["skin_mesh"]), None)
        if not mesh:
            problems.append(f"{h['id']}: no base skeletal mesh in {h['pack']}")
            continue
        skeleton = mesh["skeleton"]
        same = {n for n, a in anims.items() if a.get("skeleton", skeleton) == skeleton}
        clip = lambda n: (anims[n]["path"] + "." + n) if n and n in anims else None
        idle = pick(same, h.get("idle"), "Idle", "Idle_Combat", "idle", "Steel_Idle")
        run = pick(same, h.get("run"), "Jog_Fwd", "Run_Fwd", "Steel_Run_Fwd")
        walk = pick(same, h.get("walk"), "Walk_Fwd", run)
        attacks = [pick(same, a) for a in h["attack"]]
        attacks = [a for a in attacks if a]
        hit = pick(same, h.get("hit"), "HitReact_Front", "Hit_React_Fwd", "Knockback_Fwd", "KnockBack_Front", "Knock_Fwd", "Steel_KnockBack")
        death = pick(same, h.get("death"), "Death", "Death_Fwd", "Death_Forward", "Death_A", "Death_front", "Death_Front")
        for label, value in (("idle", idle), ("run", run), ("attack", attacks[0] if attacks else None), ("death", death)):
            if not value:
                problems.append(f"{h['id']}: no {label} clip")
        stat = h["primary"]
        is_ranged = h["range"] > 400
        energy = stat != "intelligence"
        type_of_role = {"tank": "TANK", "damage": "DPS", "healer": "HEAL", "support": "HEAL"}
        primary_type = type_of_role[h["threat"]]
        own_ids, casts, contact = [], {}, {}
        for a in h["kit"]:
            o = a["o"]
            aid = f"{h['id']}_{a['slot']}"
            kind = "ultimate" if a["slot"] == "r" else "active"
            clip_name = pick(same, a["clip"])
            if not clip_name:
                problems.append(f"{aid}: clip {a['clip']} not found")
            else:
                casts[aid] = clip(clip_name)
            d = a["delivery"]
            heal = d in ("ally_heal", "party_heal")
            damage = not heal and d not in ("self_buff", "dash", "mark")
            effect = o.get("effect", 0 if d == "dash" else 100)
            coef = o.get("coef", 0.0)
            types = o.get("types") or (["HEAL"] if heal else [primary_type if primary_type != "HEAL" else "DPS"])
            cc = o.get("cc", [])
            if kind == "ultimate":
                section = "ultimate"
            elif o.get("section"):
                section = o["section"]
            elif cc and any(c[0] in ("stun", "silence") for c in cc):
                section = "control"
            else:
                section = "spell" if stat == "intelligence" else "attack"
            cost = o.get("cost", (40 if kind == "active" else 80) if not energy else (20 if kind == "active" else 40))
            radius = o.get("radius", 0)
            dur = o.get("dur", max([c[1] for c in cc], default=0))
            effects = []
            for c in cc:
                zone = "area" if d in ("cone", "circle", "line", "self_burst", "storm", "leap") else "target"
                label = CC_LABEL[c[0]].replace("{m}", str(int(round(c[2] * 100))))
                effects.append({"type": c[0], "zone": zone, "duration": c[1], "magnitude": c[2], "radius": radius, "label": label})
            if o.get("text"):
                text = o["text"]
            elif heal:
                text = f"Heal for {{effect}}."
            else:
                shape = {"projectile": "A skillshot that deals", "pierce": "A piercing skillshot that deals", "cone": "A cone that deals",
                         "circle": "An aimed blast that deals", "line": "A line that deals", "self_burst": "A burst around you that deals",
                         "strike": "Strike the target for", "lunge": "Lunge at the target and deal", "leap": "Leap to the aimed spot and deal",
                         "pull": "Catch the first enemy in the line, drag it to you and deal", "storm": "A lasting storm that deals, in total,"}[d]
                text = f"{shape} {{effect}} damage"
                for c in cc:
                    text += {"stun": f", stunning for {c[1]:g}s", "slow": f", slowing by {int(round(c[2]*100))}% for {c[1]:g}s", "silence": f", silencing for {c[1]:g}s"}[c[0]]
                text += "."
            tags = (["Heal"] if heal else ["Damage"] if damage else []) + [c[0].capitalize() for c in cc]
            if o.get("buff"):
                tags.append({"pg_guard": "Guard", "pg_haste": "Haste", "pg_frenzy": "Frenzy", "pg_empower": "Empower"}.get(o["buff"], "Buff"))
            if d == "mark":
                tags.append("Vulnerability")
            row = {
                "id": aid, "name": a["name"], "icon": f"/Game/UI/Abilities/T_{aid}", "types": types, "kind": kind, "school": a["school"],
                "targeting": TARGETING[d], "castTime": o.get("cast", 0),
                "base": {"effect": effect, "manaCost": 0 if energy else cost, "energyCost": cost if energy else 0, "cooldown": o.get("cd", 10),
                         "castTime": o.get("cast", 0), "range": o.get("range", 0), "radius": radius, "duration": dur},
                "effectLabel": "healing" if heal else "damage" if damage else "%",
                "curve": CURVE, "status": "implemented", "signatureOnly": kind == "ultimate",
                "description": text, "effects": effects, "champions": [h["id"]], "signatureOf": [h["id"]],
                "scaling": ({"component": "heal" if heal else "damage", "base": effect, "primary": coef} if (heal or damage) else
                            {"component": "potency", "base": 0, "primary": 0, "potency": 0.4, "potencyCap": 40}),
                "castWhileMoving": o.get("cast", 0) <= 0, "section": section, "effectTags": tags or ["Utility"],
                "paragon": {"hero": h["id"], "clip": casts.get(aid), "fxGroup": a["fx"]},
            }
            # Level-15 bonus (scaling-kits), ultimate upgrade (items-v2) and the sound-event row (audio).
            level_labels = base_abilities["level15Labels"]
            bonus = ("stun" if any(c[0] == "slow" for c in cc) else "vulnerability" if any(c[0] == "stun" for c in cc) else
                     "purge" if heal or d in ("self_buff", "dash") else "dot" if a["school"] in ("fire", "poison", "shadow") else "damageAmp")
            row["level15"] = {"bonus": bonus, "label": "Lv 15: +" + level_labels[bonus],
                              "trigger": "pulse" if heal or d in ("self_buff", "dash", "self_burst", "storm", "party_heal") else "hit"}
            if kind == "ultimate":
                if heal:
                    row["ultimateUpgrade"] = {"name": "Apotheosis", "text": "Allies within 9 m also gain +30 armor for 8 s.", "center": "self", "delay": 0.0,
                                              "effects": [{"type": "partyBuff", "radius": 900, "duration": 8, "stats": {"armor": 30}}]}
                elif d in ("self_burst", "storm") and not o.get("atAim"):
                    row["ultimateUpgrade"] = {"name": "Overwhelm", "text": "You and allies within 7 m gain +25% attack speed for 6 s.", "center": "self", "delay": 0.0,
                                              "effects": [{"type": "partyBuff", "radius": 700, "duration": 6, "stats": {"attackSpeed": 25}}]}
                else:
                    row["ultimateUpgrade"] = {"name": "Shatterpoint", "text": "Enemies within 4.5 m of the target are also slowed for 3 s and armor-broken for 5 s.",
                                              "center": "target", "delay": 0.0,
                                              "effects": [{"type": "slow", "radius": 450, "duration": 3}, {"type": "armorBreak", "radius": 450, "duration": 5}]}
            elements = base_audio["schools"]
            audio_kind = ("heal" if heal else "guard" if o.get("buff") == "pg_guard" and d == "self_buff" else "buff" if d in ("self_buff", "dash", "mark") else
                          "shot" if d in ("projectile", "pierce") and o.get("style") == "arrow" else
                          "melee" if a["school"] == "physical" and d in ("strike", "lunge", "cone", "self_burst", "leap", "line", "pull") else "spell")
            audio[aid] = {"name": a["name"], "element": elements.get(a["school"], "arcane"), "kind": audio_kind}
            if audio_kind == "shot":
                audio[aid]["weapon"] = "bow"

            abilities[aid] = row
            cast_fx, impact_fx = fx_for(pack, a["fx"])
            recipe = {"hero": h["id"], "delivery": d, "fx": {"cast": cast_fx, "impact": impact_fx}}
            for key in ("angle", "warning", "hits", "speed", "style", "buff", "buffPct", "atAim", "taunt"):
                if key in o:
                    recipe[key] = o[key]
            if d in ("self_buff", "mark") and "buff" not in recipe and d == "self_buff":
                problems.append(f"{aid}: self_buff without buff")
            if kind == "ultimate":
                voice = next((s for s in pack.get("sounds", []) if s.endswith("_Ability_Ultimate_Self") or s.endswith("_Effort_Ability_Ultimate")), None)
                if voice:
                    recipe["voice"] = voice + "." + voice.split("/")[-1]
            recipes[aid] = recipe
            own_ids.append(aid)
            if kind == "active" and h.get("pool", True):
                for t in types:
                    pool[t].append(aid)
        # passive
        pid = f"{h['id']}_passive"
        p = h["passive"]
        abilities[pid] = {
            "id": pid, "name": p["name"], "icon": f"/Game/UI/Abilities/T_{pid}", "types": [primary_type], "kind": "passive",
            "school": h["kit"][0]["school"], "targeting": "passive", "castTime": 0,
            "base": {"effect": p["effect"], "manaCost": 0, "energyCost": 0, "cooldown": 0, "castTime": 0, "range": 0, "radius": 0, "duration": 0},
            "effectLabel": "%", "curve": {**CURVE, "effectCap": p["effect"] * 2.5}, "status": "implemented", "signatureOnly": True,
            "description": p["text"], "effects": [], "champions": [h["id"]], "signatureOf": [h["id"]],
            "scaling": {"component": "potency", "base": 0, "primary": 0, "potency": 0.3, "potencyCap": 30},
            "castWhileMoving": True, "section": "passive", "effectTags": ["Passive"], "paragon": {"hero": h["id"], "kind": p["kind"]}}
        aura = {"guard": "armor", "lifesteal": "magicLifesteal" if stat == "intelligence" else "physicalLifesteal", "haste": "aoeResist",
                "frenzy": "attackSpeed", "power": "magicResist" if stat == "intelligence" else "crit"}[p["kind"]]
        abilities[pid]["aura15"] = {"aura": aura, "label": "Lv 15 aura: " + base_abilities["auraLabels"][aura]}
        audio[pid] = {"name": p["name"], "element": base_audio["schools"].get(h["kit"][0]["school"], "arcane"), "kind": "passive"}
        recipes[pid] = {"hero": h["id"], "delivery": "passive", "passive": p["kind"]}
        # role template
        template = template_for(h["threat"], stat, is_ranged)
        t_roster, t_kit = roster_by_id[template], kits[template]
        fillers = [x for x in t_roster["actives"] if x["id"] not in own_ids][:3]
        actives = []
        for aid in own_ids:
            row = abilities[aid]
            if row["kind"] != "active":
                continue
            actives.append({"id": aid, "displayName": row["name"], "status": "implemented", "mechanic": row["description"].replace("{effect}", str(row["base"]["effect"])),
                            "vfxFamily": row["school"], "delivery": ROSTER_DELIVERY[recipes[aid]["delivery"]]})
        actives += fillers
        ult = abilities[own_ids[-1]]
        role_types = sorted({type_of_role[r] for r in h["roles"]}, key=["TANK", "DPS", "HEAL"].index)
        signature = own_ids + [pid] + [x["id"] for x in fillers]
        purchasable = list(dict.fromkeys(signature + t_kit["purchasable"]))
        champions_kits[h["id"]] = {"name": h["name"], "primaryRole": primary_type, "roles": role_types, "signature": signature,
                                   "purchasable": purchasable, "purchasableImplemented": purchasable}
        stats = {"strength": 10, "agility": 10, "intelligence": 10}
        stats[stat] = 20
        archetype = {"strength": 0, "agility": 1, "intelligence": 2 if h["threat"] == "healer" else 4}[stat]
        if h["threat"] == "tank":
            archetype = 0
        roster.append({
            "id": h["id"], "displayName": h["name"], "familyId": "paragon", "race": h["race"], "variant": h["cls"], "classType": h["cls"],
            "difficulty": h["diff"], "lore": h["lore"], "quote": h["quote"],
            "description": f"{h['name']}, {h['cls'].lower()}: " + ", ".join(abilities[i]["name"] for i in own_ids) + ".",
            "runtimeArchetype": archetype, "primaryStat": stat, **stats, "basicAttackRange": h["range"], "attackSeconds": 1.5,
            "attackStyle": h["style"], "threatRole": h["threat"], "roles": h["roles"],
            "artFamily": f"Paragon hero {h['name']} on its own skeletal mesh, clips and particle systems",
            "artStatus": "approved_asset",
            "artProvenance": f"Epic Games Paragon character pack ({h['pack']}), Epic-licensed; local only, never committed.",
            "startsWithSkills": [], "actives": actives,
            "passive": {"id": pid, "displayName": p["name"], "status": "implemented", "mechanic": p["text"].replace("{effect}", str(p["effect"])),
                        "vfxFamily": abilities[pid]["school"], "delivery": "passive"},
            "ultimate": {"id": ult["id"], "displayName": ult["name"], "status": "implemented", "mechanic": ult["description"].replace("{effect}", str(ult["base"]["effect"])),
                         "vfxFamily": ult["school"], "delivery": ROSTER_DELIVERY[recipes[ult["id"]]["delivery"]]}})
        # art binding (monster_native: the hero's own mesh and clips through the game's native layer)
        native = float(mesh["height"]) or 180.0
        target = max(165.0, min(250.0, 180.0 + (native - 190.0) * 0.45))
        attack_alt = attacks[1] if len(attacks) > 1 else None
        animations = {"idle": clip(idle), "walk": clip(walk or run), "run": clip(run), "attack": clip(attacks[0]) if attacks else None,
                      "casts": casts, "contact": contact}
        if attack_alt:
            animations["attackAlt"] = clip(attack_alt)
        if hit:
            animations["hit"] = clip(hit)
        if death:
            animations["death"] = clip(death)
        animations = {k: v for k, v in animations.items() if v is not None}
        mesh_obj = mesh["path"] + "." + mesh["path"].split("/")[-1]
        bindings.append({"profileId": h["id"], "status": "custom_ready", "motion": "monster_native", "reactions": True, "mesh": mesh_obj,
                         "heightCm": round(target, 1), "meshScale": round(target / native, 4), "yaw": -90.0, "groundAtPivot": True, "lockRoot": True,
                         "animations": animations, "parts": [], "source": "Epic Paragon pack (Epic-licensed, local only): " + h["pack"]})
        skin_rows = []
        body = {k: v for k, v in animations.items() if k in ("idle", "walk", "run", "attack", "attackAlt", "hit", "death")}
        for key, m, decision, detail in (classify_skins(h, hero_inv, skeleton) if not h.get("parent") else []):
            skin_table.append((h["name"], spaced(key), decision, detail))
            obj = m["path"] + "." + m["path"].split("/")[-1]
            scale = bindings[-1]["meshScale"]
            if decision == "reskin":
                row = copy.deepcopy(bindings[-1])
                row.update(profileId=f"{h['id']}@{key}", mesh=obj, heightCm=round(max(50.0, min(400.0, float(m["height"]) * scale)), 1))
                bindings.append(row)
                skin_rows.append({"key": key, "name": spaced(key), "mesh": obj})
            elif decision == "monster variant":
                race, unit, why = MONSTER_VARIANTS[(h["id"], key)]
                units.setdefault(unit, {"race": race, "alternates": []})["alternates"].append({
                    "variant": f"Paragon{h['folder']}{key}", "mesh": obj, "meshScale": scale, "yaw": -90.0,
                    "heightCm": round(float(m["height"]) * scale, 1), "lockRoot": True, "reachCm": 900.0 if h["range"] > 400 else 260.0,
                    "animations": body, "source": "Epic Paragon skin (local only): " + h["pack"]})
        sounds = pack.get("sounds", [])
        voice = lambda suffix: next((s + "." + s.split("/")[-1] for s in sounds if s.endswith(suffix)), "")
        skins = sorted({m["skin"] for m in hero_inv.get("meshes", []) if m.get("skin")})
        heroes.append({"id": h["id"], "name": h["name"], "pack": h["pack"], "mesh": mesh_obj, "skeleton": skeleton, "nativeHeightCm": native,
                       "portrait": f"/Game/ParagonDerived/Portraits/T_Portrait_{h['id']}.T_Portrait_{h['id']}",
                       "background": BACKGROUNDS.get(h["id"], template),

                       "animBlueprint": [b["path"] for b in hero_inv.get("animbp", [])], "skins": skin_rows, "skinFolders": skins, "own": own_ids,
                       "parent": h.get("parent", ""),
                       "voice": {"select": voice("_DraftSelect"), "lock": voice("_DraftLock")},
                       "inventory": {"anims": len(anims), "montages": len(hero_inv.get("montages", {})), "fxSystems": sum(len(v) for v in pack.get("fx", {}).values()),
                                     "fxGroups": sorted(pack.get("fx", {})), "sounds": len(sounds), "materials": len({mm["material"] for mm in mesh.get("materials", [])})},
                       "playable": True})
    # ParagonMinions: creep inventory for future monster variants (not used by gameplay yet).
    minions = inspect.get("ParagonMinions", {})
    creeps = {}
    # ParagonMinions as monster variants: clips come from any minion folder on the same skeleton.
    minion_anims = {}
    for unit_name, inv in minions.get("heroes", {}).items():
        for n, a in inv.get("anims", {}).items():
            minion_anims.setdefault(a.get("skeleton", ""), {}).setdefault(n.split("#")[0], a["path"] + "." + a["path"].split("/")[-1])
    for folder, mesh_name, race, unit, why in MINION_UNITS:
        inv = minions.get("heroes", {}).get(folder, {})
        m = next((x for x in inv.get("meshes", []) if x["path"].split("/")[-1] == mesh_name), None)
        if not m:
            problems.append(f"minion {mesh_name}: mesh not found")
            continue
        clips = minion_anims.get(m["skeleton"], {})
        first = lambda *names: next((clips[n] for n in names if n in clips), None)
        mb = {"idle": first("Idle", "Melee_Idle_A", "Idle_A", "Aggro_Transition_A"),
              "run": first("Run_FWD", "Run_Fwd", "Combat_JogFwd", "Jog_Fwd_Combat", "Combat_Jog_Fwd_Alt", "NonCombat_Jog_Fwd", "Melee_Run_Forward", "Fly_Fwd_FullSpeed") or (first("Idle") if unit == "aetheri_colossus" else None),
              "attack": first("Attack_A", "BiteAttack_A", "Attack_Punch_01", "Melee_Attack_01_A", "Fire_A", "Attack_Special_1", "Primary_Fire"),
              "attackAlt": first("Attack_B", "BiteAttack_B", "Attack_Punch_02", "Melee_Attack_02_A", "Fire_B", "Attack_Special_2"),
              "hit": first("HitReaction_FWD", "Hit_Front", "Hitreat_Fwd", "Melee_Hit_Front_05_A", "Hitreact_Fwd", "Hit_React_Fwd"),
              "death": first("Death_front", "Death_Fwd", "Death_Front", "Melee_Death_Fwd_01_A", "Death", "Death_A")}
        mb["walk"] = first("Walk_Fwd", "Fly_Fwd_MidSpeed") or mb["run"]
        if not mb["idle"] or not mb["run"] or not mb["attack"]:
            skin_table.append(("ParagonMinions", mesh_name, "not usable", "no idle / run / attack clips on its skeleton"))
            continue
        target = max(110.0, min(420.0, float(m["height"]) * 0.9))
        units.setdefault(unit, {"race": race, "alternates": []})["alternates"].append({
            "variant": "Paragon" + mesh_name.replace("_", ""), "mesh": m["path"] + "." + m["path"].split("/")[-1], "meshScale": round(target / float(m["height"]), 4),
            "yaw": -90.0, "heightCm": round(target, 1), "lockRoot": True,
            "reachCm": 900.0 if unit in ("aetheri_lancer", "barbspitter", "crystal_ballista") else 260.0,
            "animations": {k: v for k, v in mb.items() if v}, "source": "Epic ParagonMinions (local only)"})
        skin_table.append(("ParagonMinions", spaced(mesh_name.replace("_", "")), "monster variant", f"{race} / {unit}: {why}"))
    for unit, inv in sorted(minions.get("heroes", {}).items()):
        if not inv.get("meshes"):
            continue
        creeps[unit] = {"meshes": [m["path"] for m in inv["meshes"]][:6], "animBlueprint": [b["path"] for b in inv.get("animbp", [])],
                        "anims": len(inv.get("anims", {})), "heightCm": inv["meshes"][0]["height"]}
    source_hash = hashlib.sha256(json.dumps(HEROES, sort_keys=True).encode("utf-8")).hexdigest()
    doc = {
        "schemaVersion": 1,
        "generator": "Tools/AuthorParagonChampions.py",
        "notes": "Paragon heroes as playable champions (Docs/ParagonChampions.md). Every /Game/Paragon* path points into Epic-licensed packs that are LOCAL ONLY (gitignored, never committed); a hero registers only when its mesh exists locally. Numbers are Ability DB rows (base + coefficient x PRIMARY); recipes say how each ability lands and which Paragon clip / FX it uses.",
        "schools": base_abilities["schools"],
        "abilities": abilities,
        "champions": champions_kits,
        "poolAdditions": pool,
        "recipes": recipes,
        "audio": audio,

        "roster": {"schemaVersion": 1, "profile": "CireChampionRoster", "engine": "Unreal", "engineVersion": "5.8.3",
                   "statPolicy": "existing_stat_per_point",
                   "source": {"path": "Tools/AuthorParagonChampions.py (HEROES)", "encoding": "windows-1252", "sha256": source_hash},
                   "champions": roster},
        "bindings": bindings,
        "heroes": heroes,
        "creeps": creeps,
        "skinTable": [{"hero": a, "skin": b, "decision": c, "detail": d} for a, b, c, d in skin_table],
        "problems": problems,
    }
    text = json.dumps(doc, indent=1, ensure_ascii=False) + "\n"
    doc_table = markdown_table(HEROES + derive_new_champions(inspect), abilities, heroes, bindings)
    skins_md = "\n| Hero | Skin | Decision | Detail |\n|---|---|---|---|\n" + "".join(f"| {a} | {b} | **{c}** | {d} |\n" for a, b, c, d in skin_table) + "\n"
    if args.check:
        current = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
        if current != text:
            print("ParagonChampions.json is stale; run Tools/AuthorParagonChampions.py")
            return 1
        print("ParagonChampions.json up to date")
        return 0
    OUT.write_text(text, encoding="utf-8")
    # Monster variants live in their own small file (CireMonsterArt reads RaceMeshes.*.json, capped at 400 KB).
    (ROOT / "Content/Data/RaceMeshes.paragon.json").write_text(json.dumps({
        "schemaVersion": 1, "generator": "Tools/AuthorParagonChampions.py",
        "description": "Paragon skins and ParagonMinions as extra variants of race units (Docs/ParagonChampions.md). Merged into each unit's bodies by CireMonsterArt; a body is used only when its mesh and idle clip exist locally (Epic-licensed packs, never committed).",
        "units": units}, indent=1) + "\n", encoding="utf-8")
    md = ROOT / "Docs/ParagonChampions.md"
    if md.exists():
        body = md.read_text(encoding="utf-8")
        start, end = "<!-- paragon-table -->", "<!-- /paragon-table -->"
        if start in body and end in body:
            body = body[:body.index(start) + len(start)] + "\n" + doc_table + body[body.index(end):]
        s2, e2 = "<!-- paragon-skins -->", "<!-- /paragon-skins -->"
        if s2 in body and e2 in body:
            body = body[:body.index(s2) + len(s2)] + "\n" + skins_md + body[body.index(e2):]
            md.write_text(body, encoding="utf-8")
    from collections import Counter
    print("skins:", dict(Counter(r[2] for r in skin_table)), "units:", {k: len(v["alternates"]) for k, v in units.items()})
    print(f"heroes={len(heroes)} abilities={len(abilities)} bindings={len(bindings)} pool=" + ",".join(f"{k}:{len(v)}" for k, v in pool.items()) +
          f" creeps={len(creeps)} problems={len(problems)}")
    for p in problems:
        print("  PROBLEM", p)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
