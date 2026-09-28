"""Monster-ability, hit / kill and event Fab VFX signatures (pack-usage). Imported by Tools/MapFabVFX.py.

Every monster race ability (Content/Data/Races.json, NPCArchetypes.json, Bestiary.json) gets its own signature system
per presentation role, chosen from the race's kit (Eric: quality first - Shadow Magic, Earth Spells, Forest, State VFX and
Realistic Blood lead; the Big Pack where its effect genuinely fits) and de-duplicated inside a unit: when two abilities of
one unit would draw the same system, the second takes a recolour variant from the race palette instead of a weaker
system. Crowd-control effects are always tinted the same way whatever the race (CC_TINT: root, silence, stun, slow,
charm, blind), so the colour tells the victim what hit them.

Roles: c cast (wind-up flare on the caster; monster wind-ups use it too), p projectile, i impact (at the target /
each victim), a live area (fitted ground overlay on circle zones with a measured groundRadius).

Values are stems as the packs name them (NS_ dropped), or (stem, scale), or {"s": stem, "scale": .., "tint": [r,g,b],
"strength": 0..1}. Hand overrides in MONSTER_OVERRIDES win over the generated pick.

    python Tools/FabMonsterVFXTable.py     # print the generated table (debug)
"""
from __future__ import annotations

import json
from collections import OrderedDict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DATA = REPO / "Content" / "Data"

# Crowd-control / effect-type tints (impact + area roles): the same colour language on every race.
CC_TINT = {
    "npc_rooted": [0.45, 0.85, 0.2],    # root: living green
    "npc_silenced": [0.9, 0.25, 0.85],  # silence: magenta
    "stunned": [1.0, 0.85, 0.2],        # stun: gold
    "npc_slowed": [0.5, 0.8, 1.0],      # slow / freeze: ice blue
    "npc_mind": [0.4, 0.9, 0.95],       # charm / madness: teal
    "npc_ink": [0.15, 0.2, 0.45],       # blind: ink navy
}

# pack-usage-3: C-rated systems (visible on the catalogue stage but weak / nearly invisible / broken material, see
# Art/Fab/FabVFXInventory.json quality) are swapped for an A/B system of the same theme wherever a table names them.
# Projectiles, orbs and muzzles are small by design and stay; stage-blank systems (need motion or a target) are unrated.
# A race / CC tint on the pick still wins over the replacement's own tint.
GREEN = [0.45, 0.85, 0.3]
DEMOTED = {
    "Stab_Med": "BulletHit_High", "Fire_Magic_Hit": ("Fire_Magic_Splash", 0.8), "Earth_Magic_Stone2": "Earth_Magic_Stone1",
    "Blood_Magic_Crystal1": "Blood_Magic_Crystal2", "BloodSplash_Med": ("BloodSplash_High", 0.8), "Blood_Magic_Spike2": "Blood_Magic_Spike1",
    "Explosion_Grenade_Nature": {"s": "Earth_Spells_Hit2", "tint": GREEN}, "Explosion_Nature": {"s": "Earth_Spells_Hit3", "tint": GREEN},
    "Explosion_Cast_Nature": {"s": "Earth_Spells_Buff", "tint": GREEN}, "Wing_Nature": "TextureParticle_Forest",
    "Ice_Magic_Frozen": "Ice_Magic_Aura", "Dark_Magic_Shield": "Dark_Magic_Buff", "Fire_Magic_Circle": "Fire_Magic_Shockwave",
    "Lightning_Magic_Orb3": "Lightning_Magic_Orb2", "Earth_Spells_Slash": "Earth_Spells_Cone", "Fire_Magic_Flamethrower": "Fire_Magic_Flame1",
    "Light_Magic_Sword_Area": "Light_Magic_Sword_Line_Area1", "Light_Magic_Slash2": "Light_Magic_Slash1",
    "Light_Magic_Sword_Line": "Light_Magic_Sword_Circle", "Posion_Magic_Rain": "Posion_Magic_SpikeArea", "Blood_Magic_Wall1": "Blood_Magic_Wall2",
    "P_ky_magicCircle1": "Lightning_Magic_Tunder_Circle1", "Posion_Magic_Target": "Posion_Magic_Buff", "Dark_Magic_Slash2": "Dark_Magic_Slash1",
    "Shadow_Magic_Slash2": "Shadow_Magic_Slash3", "Ice_Magic_Circle2": "Ice_Magic_Circle1", "Ice_Magic_Circle3": "Ice_Magic_Circle1",
    "Water_Magic_Slash3": "Water_Magic_Slash1", "Water_Magic_Wave1": "Water_Magic_Waterflow1", "Water_Magic_Wall": "Water_Magic_Wall2",
    "Dark_Magic_Area_Line": "Dark_Magic_Cone1", "Air_Magic_Air_Wall": "Air_Magic_Shield", "P_ky_shootingStar1": "Light_Magic_Top",
    "Earth_Magic_Slash1": "Earth_Magic_Splash", "Earth_Magic_Slash2": "Earth_Magic_Spike3", "Air_Magic_Buff": "Air_Magic_Aura",
    "Earth_Spells_Arena": "Earth_Magic_Shockwave", "Air_Magic_Airflow": "Air_Magic_Tornado3", "Earth_Magic_Circle1": "Earth_Spells_Circle",
    "Shadow_Magic_Dash2": "Shadow_Magic_Blink1", "Lightning_Magic_Dash": "Lightning_Magic_Blink1", "P_ky_shotShockwave": "Light_Magic_Blink2",
    "Earth_Spells_Orb": "Earth_Spells_Buff", "Ice_Magic_Sheild": "Ice_Magic_Sheild2", "P_ky_laser01": "Lightning_Magic_Line1",
    "Posion_Magic_Debuff": "State_VFX_Poison1", "State_VFX_Freeze1": "Ice_Magic_Aura",
}


def demote(pick):
    """pack-usage-3: swap a C-rated stem for its replacement; the pick's own tint / scale win."""
    if pick.get("s") in DEMOTED:
        rep = as_dict(DEMOTED[pick["s"]])
        pick["s"] = rep["s"]
        if "tint" not in pick and rep.get("tint"):
            pick["tint"] = rep["tint"]
        if "scale" not in pick and rep.get("scale"):
            pick["scale"] = rep["scale"]
    return pick


# Race kits: ordered pools per slot kind (first = preferred). Systems are stems; a (stem, scale) or dict entry is allowed.
# Tints: "tint" recolours the whole kit (borrowed kits), "variants" are the recolour tints for repeats inside a unit.
def kit(**pools):
    return pools


RACE_KITS = {
    "voidborn": dict(
        variants=[[0.4, 0.85, 1.0], [0.55, 0.1, 0.6], [0.9, 0.5, 1.0]],
        pools=kit(
            cone=["Shadow_Magic_Cone1", "Shadow_Magic_Cone2", "Shadow_Magic_Cone3", "Shadow_Magic_Cone4", "Dark_Magic_Cone1"],
            self=["Shadow_Magic_Explosion1", "Shadow_Magic_Explosion2", "Shadow_Magic_Attack2", "Shadow_Magic_Attack4", "Shadow_Magic_Circle2", "Dark_Magic_AOE2"],
            target=["Shadow_Magic_Attack1", "Shadow_Magic_Attack3", "Shadow_Magic_Attack5", "Dark_Magic_Top", "Dark_Magic_Top2", "Shadow_Magic_Mass_Projectile1"],
            area=["Shadow_Magic_Area1", "Shadow_Magic_Area3", "Dark_Magic_AOE", "Dark_Magic_Circle", "Shadow_Magic_Circle1"],
            hit=["Shadow_Magic_Hit1", "Shadow_Magic_Hit2", "Shadow_Magic_Hit3", "Dark_Magic_Hit", "Dark_Magic_Splash1", "Dark_Flame_Burst", "Dark_Magic_Hit_Orb"],
            charge=["Shadow_Magic_Dash1", "Shadow_Magic_Dash2", "Dark_Magic_Dash"],
            buff=["Shadow_Magic_Buff1", "Shadow_Magic_Shield2", "Shadow_Magic_Buff2", "Dark_Magic_Shield", "Shadow_Magic_Shield1", "Dark_Magic_Buff", "Shadow_Magic_Aura2"],
            heal=["Dark_Magic_Buff", "Shadow_Magic_Buff2"], healhit=[{"s": "State_VFX_Heal1", "tint": [0.7, 0.3, 1.0]}, "Dark_Magic_Debuff"],
            summon=["Shadow_Magic_Circle2", "Dark_Magic_Circle", "Dark_Magic_Top2"],
            pull=["Shadow_Magic_Ray1", "Shadow_Magic_Line_Attack1", "Shadow_Magic_Line_Attack2", "Shadow_Magic_Line_Attack3", "Dark_Magic_Line1", "Shadow_Magic_Line_Attack4"],
            blink=["Shadow_Magic_Blink1", "Shadow_Magic_Blink2", "Dark_Magic_Blink1", "Dark_Magic_Blink2"],
            deploy=["Shadow_Magic_Orb1", "Shadow_Magic_Orb2", "Dark_Magic_Orb2"],
            projectile=["Shadow_Magic_Projectile1", "Shadow_Magic_Projectile2", "Shadow_Magic_Projectile3", "Dark_Magic_Projectile2"],
            muzzle=["Shadow_Magic_Mass_Projectile1", "Dark_Magic_Blink1"],
        )),
    "fallen_order": dict(  # profane light: the holy set in corrupted violet, with dark-magic edges
        tint=[0.62, 0.18, 0.85], strength=0.85,
        variants=[[0.9, 0.2, 0.3], [0.3, 0.9, 0.5], [0.95, 0.75, 0.2]],
        pools=kit(
            cone=["Light_Magic_Slash1", "Light_Magic_Slash2", "Light_Magic_Sword_Line", "Light_Magic_Sword_Line2", {"s": "Dark_Magic_Slash1", "tint": None}, {"s": "Dark_Magic_Slash2", "tint": None}],
            self=["Light_Magic_Sword_Circle", "Light_Magic_Explosion1", "Light_Magic_Sword_AOE", "Light_Magic_AOE1", {"s": "Dark_Magic_AOE2", "tint": None}],
            target=["Light_Magic_Top", "Light_Magic_Hit3", "Light_Magic_Sword_Line_Area1", "Light_Magic_Sword_Line_Area2", {"s": "Dark_Magic_Top", "tint": None}],
            area=["Light_Magic_Sword_Area", "Light_Magic_Top_Area", {"s": "Dark_Magic_Area_Splash1", "tint": None}, "Light_Magic_Sword_Wall"],
            hit=["Light_Magic_Hit1", "Light_Magic_Hit2", "Light_Magic_Hit3", {"s": "Dark_Magic_Hit", "tint": None}, "Light_Magic_Shield_Splash"],
            charge=["Light_Magic_Dash", {"s": "Dark_Magic_Dash", "tint": None}],
            buff=["Light_Magic_Shield", "Light_Magic_Buff", "Light_Magic_Aura", {"s": "Dark_Magic_Shield", "tint": None}, {"s": "Dark_Magic_Buff", "tint": None}, "Light_Magic_Shield_Splash"],
            heal=["Light_Magic_Heal"], healhit=["Light_Magic_Heal_Hit"],
            summon=["Light_Magic_Circle", {"s": "Dark_Magic_Top2", "tint": None}],
            pull=["Light_Magic_Beam", "Light_Magic_Area_Beam", {"s": "Dark_Magic_Line1", "tint": None}, {"s": "Dark_Magic_Area_Line", "tint": None}],
            blink=["Light_Magic_Blink1", "Light_Magic_Blink2", {"s": "Dark_Magic_Blink2", "tint": None}],
            deploy=["Light_Magic_Orb2", {"s": "Dark_Magic_Orb", "tint": None}],
            projectile=["Light_Magic_Projectile1", "Light_Magic_Projectile2", "Light_Magic_Projectile4", {"s": "Dark_Magic_Projectile1", "tint": None}],
            muzzle=["Light_Magic_Blink1", {"s": "Dark_Magic_Blink1", "tint": None}],
        )),
    "stoneborn": dict(  # the earthquake pack in full; runic (arcane) abilities in glowing cyan
        variants=[[0.3, 0.9, 1.0], [1.0, 0.55, 0.15], [0.7, 0.85, 0.3]],
        pools=kit(
            cone=["Earth_Spells_Cone", "Earth_Spells_Cone2", "Earth_Spells_Cone3", "Earth_Spells_Spike_Cone", "Earth_Spells_Slash", "Earth_Spells_Slash2", "Earth_Spells_Slash3"],
            self=["Earth_Spells_Shockwave", "Earth_Spells_Explosion", "Earth_Spells_Spike1", "Earth_Spells_Spike2", "Earth_Spells_Spike3", "Earth_Spells_Circle", "Earth_Magic_Shockwave"],
            target=["Earth_Spells_Meteorites", "Earth_Spells_Attack1", "Earth_Spells_Attack2", "Earth_Magic_Meteor", "Earth_Magic_Meteors1", "Earth_Spells_Area_Spike_Line2"],
            area=["Earth_Spells_Area2", "Earth_Spells_Area3", "Earth_Spells_Arena", "Earth_Spells_Circle", "Earth_Magic_Circle1"],
            hit=["Earth_Spells_Hit1", "Earth_Spells_Hit2", "Earth_Spells_Hit3", "Earth_Magic_Stone1", "Earth_Magic_Stone2", "Earth_Magic_Stone3", "Earth_Magic_Splash"],
            charge=["Earth_Spells_Spike_Line", "Earth_Spells_Spike_Line2", "Earth_Spells_Spike_Line3", "Earth_Spells_Spike_Line4", "Earth_Magic_Dash"],
            buff=["Earth_Spells_Shield", "Earth_Spells_Attack_Up", "Earth_Spells_Attack_Up2", "Earth_Spells_Buff", "Earth_Magic_Shield", "Earth_Spells_Aura", "Earth_Magic_Buff", "Earth_Spells_Shield_Splash"],
            heal=["Earth_Spells_Buff", "Earth_Spells_Attack_Up"], healhit=[{"s": "State_VFX_Heal1", "tint": [1.0, 0.75, 0.3]}, "Earth_Spells_Hit3"],
            summon=["Earth_Spells_Wall1", "Earth_Spells_Wall3", "Earth_Magic_Stoneflow", "Earth_Spells_Circle"],
            pull=["Earth_Spells_Meteorites_Line", "Earth_Spells_Area_Spike_Line1", "Earth_Magic_Flow", "Earth_Spells_Area_Spike_Line3"],
            blink=["Earth_Magic_Dash", "Earth_Spells_Attack_Up", "Earth_Spells_Spike_Line1_v2"],
            deploy=["Earth_Spells_Orb", "Earth_Magic_Orb", "Earth_Spells_Wall2"],
            projectile=["Earth_Spells_Projectile1", "Earth_Spells_Projectile2", "Earth_Spells_Projectile3", "Earth_Spells_Projectile4", "Earth_Magic_Projectile"],
            muzzle=["Earth_Magic_Muzzle", "Earth_Spells_Attack_Up"],
        )),
    "feral_kin": dict(  # beasts: quakes and stomps from the earthquake pack, wild spirits from the forest kit, red claws
        variants=[[0.95, 0.35, 0.15], [0.5, 0.9, 0.3], [0.9, 0.8, 0.4]],
        pools=kit(
            cone=["Blood_Magic_Slash1", "Blood_Magic_Slash2", "Earth_Spells_Cone", "Air_Magic_Slash3", "Earth_Spells_Slash2", "Earth_Spells_Cone3"],
            self=["Earth_Spells_Shockwave", "Earth_Spells_Spike_Cone", "Earth_Spells_Arena", "Earth_Spells_Explosion", "Earth_Magic_Shockwave", "Earth_Spells_Spike2"],
            target=["Vine_attack2", "Earth_Spells_Attack1", "Blood_Magic_Marker", "Wing_Nature", "Earth_Spells_Meteorites"],
            area=["Earth_Spells_Area2", "Vine_attack1", "Earth_Spells_Circle", "AreaBuff_Applied"],
            hit=["Slash_Med", "Stab_Med", "Earth_Spells_Hit2", "Blood_Magic_Hit", "Earth_Magic_Splash", "Blood_Magic_Explo2"],
            charge=["Earth_Magic_Dash", "Earth_Spells_Spike_Line1_v2", "Blood_Magic_Dash", "Air_Magic_Dash"],
            buff=["Blood_Magic_Buff", "Wood_Hide_Forest", "Aura_Nature", "Earth_Spells_Attack_Up2", "Blood_Magic_Aura", "Earth_Spells_Shield", "Wing_Nature"],
            heal=["HealBeam", "Aura_Nature"], healhit=["Explosion_Small_Nature", "State_VFX_Heal1"],
            summon=["RoundedVine_Forest", "Explosion_Cast_Nature", "TextureParticle_Forest"],
            pull=["Vine_attack1", "Earth_Spells_Spike_Line", "Blood_Magic_Beam1"],
            blink=["Air_Magic_Dash", "Explosion_Cast_Nature"],
            deploy=["RoundedVine_Forest", "Earth_Spells_Orb"],
            projectile=["Ribbon_Nature", "Projectile_Grenade_Nature", "Earth_Spells_Projectile3", "Blood_Magic_Projectile3"],
            muzzle=["Explosion_Cast_Nature", "Air_Magic_Muzzle3"],
        )),
    "blightwood": dict(  # rot and vines: poison set, forest vines, green-tinted earth for roots that burst from the ground
        variants=[[0.9, 0.85, 0.2], [0.35, 0.2, 0.6], [0.2, 0.9, 0.6]],
        pools=kit(
            cone=["Vine_attack1", "Posion_Magic_Slash1", "Posion_Magic_Slash2", "Posion_Magic_SpikeArea", "Posion_Magic_Wave1", "Earth_Spells_Cone2"],
            self=["Posion_Magic_AreaWave", "Posion_Magic_Explosion3", "Posion_Magic_SpikeArea", "Explosion_Nature", {"s": "Earth_Spells_Spike1", "tint": [0.35, 0.8, 0.25]}, "Posion_Magic_Explosion1"],
            target=["Posion_Magic_Target", "Posion_Magic_Rain", "Vine_attack2", "Projectile_Grenade_Big_Nature", {"s": "Earth_Spells_Spike3", "tint": [0.35, 0.8, 0.25]}],
            area=["Posion_Magic_Area1", "Posion_Magic_Area2", "Posion_Magic_Area3", "Posion_Magic_PoisonFlow", "Vine_attack1", "AreaBuff"],
            hit=["Posion_Magic_Hit", "Posion_Magic_SpikeHit", "Posion_Magic_Explosion2", "Explosion_Small_Nature", "Explosion_Grenade_Nature", "Posion_Magic_Explosion1"],
            charge=["Posion_Magic_Dash", "RoundedVine_Forest", {"s": "Earth_Magic_Dash", "tint": [0.35, 0.8, 0.25]}],
            buff=["Vine_Barrior", "Wood_Hide_Forest", "Posion_Magic_Shield", "Posion_Magic_Buff", "Aura_Nature", "Posion_Magic_Aura", "Posion_Magic_Shild_Splash"],
            heal=["HealBeam", "Aura_Nature"], healhit=["Explosion_Small_Nature", "State_VFX_Heal1"],
            summon=["RoundedVine_Forest", "Explosion_Cast_Nature", "Posion_Magic_Spike1"],
            pull=["Vine_attack1", "Posion_Magic_Beam", "Posion_Magic_Spike2"],
            blink=["Posion_Magic_Dash", "Explosion_Cast_Nature", "Wing_Nature"],
            deploy=["Posion_Magic_Orb", "RoundedVine_Forest"],
            projectile=["Projectile_Grenade_Nature", "Posion_Magic_Projectile1", "Posion_Magic_Projectile3", "Posion_Magic_Projectile5", "Ribbon_Nature"],
            muzzle=["Explosion_Cast_Nature", "Posion_Magic_Target"],
        )),
    "drowned_deep": dict(  # tides, ink and drowned minds: water set; ink in navy dark magic; madness in teal air magic
        variants=[[0.1, 0.95, 0.8], [0.6, 0.2, 0.9], [0.9, 0.9, 0.5]],
        pools=kit(
            cone=["Water_Magic_Slash1", "Water_Magic_Slash2", "Water_Magic_Slash3", "Water_Magic_Wave1", "Water_Magic_Splash4"],
            self=["Water_Magic_Shockwave", "Water_Magic_Splash1", "Water_Magic_Splash2", "Water_Magic_Splash3", {"s": "Air_Magic_AOE", "tint": [0.3, 0.9, 0.9]}, "Water_Magic_Wave1"],
            target=["Water_Magic_TargetBubble", "Water_Magic_Waterflow1", "Water_Magic_Waterflow2", {"s": "Dark_Magic_Top", "tint": [0.15, 0.25, 0.6]}, {"s": "Air_Magic_Tornado1", "tint": [0.3, 0.9, 0.9]}],
            area=["Water_Magic_Area1", "Water_Magic_Area2", "Water_Magic_Area3", {"s": "Dark_Magic_AOE", "tint": [0.15, 0.25, 0.6]}, "Water_Magic_Wall"],
            hit=["Water_Magic_Hit", "Water_Magic_Splash1", "Water_Magic_Splash2", {"s": "Dark_Magic_Splash1", "tint": [0.15, 0.25, 0.6]}, {"s": "Air_Magic_Hit2", "tint": [0.3, 0.9, 0.9]}, "Water_Magic_ShieldSplash"],
            charge=["Water_Magic_Dash", "Water_Magic_Waterstep", {"s": "Dark_Magic_Dash", "tint": [0.15, 0.25, 0.6]}],
            buff=["Water_Magic_Shield", "Water_Magic_Buff", "Water_Magic_Aura", {"s": "Air_Magic_Shield", "tint": [0.3, 0.9, 0.9]}, "Water_Magic_ShieldSplash", "Water_Magic_Wall2"],
            heal=["Water_Magic_Buff", "Water_Magic_Waterflow2"], healhit=[{"s": "State_VFX_Heal1", "tint": [0.3, 0.8, 1.0]}, "Water_Magic_TargetBubble"],
            summon=["Water_Magic_Wall", "Water_Magic_Wall2", "Water_Magic_Waterflow1"],
            pull=[{"s": "Dark_Magic_Line1", "tint": [0.15, 0.25, 0.6]}, {"s": "Dark_Magic_Area_Line", "tint": [0.15, 0.25, 0.6]}, "Water_Magic_Waterflow2", {"s": "Air_Magic_Airflow", "tint": [0.3, 0.9, 0.9]}],
            blink=["Water_Magic_Waterstep", "Water_Magic_Dash", {"s": "Dark_Magic_Blink1", "tint": [0.15, 0.25, 0.6]}],
            deploy=["Water_Magic_Orb", "Water_Magic_TargetBubble"],
            projectile=["Water_Magic_Projectile1", "Water_Magic_Projectile2", "Water_Magic_Projectile3", "Water_Magic_Projectile4"],
            muzzle=["Water_Magic_Muzzle", "Water_Magic_Waterstep"],
        )),
    "ironhide": dict(  # the warband: blood magic, flying axes, earth-splitting stomps, real blood on rends
        variants=[[1.0, 0.6, 0.1], [0.6, 0.1, 0.1], [0.9, 0.9, 0.9]],
        pools=kit(
            cone=["Blood_Magic_Slash1", "Blood_Magic_Slash2", "Air_Magic_Slash1", "Air_Magic_Slash3", "Air_Magic_Slash5", "Earth_Spells_Slash3", "Air_Magic_Blades"],
            self=["Blood_Magic_Explo", "Blood_Magic_Explo2", "Earth_Spells_Shockwave", "Blood_Magic_Tornado", "Earth_Spells_Spike2", "Blood_Magic_Spike1"],
            target=["Blood_Magic_Marker", "Blood_Magic_Rain", "Earth_Spells_Meteorites", "Blood_Magic_Spike2", "Blood_Magic_Crystal2"],
            area=["Blood_Magic_Area1", "Blood_Magic_Area2", "Blood_Magic_Area3", "Earth_Spells_Area3"],
            hit=["Slash_Med", "Blood_Magic_Hit", "Stab_Med", "Blood_Magic_Crystal1", "Earth_Spells_Hit1", "Slash_High"],
            charge=["Blood_Magic_Dash", "Earth_Magic_Dash", "Fire_Magic_Dash2", "Earth_Spells_Spike_Line4"],
            buff=["Blood_Magic_Buff", "Blood_Magic_Shield", "Blood_Magic_Aura", "Earth_Spells_Shield", "Blood_Magic_Wall1", "Earth_Spells_Attack_Up2", "Blood_Magic_Shield_Splash"],
            heal=["Blood_Magic_Buff", "Blood_Magic_Marker"], healhit=[{"s": "State_VFX_Heal1", "tint": [1.0, 0.3, 0.2]}, "Blood_Magic_Shield_Splash"],
            summon=["Blood_Magic_Wall2", "Blood_Magic_Marker", "Earth_Spells_Circle"],
            pull=["Blood_Magic_Beam1", "Blood_Magic_Beam2", "Blood_Magic_Spike3", "Earth_Spells_Spike_Line"],
            blink=["Blood_Magic_Dash", "Air_Magic_Dash"],
            deploy=["Blood_Magic_Orb", "Blood_Magic_Crystal3"],
            projectile=["Blood_Magic_Projectile2", "Blood_Magic_Projectile3", "Blood_Magic_Projectile4", "Blood_Magic_Projectile1", "Earth_Spells_Projectile3"],
            muzzle=["Blood_Magic_Muzzle", "Air_Magic_Muzzle3"],
        )),
    "drakkari": dict(  # the brood: fire set; molten stone from the earthquake pack in ember orange
        variants=[[1.0, 0.3, 0.05], [0.2, 0.8, 1.0], [0.85, 0.15, 0.6]],
        pools=kit(
            cone=["Fire_Magic_Flamethrower", "Fire_Magic_FrontSplash", "Fire_Magic_Slash1", "Fire_Magic_Slash2", "Fire_Magic_Flame1", {"s": "Earth_Spells_Cone3", "tint": [1.0, 0.45, 0.1]}],
            self=["Fire_Magic_Shockwave", "Fire_Magic_Explosion", "Fire_Magic_Spike1", "Fire_Magic_Spike2", {"s": "Earth_Spells_Spike1", "tint": [1.0, 0.45, 0.1]}, "Fire_Magic_Circle"],
            target=["Fire_Magic_Target", "P_ky_fireStorm", "Fire_Magic_Wall", {"s": "Earth_Spells_Meteorites", "tint": [1.0, 0.45, 0.1]}, "Fire_Magic_Orb", "P_ky_explosion"],
            area=["Fire_Magic_AOE", "Fire_Magic_Arena", "Fire_Magic_Circle", "Fire_Magic_Flame3", {"s": "Earth_Spells_Area2", "tint": [1.0, 0.45, 0.1]}],
            hit=["Fire_Magic_Hit", "Fire_Magic_Splash", "Fire_Magic_SpearSplash", "Fire_Magic_Explosion", {"s": "Earth_Spells_Hit2", "tint": [1.0, 0.45, 0.1]}, "Dark_Flame_Burst"],
            charge=["Fire_Magic_Dash", "Fire_Magic_Dash2", {"s": "Earth_Magic_Dash", "tint": [1.0, 0.45, 0.1]}],
            buff=["Fire_Magic_Sheild", "Fire_Magic_Buff", "Fire_Magic_Aura", "Fire_Magic_Flame2", {"s": "Earth_Spells_Shield", "tint": [1.0, 0.45, 0.1]}, "Fire_Magic_Wall"],
            heal=["Fire_Magic_Buff", "Fire_Magic_Flame2"], healhit=[{"s": "State_VFX_Heal1", "tint": [1.0, 0.55, 0.15]}, "Fire_Magic_Splash"],
            summon=["Fire_Magic_Circle", "Fire_Magic_Wall", "Fire_Magic_Arena"],
            pull=["Fire_Magic_Flamethrower", {"s": "Earth_Spells_Meteorites_Line", "tint": [1.0, 0.45, 0.1]}, "Fire_Magic_Flame1"],
            blink=["Fire_Magic_Dash2", "Fire_Magic_Dash", "Fire_Magic_Muzzle"],
            deploy=["Fire_Magic_Orb", "Fire_Magic_Circle"],
            projectile=["Fire_Magic_Projectile", "Fire_Magic_Projectile2", "Fire_Magic_Projectile3", "Fire_Magic_Projectile5", "P_ky_fireBall"],
            muzzle=["Fire_Magic_Muzzle", "Fire_Magic_Flame2"],
        )),
    "hollow": dict(  # the undead legion: shadow magic in grave-green, rusted steel and rubble
        tint=[0.4, 0.9, 0.4], strength=0.75,
        variants=[[0.8, 0.6, 0.2], [0.55, 0.25, 0.85], [0.3, 0.7, 0.9]],
        pools=kit(
            cone=["Shadow_Magic_Slash1", "Shadow_Magic_Slash3", "Shadow_Magic_Cone2", {"s": "Air_Magic_Slash1", "tint": None}, "Shadow_Magic_Cone4", {"s": "Earth_Spells_Slash", "tint": None}],
            self=["Shadow_Magic_Explosion2", {"s": "Earth_Spells_Shockwave", "tint": None}, "Shadow_Magic_Circle2", {"s": "Earth_Spells_Spike3", "tint": None}, "Shadow_Magic_Attack4"],
            target=["Shadow_Magic_Attack3", {"s": "Earth_Spells_Meteorites", "tint": None}, "Shadow_Magic_Attack5", "Dark_Magic_Target", {"s": "Earth_Magic_Meteor", "tint": None}],
            area=["Shadow_Magic_Area2", "Shadow_Magic_Area4", "Dark_Magic_Circle", "Shadow_Magic_Area3"],
            hit=["Shadow_Magic_Hit2", {"s": "Slash_Low", "tint": None}, "Shadow_Magic_Hit3", {"s": "Earth_Spells_Hit1", "tint": None}, "Dark_Magic_Hit", {"s": "Stab_Low", "tint": None}],
            charge=["Shadow_Magic_Dash2", {"s": "Earth_Magic_Dash", "tint": None}, "Dark_Magic_Dash"],
            buff=["Shadow_Magic_Shield2", "Shadow_Magic_Buff1", "Dark_Magic_Shield", {"s": "Earth_Spells_Shield", "tint": None}, "Shadow_Magic_Aura1", "Shadow_Magic_Shield_Splash2"],
            heal=["Shadow_Magic_Buff2"], healhit=[{"s": "State_VFX_Heal1", "tint": [0.4, 0.9, 0.4]}],
            summon=["Shadow_Magic_Circle1", "Dark_Magic_Top2"],
            pull=["Shadow_Magic_Line_Attack5", "Shadow_Magic_Ray1", "Shadow_Magic_Line_Attack3", "Dark_Magic_Line_Splash1"],
            blink=["Shadow_Magic_Blink2", "Dark_Magic_Blink2"],
            deploy=["Shadow_Magic_Orb2", "Dark_Magic_Orb"],
            projectile=["Shadow_Magic_Projectile3", "Dark_Magic_Projectile1", {"s": "Air_Magic_Arrow3", "tint": None}, "Shadow_Magic_Projectile2"],
            muzzle=["Shadow_Magic_Mass_Projectile1", {"s": "Air_Magic_Muzzle3", "tint": None}],
        )),
    "aetheri": dict(  # the remnant: lightning and air magic in aether cyan, lasers and shockwaves from the variety pack
        tint=[0.25, 0.85, 1.0], strength=0.6,
        variants=[[0.95, 0.3, 0.9], [1.0, 0.8, 0.2], [0.5, 1.0, 0.5]],
        pools=kit(
            cone=["Lightning_Magic_Cone1", "Lightning_Magic_Cone_Lightning1", "Lightning_Magic_Beam", "P_ky_laser01", "Lightning_Magic_Slash1", "Air_Magic_Slash4"],
            self=["Lightning_Magic_Shockwave", "Lightning_Magic_Tunder", "Lightning_Magic_Tunder2", "P_ky_shotShockwave", "Air_Magic_AOE", "Lightning_Magic_Area"],
            target=["Lightning_Magic_Target", "Lightning_Magic_Lightning1", "Lightning_Magic_Lightning2", "P_ky_thunderBall", "Air_Magic_Tornado2", "Lightning_Magic_Tunder_Circle1"],
            area=["Lightning_Magic_Tunder_Area", "Lightning_Magic_Area", "Lightning_Magic_Tornado_Area", "Air_Magic_AOE", "P_ky_magicCircle1"],
            hit=["Lightning_Magic_Shield_Splash", "P_ky_ThunderBallHit", "Air_Magic_Hit4", "Lightning_Magic_Slash2", "Air_Magic_Hit1", "P_ky_hit1", "P_ky_hit2"],
            charge=["Lightning_Magic_Dash", "Air_Magic_Dash", "Lightning_Magic_Line2"],
            buff=["Lightning_Magic_Shield", "Air_Magic_Shield", "Lightning_Magic_Buff1", "Lightning_Magic_Buff2", "Air_Magic_Buff", "Lightning_Magic_Slash_Aura", "Air_Magic_Air_Wall"],
            heal=["Air_Magic_Buff", "Lightning_Magic_Buff2"], healhit=["State_VFX_Mana1", "State_VFX_Shock1"],
            summon=["Lightning_Magic_Orb", "Lightning_Magic_Orb2", "Lightning_Magic_Orb3", "P_ky_magicCircle1"],
            pull=["Lightning_Magic_Laser", "Lightning_Magic_Tunder_Line", "Lightning_Magic_Tornado_Line", "Air_Magic_Airflow"],
            blink=["Lightning_Magic_Blink1", "Lightning_Magic_Blink2", "Air_Magic_Muzzle4"],
            deploy=["Lightning_Magic_Orb3", "Lightning_Magic_Orb2", "P_ky_magicCircle1", "Air_Magic_Orb"],
            projectile=["Lightning_Magic_Projectile1", "Lightning_Magic_Projectile2", "Lightning_Magic_Projectile3", "Air_Magic_Projectile1", "Air_Magic_Projectile2"],
            muzzle=["Lightning_Magic_Blink1", "Air_Magic_Muzzle2", "Air_Magic_Muzzle4"],
        )),
    # Legacy archetypes and Bestiary creatures (no race): steel and earth, ice for the lich / frostfang, air for griffons.
    "_legacy": dict(
        variants=[[0.9, 0.7, 0.3], [0.5, 0.8, 1.0]],
        pools=kit(
            cone=["Air_Magic_Slash2", "Earth_Spells_Cone2", "Air_Magic_Slash5", "Ice_Magic_Slash1", "Ice_Magic_Slash2"],
            self=["Earth_Spells_Shockwave", "Ice_Magic_Shockwave", "Earth_Magic_Shockwave", "Ice_Magic_Circle4"],
            target=["Earth_Spells_Meteorites", "Ice_Magic_IceSpike", "Ice_Magic_Target", "Air_Magic_Tornado3", "Posion_Magic_Rain"],
            area=["Earth_Spells_Area3", "Ice_Magic_Arena", "Posion_Magic_Area1", "Ice_Magic_Snowstorm1"],
            hit=["Slash_Low", "Stab_Low", "Ice_Magic_Hit", "Earth_Spells_Hit1", "Blood_Magic_Hit", "Ice_Magic_FrontSpike"],
            charge=["Earth_Magic_Dash", "Ice_Magic_Dash", "Air_Magic_Dash"],
            buff=["Earth_Spells_Shield", "Ice_Magic_Sheild", "Ice_Magic_Sheild2", "Blood_Magic_Buff", "Earth_Magic_Buff", "Ice_Magic_Wall"],
            heal=["Light_Magic_Heal", "Ice_Magic_Buff"], healhit=["Light_Magic_Heal_Hit", "State_VFX_Heal1"],
            summon=["Ice_Magic_Circle1", "Earth_Spells_Circle", "Dark_Magic_Circle"],
            pull=["Earth_Spells_Spike_Line", "Ice_Magic_IceSpike2", "Air_Magic_Airflow"],
            blink=["Air_Magic_Dash", "Ice_Magic_Dash", "Shadow_Magic_Blink1"],
            deploy=["Earth_Spells_Orb", "Ice_Magic_Orb"],
            projectile=["Air_Magic_Arrow1", "Air_Magic_Arrow4", "Ice_Magic_Projectile", "Shadow_Magic_Projectile1", "Earth_Spells_Projectile2"],
            muzzle=["Air_Magic_Muzzle1", "Ice_Magic_Muzzle", "Earth_Magic_Muzzle"],
        )),
}
# Keyword kits for legacy / Bestiary ids (first match wins), else "_legacy".
# pack-usage-3: the hit / target / buff pools that wrapped (the same system recoloured 3-4 times inside a race) take the
# still-unused A/B systems of their theme first (quality first; a recolour only after every good system of the theme is
# drawn). Shadow_Magic_Hit2 (a small teal ribbon swirl) leaves the Hollow kit, where the grave-green tint made it the
# "greenish-blue swirl on every hit" of playtest 6, and goes last in the Voidborn pool, where it suits.
_G = [0.35, 0.8, 0.25]
POOL_ADDITIONS = {
    "aetheri": {"hit": ["Lightning_Magic_Blink2", "P_ky_lightning2", "Lightning_Magic_Projectile2"], "deploy": ["Lightning_Magic_Buff2"],
                "target": ["P_ky_thunderStorm", "P_ky_lightning1"]},
    "blightwood": {"hit": ["Posion_Magic_Explosion3", "Posion_Magic_Shild_Splash", "State_VFX_Poison1"],
                   "target": ["Posion_Magic_SpikeArea", {"s": "Earth_Spells_Area_Spike_Line3", "tint": _G}]},
    "drakkari": {"hit": ["Fire_Magic_Spike2"], "buff": ["Fire_Magic_Flame3"], "target": ["Fire_Magic_Arena"]},
    "drowned_deep": {"hit": ["P_ky_waterBallHit"], "target": ["P_ky_aquaStorm", "Water_Magic_Splash2"]},
    "fallen_order": {"hit": ["Light_Magic_Explosion1", "Dark_Magic_Shield_Splash"], "charge": ["Light_Magic_Blink2"], "target": ["Dark_Magic_Wall2"]},
    "feral_kin": {"hit": ["Earth_Magic_Hit", "Blood_Magic_Crystal5"], "charge": ["Blood_Magic_Slash1"], "buff": ["Blood_Magic_Shield"]},
    "hollow": {"hit": ["Earth_Spells_Hit3"], "buff": ["Shadow_Magic_Wall2"], "target": ["Shadow_Magic_Area4"]},
    "ironhide": {"hit": ["Blood_Magic_Explo2", "BloodSplash_High", "Blood_Magic_Crystal5"]},
    "stoneborn": {"hit": ["Earth_Magic_Hit", "Earth_Magic_Spike6"], "target": ["Earth_Spells_Area_Spike_Line3", "Earth_Spells_Wall4"]},
    "voidborn": {"hit": ["Shadow_Magic_Ray1", "Dark_Magic_Dark_Flame"], "area": ["Shadow_Magic_Wall1", "Dark_Magic_Wall2", "P_ky_darkStorm"]},
    "_legacy": {"target": ["Ice_Magic_IceSpike2"]},
}
for _race, _add in POOL_ADDITIONS.items():
    for _pool, _stems in _add.items():
        _p = RACE_KITS[_race]["pools"].setdefault(_pool, [])
        _p.extend(x for x in _stems if x not in _p)
# ...and the unused A/B systems with one obvious owner go straight to it (MONSTER_OVERRIDES is defined below; merged there).
PACK_USAGE_3_OVERRIDES = {
    "frostfang_hamstring": {"c": "Ice_Magic_Slash1", "i": "Ice_Magic_IceSpike"}, "frostfang_howl": {"c": "Ice_Magic_Snowstorm1"},
    "lich_frost_nova": {"c": "Ice_Magic_Shockwave", "i": "Ice_Magic_IceSpike2"},
    "ironhide_chain_hook": {"c": "Blood_Magic_Beam2"}, "fallen_consecrated_wall": {"c": "Dark_Magic_Wall1"},
    "aether_psi_sweep": {"c": "Lightning_Magic_Slash1"}, "aether_psionic_storm": {"c": "P_ky_thunderStorm"},
    "aether_orbital_strike": {"i": "P_ky_lightning1"}, "aether_core_burst": {"i": "P_ky_lightning3"},
    "void_event_horizon": {"c": "Shadow_Magic_Shield_Splash1"}, "void_gravity_well": {"c": "P_ky_darkStorm"},
    "void_titan_slam": {"c": "Shadow_Magic_Wall1"}, "stoneborn_forge_sentinels": {"c": "Earth_Spells_Wall4"},
}
_hollow_hit = RACE_KITS["hollow"]["pools"]["hit"]
_hollow_hit[:] = [x for x in _hollow_hit if x != "Shadow_Magic_Hit2"]
_void_hit = RACE_KITS["voidborn"]["pools"]["hit"]
_void_hit[:] = [x for x in _void_hit if x != "Shadow_Magic_Hit2"] + ["Shadow_Magic_Hit2"]


LEGACY_KIT_BY_KEYWORD = [("lich", "_legacy"), ("frostfang", "_legacy"), ("shambler", "blightwood"), ("griffon", "aetheri"), ("centaur", "feral_kin"),
                         ("brute", "ironhide"), ("drake", "drakkari"), ("bone_volley", "hollow"), ("npc_shadow_bolt", "voidborn"), ("npc_blight_pool", "blightwood"),
                         ("boss_leader", "feral_kin"), ("boss_siege", "ironhide")]

# Ability type -> (role, pool) per role letter. "area" only when the zone lasts (duration / persistent).
TYPE_ROLES = {
    "cone": {"c": "cone", "i": "hit"},
    "selfCircle": {"c": "self", "i": "hit", "a": "area"},
    "targetCircle": {"c": "target", "i": "hit", "a": "area"},
    "charge": {"c": "charge", "i": "hit"},
    "enrage": {"c": "buff"}, "rally": {"c": "buff"}, "provoke": {"c": "buff"}, "guard": {"c": "buff"}, "shieldWall": {"c": "buff"},
    "healAlly": {"c": "heal", "i": "healhit"},
    "summon": {"c": "summon"},
    "pull": {"c": "pull", "i": "hit"},
    "disengage": {"c": "blink"},
    "deploy": {"c": "deploy"},
    "projectile": {"c": "muzzle", "p": "projectile", "i": "hit"},
}
ROLE_SCALE = {"c": 1.0, "p": 1.0, "i": 1.25, "a": 1.0}
# Recolour variants tried after a race's own palette when a system is already taken by a race-mate's ability.
EXTRA_VARIANTS = [[1.0, 0.85, 0.2], [0.2, 0.9, 0.5], [0.3, 0.6, 1.0], [1.0, 0.4, 0.7], [0.95, 0.95, 0.9], [1.0, 0.5, 0.1]]
# Hand picks that beat the generator (ability id -> {role letter: value}); Eric's named cases first.
MONSTER_OVERRIDES = {
    "feral_earthquake": {"c": "Earth_Spells_Arena", "a": "Earth_Spells_Area2", "i": "Earth_Spells_Hit2"},
    "feral_earthshaker": {"c": "Earth_Spells_Shockwave", "a": "Earth_Spells_Circle", "i": "Earth_Spells_Spike2"},
    "stoneborn_quake": {"c": "Earth_Spells_Explosion", "a": "Earth_Spells_Arena", "i": "Earth_Spells_Hit1"},
    "stoneborn_tremor": {"c": "Earth_Spells_Spike_Cone", "a": "Earth_Spells_Area3", "i": "Earth_Spells_Hit3"},
    "stoneborn_ground_pound": {"c": "Earth_Spells_Shockwave", "i": "Earth_Magic_Stone2"},
    "stoneborn_petrify": {"c": "Earth_Spells_Attack2", "i": {"s": "Earth_Spells_Spike1", "tint": [0.45, 0.85, 0.2]}},
    "stoneborn_stoneskin": {"c": "Earth_Spells_Shield"}, "stoneborn_shield_matrix": {"c": {"s": "Earth_Spells_Shield_Splash", "tint": [0.3, 0.9, 1.0]}},
    "ironhide_earthsplitter": {"c": "Earth_Spells_Spike_Cone", "i": "Earth_Spells_Hit2"},
    "ironhide_boulder_hurl": {"c": "Earth_Spells_Meteorites", "i": "Earth_Magic_Stone3"},
    "hollow_rubble_toss": {"c": {"s": "Earth_Spells_Meteorites", "tint": None}, "i": {"s": "Earth_Magic_Stone1", "tint": None}},
    "blight_root_quake": {"c": {"s": "Earth_Spells_Spike2", "tint": [0.35, 0.8, 0.25]}, "a": "Vine_attack1"},
    "blight_root_eruption": {"c": {"s": "Earth_Spells_Spike3", "tint": [0.35, 0.8, 0.25]}, "a": "Vine_attack2"},
    "void_rift": {"c": "Shadow_Magic_Attack1", "a": "Shadow_Magic_Area1", "i": "Shadow_Magic_Hit1"},
    "void_singularity": {"c": "Shadow_Magic_Circle1", "a": "Dark_Magic_AOE", "i": "Shadow_Magic_Explosion1"},
    "drowned_whirlpool": {"c": "Water_Magic_Waterflow1", "a": "Water_Magic_Area2", "i": "Water_Magic_Splash3"},
    "void_open_the_rift": {"c": "Shadow_Magic_Circle2"}, "void_event_horizon": {"c": "Shadow_Magic_Shield2"},
    "lich_frost_nova": {"c": "Ice_Magic_Shockwave", "i": "Ice_Magic_FrontSpike"}, "lich_soul_rend": {"c": "Shadow_Magic_Attack1", "i": "Shadow_Magic_Hit1"},
    "lich_mend": {"c": "Ice_Magic_Buff", "i": {"s": "State_VFX_Heal1", "tint": [0.5, 0.8, 1.0]}},
    "npc_shadow_bolt": {"c": "Shadow_Magic_Mass_Projectile1", "p": "Shadow_Magic_Projectile1", "i": "Shadow_Magic_Hit1"},
    "npc_bolt": {"p": "Shadow_Magic_Projectile2", "i": "Shadow_Magic_Hit2"},
    "npc_barbed_shot": {"p": "Air_Magic_Arrow3", "i": "Stab_Low"}, "npc_shot": {"p": "Air_Magic_Arrow4", "i": "Stab_Low"},
}

# Hit signatures (steel-school impacts with no ability of their own: basic attacks, monster melee, weapon strikes) by
# target body layer and attacker weapon: hit.<layer>[.<weapon>][.crit]. Restrained: Low on hits, Med/High only on crits.
for _ab, _roles in PACK_USAGE_3_OVERRIDES.items():
    MONSTER_OVERRIDES.setdefault(_ab, {}).update(_roles)

SPARK, SPARK_HOT, SPARK_STEEL = [1.0, 0.82, 0.45], [1.0, 0.6, 0.2], [0.85, 0.85, 0.7]
HIT_VFX = {
    "hit.flesh": {"i": ("Slash_Low", 0.9)}, "hit.flesh.crit": {"i": ("Slash_High", 1.0)},
    "hit.flesh.sword": {"i": ("Slash_Low", 0.9)}, "hit.flesh.sword.crit": {"i": ("Slash_High", 1.0)},
    "hit.flesh.axe": {"i": ("Slash_Med", 0.9)}, "hit.flesh.axe.crit": {"i": ("Slash_High", 1.05)},
    "hit.flesh.gunblade": {"i": ("Slash_Low", 0.85)}, "hit.flesh.gunblade.crit": {"i": ("Slash_High", 1.0)},
    "hit.flesh.glaive": {"i": ("Slash_Low", 0.9)}, "hit.flesh.glaive.crit": {"i": ("Slash_Med", 1.0)},
    "hit.flesh.dagger": {"i": ("Stab_Low", 0.9)}, "hit.flesh.dagger.crit": {"i": ("Stab_High", 1.0)},
    "hit.flesh.spear": {"i": ("Stab_Low", 0.95)}, "hit.flesh.spear.crit": {"i": ("Stab_High", 1.05)},
    "hit.flesh.bow": {"i": ("Stab_Low", 0.85)}, "hit.flesh.bow.crit": {"i": ("Stab_Med", 1.0)},
    "hit.flesh.crossbow": {"i": ("Stab_Med", 0.9)}, "hit.flesh.crossbow.crit": {"i": ("Stab_High", 1.0)},
    "hit.flesh.claws": {"i": ("Slash_Low", 0.85)}, "hit.flesh.claws.crit": {"i": ("Slash_Med", 1.0)},
    "hit.flesh.mace": {"i": ("BloodSplash_Low", 0.9)}, "hit.flesh.mace.crit": {"i": ("BloodSplash_Med", 1.0)},
    "hit.flesh.shield": {"i": ("BloodSplash_Low", 0.85)}, "hit.flesh.shield.crit": {"i": ("BloodSplash_Med", 1.0)},
    "hit.flesh.staff": {"i": ("BloodSplash_Low", 0.8)}, "hit.flesh.staff.crit": {"i": ("BloodSplash_Med", 0.95)},
    "hit.flesh.pistol": {"i": ("BulletHit_Sample", 0.9)}, "hit.flesh.pistol.crit": {"i": ("BulletHit_High", 1.0)},
    "hit.flesh.blunderbuss": {"i": ("BulletHit_Med", 0.95)}, "hit.flesh.blunderbuss.crit": {"i": ("BloodBurst_High", 0.8)},
    # armour: warm steel sparks, a different spark per weapon family (pack-usage-3: the green Air_Magic_Hit3 crescent swirl
    # used to fire on every plate / mail target - playtest 6 "greenish-blue swirl on too many hits"); stone / bark: debris;
    # nothing (whisps): the pale wind swirl stays here, where a spirit puff suits it.
    "hit.armor": {"i": {"s": "Light_Magic_Hit3", "scale": 0.5, "tint": SPARK}},
    "hit.armor.crit": {"i": {"s": "Lightning_Magic_Blink2", "scale": 0.5, "tint": SPARK}},
    **{"hit.armor." + w: {"i": {"s": "Light_Magic_Hit1", "scale": 0.5, "tint": SPARK}} for w in ("sword", "gunblade", "glaive")},
    **{"hit.armor." + w: {"i": {"s": "Lightning_Magic_Projectile2", "scale": 0.45, "tint": SPARK_HOT}} for w in ("axe", "claws")},
    **{"hit.armor." + w: {"i": {"s": "State_VFX_Shock1", "scale": 0.55, "tint": SPARK}} for w in ("mace", "shield", "staff")},
    **{"hit.armor." + w: {"i": {"s": "Ice_Magic_Hit", "scale": 0.5, "tint": SPARK_STEEL, "strength": 0.8}} for w in ("dagger", "spear", "bow", "crossbow")},
    **{"hit.armor." + w: {"i": {"s": "Light_Magic_Hit2", "scale": 0.45, "tint": SPARK_HOT}} for w in ("pistol", "blunderbuss")},
    **{"hit.armor.%s.crit" % w: {"i": {"s": "Fire_Magic_Explosion", "scale": 0.35, "tint": SPARK}} for w in ("mace", "shield", "staff", "axe")},
    "hit.stone": {"i": ("Earth_Spells_Hit1", 0.6)}, "hit.stone.crit": {"i": ("Earth_Magic_Stone1", 0.75)},
    "hit.wood": {"i": ("Explosion_Small_Nature", 0.55)}, "hit.wood.crit": {"i": {"s": "State_VFX_Root1", "scale": 0.6}},
    "hit.none": {"i": ("Air_Magic_Splash", 0.5)}, "hit.none.crit": {"i": ("Air_Magic_Hit2", 0.6)},
}
# Kill bursts (UCireMonsterArt::MulticastDeath): one system per death by body class; decals only on bosses.
KILL_VFX = {
    "kill.humanoid": {"i": ("BloodBurst_Med", 0.8)}, "kill.creature": {"i": ("BloodSplash_Med", 0.85)},
    "kill.golem": {"i": ("Earth_Spells_Explosion", 0.55)}, "kill.ethereal": {"i": ("Shadow_Magic_Explosion2", 0.6)},
    "kill.boss": {"i": ("BloodBurst_Extreme", 0.9)}, "kill": {"i": ("BloodBurst_Low", 0.8)},
}
EVENT_VFX = {"level_up": {"c": ("Light_Magic_Top", 0.9)}}


def load_monster_abilities():
    """[(ability id, kit key, type, buff, has_duration)] for every monster ability (races, legacy archetypes, bestiary)."""
    rows, seen = [], set()

    def add(ab, kit_key, unit_id):
        if not isinstance(ab, dict) or "id" not in ab or "type" not in ab or ab["id"] in seen or ab.get("basic"):
            return
        seen.add(ab["id"])
        lasting = bool(ab.get("duration")) or bool(ab.get("damagePerSecond")) or bool(ab.get("persistent"))
        rows.append((ab["id"], kit_key, ab["type"], ab.get("buff", ""), lasting, unit_id))

    races = json.loads((DATA / "Races.json").read_text("utf-8"))["races"]
    npc = json.loads((DATA / "NPCArchetypes.json").read_text("utf-8"))
    for race, r in races.items():
        for unit_id, unit in r.get("units", {}).items():
            for ab in unit.get("archetype", {}).get("abilities", []) + unit.get("abilities", []):
                add(ab, race, unit_id)
            # 'extends' units (the Hollow Legion) keep their NPCArchetypes.json definition and add pool abilities there.
            for ab in npc.get("archetypes", {}).get(unit_id, {}).get("abilities", []):
                add(ab, race, unit_id)
    for arch_id, arch in npc.get("archetypes", {}).items():
        for ab in arch.get("abilities", []):
            add(ab, next((k for kw, k in LEGACY_KIT_BY_KEYWORD if kw in ab["id"]), "_legacy"), arch_id)
    best = json.loads((DATA / "Bestiary.json").read_text("utf-8"))
    for unit_id, unit in best.get("units", {}).items():
        for ab in unit.get("archetype", {}).get("abilities", []):
            add(ab, next((k for kw, k in LEGACY_KIT_BY_KEYWORD if kw in ab["id"]), "_legacy"), unit_id)
    return rows


def as_dict(value):
    if isinstance(value, dict):
        return dict(value)
    if isinstance(value, tuple):
        return {"s": value[0], "scale": value[1]}
    return {"s": value}


def build_monster_table():
    """ability id -> {role letter: {"s":, "scale":, "tint":, "strength":}} with per-unit de-duplication."""
    table = OrderedDict()
    used = {}  # (kit, pool) -> count of picks, cycles the pool so units of one race differ where the pool allows
    unit_looks = {}  # unit -> {(stem, tint)} of cast looks already taken (no two abilities of one unit look alike)
    for ability, kit_key, kind, buff, lasting, unit_id in load_monster_abilities():
        kit_def = RACE_KITS.get(kit_key) or RACE_KITS["_legacy"]
        pools, roles = kit_def["pools"], TYPE_ROLES.get(kind, {"c": "self"})
        row = OrderedDict()
        for letter, pool_name in roles.items():
            if letter == "a" and not lasting:
                continue
            pool = pools.get(pool_name) or pools["self"]
            override = MONSTER_OVERRIDES.get(ability, {}).get(letter)
            if override is not None:
                pick = demote(as_dict(override))
            else:
                n = used.get((kit_key, pool_name), 0)
                used[(kit_key, pool_name)] = n + 1
                pick = demote(as_dict(pool[n % len(pool)]))
                lap = n // len(pool)
                if lap > 0 and pick.get("tint", "unset") in ("unset", None):
                    variants = kit_def.get("variants") or [[1, 1, 1]]
                    pick["tint"] = variants[(lap - 1) % len(variants)]
            # Kit-wide tint (borrowed kit) unless the pick says "tint": None (a system already in the race's colour).
            if "tint" not in pick and kit_def.get("tint"):
                pick["tint"], pick["strength"] = kit_def["tint"], kit_def.get("strength", 1.0)
            # Effect-type colour: what hits the victim is coloured by the CC it applies.
            if letter in ("i", "a") and buff in CC_TINT:
                pick["tint"], pick["strength"] = CC_TINT[buff], 0.9
            if pick.get("tint") is None:
                pick.pop("tint", None)
                pick.pop("strength", None)
            pick.setdefault("scale", ROLE_SCALE[letter])
            if letter == "c":
                # Same system already used by another ability of this race: recolour it (next variant) until it differs.
                # Per race, not per unit: pool skills and borrowed pack kits move abilities between a race's units at runtime.
                looks = unit_looks.setdefault(kit_key, set())
                variants = (kit_def.get("variants") or []) + EXTRA_VARIANTS
                for step in range(len(variants) + 1):
                    look = (pick["s"], tuple(pick.get("tint") or ()))
                    if look not in looks:
                        break
                    pick["tint"] = variants[step % len(variants)]
                    pick.pop("strength", None)
                looks.add((pick["s"], tuple(pick.get("tint") or ())))
            row[letter] = pick
        table[ability] = row
    return table


if __name__ == "__main__":
    t = build_monster_table()
    print(len(t), "monster abilities")
    for k, v in list(t.items())[:12]:
        print(k, v)
