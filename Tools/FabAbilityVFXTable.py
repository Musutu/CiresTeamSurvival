"""Curated per-ability and per-buff Fab VFX picks (fab-coverage). Imported by Tools/MapFabVFX.py.

Every champion ability in Content/Data/Abilities.json gets its own signature system for the presentation roles
it actually shows (CireFabVFX::FindFor tries "abilities.<id>.<role>" before the shared school set), and every
BuffVisuals effect gets its own looping state overlay. Values are system stems as they appear in the packs
(NS_ prefix dropped; Kakky FX Variety Pack Cascade systems keep their P_ky_ name), optionally (stem, scale).

Roles: c = cast (caster flare / heal flash on the target), p = projectile (follows the missile), i = impact,
a = live area (scaled by the zone radius; telegraphs never get an overlay, so warnings stay dim).

Picks follow the ability's school and fantasy, keep the art vibrant and crisp, and avoid reusing a system
another ability already owns in the same role wherever the packs allow it.

Reviewed in Tools/RunAbilityVFXGallery.py / RunAuraGallery.py captures and deliberately NOT used:
  *_Magic_Target (Lord Enot)   a homing streak or a sky-beacon cone: a fire trail across the screen, a grey bell at scale;
  P_ky_lightning1-3, P_ky_thunderStorm, P_ky_darkStorm, P_ky_aquaStorm   strike from storm clouds far above the unit;
  Earth_Magic_Shield           dark orbiting boulders, too heavy for a buff overlay.
"""

ABILITY_VFX = {
    # --- Iron Warden (knight) ---
    "shield_slam": {"c": "Light_Magic_Slash1", "i": "Light_Magic_Hit2"},
    "iron_guard": {"c": "Earth_Magic_Buff"},
    "war_cry": {"c": "Fire_Magic_Shockwave", "a": "Blood_Magic_Area1"},
    "cleaving_strike": {"c": "Air_Magic_Slash3", "i": "Slash_Med", "a": "Air_Magic_AOE"},
    "second_wind": {"c": "AreaBuff_Applied"},
    "protection_dome": {"c": "Light_Magic_Shield", "a": ("Light_Magic_Shield", 1.3)},
    "bastion_of_dawn": {"c": "Light_Magic_Sword_Circle", "a": "Light_Magic_AOE1"},
    "decimating_strike": {"c": "Dark_Magic_Slash2", "i": "Slash_High"},
    "executioners_verdict": {"c": "Dark_Magic_Slash1", "i": "Blood_Magic_Explo"},
    "last_stand": {"c": "Blood_Magic_Shield"},
    "challenge_of_iron": {"c": "Earth_Magic_Shockwave"},
    "seismic_reprisal": {"c": "Earth_Magic_Spike3", "i": "Earth_Spells_Hit2", "a": "Earth_Magic_Meteors2"},
    # --- Ranger ---
    "piercing_shot": {"c": "Air_Magic_Muzzle3", "p": "Air_Magic_Arrow2", "i": "Air_Magic_Hit3"},
    "frost_bind": {"c": "Ice_Magic_Muzzle", "p": "Ice_Magic_Spear", "i": "Ice_Magic_SpearSplash"},
    "shadow_step": {"c": "Shadow_Magic_Blink2", "i": "Shadow_Magic_Hit2"},
    "venom_ground": {"c": "Posion_Magic_Wave1", "a": "Posion_Magic_Area2", "i": "Posion_Magic_Explosion2"},
    "grave_line": {"c": "Shadow_Magic_Line_Attack1", "a": "Shadow_Magic_Area_Line_Attack1", "i": "Shadow_Magic_Hit3"},
    "spectral_pack": {"c": "Shadow_Magic_Buff2", "i": "Dark_Magic_Hit_Orb"},
    "spectral_hunt": {"c": "Shadow_Magic_Mass_Projectile1", "p": "Shadow_Magic_Projectile2", "i": "Shadow_Magic_Explosion2"},
    "blight_sigil": {"c": "Posion_Magic_SpikeArea", "a": "Posion_Magic_Area3"},
    # --- Veil Scholar ---
    "restoring_light": {"c": "Light_Magic_Heal", "i": "Light_Magic_Heal_Hit"},
    "sanctuary": {"c": "Light_Magic_Circle", "a": "Light_Magic_Top_Area"},
    "purify": {"c": "Light_Magic_Blink1"},
    "chain_spark": {"c": "Lightning_Magic_Blink2", "p": "Lightning_Magic_Projectile2", "i": "Lightning_Magic_Shield_Splash"},
    "ember_lance": {"c": "Fire_Magic_Muzzle", "p": "Fire_Magic_Projectile3", "i": "Fire_Magic_SpearSplash"},
    "renewal": {"c": "P_ky_healAura", "a": "Light_Magic_Area_Beam"},
    "polymorph": {"c": "Air_Magic_Muzzle2", "i": "Air_Magic_Splash"},
    "starfall": {"c": "Air_Magic_Tornado1", "a": "P_ky_shootingStar1", "i": "Air_Magic_Hit4"},
    "mass_aegis": {"c": "Water_Magic_Shield"},
    "wellspring": {"c": "Water_Magic_Waterflow1", "i": "P_ky_waterBallHit", "a": "Water_Magic_Area2"},
    # --- Lancer / Summoner ---
    "ashen_square": {"c": "Fire_Magic_Buff", "a": "Fire_Magic_Arena", "i": "Fire_Magic_Hit"},
    "oathbound_guardian": {"c": "Dark_Magic_Top2", "i": "Dark_Magic_Circle"},
    "summoned_wall": {"c": "Earth_Spells_Wall1", "a": "Earth_Spells_Wall2"},
    "cataclysm": {"c": "Fire_Magic_Circle", "i": "P_ky_explosion", "a": "P_ky_fireStorm"},
    "cinder_cone": {"c": "Fire_Magic_Flamethrower", "a": "Fire_Magic_Flame1"},
    # --- Bear ---
    "bear_maul": {"c": "Earth_Magic_Slash1", "i": "Earth_Magic_Hit"},
    "bear_roar": {"c": "Earth_Spells_Cone", "i": "Earth_Spells_Hit3", "a": "Earth_Spells_Cone"},
    "bear_charge": {"c": "Earth_Magic_Dash", "i": "Earth_Spells_Shockwave", "a": "Earth_Spells_Spike_Line4"},
    "bear_hibernate": {"c": "Wood_Hide_Forest"},
    "bear_colossus": {"c": "Earth_Spells_Attack_Up2", "a": "Earth_Spells_Arena"},
    # --- Paladins ---
    "paladin_righteous_flail": {"c": "Light_Magic_Slash2", "i": "Light_Magic_Hit3", "a": "Light_Magic_Sword_Line2"},
    "paladin_relic_vow": {"c": "Light_Magic_Buff", "i": "Light_Magic_Shield_Splash"},
    "paladin_holy_flail": {"c": "Light_Magic_Sword_Line", "i": "Light_Magic_Explosion1"},
    "paladin_pilgrim_light": {"c": "Light_Magic_Blink2", "p": "Light_Magic_Projectile2", "i": "Light_Magic_Hit1"},
    # --- Dwarf Miner ---
    "miner_pickfall": {"c": "Earth_Magic_Slash2", "i": "Earth_Magic_Spike1"},
    "miner_faultline": {"c": "Earth_Spells_Area_Spike_Line1", "a": "Earth_Spells_Spike_Line", "i": "Earth_Spells_Hit1"},
    "miner_lantern": {"c": "Fire_Magic_Orb", "a": "Fire_Magic_Circle"},
    "miner_mountain": {"c": "Earth_Spells_Wall3", "a": "Earth_Spells_Wall4"},
    # --- Ether golems ---
    "golem_granite_fist": {"c": "Earth_Spells_Cone2", "i": "Earth_Magic_Stone2", "a": "Earth_Spells_Spike_Cone"},
    "golem_ether_anchor": {"c": "Air_Magic_Muzzle4", "a": "Earth_Magic_Circle1"},
    "golem_worldstone": {"c": "Earth_Spells_Explosion"},
    "golem_moss_bloom": {"c": "RoundedVine_Forest", "a": "AreaBuff"},
    "golem_living_granite": {"c": "Vine_Barrior", "i": "Wood_Hide_Forest"},
    "golem_fel_fist": {"c": "Dark_Magic_Dark_Flame", "i": "Dark_Flame_Burst", "a": "Dark_Magic_Area_Splash1"},
    "golem_ether_furnace": {"c": "Fire_Magic_Flame2"},
    # --- Orc Chieftain ---
    "chieftain_axe_hook": {"c": "Earth_Magic_Muzzle", "p": "Earth_Spells_Projectile3", "i": "Stab_Med"},
    "chieftain_banner": {"c": "Blood_Magic_Marker", "a": "Blood_Magic_Area2"},
    "chieftain_earthshout": {"c": "Earth_Spells_Shockwave", "a": "Earth_Spells_Circle"},
    # --- Totemic Behemoth ---
    "behemoth_totem_sweep": {"c": "Earth_Spells_Cone3", "i": "Earth_Spells_Slash2", "a": "Earth_Spells_Area2"},
    "behemoth_tusk_line": {"c": "Earth_Spells_Spike_Line2", "i": "Earth_Magic_Splash", "a": "Earth_Spells_Spike_Line1_v2"},
    "behemoth_totem_bulwark": {"c": "Earth_Spells_Shield", "a": "Earth_Magic_Earth_Wall2"},
    "behemoth_stampede": {"c": "Earth_Spells_Meteorites_Line", "a": "Earth_Spells_Area_Spike_Line3"},
    # --- Drakish Footman ---
    "drakish_dragon_oath": {"c": "Fire_Magic_Dash2"},
    "drakish_scale_guard": {"c": "Fire_Magic_Sheild"},
    "drakish_wing_rebuke": {"c": "Fire_Magic_Slash2", "i": "Fire_Magic_Splash", "a": "Fire_Magic_FrontSplash"},
    "drakish_ancient_pact": {"c": "Fire_Magic_Wall", "a": "Fire_Magic_Spike2"},
    # --- Troll Berserkers ---
    "troll_axe_frenzy": {"c": "Blood_Magic_Slash1", "i": "Slash_Med"},
    "troll_blood_leap": {"c": "Blood_Magic_Dash", "i": "Blood_Magic_Explo2", "a": "Blood_Magic_Area3"},
    "troll_red_moon": {"c": "Blood_Magic_Tornado"},
    "troll_twin_throw": {"c": "Blood_Magic_Muzzle", "p": "Blood_Magic_Projectile3", "i": "Stab_Low"},
    "troll_returning_axes": {"c": "Blood_Magic_Slash2", "p": "Blood_Magic_Projectile4", "a": "Blood_Magic_Spike1", "i": "Blood_Magic_Crystal1"},
    # --- Dryad ---
    "dryad_root_snare": {"c": "Explosion_Cast_Nature", "a": "Vine_attack2"},
    "dryad_seed_mend": {"c": "HealBeam", "p": "Projectile_Grenade_Nature", "i": "Explosion_Nature"},
    "dryad_thorn_line": {"c": "Earth_Spells_Spike_Line3", "a": "Vine_attack1"},
    "dryad_grove_renewal": {"c": "Aura_Nature", "a": ("AreaBuff", 1.25)},
    # --- Whisp ---
    "whisp_guiding_mote": {"c": "Air_Magic_Muzzle1", "p": "Air_Magic_Orb", "i": "State_VFX_Heal1"},
    "whisp_spirit_tether": {"c": "Light_Magic_Beam", "i": "Light_Magic_Heal_Hit"},
    "whisp_fey_trail": {"c": "Light_Magic_Dash", "a": "Air_Magic_Airflow"},
    "whisp_constellation": {"c": "Light_Magic_Orb_Base", "a": "Light_Magic_Circle"},
    # --- Evergrove Centaur ---
    "centaur_grove_javelin": {"c": "Explosion_Cast_Nature", "p": "Ribbon_Nature", "i": "Explosion_Grenade_Nature"},
    "centaur_trailblaze": {"c": "Air_Magic_Dash", "a": "RoundedVine_Forest"},
    "centaur_herd_call": {"c": "Wing_Nature", "i": "TextureParticle_Forest"},
    "centaur_spring_march": {"c": "Air_Magic_Buff", "a": "AreaBuff_Applied"},
    # --- Keeper of Light ---
    "keeper_dawn_beam": {"c": "P_ky_laser01", "a": "Light_Magic_Sword_Line_Area1"},
    "keeper_lantern_ward": {"c": "Light_Magic_Top", "a": "Light_Magic_Sword_Wall"},
    "keeper_beacon": {"c": "Light_Magic_Sword_AOE", "a": "Light_Magic_Sword_Area"},
    "keeper_sunrise": {"c": "Light_Magic_Explosion1", "a": ("Light_Magic_Top_Area", 1.2)},
    # --- Gunblade ---
    "silver_shot": {"c": "P_ky_shotShockwave", "p": "Light_Magic_Projectile3", "i": "BulletHit_Med"},
    "hex_mark": {"c": "Shadow_Magic_Circle2", "i": "Dark_Magic_Debuff"},
    "powder_flask": {"c": "Fire_Magic_Muzzle", "p": "Fire_Magic_Projectile4", "i": "Fire_Magic_Explosion", "a": "Fire_Magic_AOE"},
    "blade_flurry": {"c": "Air_Magic_Blades", "i": "Slash_Low"},
    "hunters_stride": {"c": "Lightning_Magic_Dash", "a": "Lightning_Magic_Line1"},
    "warding_talisman": {"c": "Light_Magic_Shield_Splash"},
    "collect_the_bounty": {"c": "Dark_Magic_Blink2", "i": "BulletHit_High"},
    # --- Witch Slayer ---
    "arcane_blunderbuss": {"c": "Dark_Magic_Cone1", "i": "Air_Magic_Hit2", "a": "Shadow_Magic_Cone1"},
    "spirit_lantern": {"c": "Shadow_Magic_Orb1", "a": "Shadow_Magic_Circle1", "i": "State_VFX_Silence1"},
    "purge": {"c": "Light_Magic_Blink2", "i": "Light_Magic_Explosion1"},
    "banishment": {"c": "Dark_Magic_Top", "i": "Shadow_Magic_Explosion1"},
    "witchfinders_mark": {"c": "Shadow_Magic_Attack1", "i": "Dark_Magic_Splash1"},
    "spectral_blade": {"c": "Shadow_Magic_Slash2", "i": "Shadow_Magic_Attack3"},
    "hexbane_judgment": {"c": "Shadow_Magic_Circle1", "a": "Shadow_Magic_Area3", "i": "Shadow_Magic_Attack5"},
    # --- Huntress ---
    "bouncing_glaive": {"c": "Air_Magic_Slash2", "p": "Air_Magic_Wind_Blade", "i": "Air_Magic_Hit1"},
    "sabercat_pounce": {"c": "Air_Magic_Dash", "i": "Stab_Low"},
    "owl_scout": {"c": "Air_Magic_Tornado2", "a": "AreaBuff_Applied"},
    "moonlit_sprint": {"c": "Ice_Magic_Dash"},
    "crescent_volley": {"c": "Air_Magic_Muzzle3", "p": "Air_Magic_Slash4", "i": "Air_Magic_Hit2"},
    "sabercat_maul": {"c": "Blood_Magic_Slash2", "i": "Slash_Low"},
    "sabercat_roar": {"c": "Air_Magic_Splash"},
    "glaive_storm": {"c": "Air_Magic_Tornado3", "a": "P_ky_storm"},
    # --- Aetheri Artificer ---
    "photon_turret": {"c": "Lightning_Magic_Orb2", "p": "Lightning_Magic_Projectile3", "i": "Lightning_Magic_Slash2", "a": "Lightning_Magic_Orb"},
    "skitter_swarm": {"c": "Lightning_Magic_Blink1", "i": "Lightning_Magic_Blink2", "a": "Lightning_Magic_Area"},
    "arc_mine": {"c": "Lightning_Magic_Orb3", "i": "Lightning_Magic_Shockwave", "a": "Lightning_Magic_Tunder_Area"},
    "disruption_pylon": {"c": "Lightning_Magic_Orb", "a": "Lightning_Magic_Tunder_Circle1"},
    "phase_lance": {"c": "Lightning_Magic_Blink1", "p": ("Air_Magic_Projectile2", 0.55), "i": "P_ky_ThunderBallHit"},
    "overcharge": {"c": "Lightning_Magic_Buff2"},
    "warp_obelisk": {"c": "Lightning_Magic_Tornado", "p": "Lightning_Magic_Laser", "a": "Lightning_Magic_Tornado_Area"},
    # --- Aetheri Warden ---
    "aegis_pylon": {"c": "Air_Magic_Muzzle4", "a": "Air_Magic_Shield"},
    "haste_pylon": {"c": "Lightning_Magic_Orb3", "a": "Lightning_Magic_Aura2"},
    "gravity_pylon": {"c": "Dark_Magic_Orb2", "a": "Dark_Magic_AOE2"},
    "stasis_snare": {"c": "Ice_Magic_Buff", "i": "Ice_Magic_Frozen", "a": "Ice_Magic_Circle3"},
    "aether_mend": {"c": "Water_Magic_Buff", "i": "Water_Magic_TargetBubble"},
    "repulsor_pulse": {"c": "Water_Magic_Shockwave"},
    "aether_nexus": {"c": "Lightning_Magic_Slash_Aura", "a": "Lightning_Magic_Aura1"},
    # --- Role skills (scaling kits) ---
    "shield_bash": {"c": "Earth_Magic_Muzzle", "i": "Earth_Magic_Stone1"},
    "shield_toss": {"c": "Air_Magic_Muzzle1", "p": "Earth_Magic_Orb", "i": "Earth_Magic_Splash"},
    "shield_wall": {"c": "Ice_Magic_Sheild"},
    "pavise": {"c": "Earth_Magic_Earth_Wall1", "a": "Earth_Magic_Earth_Wall1"},
    "mechanical_tank": {"c": "Earth_Magic_Stoneflow", "a": "Earth_Magic_Shockwave"},
    "artillery": {"c": "Fire_Magic_Buff"},
    "eagle_eye": {"c": "Wing_Nature"},
    "longshot": {"c": "Air_Magic_Muzzle1"},
    # --- Dodge-roll skills (actives) ---
    "tumble_strike": {"c": "Air_Magic_Slash5", "i": "Slash_Low"},
    "mine_layer": {"c": "Earth_Magic_Muzzle", "a": "Earth_Magic_Spike4"},
    "taunting_tumble": {"c": "Fire_Magic_Shockwave"},
    "shield_tumble": {"c": "Light_Magic_Shield_Splash"},
    "venom_tumble": {"c": "Posion_Magic_Dash", "a": "Posion_Magic_PoisonFlow"},
    "shadow_dance": {"c": "Shadow_Magic_Dash1"},
    "evasive_stance": {"c": "Water_Magic_Waterstep"},
    # --- Passives with a visible proc or trail ---
    "ember_wake": {"a": "Fire_Magic_Flame3"},
    "frost_wake": {"a": "Ice_Magic_Circle2"},
    "executioner": {"i": "Slash_High"},
    "headshot": {"i": "BulletHit_High"},
    "moon_glaive": {"p": "Air_Magic_Wind_Blade", "i": "Air_Magic_Hit1"},
    "drakish_ember_memory": {"p": "Fire_Magic_Projectile2", "i": "Fire_Magic_Hit"},
}

# BuffVisuals ids -> looping state overlay (CireAuraVisuals: exact id is tried before kind.school / kind).
BUFF_VFX = {
    # champion buffs and stances
    "iron_guard": ("Light_Magic_Shield", 0.65), "shield_wall": ("Ice_Magic_Sheild2", 0.9), "challenge_of_iron": "Fire_Magic_Sheild",
    "war_cry": "Blood_Magic_Aura", "taunting": "Fire_Magic_Aura", "taunting_tumble": "Fire_Magic_Aura",
    "guarded": "Earth_Spells_Shield", "bastion_of_dawn": "Light_Magic_Shield", "sanctuary": "P_ky_healAura",
    "mass_aegis": "Water_Magic_Shield", "wellspring": "Water_Magic_Buff", "warding_talisman": "Light_Magic_Aura",
    "party_barrier": "Light_Magic_Shield", "oathshield": "Air_Magic_Shield", "vigil_banner": "Light_Magic_Buff",
    "apotheosis": "Light_Magic_Aura", "blessing": "Light_Magic_Buff", "shield_tumble": "Light_Magic_Buff",
    "artillery": "Fire_Magic_Buff", "eagle_eye": "Wing_Nature", "longshot": "Light_Magic_Circle",
    "hunters_stride": "Lightning_Magic_Buff1", "moonlit_sprint": "Ice_Magic_Aura", "windrunner": "Air_Magic_Buff",
    "scatter": "Air_Magic_Aura", "quickened_mind": "Air_Magic_Buff", "momentum": "Lightning_Magic_Slash_Aura",
    "tumblers_edge": "Water_Magic_Aura", "killer_instinct": "Shadow_Magic_Aura2", "executioner_ready": "Dark_Magic_Buff",
    "blur_step": "Shadow_Magic_Buff2", "shadow_dance": "Shadow_Magic_Aura1", "evasive_stance": "Water_Magic_Buff",
    "mine_layer": "Earth_Magic_Buff", "venom_tumble": "Posion_Magic_Buff", "frost_weapon": "Ice_Magic_Buff",
    "blood_rage": "Blood_Magic_Aura", "borrowed_time": "Light_Magic_Circle", "overcharge": "State_VFX_Shock1",
    "aether_aegis": "Air_Magic_Shield", "aether_haste": "Lightning_Magic_Buff2", "aether_nexus": "Lightning_Magic_Aura2",
    "regeneration": "State_VFX_Heal1", "mana_restore": "State_VFX_Mana1", "rallied": "Blood_Magic_Buff",
    # debuffs on enemies
    "armor_broken": "Earth_Magic_Aura", "aether_weakened": "Air_Magic_Aura", "banished": "Shadow_Magic_Shield1",
    "bounty_mark": "State_VFX_Cursed1", "witch_mark": "Dark_Magic_Debuff", "tracked": "State_VFX_Cursed1",
    "frost_bind": "State_VFX_Freeze1", "healing_cut": "State_VFX_Cursed1", "heal_cut_done": "Posion_Magic_Debuff",
    "interrupted": "Stun1", "stunned": "Stun1", "shield_slam": "Stun1", "silenced": "State_VFX_Silence1",
    "slowed": "State_VFX_Slow1", "poisoned": "State_VFX_Poison1", "polymorphed": "State_VFX_Charm1",
    "taunted": "Blood_Magic_Debuff", "l15_amped": "Dark_Magic_Debuff", "l15_vulnerable": "Shadow_Magic_Aura1",
    "mech_weakened": "Earth_Spells_Aura",
    # monster states
    "npc_rooted": "State_VFX_Root1", "npc_silenced": "State_VFX_Silence1", "npc_spores": "Posion_Magic_Aura",
    "npc_dragonfire": "State_VFX_Burn1", "npc_profane": "State_VFX_Cursed1", "npc_mind": "State_VFX_Charm1",
    "npc_ink": "State_VFX_Blind1", "npc_aether": "State_VFX_Shock1", "npc_feral": "State_VFX_Bleed1",
    "npc_runic": "Air_Magic_Aura", "npc_scaleward": "Earth_Spells_Buff", "npc_sundered": "Earth_Magic_Aura",
    "npc_thorns": "RoundedVine_Forest", "npc_tide": "Water_Magic_Aura", "npc_void": "Dark_Magic_Aura",
    "npc_bloodlust": "Blood_Magic_Buff", "npc_aether_empowered": "Lightning_Magic_Aura1", "enraged": "Dark_Magic_Aura",
    "npc_tank_guard": "Earth_Magic_Buff", "npc_tank_provoke": "Fire_Magic_Aura", "npc_tank_provoke_debuff": "Blood_Magic_Debuff",
    "npc_tank_wall": "Earth_Spells_Shield", "boss_leader_frenzy": "Blood_Magic_Buff", "boss_siege_fury": "Fire_Magic_Aura",
    "toll_of_the_grave": "Shadow_Magic_Aura2",
}
# Always-on passives and the level-15 party auras keep only their procedural signature: a looping Fab
# overlay on every ally for the whole match would drown the combat read (Eric: telegraph/aura clutter).
BUFF_NO_FAB = {"battle_rhythm", "soul_conduit", "aura15_aoeResist", "aura15_armor", "aura15_attackSpeed", "aura15_crit",
               "aura15_doubleAttack", "aura15_magicLifesteal", "aura15_magicResist", "aura15_physicalLifesteal",
               "aura15_rangedDamage", "aura15_stunIgnore", "aura15_stunOnHit"}

ROLE = {"c": "cast", "p": "projectile", "i": "impact", "a": "area"}
ROLE_SCALE = {"cast": 1.0, "projectile": 1.0, "impact": 1.25, "area": 1.0}
# Kakky's Cascade systems are authored at UE4 demo scale: bring them down to the Lord Enot footprint.
CASCADE_SCALE = 0.6
