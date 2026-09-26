"""kits-complete: the 63 signature skills of the thirteen roster champions whose kits were "planned".

Bear, Righteous and Holy Paladin, Dwarf Miner, the three Ether Golems, Orc Chieftain, Totemic Behemoth,
Drakish Footman, both Troll Berserkers, Dryad, Whisp, Evergrove Centaur and Keeper of Light.

Implemented natively by CireKitSkills (Source/CiresTeamSurvival/CireKitSkills.cpp), routed through
CireSignatureSkills like the other signature kits. Same tuple layout as NEW_CHAMPION_SKILLS in
BuildAbilityDB.py:

    id: (name, roles, kind, school, targeting, castTime, mana, energy, cooldown, effect, effectLabel,
         range, radius, duration, description, effects, extra)

extra: "curve" (level-curve overrides), "category" ("construct" / "pet"), "section" (Skill Shop section),
"scaling" (component/base/primary override of the shape-derived coefficient), "void" (landing rift).
Heals carry a cast time or a channel (Eric: "heals obvious and have cast times"). Strength / agility
champions pay energy, intelligence champions mana; skills shared across both pay energy.
"""


def fx(kind, zone="target", duration=0.0, magnitude=0.0, radius=0.0, lockout=0.0, label=""):
    e = dict(type=kind, zone=zone, duration=duration, magnitude=magnitude, radius=radius)
    if lockout:
        e["lockoutSeconds"] = lockout
    if label:
        e["label"] = label
    return e


CONSTRUCT = {"category": "construct"}


def S(component, base, primary, **kw):
    return dict(component=component, base=base, primary=primary, **kw)


# Void rift where a leap / charge lands (inner stun, outer slow), like Shadow Step's.
LANDING_RIFT = dict(innerRadius=160, outerRadius=380, innerEffect="stun", innerDuration=0.8, outerEffect="slow", outerDuration=2.0,
                    outerMagnitude=0.35, damage=25, selfHealMaxHealthFraction=0.0)

KIT_SIGNATURES = {
    # ================================================================ Gravewood Bear (tank, STR, energy)
    "bear_maul": ("Gravewood Maul", ["tank"], "active", "physical", "enemy", 0, 0, 20, 8, 35, "damage", 280, 0, 3,
                  "Heavy claw strike for {effect} damage: triple threat, and the target is taunted for 3s.",
                  [fx("taunt", "target", 3)], {"section": "attack"}),
    "bear_roar": ("Deepwood Roar", ["tank"], "active", "physical", "aim", 0, 0, 25, 12, 20, "damage", 550, 550, 4,
                  "Roar through a 90-degree cone (5.5m): {effect} damage, casts interrupted, enemies taunted for 3s and their attacks weakened by 25% for 4s.",
                  [fx("interrupt", "area", 0, 0, 550, lockout=2.0, label="Interrupted"), fx("taunt", "area", 3, radius=550),
                   fx("weaken", "area", 4, 0.25, 550, label="Damage -25%")], {"section": "control"}),
    "bear_charge": ("Rootbreaker Charge", ["tank"], "active", "physical", "aim", 0, 0, 30, 14, 40, "damage", 750, 90, 1,
                    "Rush up to 7.5m down a warned lane. Walls stop you, and so does the first enemy: it takes {effect} damage and is stunned for 1s. A void rift opens where you stop.",
                    [fx("stun", "target", 1.0, label="Stunned")], {"void": LANDING_RIFT}),
    "bear_hibernate": ("Ironroot Slumber", ["tank"], "active", "nature", "self", 0, 0, 20, 20, 30, "healing per second", 0, 0, 4,
                       "Channel for up to 4s while standing still: restore {effect} health per second and take 20% less damage. Moving or a heavy hit (over 10% of your max health) ends the slumber.",
                       [fx("guard", "self", 4, 0.2, label="DEF +20%")], {"scaling": S("heal", 30, 0.5), "section": "defensive"}),
    "bear_ancient_hide": ("Ancient Hide", ["tank"], "passive", "physical", "passive", 0, 0, 0, 0, 25, "% damage reduction", 0, 0, 4,
                          "After 5 hits within 4s your hide hardens: take {effect}% less damage for 4s (once every 12s).", [], {"curve": {"effectCap": 40}}),
    "bear_colossus": ("Elder of the Deepwood", ["tank"], "ultimate", "physical", "self", 0, 0, 50, 80, 80, "damage", 0, 600, 12,
                      "Become the Elder of the Deepwood for 12s: you grow 30% larger, slam enemies within 6m for {effect} damage and taunt them for 4s, take 30% less damage, reach 1.5m further and double your threat.",
                      [fx("taunt", "area", 4, radius=600), fx("guard", "self", 12, 0.3, label="DEF +30%")], {}),
    # ================================================================ Paladins
    "paladin_righteous_flail": ("Righteous Flail", ["tank"], "active", "holy", "aim", 0, 0, 25, 9, 90, "damage", 420, 420, 6,
                                "Swing the relic flail through a 100-degree cone (4.2m): {effect} damage; every enemy hit is marked for 6s (it takes 10% more damage from you) and turns on you for 2s.",
                                [fx("mark", "area", 6, 0.10, 420, label="Damage taken +10%"), fx("taunt", "area", 2, radius=420)], {"section": "attack"}),
    "paladin_relic_vow": ("Relic Vow", ["tank", "heal"], "active", "holy", "ally", 0, 0, 25, 18, 100, "barrier health", 900, 0, 8,
                          "Bind an ally to the relic for 8s: they gain a {effect} barrier and 30% of the damage they take is redirected to you.",
                          [fx("shield", "target", 8, 0.3, label="Relic Vow")], {"scaling": S("shield", 100, 2.5), "section": "defensive"}),
    "paladin_holy_flail": ("Merciful Censer", ["heal"], "active", "holy", "enemy", 0, 30, 0, 8, 70, "damage", 320, 450, 0,
                           "Strike an enemy with the censer for {effect} holy damage; the impact releases a healing pulse that restores allies within 4.5m of the target for 15% of the damage dealt.",
                           [], {"section": "spell"}),
    "paladin_pilgrim_light": ("Pilgrim Light", ["heal"], "active", "holy", "aim", 1.0, 40, 0, 7, 50, "healing", 1100, 60, 0,
                              "Cast 1s: release a slow orb of light down a line (11m); the first ally it reaches is healed for {effect}. With no ally in its path it bursts at the end, healing allies within 2m.",
                              [], {}),
    # ================================================================ Dwarf Miner (tank, STR)
    "miner_pickfall": ("Pickfall", ["tank"], "active", "earth", "enemy", 0, 0, 20, 8, 110, "damage", 280, 0, 5,
                       "Pick strike for {effect} damage that cracks armour: the target's defences are 20% weaker for 5s (Vulnerability).",
                       [fx("mark", "target", 5, 0.2, label="Vulnerable")], {"section": "attack"}),
    "miner_faultline": ("Faultline", ["tank"], "active", "earth", "aim", 0, 0, 30, 12, 130, "damage", 900, 110, 2,
                        "A warned faultline (9m x 2.2m) erupts after 0.8s: {effect} damage, a 0.8s stun and a 40% slow for 2s.",
                        [fx("stun", "area", 0.8, label="Stunned"), fx("slow", "area", 2, 0.4, label="Move -40%")], {}),
    "miner_lantern": ("Deep Lantern", ["tank"], "active", "fire", "aim", 0, 0, 25, 20, 20, "% damage reduction", 700, 450, 15,
                      "Place a Deep Lantern (15s, destructible): allies within 4.5m take {effect}% less damage while its light reveals the ground.",
                      [fx("guard", "area", 0, 0.2, 450, label="DEF +20%")], dict(CONSTRUCT, curve={"effectCap": 30})),
    "miner_orehide": ("Orehide", ["tank"], "passive", "earth", "passive", 0, 0, 0, 0, 30, "barrier per stack", 0, 0, 6,
                      "Every 4th hit you take hardens your ore skin: a {effect} barrier for 6s (barriers never stack; the stronger one holds).",
                      [], {"scaling": S("shield", 30, 1.0)}),
    "miner_mountain": ("Heart of the Mountain", ["tank"], "ultimate", "earth", "aim", 0, 0, 50, 80, 180, "damage", 800, 500, 6,
                       "After a 1s warning a stone ring (5m) erupts at the target: {effect} damage and a 1s stun to enemies inside. For 6s allies inside take 25% less damage and enemies inside are slowed 40%.",
                       [fx("stun", "area", 1.0, radius=500, label="Stunned"), fx("guard", "area", 6, 0.25, 500, label="DEF +25%")], {}),
    # ================================================================ Ether Golems
    "golem_granite_fist": ("Granite Fist", ["tank"], "active", "earth", "aim", 0, 0, 25, 9, 28, "damage", 450, 450, 2,
                           "Slam a warned 70-degree cone (4.5m): {effect} damage, triple threat, a 2s taunt and a 30% slow for 2s.",
                           [fx("slow", "area", 2, 0.3, 450, label="Move -30%"), fx("taunt", "area", 2, radius=450)], {"section": "control"}),
    "golem_ether_anchor": ("Ether Anchor", ["tank"], "active", "arcane", "aim", 0, 0, 25, 14, 25, "damage", 800, 250, 2,
                           "After a 0.5s warning an ether circle (2.5m) anchors enemies: {effect} damage and they are rooted for 2s.",
                           [fx("root", "area", 2, radius=250, label="Rooted")], {}),
    "golem_construct_core": ("Construct Core", ["tank", "dps"], "passive", "arcane", "passive", 0, 0, 0, 0, 2, "% per charge", 0, 0, 6,
                             "Every 150 damage you take adds a core charge (max 10, fades 6s after your last hit): each gives +{effect}% damage and +1% damage reduction.",
                             [], {"curve": {"effectCap": 4}}),
    "golem_worldstone": ("Worldstone Awakened", ["tank", "heal", "dps"], "ultimate", "arcane", "self", 0, 0, 50, 85, 60, "damage or healing", 0, 600, 10,
                         "Awaken the worldstone: a 6m shockwave deals {effect} damage to enemies (Support golem: heals allies for it). For 10s the tank golem taunts enemies within 6m and takes 35% less damage, the support golem pulses the heal again every 2s, and the bruiser golem deals 30% more damage.",
                         [], {"scaling": S("damage", 120, 2.8)}),
    "golem_moss_bloom": ("Verdant Bloom", ["heal"], "active", "nature", "aim", 1.5, 55, 0, 14, 55, "healing per second", 900, 350, 6,
                         "Cast 1.5s: open a Verdant Bloom (3.5m) for 6s; allies inside regenerate {effect} health per second.",
                         [], {"scaling": S("heal", 28, 0.45)}),
    "golem_living_granite": ("Living Granite", ["heal"], "active", "nature", "ally", 1.0, 45, 0, 10, 120, "barrier health", 1100, 0, 8,
                             "Cast 1s: cover an ally in living granite: a {effect} barrier for 8s; while it holds they regenerate 2% of their max health per second.",
                             [fx("shield", "target", 8, label="Living Granite")], {"scaling": S("shield", 120, 2.5), "section": "defensive"}),
    "golem_fel_fist": ("Felfire Fist", ["dps"], "active", "fire", "aim", 0, 0, 25, 8, 65, "damage", 400, 400, 3,
                       "Felfire punch through a 60-degree cone (4m): {effect} fire damage, and enemies hit burn for 30% more over 3s.",
                       [], {"section": "attack"}),
    "golem_ether_furnace": ("Ether Furnace", ["dps"], "active", "fire", "self", 0, 0, 30, 14, 40, "% attack damage", 0, 150, 6,
                            "Stoke the furnace: your next 4 basic attacks within 6s deal {effect}% more damage as fire and splash half of it to enemies within 1.5m.",
                            [], {"section": "attack"}),
    # ================================================================ Orc Chieftain (tank/support, STR)
    "chieftain_axe_hook": ("Chieftain Hook", ["tank"], "active", "physical", "aim", 0, 0, 30, 12, 200, "damage", 900, 50, 2,
                           "Throw a chained axe down a line (9m): the first enemy hit takes {effect} damage, is dragged up to 5m toward you and taunted for 2s. Bosses are not dragged.",
                           [fx("taunt", "target", 2)], {"section": "control"}),
    "chieftain_banner": ("Blood-Oath Banner", ["tank", "heal"], "active", "physical", "aim", 0, 0, 30, 22, 15, "% damage and attack speed", 700, 500, 15,
                         "Plant a Blood-Oath Banner (15s, destructible): allies within 5m deal {effect}% more damage and attack 10% faster.",
                         [fx("haste", "area", 0, 0.1, 500, label="ATK +15%")], dict(CONSTRUCT, curve={"effectCap": 25})),
    "chieftain_courage": ("Unbroken Clan", ["tank"], "passive", "physical", "passive", 0, 0, 0, 0, 5, "% damage reduction per ally", 0, 800, 0,
                          "For each ally within 8m you take {effect}% less damage (up to 3 allies).", [], {"curve": {"effectCap": 8}}),
    "chieftain_earthshout": ("Earthshout", ["tank", "heal"], "ultimate", "physical", "self", 0, 0, 50, 80, 150, "barrier health", 0, 700, 8,
                             "After a 0.8s warning you shout (7m): enemies are taunted for 4s and silenced for 1.5s; allies gain a {effect} barrier for 8s.",
                             [fx("taunt", "area", 4, radius=700), fx("silence", "area", 1.5, label="Silenced")], {"scaling": S("shield", 150, 4.0)}),
    # ================================================================ Totemic Behemoth (tank, STR)
    "behemoth_totem_sweep": ("Totem Sweep", ["tank"], "active", "earth", "aim", 0, 0, 25, 9, 40, "damage", 450, 450, 2,
                             "Sweep the totem through a warned 120-degree cone (4.5m): {effect} damage, enemies knocked back 2m and taunted for 2s.",
                             [fx("taunt", "area", 2, radius=450)], {"section": "control"}),
    "behemoth_tusk_line": ("Tuskbreaker", ["tank"], "active", "earth", "aim", 0, 0, 30, 14, 45, "damage", 800, 100, 1.5,
                           "Charge up to 8m down a warned lane, stopping at walls: every enemy in the lane takes {effect} damage and is slowed 40% for 1.5s.",
                           [fx("slow", "area", 1.5, 0.4, label="Move -40%")], {}),
    "behemoth_totem_bulwark": ("Totem Bulwark", ["tank"], "active", "earth", "aim", 0, 0, 30, 20, 400, "barrier health", 500, 180, 8,
                               "Brace the totem as a barricade (8s, {effect} health, 3.6m wide): it blocks enemy projectiles and movement, and allies behind it take 20% less damage.",
                               [], dict(CONSTRUCT, scaling=S("shield", 400, 5.0))),
    "behemoth_ancestral_weight": ("Ancestral Weight", ["tank"], "passive", "earth", "passive", 0, 0, 0, 0, 15, "% damage reduction", 0, 0, 0,
                                  "Stand still for 1.5s to root in the ancestors: {effect}% less damage taken and 50% more threat until you move.",
                                  [], {"curve": {"effectCap": 25}}),
    "behemoth_stampede": ("Ancestral Stampede", ["tank"], "ultimate", "earth", "aim", 0, 0, 50, 80, 100, "damage", 1200, 200, 1,
                          "After a 1s warning a spectral stampede crosses a 12m x 4m lane: {effect} damage, a 1s stun and every enemy thrown aside.",
                          [fx("stun", "area", 1.0, label="Stunned")], {}),
    # ================================================================ Drakish Footman (tank, STR)
    "drakish_dragon_oath": ("Dragon Oath", ["tank"], "active", "fire", "self", 0, 0, 35, 16, 30, "damage per slash", 1200, 350, 6,
                            "Take dragon form for up to 6s: your next two basic attacks become 120-degree cleaving slashes ({effect} damage each, 3.5m), then you breathe a fireball at the furthest enemy within 12m (40% of a slash) that taunts it for 2s, and return to human form.",
                            [], {"section": "attack"}),
    "drakish_scale_guard": ("Scale Guard", ["tank"], "active", "fire", "self", 0, 0, 20, 16, 35, "% damage reduction", 0, 0, 4,
                            "Dragon scales cover you for 4s: take {effect}% less damage and scorch melee attackers for 15 fire damage (scales with Primary).",
                            [fx("guard", "self", 4, 0.35, label="DEF +35%")], {"curve": {"effectCap": 50}}),
    "drakish_wing_rebuke": ("Wing Rebuke", ["tank"], "active", "fire", "aim", 0, 0, 25, 12, 30, "damage", 400, 400, 0,
                            "A warned wing sweep (120 degrees, 4m): {effect} damage, casts interrupted and enemies knocked back 3m away from your allies.",
                            [fx("interrupt", "area", 0, 0, 400, lockout=1.5, label="Interrupted")], {"section": "control"}),
    "drakish_ember_memory": ("Ember Memory", ["tank"], "passive", "fire", "passive", 0, 0, 0, 0, 20, "% of the hit as burn", 0, 0, 3,
                             "Your dragon slashes, fireballs, Wing Rebuke, Ancient Pact and every 3rd basic attack set the target burning for {effect}% of the hit over 3s.",
                             [], {"curve": {"effectCap": 35}}),
    "drakish_ancient_pact": ("Ancient Pact", ["tank"], "ultimate", "fire", "self", 0, 0, 50, 80, 80, "damage", 0, 500, 10,
                             "Land in full dragon form: after a 0.6s warning the landing circle (5m) deals {effect} fire damage. For 10s you and allies within 5m take 20% less damage, and your dragon form lasts the whole time with unlimited cleaving slashes.",
                             [fx("guard", "area", 10, 0.2, 500, label="DEF +20%")], {}),
    # ================================================================ Troll Berserkers (DPS, AGI)
    "troll_axe_frenzy": ("Axe Frenzy", ["dps"], "active", "physical", "enemy", 0, 0, 30, 9, 42, "damage per hit", 280, 0, 1.2,
                         "Commit to a 1.2s dual-axe frenzy: 5 rapid hits of {effect} damage on your target; you are slowed 50% while committed.",
                         [], {"scaling": S("damage", 30, 0.45)}),
    "troll_blood_leap": ("Bloodbound Leap", ["dps"], "active", "physical", "aim", 0, 0, 30, 12, 80, "damage", 700, 300, 0,
                         "Leap to a warned spot up to 7m away and cleave everything within 3m on landing for {effect} damage; a void rift at the landing stuns the centre and slows the ring.",
                         [], {"void": LANDING_RIFT}),
    "troll_hunger": ("Berserker Hunger", ["dps"], "passive", "physical", "passive", 0, 0, 0, 0, 5, "% attack speed per 10% missing health", 0, 0, 0,
                     "Gain {effect}% attack speed for every 10% of health you are missing (up to +40%).", [], {"curve": {"effectCap": 8}}),
    "troll_red_moon": ("Red Moon Frenzy", ["dps"], "ultimate", "physical", "self", 0, 0, 50, 75, 50, "% attack speed", 0, 250, 8,
                       "Red Moon Frenzy for 8s: +{effect}% attack speed, 15% lifesteal, and every basic attack also strikes a second enemy within 2.5m for half damage (melee cleave or split throw).",
                       [fx("haste", "self", 8, 0.5, label="ATK speed +50%")], {"curve": {"effectCap": 90}}),
    "troll_twin_throw": ("Twin Throw", ["dps"], "active", "physical", "aim", 0, 0, 25, 7, 90, "damage per axe", 1300, 30, 0,
                         "Throw two axes in a narrow fork (6 degrees apart): each strikes the first enemy in its path for {effect} damage.",
                         [], {"scaling": S("damage", 70, 1.1)}),
    "troll_returning_axes": ("Returning Axes", ["dps"], "active", "physical", "aim", 0, 0, 30, 11, 70, "damage per pass", 1100, 70, 0,
                             "Hurl an axe down a warned lane (11m x 1.4m) that returns to you: enemies take {effect} damage on the way out and again on the way back.",
                             [], {"scaling": S("damage", 55, 1.0)}),
    # ================================================================ Dryad (support, INT)
    "dryad_root_snare": ("Root Snare", ["heal"], "active", "nature", "aim", 0, 40, 0, 13, 45, "damage", 1000, 250, 2,
                         "Roots burst from a warned circle (2.5m) after 0.6s: {effect} damage and enemies are rooted for 2s.",
                         [fx("root", "area", 2, radius=250, label="Rooted")], {}),
    "dryad_seed_mend": ("Seed Mend", ["heal"], "active", "nature", "ally", 1.0, 40, 0, 8, 140, "healing", 1200, 0, 2,
                        "Cast 1s: plant a healing seed on an ally (or yourself); after 2s it blooms for {effect} healing, 50% more if they are below 40% health.",
                        [], {}),
    "dryad_thorn_line": ("Thornweave", ["heal", "dps"], "active", "nature", "aim", 0, 40, 0, 10, 20, "damage per second", 900, 90, 5,
                         "Weave a line of thorns (9m x 1.8m) for 5s: enemies on it take {effect} damage per second and are slowed 35%.",
                         [fx("slow", "area", 1, 0.35, label="Move -35%")], {"scaling": S("damage", 20, 0.35), "section": "control"}),
    "dryad_green_covenant": ("Green Covenant", ["heal"], "passive", "nature", "passive", 0, 0, 0, 0, 60, "reserve healing", 0, 1200, 0,
                             "20% of your effective healing is banked in a regeneration reserve (up to {effect}, scaling with Primary) that heals the most wounded ally within 12m every 2s.",
                             [], {"scaling": S("heal", 60, 2.0)}),
    "dryad_grove_renewal": ("Grove Renewal", ["heal"], "ultimate", "nature", "aim", 2.0, 120, 0, 85, 45, "healing per second", 1000, 600, 8,
                            "Cast 2s: grow a sanctuary of healing trees (6m) for 8s: living allies inside regenerate {effect} health per second and take 15% less damage.",
                            [fx("guard", "area", 8, 0.15, 600, label="DEF +15%")], {"scaling": S("heal", 45, 0.5)}),
    # ================================================================ Whisp (support, INT)
    "whisp_guiding_mote": ("Guiding Mote", ["heal"], "active", "arcane", "aim", 1.0, 35, 0, 6, 120, "healing", 1200, 60, 0,
                           "Cast 1s: send a mote down a line (12m); the first ally it reaches is healed for {effect}. With no ally in its path it bursts at the end, healing allies within 2m.",
                           [], {}),
    "whisp_spirit_tether": ("Spirit Tether", ["heal"], "active", "arcane", "ally", 0.5, 45, 0, 12, 36, "healing per second", 700, 0, 6,
                            "Cast 0.5s: tether an ally within 7m for 6s; they heal {effect} per second. The tether snaps if they move beyond 9m.",
                            [], {"scaling": S("heal", 30, 0.45)}),
    "whisp_fey_trail": ("Fey Trail", ["heal"], "active", "arcane", "aim", 0.8, 40, 0, 14, 60, "healing per mote", 800, 120, 6,
                        "Cast 0.8s: lay a luminous trail of 5 motes (8m) for 6s; an ally touching a mote consumes it and heals {effect}.",
                        [], {"scaling": S("heal", 60, 0.8), "section": "defensive"}),
    "whisp_lantern_soul": ("Lantern Soul", ["heal"], "passive", "arcane", "passive", 0, 0, 0, 0, 40, "% mana regeneration", 0, 600, 0,
                           "While you are within 6m of an ally below 60% health, your mana regenerates {effect}% faster.", [], {"curve": {"effectCap": 80}}),
    "whisp_constellation": ("Kindred Constellation", ["heal"], "ultimate", "arcane", "self", 1.5, 120, 0, 85, 70, "healing per pulse", 0, 900, 8,
                            "Cast 1.5s: link every living ally within 9m for 8s; each link pulses {effect} healing every 2s while the ally stays within 12m.",
                            [], {"scaling": S("heal", 60, 0.7)}),
    # ================================================================ Evergrove Centaur (support/DPS, INT)
    "centaur_grove_javelin": ("Grove Javelin", ["heal", "dps"], "active", "nature", "aim", 0, 35, 0, 8, 80, "damage", 1300, 30, 3,
                              "Hurl a root-tipped javelin: the first enemy hit takes {effect} damage and a healing bloom (2.5m) grows there for 3s, healing allies inside for 100% of the damage.",
                              [], {"section": "spell"}),
    "centaur_trailblaze": ("Evergrove Trail", ["heal"], "active", "nature", "aim", 0, 35, 0, 14, 30, "% move speed", 900, 140, 5,
                           "Blaze a warned path (9m x 2.8m) for 5s: allies on it move {effect}% faster.",
                           [fx("haste", "area", 5, 0.3, 140, label="Move +30%")], {"curve": {"effectCap": 50}}),
    "centaur_herd_call": ("Herd Call", ["heal", "dps"], "active", "nature", "aim", 0, 50, 0, 22, 18, "damage per hit", 600, 60, 15,
                          "Summon a spectral grove stag for 15s: it attacks your target for {effect} per hit.",
                          [], {"scaling": S("summon", 18, 0.5), "section": "summon"}),
    "centaur_steady_gait": ("Steady Gait", ["heal"], "passive", "nature", "passive", 0, 0, 0, 0, 30, "% next heal", 0, 0, 0,
                            "After 2s of moving without taking damage, your next heal is {effect}% stronger.", [], {"curve": {"effectCap": 50}}),
    "centaur_spring_march": ("Spring March", ["heal"], "ultimate", "nature", "self", 1.0, 110, 0, 80, 45, "healing per second", 0, 600, 8,
                             "Cast 1s: for 8s a grove aura travels with you (6m): allies inside regenerate {effect} health per second and move 15% faster.",
                             [fx("haste", "area", 8, 0.15, 600, label="Move +15%")], {"scaling": S("heal", 30, 0.45)}),
    # ================================================================ Keeper of Light (support, INT)
    "keeper_dawn_beam": ("Dawn Beam", ["heal"], "active", "holy", "aim", 1.2, 45, 0, 9, 190, "healing", 1000, 100, 4,
                         "Cast 1.2s: a warned beam of dawn (10m x 2m): allies in it are healed for {effect}; enemies take 40% of it as holy damage and receive 30% less healing for 4s.",
                         [fx("healCut", "area", 4.0, 0.3, label="Healing -30%")], {"section": "defensive"}),
    "keeper_lantern_ward": ("Lantern Ward", ["heal"], "active", "holy", "aim", 0, 45, 0, 18, 40, "barrier per pulse", 800, 450, 12,
                            "Place a destructible Lantern Ward (12s): every 3s allies within 4.5m gain a {effect} barrier (refreshed; barriers never stack).",
                            [fx("shield", "area", 0, 0, 450, label="Barrier")], dict(CONSTRUCT, scaling=S("shield", 40, 1.0))),
    "keeper_beacon": ("Beacon of Return", ["heal"], "active", "holy", "aim", 0, 35, 0, 16, 25, "% move speed", 1000, 400, 8,
                      "Raise a Beacon of Return (4m) for 8s: allies inside move {effect}% faster and regenerate 1.5% of their max health per second.",
                      [fx("haste", "area", 0, 0.25, 400, label="Move +25%")], {"curve": {"effectCap": 45}, "section": "defensive"}),
    "keeper_last_light": ("Last Light", ["heal"], "passive", "holy", "passive", 0, 0, 0, 0, 80, "barrier health", 0, 1200, 0,
                          "When an ally within 12m drops below 30% health they gain a {effect} barrier for 6s (once per ally every 20s).",
                          [], {"scaling": S("shield", 80, 2.0)}),
    "keeper_sunrise": ("Sunrise Vigil", ["heal"], "ultimate", "holy", "self", 2.0, 130, 0, 90, 90, "healing per second", 0, 900, 6,
                       "Cast 2s: a broad sunrise (9m) restores living allies for {effect} health per second for 6s and clears their slows.",
                       [fx("cleanse", "area", radius=900)], {"scaling": S("heal", 40, 0.6)}),
}


# ---------------------------------------------------------------------------------------------------
# Balance lab (Tools/RunChampionLab.py, Docs/BalanceFindings.md "kits-complete"): measured primary coefficients.
# Tanks are held to the tank damage median, healers to the healer HPS median, DPS to the DPS median (0.8-1.25x).
LAB_SCALING = {"bear_maul": (35, 0.8), "bear_roar": (20, 0.7), "bear_charge": (40, 0.8), "bear_colossus": (80, 1.6), "drakish_dragon_oath": (30, 0.7), "drakish_wing_rebuke": (30, 0.7), "drakish_ancient_pact": (80, 1.8), "golem_granite_fist": (28, 0.7), "golem_ether_anchor": (25, 0.7), "golem_worldstone": (60, 1.6), "behemoth_totem_sweep": (40, 0.8), "behemoth_tusk_line": (45, 0.9), "behemoth_stampede": (100, 2), "miner_pickfall": (110, 2.2), "miner_faultline": (130, 1.6), "miner_mountain": (180, 3), "chieftain_axe_hook": (200, 2.2), "paladin_righteous_flail": (90, 2), "troll_axe_frenzy": (42, 0.6), "troll_twin_throw": (90, 1.3), "troll_returning_axes": (70, 1.2), "golem_fel_fist": (65, 1), "golem_moss_bloom": (55, 0.8), "centaur_spring_march": (45, 0.6), "keeper_sunrise": (90, 1.1), "whisp_spirit_tether": (36, 0.55), "troll_blood_leap": (80, 1.6), "paladin_pilgrim_light": (50, 1.6), "keeper_dawn_beam": (190, 2.7), "whisp_constellation": (70, 0.8)}
for _sid, (_base, _coef) in LAB_SCALING.items():
    _row = KIT_SIGNATURES[_sid]
    _comp = (_row[16].get("scaling") or {}).get("component") or ("heal" if "heal" in _row[10] else "damage")
    _row[16]["scaling"] = S(_comp, _base, _coef)
