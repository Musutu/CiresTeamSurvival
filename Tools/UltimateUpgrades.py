"""items-v2: ultimate upgrades (the Sigil of Apotheosis path unique).

Every ultimate keeps its numbers; the upgrade *adds* one extra effect, merged into
Content/Data/Abilities.json as "ultimateUpgrade" by Tools/BuildAbilityDB.py and executed by
CireUltimateUpgrades.cpp when a champion carrying an ultimateUpgrade passive casts the ultimate.

Effect primitives (all data-driven, radius in cm, duration in seconds):
  partyBuff   allies within radius (radius 0 = only you) gain "stats" (item stat keys) for duration
  barrier     allies within radius gain an absorb shield: amount + scaling x primary stat + healthScaling x your max health
  heal        allies within radius heal amount + scaling x primary stat + healthScaling x your max health
  restore     allies within radius restore "magnitude" of their max mana (and the same fraction of 100 energy)
  cleanse     allies within radius lose slows
  stun / silence / slow / armorBreak   enemies within radius of the center for duration
  damage      enemies within radius take amount + scaling x primary attribute
  cooldownRefund   your other abilities' cooldowns shrink by "magnitude" (fraction)
"center" is "self" (default) or "target" (the aimed point / hostile target); "delay" waits before firing.
"""

UP = lambda name, text, effects, center="self", delay=0.0: dict(name=name, text=text, center=center, delay=delay, effects=effects)
FX = lambda type_, **kw: dict(type=type_, **kw)

ULTIMATE_UPGRADES = {
    # ---- implemented ultimates
    "bastion_of_dawn": UP("Dawnward", "Allies within 7 m also gain a shield absorbing 150 + 10% of your max health for 8 s.",
                          [FX("barrier", radius=700, duration=8, amount=150, healthScaling=0.10)]),
    "cataclysm": UP("Scorched Earth", "Enemies within 4.5 m of the target are also slowed for 3 s and armor-broken for 5 s.",
                    [FX("slow", radius=450, duration=3), FX("armorBreak", radius=450, duration=5)], center="target"),
    "executioners_verdict": UP("Verdict Rendered", "Your other cooldowns shrink by 50% and you gain +25% attack speed for 6 s.",
                               [FX("cooldownRefund", magnitude=0.5), FX("partyBuff", radius=0, duration=6, stats=dict(attackSpeed=25))]),
    "renewal": UP("Second Dawn", "Allies within 9 m also gain +30 armor and +30 ward for 8 s (a party aura).",
                  [FX("partyBuff", radius=900, duration=8, stats=dict(armor=30, ward=30))]),
    "last_stand": UP("Rallying Stand", "Allies within 8 m are also healed for 12% of your max health.",
                     [FX("heal", radius=800, healthScaling=0.12)]),
    "challenge_of_iron": UP("Iron Echo", "Enemies within 8.5 m are also stunned for 1.5 s.",
                            [FX("stun", radius=850, duration=1.5)]),
    "seismic_reprisal": UP("Aftershock", "The burst also breaks enemy armor for 6 s, and allies within 6 m gain +40 armor for 8 s.",
                           [FX("armorBreak", radius=450, duration=6), FX("partyBuff", radius=600, duration=8, stats=dict(armor=40))], delay=0.8),
    "starfall": UP("Falling Sky", "The impact also silences enemies for 2 s, and you restore 20% of your max mana.",
                   [FX("silence", radius=500, duration=2), FX("restore", radius=0, magnitude=0.2)], center="target", delay=1.25),
    "spectral_hunt": UP("Pack Leader", "You and allies within 9 m gain +20% attack speed and +10% move speed for 8 s.",
                        [FX("partyBuff", radius=900, duration=8, stats=dict(attackSpeed=20, moveSpeed=10))]),
    "mass_aegis": UP("Aegis Bloom", "Allies within 9 m also gain a shield absorbing 200 + 2x primary stat for 12 s.",
                     [FX("barrier", radius=900, duration=12, amount=200, scaling=2)]),
    "wellspring": UP("Overflow", "Allies within 6 m of you also heal 150 and restore 15% of their max mana.",
                     [FX("heal", radius=600, amount=150), FX("restore", radius=600, magnitude=0.15)]),
    "collect_the_bounty": UP("Bounty Hunter", "You gain +30% move speed and +12 primary stat for 6 s; other cooldowns shrink by 30%.",
                             [FX("partyBuff", radius=0, duration=6, stats=dict(moveSpeed=30, primaryStat=12)), FX("cooldownRefund", magnitude=0.3)]),
    "hexbane_judgment": UP("Witchlight", "Enemies in the circle are also armor-broken for 6 s; allies within 9 m of you gain +25 ward for 8 s.",
                           [FX("armorBreak", radius=450, duration=6, atTarget=True), FX("partyBuff", radius=900, duration=8, stats=dict(ward=25), atSelf=True)],
                           center="target", delay=1.0),
    "glaive_storm": UP("Eye of the Storm", "You gain a shield absorbing 200 + 2x primary stat and +20% move speed for 6 s.",
                       [FX("barrier", radius=0, duration=6, amount=200, scaling=2), FX("partyBuff", radius=0, duration=6, stats=dict(moveSpeed=20))]),
    "warp_obelisk": UP("Siege Protocol", "Enemies within 6 m of the obelisk are stunned for 1 s; you gain +12 primary stat for 15 s.",
                       [FX("stun", radius=600, duration=1, atTarget=True), FX("partyBuff", radius=0, duration=15, stats=dict(primaryStat=12), atSelf=True)],
                       center="target"),
    "aether_nexus": UP("Resonant Field", "Allies within 9 m of you restore 20% of their max mana and gain a shield absorbing 150 for 10 s.",
                       [FX("restore", radius=900, magnitude=0.2), FX("barrier", radius=900, duration=10, amount=150)]),
    # ---- planned ultimates (fire as soon as their casts land)
    "bear_colossus": UP("Den Mother", "Allies within 8 m gain +40 armor for 8 s.",
                        [FX("partyBuff", radius=800, duration=8, stats=dict(armor=40))]),
    "miner_mountain": UP("Rockfall", "Enemies within 4 m of the ring are stunned for 1 s.",
                         [FX("stun", radius=400, duration=1)], center="target"),
    "golem_worldstone": UP("Worldstone Ward", "Allies within 7 m gain a shield absorbing 200 + 8% of your max health for 8 s.",
                           [FX("barrier", radius=700, duration=8, amount=200, healthScaling=0.08)]),
    "chieftain_earthshout": UP("Warband", "Allies within 9 m gain +8 primary stat and +15% attack speed for 8 s.",
                               [FX("partyBuff", radius=900, duration=8, stats=dict(primaryStat=8, attackSpeed=15))]),
    "behemoth_stampede": UP("Trampled", "Enemies within 6 m are slowed for 3 s and armor-broken for 5 s.",
                            [FX("slow", radius=600, duration=3), FX("armorBreak", radius=600, duration=5)], center="target"),
    "drakish_ancient_pact": UP("Scaled Oath", "Allies within 8 m gain +40 ward and +20 armor for 8 s.",
                               [FX("partyBuff", radius=800, duration=8, stats=dict(ward=40, armor=20))]),
    "troll_red_moon": UP("Blood Moon", "You gain +20% lifesteal and +20% attack speed for 8 s.",
                         [FX("partyBuff", radius=0, duration=8, stats=dict(lifesteal=20, attackSpeed=20))]),
    "dryad_grove_renewal": UP("Heartwood", "Allies within 8 m lose their slows and gain a shield absorbing 150 + 2x primary stat for 8 s.",
                              [FX("cleanse", radius=800), FX("barrier", radius=800, duration=8, amount=150, scaling=2)]),
    "whisp_constellation": UP("Starlit Well", "Allies within 9 m heal 150 and restore 15% of their max mana.",
                              [FX("heal", radius=900, amount=150), FX("restore", radius=900, magnitude=0.15)]),
    "centaur_spring_march": UP("Spring Stride", "Allies within 9 m lose their slows and gain +20% move speed for 8 s.",
                               [FX("cleanse", radius=900), FX("partyBuff", radius=900, duration=8, stats=dict(moveSpeed=20))]),
    "keeper_sunrise": UP("High Noon", "Allies within 9 m gain +8 primary stat and +20 ward for 8 s.",
                         [FX("partyBuff", radius=900, duration=8, stats=dict(primaryStat=8, ward=20))]),
}

EFFECT_TYPES = {"partyBuff", "barrier", "heal", "restore", "cleanse", "stun", "silence", "slow", "armorBreak", "damage", "cooldownRefund"}
STAT_KEYS = {"strength", "agility", "intelligence", "health", "mana", "attackDamage", "spellPower", "armor", "ward", "attackSpeed",
             "critChance", "lifesteal", "cooldownReduction", "moveSpeed", "healthRegen", "manaRegen", "energyRegen",
             "primaryStat", "damageReduction", "damageBlock"}
BANNED_KEYS = {"strength", "agility", "intelligence", "attackDamage", "spellPower"}  # universal primary scaling


def validate(abilities):
    errors = []
    for sid, a in abilities.items():
        if a["kind"] == "ultimate" and sid not in ULTIMATE_UPGRADES:
            errors.append(f"{sid}: ultimate without an ultimateUpgrade")
    for sid, up in ULTIMATE_UPGRADES.items():
        if sid not in abilities or abilities[sid]["kind"] != "ultimate":
            errors.append(f"ultimateUpgrade {sid}: not an ultimate")
        if sum(e["type"] == "partyBuff" for e in up["effects"]) > 1:
            errors.append(f"ultimateUpgrade {sid}: at most one partyBuff (its stats are summed)")
        if not up["effects"] or not up["text"]:
            errors.append(f"ultimateUpgrade {sid}: empty")
        for e in up["effects"]:
            if e["type"] not in EFFECT_TYPES:
                errors.append(f"ultimateUpgrade {sid}: effect {e['type']}")
            if any(k not in STAT_KEYS or k in BANNED_KEYS for k in e.get("stats", {})):
                errors.append(f"ultimateUpgrade {sid}: stats {e['stats']}")
            if e["type"] == "partyBuff" and not (0 < e.get("duration", 0) <= 30 and e.get("stats")):
                errors.append(f"ultimateUpgrade {sid}: partyBuff needs stats and 0-30 s")
            if not 0 <= e.get("radius", 0) <= 2000:
                errors.append(f"ultimateUpgrade {sid}: radius")
    return errors
