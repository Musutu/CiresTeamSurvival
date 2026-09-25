"""Author Content/Data/Bestiary.json and Content/Data/AudioCues.expansion.json (monster-expansion, Docs/MonsterExpansion.md).

Bestiary.json adds the creatures built from the purchased creature packs that the game did not use yet. Each is a full NPC
archetype (merged into the NPC database after Races.json by CireMonsterExpansion::MergeInto) with:
  kind      bonus (Bonus Loot Wave), rare (Rare Spawn pool) and/or variant (joins a race's waves by slot)
  fallback  the archetype whose body it borrows when the Fab pack is not installed (clean clones, -CireNoFab)
  race/slot for race variants: every `every`-th unit of that race slot in a wave spawns as this creature instead
  sounds    creature cues (AudioCues.expansion.json): attack, hit, death, spawn
Art lives in RaceMeshes.fabx.json (Tools/BuildFabExpansionCreatures.py, local packs only).

Usage: python Tools/AuthorBestiary.py   (plain Python; no editor)
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CS, MS, UI = "/Game/Combat_Sounds_-_Lite", "/Game/Magic_Spell_SFX_Pack_Vol1", "/Game/Fantasy_UI_SFX_Pack/WAVs"


def melee(name, desc, rng=170):
    return {"id": "npc_melee", "name": name, "description": desc, "type": "melee", "basic": True, "range": rng}


def arch(name, role, tuning, speed, reach, interval, scale, health, tint, abilities, **extra):
    a = {"displayName": name, "role": role, "classification": "normal", "tuningKind": tuning, "moveSpeed": speed, "attackRange": reach,
         "attackInterval": interval, "scale": scale, "healthScale": health, "mesh": {"slot": "bestiary_" + name.lower().replace(" ", "_"), "path": "", "tint": tint},
         "abilities": abilities}
    a.update(extra)
    return a


FERAL, SUNDER, RAGE, FIRE, WARD = "npc_feral", "npc_sundered", "npc_bloodlust", "npc_dragonfire", "npc_scaleward"
UNITS = {
    "treasure_goblin": dict(kind=["bonus"], fallback="hollow_infantry", sounds="goblin",
        look="A hunched goblin re-dyed in treasure gold, clutching stolen coin; it squeals and bolts from champions.",
        archetype=arch("Treasure Goblin", "bruiser", "basic", 330, 150, 2.0, .9, .9, [0.95, 0.72, 0.2],
                       [melee("Purse Swipe", "A panicked swipe with its coin purse.", 150)])),
    "gilded_stag": dict(kind=["bonus"], fallback="grave_hound", sounds="stag",
        look="A golden stag with gilded antlers: a living treasure that runs from the fight.",
        archetype=arch("Gilded Stag", "bruiser", "basic", 360, 180, 2.0, 1.0, 1.3, [1.0, 0.8, 0.3],
                       [melee("Antler Toss", "A startled toss of its gilded antlers.", 180)])),
    "rotting_shambler": dict(kind=["variant"], race="hollow", slot="line", every=3, fallback="hollow_infantry", sounds="zombie",
        look="A bloated grave-green corpse that shambles in the Hollow Legion's line, slower but hard to put down.",
        archetype=arch("Rotting Shambler", "bruiser", "basic", 175, 165, 2.0, 1.0, 1.25, [0.46, 0.52, 0.38], [
            melee("Rotten Claws", "Filthy claws rake its current target.", 165),
            {"id": "shambler_grasp", "name": "Grave Grasp", "description": "A telegraphed lunge that roots champions in the cone for 1.5s.", "type": "cone",
             "cooldown": 12, "castTime": 1.0, "range": 260, "radius": 260, "angle": 70, "damageMultiplier": 1.4, "color": [0.35, 0.6, 0.2, 0.35],
             "buff": FERAL, "cue": "npc.root.start", "root": 1.5},
            {"id": "shambler_rot", "name": "Festering Rot", "description": "Marks the ground under its target; a pool of rot seeps there for 5s.", "type": "targetCircle",
             "cooldown": 14, "castTime": 1.2, "range": 420, "radius": 200, "duration": 5, "damagePerSecond": 10, "color": [0.3, 0.55, 0.1, 0.35], "cue": "npc.spores.start"}])),
    "bone_archer": dict(kind=["variant"], race="hollow", slot="ranged", every=2, fallback="barbed_hunter", sounds="archer",
        look="A helmeted skeleton archer with quiver and bone bow, loosing from the Hollow Legion's rear.",
        archetype=arch("Bone Archer", "ranged", "ranged", 205, 650, 2.2, 1.0, .95, [0.8, 0.78, 0.7], [
            {"id": "npc_barbed_shot", "name": "Bone Arrow", "description": "Short aim, then an arrow in a fixed direction.", "type": "projectile", "basic": True,
             "skillshot": "npc_barbed_shot", "castTime": 0.6, "range": 650},
            {"id": "bone_volley", "name": "Bone Volley", "description": "Marks a circle on its target, then a volley of bone arrows lands there.", "type": "targetCircle",
             "cooldown": 11, "castTime": 1.0, "range": 650, "radius": 200, "damageMultiplier": 1.8, "color": [0.75, 0.7, 0.55, 0.35]}],
            preferredRange=520, kiteRange=300)),
    "centaur_blademaster": dict(kind=["variant"], race="feral_kin", slot="bruiser", every=3, fallback="wild_outrider", sounds="centaur",
        look="An armoured centaur with a drawn broadsword, pauldrons and mane: the feral kin's cavalry blade.",
        archetype=arch("Centaur Blademaster", "bruiser", "bruiser", 230, 200, 1.8, 1.0, 1.1, [0.5, 0.36, 0.22], [
            melee("Broadsword", "A sweeping broadsword cut at its current target.", 200),
            {"id": "centaur_trample", "name": "Trample", "description": "Marks a line and gallops through it, knocking champions aside.", "type": "charge",
             "cooldown": 13, "castTime": 0.9, "minRange": 320, "range": 900, "length": 900, "width": 190, "damageMultiplier": 1.8, "color": [0.8, 0.4, 0.15, 0.35],
             "buff": SUNDER, "cue": "npc.cleave.start", "knockback": 320},
            {"id": "centaur_sweep", "name": "Blade Sweep", "description": "A telegraphed sweep of the broadsword in a wide cone.", "type": "cone",
             "cooldown": 9, "castTime": 1.0, "range": 330, "radius": 330, "angle": 100, "damageMultiplier": 2.0, "color": [0.8, 0.4, 0.15, 0.35],
             "buff": FERAL, "cue": "npc.cleave.start"}])),
    "horned_brute": dict(kind=["variant", "rare"], race="ironhide", slot="bruiser", every=3, fallback="ironbound_bruiser", sounds="brute",
        look="A horned, armoured brute with a heavy blade, re-lit with ember eyes: the warband's shock trooper.",
        archetype=arch("Horned Brute", "bruiser", "bruiser", 190, 185, 1.9, 1.05, 1.2, [0.45, 0.3, 0.2], [
            melee("Heavy Blade", "A crushing blade chop at its current target.", 185),
            {"id": "brute_cleave", "name": "Horned Cleave", "description": "Winds up a heavy cleave in a cone.", "type": "cone",
             "cooldown": 9, "castTime": 1.1, "range": 340, "radius": 340, "angle": 85, "damageMultiplier": 2.2, "color": [0.85, 0.12, 0.05, 0.4],
             "buff": SUNDER, "cue": "npc.cleave.start"},
            {"id": "brute_leap", "name": "Leap Slam", "description": "Marks a line and leaps along it, knocking champions aside.", "type": "charge",
             "cooldown": 14, "castTime": 0.9, "minRange": 320, "range": 850, "length": 850, "width": 180, "damageMultiplier": 1.8, "color": [0.85, 0.12, 0.05, 0.4],
             "buff": SUNDER, "cue": "npc.warcry.start", "knockback": 300},
            {"id": "brute_frenzy", "name": "Frenzy", "description": "At 40% health it goes berserk: +40% damage and faster attacks.", "type": "enrage",
             "cooldown": 0, "castTime": 1.0, "range": 5000, "magnitude": 0.4, "healthThreshold": 0.4, "buff": RAGE, "cue": "npc.warcry.start"}],
            # The Khornes pack's own blade (local-only Fab path; skipped when the pack is missing).
            props=[{"asset": "/Game/Monster/Mesh/SM_Monster_Sword.SM_Monster_Sword", "bone": "hand_r", "scale": 1.5}])),
    "lich_revenant": dict(kind=["rare", "variant"], race="fallen_order", slot="caster", every=4, fallback="blight_caster", sounds="lich",
        look="A floating lich in rags and bone armour with a soul-lit chest: a rare terror that drifts in with the wave.",
        archetype=arch("Lich Revenant", "caster", "caster", 185, 650, 2.2, 1.0, 1.3, [0.35, 0.3, 0.45], [
            {"id": "npc_shadow_bolt", "name": "Soul Bolt", "description": "Interruptible cast. Fires a soul bolt in a fixed direction.", "type": "projectile", "basic": True,
             "interruptible": True, "skillshot": "npc_shadow_bolt", "castTime": 1.3, "range": 650},
            {"id": "lich_frost_nova", "name": "Grave Nova", "description": "Marks a ring around itself, then a nova roots champions inside for 1.5s.", "type": "selfCircle",
             "cooldown": 14, "castTime": 1.2, "range": 320, "radius": 320, "damageMultiplier": 1.5, "color": [0.4, 0.7, 1.0, 0.35], "cue": "npc.root.start", "root": 1.5},
            {"id": "lich_soul_rend", "name": "Soul Rend", "description": "Marks a circle under its target, then tears at the souls there.", "type": "targetCircle",
             "cooldown": 10, "castTime": 1.2, "range": 650, "radius": 210, "damageMultiplier": 2.0, "color": [0.45, 0.2, 0.75, 0.4], "cue": "npc.void.cast"},
            {"id": "lich_mend", "name": "Unholy Mending", "description": "Interruptible 2s cast that restores 20% health to the most injured ally below 60%.", "type": "healAlly",
             "interruptible": True, "cooldown": 14, "castTime": 2.0, "range": 900, "radius": 900, "magnitude": 0.2, "healthThreshold": 0.6, "cue": "npc.heal.cast"}],
            preferredRange=560, kiteRange=320)),
    "storm_griffon": dict(kind=["rare"], fallback="dire_wolf", sounds="griffon",
        look="A great griffon with storm-grey feathers that stalks the lane on talons and beak.",
        archetype=arch("Storm Griffon", "bruiser", "bruiser", 240, 230, 1.8, 1.0, 1.5, [0.55, 0.5, 0.42], [
            melee("Talon Rake", "Raking talons at its current target.", 230),
            {"id": "griffon_dive", "name": "Diving Pounce", "description": "Marks a line and pounces along it, knocking champions aside.", "type": "charge",
             "cooldown": 12, "castTime": 0.9, "minRange": 320, "range": 900, "length": 900, "width": 200, "damageMultiplier": 1.9, "color": [0.55, 0.75, 1.0, 0.35],
             "buff": SUNDER, "cue": "npc.feral.start", "knockback": 350},
            {"id": "griffon_frenzy", "name": "Talon Frenzy", "description": "A telegraphed flurry of talons and beak in a cone.", "type": "cone",
             "cooldown": 9, "castTime": 1.0, "range": 340, "radius": 340, "angle": 90, "damageMultiplier": 2.1, "color": [0.55, 0.75, 1.0, 0.35], "buff": FERAL, "cue": "npc.feral.start"}])),
    "cinder_drake": dict(kind=["rare"], fallback="ember_whelp", sounds="drake",
        look="A low, horned drake with ember-lit scales that breathes a cone of fire.",
        archetype=arch("Cinder Drake", "bruiser", "bruiser", 210, 220, 1.9, 1.0, 1.6, [0.6, 0.25, 0.12], [
            melee("Bite", "A crushing bite at its current target.", 220),
            {"id": "drake_breath", "name": "Cinder Breath", "description": "Breathes a long cone of fire.", "type": "cone",
             "cooldown": 10, "castTime": 1.2, "range": 480, "radius": 480, "angle": 55, "damageMultiplier": 1.9, "color": [1.0, 0.45, 0.08, 0.4], "buff": FIRE, "cue": "npc.fire.start"},
            {"id": "drake_embers", "name": "Ember Spit", "description": "Spits embers that burn the ground under its target for 5s.", "type": "targetCircle",
             "cooldown": 12, "castTime": 1.2, "range": 650, "radius": 220, "damagePerSecond": 14, "duration": 5, "color": [1.0, 0.45, 0.08, 0.4], "buff": FIRE, "cue": "npc.fire.cast"}])),
    "frostfang_alpha": dict(kind=["rare"], fallback="dire_wolf", sounds="wolf",
        look="A huge white wolf with ice-blue eyes: the alpha of a pack no one has seen.",
        archetype=arch("Frostfang Alpha", "bruiser", "bruiser", 270, 170, 1.5, 1.0, 1.3, [0.85, 0.9, 1.0], [
            melee("Frost Bite", "A savage bite at its current target.", 170),
            {"id": "frostfang_pounce", "name": "Pounce", "description": "Marks a short line and pounces along it.", "type": "charge",
             "cooldown": 11, "castTime": 0.7, "minRange": 260, "range": 700, "length": 700, "width": 170, "damageMultiplier": 1.7, "color": [0.55, 0.8, 1.0, 0.35],
             "buff": FERAL, "cue": "npc.feral.start"},
            {"id": "frostfang_hamstring", "name": "Frozen Hamstring", "description": "A telegraphed bite that slows the victims for 3s.", "type": "cone",
             "cooldown": 9, "castTime": 0.8, "range": 260, "radius": 260, "angle": 70, "damageMultiplier": 1.6, "color": [0.55, 0.8, 1.0, 0.35],
             "buff": FERAL, "cue": "npc.feral.start", "slow": 3},
            {"id": "frostfang_howl", "name": "Winter Howl", "description": "Howls: nearby monsters gain +20% damage and attack speed for 8s.", "type": "rally",
             "cooldown": 20, "castTime": 1.2, "range": 5000, "radius": 1000, "duration": 8, "magnitude": 0.2, "color": [0.55, 0.8, 1.0, 0.25], "buff": RAGE, "cue": "npc.roar.cast"}])),
}


def cue(pack, sounds, volume=0.7, pitch=(0.95, 1.05), cooldown=0.12, **extra):
    c = {"sounds": sounds, "volume": volume, "pitch": list(pitch), "cooldown": cooldown, "pack": pack, "combat": True, "attenuation": "combat"}
    c.update(extra)
    return c


W = lambda folder, n: "%s/%s/wav/%s" % (CS, folder, n)
M = lambda folder, n: "%s/%s/wav/%s" % (MS, folder, n)
SNARL, GROWL, LOOT, SPARK = ["Auras/AUR_Snarl"], ["SFX/SFX_PackLeaderGrowl"], ["SFX/SFX_LootPickup"], ["Auras/AUR_Sparkle"]
CUES = {
    # creature voices: attack whoosh/strike, hit, death (spawn for rares that arrive with a flourish)
    "creature.goblin.attack": cue([W("Combat", "Whoosh_1_1")], SNARL, .55, (1.3, 1.45)),
    "creature.goblin.hit": cue(["%s/Cute_Fantasy_UI/WAV_Fantasy_UI_Cute_Item_Collect_01_mono" % UI, "%s/Cute_Fantasy_UI/WAV_Fantasy_UI_Cute_Item_Collect_02_mono" % UI], LOOT, .65, (0.95, 1.1), .25),
    "creature.goblin.death": cue(["%s/Cute_Fantasy_UI/WAV_Fantasy_UI_Cute_Star_Burst_01_stereo" % UI], SPARK, .8, (1.0, 1.0), .2),
    "creature.stag.attack": cue([W("Combat", "Whoosh_3_1")], SNARL, .55, (0.9, 1.0)),
    "creature.stag.hit": cue([W("Combat", "Punch_2_1")], SNARL, .6, (1.1, 1.2), .25),
    "creature.stag.death": cue([M("General_Spells_and_Effects", "Positive_Magic_Effect_3-1_wav"), W("Combat", "Body_Fall_1_1")], SPARK, .75, (1.0, 1.0), .2),
    "creature.zombie.attack": cue([W("Combat", "Blood___Gore_1_2"), W("Combat", "Blood___Gore_2_4")], SNARL, .6, (0.8, 0.9)),
    "creature.zombie.hit": cue([W("Combat", "Blood_Drop_1")], SNARL, .55, (0.8, 0.95), .3),
    "creature.zombie.death": cue([W("Combat", "Body_Fall_2_4")], GROWL, .7, (0.75, 0.85), .2),
    "creature.archer.attack": cue([W("Weapons_Shield", "Arrow_Shot_1_1")], SNARL, .6, (0.95, 1.05)),
    "creature.archer.hit": cue([W("Combat", "Bone_1_12")], SNARL, .6, (0.95, 1.05), .3),
    "creature.archer.death": cue([W("Combat", "Bone_2_7")], SNARL, .7, (0.9, 1.0), .2),
    "creature.centaur.attack": cue([W("Weapons_Shield", "Whoosh_Metal_1_1")], SNARL, .6, (0.9, 1.0)),
    "creature.centaur.hit": cue([W("Combat", "Punch_3_1")], SNARL, .6, (0.85, 0.95), .3),
    "creature.centaur.death": cue([W("Combat", "Body_Fall_2_4")], GROWL, .75, (0.8, 0.9), .2),
    "creature.brute.attack": cue([W("Weapons_Shield", "Metal_Weapon_Clash_1_11"), W("Weapons_Shield", "Metal_Weapon_Clash_1_12")], SNARL, .65, (0.8, 0.9)),
    "creature.brute.hit": cue([W("Combat", "Generic_Hit_4_1")], SNARL, .6, (0.8, 0.9), .3),
    "creature.brute.death": cue([W("Combat", "Body_Fall_2_4")], GROWL, .8, (0.7, 0.8), .2),
    "creature.lich.attack": cue([M("Dark", "Dark_Magic_Spell_3-1_wav"), M("Dark", "Dark_Magic_Spell_3-2_wav")], SNARL, .55, (0.95, 1.05)),
    "creature.lich.hit": cue([M("General_Spells_and_Effects", "Negative_Magic_Effect_2-1_wav")], SNARL, .5, (1.0, 1.1), .35),
    "creature.lich.death": cue([M("Dark", "Dark_Magic_Spell_8-1_wav")], GROWL, .8, (0.9, 0.9), .2),
    "creature.lich.spawn": cue([M("Dark", "Dark_Magic_Spell_7-1_wav")], GROWL, .75, (0.9, 0.9), 1.0),
    "creature.griffon.attack": cue([W("Combat", "Whoosh_3_1"), W("Combat", "Stab_2_1")], SNARL, .65, (0.8, 0.9)),
    "creature.griffon.hit": cue([W("Combat", "Punch_3_1")], SNARL, .6, (0.75, 0.85), .3),
    "creature.griffon.death": cue([W("Combat", "Body_Fall_1_1")], GROWL, .85, (0.6, 0.7), .2),
    "creature.drake.attack": cue([M("Fire", "Fire_Magic_Spell_1-1_wav"), M("Fire", "Fire_Magic_Spell_1-2_wav")], SNARL, .6, (0.85, 0.95)),
    "creature.drake.hit": cue([W("Combat", "Generic_Hit_4_1")], SNARL, .6, (0.7, 0.8), .3),
    "creature.drake.death": cue([M("Fire", "Fire_Magic_Spell_3-1_wav"), W("Combat", "Body_Fall_2_4")], GROWL, .85, (0.7, 0.8), .2),
    "creature.wolf.attack": cue([W("Combat", "Stab_3_1")], SNARL, .6, (1.0, 1.1)),
    "creature.wolf.hit": cue([M("Frost", "Frost_Magic_Spell_2-1_wav")], SNARL, .45, (1.1, 1.2), .35),
    "creature.wolf.death": cue([M("Frost", "Frost_Magic_Spell_5-1_wav"), W("Combat", "Body_Fall_1_1")], GROWL, .75, (0.9, 1.0), .2),
    # stings (2D, UI bus): a rare arrives, a bonus wave starts, a bonus creature escapes / is caught
    "sting.rare": cue(["%s/Dark_Fantasy_UI/WAV_Fantasy_UI_Dark_Alert_01_stereo" % UI, "%s/Dark_Fantasy_UI/WAV_Fantasy_UI_Dark_Bell_01_mono" % UI],
                      ["SFX/SFX_WarHornDistant"], .6, (1.0, 1.0), 2.0, bus="ui", combat=False),
    "sting.bonus_wave": cue(["%s/Cute_Fantasy_UI/WAV_Fantasy_UI_Cute_Game_Start_01_stereo" % UI, "%s/Cute_Fantasy_UI/WAV_Fantasy_UI_Cute_Reward_Obtained_01_stereo" % UI],
                            ["SFX/SFX_WarHornDistant"], .75, (1.0, 1.0), 2.0, bus="ui", combat=False, **{"with": ["loot_shine"]}),
    "bonus.escape": cue(["%s/Cute_Fantasy_UI/WAV_Fantasy_UI_Cute_Exit_01_stereo" % UI], SPARK, .7, (1.0, 1.0), .3),
    "bonus.caught": cue(["%s/Generic_Fantasy_UI/WAV_Fantasy_UI_Gen_Reward_Obtained_stereo" % UI], LOOT, .7, (1.0, 1.05), .2, **{"with": ["loot_shine"]}),
}


def main():
    doc = {"schemaVersion": 1,
           "notes": "monster-expansion bestiary (Docs/MonsterExpansion.md). Generated by Tools/AuthorBestiary.py: edit the script, then run it. "
                    "Creatures from the purchased creature packs that the races did not use yet. kind: bonus (Waves.json bonusWave), rare (Waves.json rareSpawn.pool), "
                    "variant (every `every`-th unit of race/slot in a wave spawns as this creature). fallback = the body used without the Fab packs. Art: RaceMeshes.fabx.json.",
           "specialColors": {"rare": [0.2, 0.95, 1.0], "bonus": [1.0, 0.78, 0.12]},
           "units": {}}
    for unit, spec in UNITS.items():
        row = {k: v for k, v in spec.items() if k != "archetype"}
        row["sounds"] = {r: "creature.%s.%s" % (spec["sounds"], r) for r in ("attack", "hit", "death")}
        if spec["sounds"] == "lich":
            row["sounds"]["spawn"] = "creature.lich.spawn"
        row["archetype"] = spec["archetype"]
        doc["units"][unit] = row
    (ROOT / "Content/Data/Bestiary.json").write_text(json.dumps(doc, indent=1) + "\n", encoding="utf-8")
    cues = {"schemaVersion": 1,
            "notes": "monster-expansion cue overlay, merged into AudioCues.json cues by CireAudio (Docs/MonsterExpansion.md). Generated by Tools/AuthorBestiary.py. "
                     "`pack` = purchased pack sounds (preferred when installed); `sounds` = shipped fallback.",
            "cues": CUES}
    (ROOT / "Content/Data/AudioCues.expansion.json").write_text(json.dumps(cues, indent=1) + "\n", encoding="utf-8")
    print("BESTIARY units=%d cues=%d" % (len(UNITS), len(CUES)))


if __name__ == "__main__":
    main()
