"""kits-complete: a distinct Fab Niagara overlay per roster-kit skill (Content/Data/FabVFX.json "abilities").

The school/role table in FabVFX.json gives every ability of a school the same systems. The 63 signature skills
of the thirteen roster champions (Tools/ChampionKits.py) each get their own systems here instead, chosen by
theme from the purchased packs (Big Pack Magic, Shadow Magic, Earth Spells, Forest VFX, State VFX, Realistic
Blood), plus a distinct aura for every buff record they apply ("buffs", locked). Paths point into the locally
installed, never-committed packs; CireFabVFX::FindAbility resolves them at runtime and falls back to the school
table (and then to the procedural presentation) when a pack is missing.

Roles: cast (caster flare / channel), impact (hit or heal landing), area (ground zone / cone / line), projectile.

    python Tools/MapKitVFX.py            # merge into Content/Data/FabVFX.json
    python Tools/MapKitVFX.py --check    # verify every system name resolves in the installed packs
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "Content" / "Data" / "FabVFX.json"
PACKS = ["Big_Pack_Magic_VFX", "Shadow_Magic", "Earth_Spells", "Forest_VFX", "State_VFX", "RealisticBlood"]

# id: {role: Niagara system name}
KIT_VFX = {
    "bear_maul": {"impact": "NS_Slash_Med", "cast": "NS_Earth_Magic_Slash1"},
    "bear_roar": {"area": "NS_Earth_Spells_Cone", "cast": "NS_Air_Magic_Muzzle3", "impact": "NS_Air_Magic_Hit3"},
    "bear_charge": {"cast": "NS_Earth_Magic_Dash", "impact": "NS_Earth_Spells_Hit2"},
    "bear_hibernate": {"cast": "NS_Aura_Nature", "impact": "NS_State_VFX_Heal1"},
    "bear_ancient_hide": {"impact": "NS_Wood_Hide_Forest"},
    "bear_colossus": {"cast": "NS_Earth_Spells_Attack_Up2", "area": "NS_Earth_Spells_Shockwave", "impact": "NS_Earth_Spells_Hit3"},
    "paladin_righteous_flail": {"area": "NS_Light_Magic_Sword_Area", "impact": "NS_Light_Magic_Hit2", "cast": "NS_Light_Magic_Slash1"},
    "paladin_relic_vow": {"cast": "NS_Light_Magic_Top", "impact": "NS_Light_Magic_Shield_Splash"},
    "paladin_holy_flail": {"impact": "NS_Light_Magic_Explosion1", "cast": "NS_Light_Magic_Slash2"},
    "paladin_pilgrim_light": {"cast": "NS_Light_Magic_Orb2", "impact": "NS_Light_Magic_Heal_Hit"},
    "miner_pickfall": {"impact": "NS_Earth_Magic_Hit", "cast": "NS_Earth_Magic_Slash2"},
    "miner_faultline": {"area": "NS_Earth_Spells_Spike_Line2", "impact": "NS_Earth_Magic_Splash", "cast": "NS_Earth_Magic_Muzzle"},
    "miner_lantern": {"cast": "NS_Fire_Magic_Orb", "area": "NS_Fire_Magic_Circle"},
    "miner_orehide": {"impact": "NS_Earth_Magic_Shield"},
    "miner_mountain": {"area": "NS_Earth_Spells_Arena", "impact": "NS_Earth_Spells_Explosion", "cast": "NS_Earth_Magic_Stone1"},
    "golem_granite_fist": {"area": "NS_Earth_Spells_Cone2", "impact": "NS_Earth_Spells_Hit1", "cast": "NS_Earth_Spells_Attack1"},
    "golem_ether_anchor": {"area": "NS_Dark_Magic_Circle", "impact": "NS_Dark_Magic_Hit", "cast": "NS_Dark_Magic_Top"},
    "golem_construct_core": {"impact": "NS_Air_Magic_Target"},
    "golem_worldstone": {"cast": "NS_Earth_Spells_Attack_Up", "area": "NS_Earth_Magic_Shockwave", "impact": "NS_Earth_Magic_Stone2"},
    "golem_moss_bloom": {"area": "NS_AreaBuff", "cast": "NS_Explosion_Cast_Nature", "impact": "NS_AreaBuff_Applied"},
    "golem_living_granite": {"cast": "NS_Vine_Barrior", "impact": "NS_Earth_Spells_Shield_Splash"},
    "golem_fel_fist": {"area": "NS_Fire_Magic_FrontSplash", "impact": "NS_Fire_Magic_Hit", "cast": "NS_Fire_Magic_Muzzle"},
    "golem_ether_furnace": {"cast": "NS_Fire_Magic_Flame2", "impact": "NS_Fire_Magic_Splash"},
    "chieftain_axe_hook": {"cast": "NS_Air_Magic_Slash3", "impact": "NS_Blood_Magic_Hit"},
    "chieftain_banner": {"cast": "NS_Blood_Magic_Marker", "area": "NS_Blood_Magic_Area3"},
    "chieftain_courage": {"impact": "NS_Blood_Magic_Shield_Splash"},
    "chieftain_earthshout": {"cast": "NS_Blood_Magic_Area2", "area": "NS_Earth_Spells_Circle", "impact": "NS_Blood_Magic_Explo2"},
    "behemoth_totem_sweep": {"area": "NS_Earth_Spells_Cone3", "impact": "NS_Earth_Spells_Slash3", "cast": "NS_Earth_Spells_Slash"},
    "behemoth_tusk_line": {"cast": "NS_Earth_Spells_Area_Spike_Line1", "impact": "NS_Earth_Spells_Spike1"},
    "behemoth_totem_bulwark": {"cast": "NS_Earth_Spells_Wall2", "impact": "NS_Earth_Magic_Earth_Wall1"},
    "behemoth_ancestral_weight": {"impact": "NS_Earth_Spells_Orb"},
    "behemoth_stampede": {"area": "NS_Earth_Spells_Meteorites_Line", "impact": "NS_Earth_Spells_Spike3", "cast": "NS_Earth_Magic_Flow"},
    "drakish_dragon_oath": {"cast": "NS_Fire_Magic_Flame1", "impact": "NS_Fire_Magic_Slash1"},
    "drakish_scale_guard": {"cast": "NS_Fire_Magic_Flame3", "impact": "NS_Fire_Magic_Spike1"},
    "drakish_wing_rebuke": {"area": "NS_Fire_Magic_Shockwave", "impact": "NS_Fire_Magic_Slash2", "cast": "NS_Fire_Magic_Dash2"},
    "drakish_ember_memory": {"impact": "NS_State_VFX_Burn1"},
    "drakish_ancient_pact": {"area": "NS_Fire_Magic_Arena", "cast": "NS_Fire_Magic_Spike2", "impact": "NS_Fire_Magic_SpearSplash"},
    "troll_axe_frenzy": {"impact": "NS_Blood_Magic_Slash1", "cast": "NS_Blood_Magic_Slash2"},
    "troll_blood_leap": {"area": "NS_Blood_Magic_Area1", "cast": "NS_Blood_Magic_Dash", "impact": "NS_Blood_Magic_Explo"},
    "troll_hunger": {"impact": "NS_Blood_Magic_Debuff"},
    "troll_red_moon": {"cast": "NS_Blood_Magic_Spike1", "impact": "NS_Blood_Magic_Spike2"},
    "troll_twin_throw": {"projectile": "NS_Air_Magic_Wind_Blade", "impact": "NS_Blood_Magic_Crystal1"},
    "troll_returning_axes": {"area": "NS_Air_Magic_Blades", "impact": "NS_Blood_Magic_Crystal2", "cast": "NS_Air_Magic_Arrow1"},
    "dryad_root_snare": {"area": "NS_Vine_attack1", "impact": "NS_State_VFX_Root1", "cast": "NS_Explosion_Small_Nature"},
    "dryad_seed_mend": {"cast": "NS_Projectile_Grenade_Nature", "impact": "NS_Explosion_Nature"},
    "dryad_thorn_line": {"area": "NS_Vine_attack2", "impact": "NS_Posion_Magic_SpikeHit"},
    "dryad_green_covenant": {"impact": "NS_Ribbon_Nature"},
    "dryad_grove_renewal": {"area": "NS_TextureParticle_Forest", "cast": "NS_Wing_Nature", "impact": "NS_Explosion_Grenade_Nature"},
    "whisp_guiding_mote": {"cast": "NS_Air_Magic_Orb", "impact": "NS_Water_Magic_Splash1"},
    "whisp_spirit_tether": {"cast": "NS_HealBeam", "impact": "NS_Water_Magic_Hit"},
    "whisp_fey_trail": {"area": "NS_Water_Magic_Waterflow1", "impact": "NS_Water_Magic_Splash2", "cast": "NS_Water_Magic_Muzzle"},
    "whisp_lantern_soul": {"impact": "NS_Water_Magic_TargetBubble"},
    "whisp_constellation": {"cast": "NS_Lightning_Magic_Aura1", "impact": "NS_Water_Magic_Splash3"},
    "centaur_grove_javelin": {"projectile": "NS_Posion_Magic_Projectile2", "impact": "NS_Posion_Magic_Hit", "area": "NS_Posion_Magic_Area1"},
    "centaur_trailblaze": {"area": "NS_Air_Magic_Airflow", "cast": "NS_Air_Magic_Dash"},
    "centaur_herd_call": {"cast": "NS_Projectile_Grenade_Big_Nature", "impact": "NS_Posion_Magic_Explosion1"},
    "centaur_steady_gait": {"impact": "NS_Air_Magic_Tornado1"},
    "centaur_spring_march": {"cast": "NS_Posion_Magic_AreaWave", "area": "NS_Posion_Magic_Area2", "impact": "NS_Posion_Magic_Explosion2"},
    "keeper_dawn_beam": {"cast": "NS_Light_Magic_Beam", "area": "NS_Light_Magic_Area_Beam", "impact": "NS_Light_Magic_Hit3"},
    "keeper_lantern_ward": {"cast": "NS_Light_Magic_Circle", "impact": "NS_Light_Magic_Hit1"},
    "keeper_beacon": {"area": "NS_Light_Magic_AOE1", "cast": "NS_Light_Magic_Top_Area"},
    "keeper_last_light": {"impact": "NS_Light_Magic_Blink1"},
    "keeper_sunrise": {"cast": "NS_Light_Magic_Aura", "impact": "NS_Light_Magic_Blink2"},
}
# Buff records of the kits (CireKitSkills::BuffIds): the aura each one wears.
KIT_BUFF_VFX = {
    "bear_hibernate": "NS_Aura_Nature", "ancient_hide": "NS_Wood_Hide_Forest", "bear_colossus": "NS_Earth_Spells_Aura",
    "bear_cowed": "NS_State_VFX_Charm1", "relic_mark": "NS_Light_Magic_Target", "relic_vow": "NS_Light_Magic_Shield",
    "mountain_heart": "NS_Earth_Magic_Shield", "construct_core": "NS_Air_Magic_Aura", "worldstone_tank": "NS_Earth_Magic_Aura",
    "worldstone_bruiser": "NS_Fire_Magic_Buff", "living_granite": "NS_Earth_Spells_Shield", "ether_furnace": "NS_Fire_Magic_Aura",
    "blood_oath_banner": "NS_Blood_Magic_Buff", "deep_lantern": "NS_Fire_Magic_Sheild", "ancestral_weight": "NS_Earth_Spells_Buff",
    "dragon_form": "NS_Fire_Magic_Wall", "scale_guard": "NS_Fire_Magic_Target", "ancient_pact": "NS_Dark_Flame_Burst",
    "red_moon": "NS_Blood_Magic_Tornado", "seed_mend": "NS_RoundedVine_Forest", "grove_renewal": "NS_Posion_Magic_Buff",
    "spirit_tether": "NS_Water_Magic_Aura", "kindred_link": "NS_Lightning_Magic_Buff1", "evergrove_trail": "NS_Lightning_Magic_Buff2",
    "steady_gait": "NS_Air_Magic_Buff", "spring_march": "NS_Posion_Magic_Aura", "beacon_of_return": "NS_Light_Magic_Buff",
    "sunrise_vigil": "NS_Light_Magic_Heal", "kit_burning": "NS_State_VFX_Burn1", "axe_frenzy": "NS_Blood_Magic_Aura",
    "tumbling_mend": "NS_State_VFX_Heal1",
}


def main_checkout() -> Path:
    """Packs are junctioned from the main checkout; resolve through git so worktrees work too."""
    try:
        common = subprocess.run(["git", "rev-parse", "--git-common-dir"], cwd=REPO, capture_output=True, text=True, check=True).stdout.strip()
        return (REPO / common).resolve().parent
    except (OSError, subprocess.CalledProcessError):
        return REPO


def index_packs() -> dict[str, str]:
    found: dict[str, str] = {}
    for root in (REPO / "Content", main_checkout() / "Content"):
        for pack in PACKS:
            base = root / pack
            if not base.exists():
                continue
            for f in base.rglob("NS_*.uasset"):
                rel = f.relative_to(root).with_suffix("").as_posix()
                found.setdefault(f.stem, f"/Game/{rel}.{f.stem}")
    return found


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--check", action="store_true")
    a = p.parse_args()
    found = index_packs()
    missing = sorted({n for roles in KIT_VFX.values() for n in roles.values() if n not in found} | {n for n in KIT_BUFF_VFX.values() if n not in found})
    names = [n for roles in KIT_VFX.values() for n in roles.values()]
    print(f"{len(KIT_VFX)} abilities, {len(names)} ability systems ({len(set(names))} distinct), {len(KIT_BUFF_VFX)} buff auras")
    if missing:
        print("missing: " + ", ".join(missing))
        if not found:
            print("(packs not installed: nothing to resolve)")
        return 1 if found else 0
    if a.check:
        return 0
    data = json.loads(OUT.read_text(encoding="utf-8"))
    data["abilities"] = {sid: {role: {"paths": [found[n]], "scale": 1.0} for role, n in roles.items()} for sid, roles in KIT_VFX.items()}
    for key, n in KIT_BUFF_VFX.items():
        data.setdefault("buffs", {})[key] = {"paths": [found[n]], "scale": 1.0, "locked": True}
    OUT.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print("wrote " + str(OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
