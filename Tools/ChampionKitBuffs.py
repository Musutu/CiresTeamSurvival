"""kits-complete: buff records applied by the roster-kit skills (CireKitSkills::BuffIds).

Each record gets a signature visual (Content/Data/BuffVisuals.json, merged by this script) and a modifier summary
for the HUD (Abilities.json "buffModifiers", merged by Tools/BuildAbilityDB.py): "DEF +20%", "ATK +15%"...
Sounds reuse existing pack-backed buff cues (Docs/BuffVisuals.md).

    python Tools/ChampionKitBuffs.py     # add missing BuffVisuals.json rows (hand edits to existing rows are kept)
"""
import json
from pathlib import Path


def L(shape, attach, **kw):
    return dict(shape=shape, attach=attach, **kw)


def M(type_, mods, line, control="none", kind=None, callout=True):
    row = {"type": type_, "control": control, "mods": [{"stat": s, "value": v, "unit": u} for s, v, u in mods], "line": line}
    if kind:
        row["kind"] = kind
    if not callout:
        row["callout"] = False
    return row


# id: (name, kind, school, primary rgb, layers, attack modifier or None, start sound, modifier row)
KIT_BUFFS = {
    "bear_hibernate": ("Ironroot Slumber", "stance", "nature", [0.45, 0.8, 0.3], [L("swirl", "ground", count=3, speed=0.4), L("motes", "body", style="leaf", count=10, speed=0.5)], None,
                       "buff.regeneration.start", M("magic", [("DEF", 20, "%"), ("HP Regen", 0, "")], "Channelling: healing each second and taking less damage. Moving ends it.", kind="stance")),
    "ancient_hide": ("Ancient Hide", "buff", "nature", [0.55, 0.42, 0.25], [L("plates", "body", count=6, speed=0.4), L("cracks", "ground", size=0.9)], None,
                     "npc.ward.start", M("physical", [("DEF", 25, "%")], "Hide hardened: less damage taken.", kind="buff")),
    "bear_colossus": ("Elder of the Deepwood", "buff", "nature", [0.7, 0.5, 0.25],
                      [L("tower", "body", size=1.2), L("motes", "body", style="leaf", count=14, speed=0.8), L("ring", "ground", style="runes", count=8, speed=0.4)],
                      {"swipe": "gold", "onHit": "sparks", "primary": [0.8, 0.55, 0.2]}, "buff.rallied.start",
                      M("physical", [("DEF", 30, "%"), ("Threat", 100, "%")], "Grown huge: less damage taken, longer reach, double threat.", kind="buff")),
    "bear_cowed": ("Cowed", "debuff", "nature", [0.6, 0.45, 0.3], [L("glyph", "overhead", style="exclamation", speed=0.8), L("ripple", "ground", style="shock", speed=1.0)], None,
                   "npc.feral.start", M("none", [("ATK", -25, "%")], "Roared at: deals less damage.")),
    "relic_mark": ("Relic Mark", "debuff", "holy", [1.0, 0.85, 0.4], [L("glyph", "overhead", style="crown", speed=0.6), L("ring", "ground", style="runes", count=6, speed=0.5)], None,
                   "npc.rune.start", M("magic", [("Damage Taken", 10, "%")], "Marked by the relic flail: takes more damage from the paladin.")),
    "relic_vow": ("Relic Vow", "buff", "holy", [1.0, 0.9, 0.55], [L("tether", "link", style="beam"), L("aegis", "body", speed=0.5)], None,
                  "item.oathshield.start", M("magic", [("Damage Taken", -30, "%")], "Bound to a paladin: a barrier, and 30% of the damage goes to the paladin.", kind="buff")),
    "mountain_heart": ("Heart of the Mountain", "buff", "earth", [0.62, 0.52, 0.4], [L("crystals", "ground", count=6, speed=0.3), L("plates", "body", count=4, speed=0.4)], None,
                       "npc.ward.start", M("physical", [("DEF", 25, "%")], "Inside the stone ring: less damage taken.", kind="buff")),
    "construct_core": ("Construct Core", "passive", "arcane", [0.5, 0.7, 1.0], [L("crystals", "overhead", count=4, speed=0.8), L("halo", "overhead", count=3, speed=0.5)], None,
                       "npc.rune.start", M("magic", [("ATK", 2, "%"), ("DEF", 1, "%")], "Core charges: more damage and less damage taken per charge.", kind="passive", callout=False)),
    "worldstone_tank": ("Worldstone: Bulwark", "buff", "arcane", [0.55, 0.65, 1.0], [L("shell", "body", speed=0.5), L("crystals", "ground", count=6, speed=0.3), L("halo", "overhead", count=4, speed=0.4)], None,
                        "buff.guarded.start", M("magic", [("DEF", 35, "%")], "Worldstone awakened: much less damage taken.", kind="buff")),
    "worldstone_bruiser": ("Worldstone: Fury", "buff", "fire", [1.0, 0.45, 0.2], [L("flames", "hands", count=4, speed=1.4), L("swirl", "body", count=2, speed=1.2)],
                           {"swipe": "ember", "onHit": "sparks"}, "buff.blood_rage.start", M("magic", [("ATK", 30, "%")], "Worldstone awakened: much more damage.", kind="buff")),
    "living_granite": ("Living Granite", "buff", "nature", [0.5, 0.75, 0.4], [L("plates", "body", count=6, speed=0.3), L("motes", "body", style="leaf", count=6, speed=0.4)], None,
                       "npc.ward.start", M("magic", [("HP Regen", 1.5, "%")], "A mossy barrier; regenerates health while it holds.", kind="buff")),
    "ether_furnace": ("Ether Furnace", "buff", "fire", [1.0, 0.5, 0.15], [L("flames", "hands", count=5, speed=1.6), L("motes", "body", style="ember", count=8, speed=1.2)],
                      {"swipe": "ember", "onHit": "splash"}, "buff.blood_rage.start", M("magic", [("ATK", 40, "%")], "Stoked: the next basic attacks burn and splash.", kind="buff")),
    "blood_oath_banner": ("Blood-Oath Banner", "aura", "blood", [0.85, 0.15, 0.12], [L("glyph", "overhead", style="horn", speed=0.6), L("ring", "ground", style="dashed", count=6, speed=1.0)],
                          {"swipe": "blood", "onHit": "sparks"}, "buff.rallied.start", M("none", [("ATK", 15, "%"), ("Haste", 10, "%")], "Near the banner: more damage and attack speed.", kind="buff")),
    "deep_lantern": ("Deep Lantern", "aura", "fire", [1.0, 0.75, 0.35], [L("halo", "overhead", count=6, speed=0.5), L("ring", "ground", style="runes", count=6, speed=0.3)], None,
                     "npc.ward.start", M("magic", [("DEF", 20, "%")], "In the lantern light: less damage taken.", kind="buff")),
    "ancestral_weight": ("Ancestral Weight", "stance", "earth", [0.6, 0.5, 0.35], [L("cracks", "ground", size=1.0), L("chains", "body", count=4, speed=0.3), L("glyph", "overhead", style="hourglass", speed=0.4)], None,
                         "stance.shield_wall.start", M("physical", [("DEF", 15, "%"), ("Threat", 50, "%")], "Rooted in the ancestors: less damage, more threat until you move.", kind="stance")),
    "dragon_form": ("Dragon Form", "stance", "fire", [1.0, 0.4, 0.1],
                    [L("flames", "body", count=6, speed=1.4), L("swirl", "body", count=2, speed=1.0), L("motes", "body", style="ember", count=10, speed=1.4)],
                    {"swipe": "ember", "onHit": "splash", "primary": [1.0, 0.35, 0.05]}, "stance.siege_fury.start",
                    M("magic", [("ATK", 0, "")], "Dragon form: basic attacks cleave, then a fireball.", kind="stance")),
    "scale_guard": ("Scale Guard", "buff", "fire", [0.9, 0.45, 0.2], [L("plates", "body", count=8, speed=0.6), L("flames", "ground", count=3, speed=0.8)], None,
                    "stance.shield_wall.start", M("magic", [("DEF", 35, "%")], "Dragon scales: less damage; melee attackers are scorched.", kind="buff")),
    "ancient_pact": ("Ancient Pact", "aura", "fire", [1.0, 0.55, 0.2], [L("aegis", "body", speed=0.5), L("flames", "ground", count=4, speed=0.6), L("halo", "overhead", count=6, speed=0.5)], None,
                     "buff.mass_aegis.start", M("magic", [("DEF", 20, "%")], "Under the dragon's pact: less damage taken.", kind="buff")),
    "red_moon": ("Red Moon Frenzy", "buff", "blood", [0.95, 0.1, 0.12],
                 [L("swirl", "body", count=3, speed=1.6), L("halo", "overhead", count=1, style="blood", speed=0.4), L("pool", "ground", size=0.8)],
                 {"swipe": "blood", "onHit": "splash", "primary": [1.0, 0.1, 0.1]}, "buff.blood_rage.start",
                 M("physical", [("Haste", 50, "%"), ("Lifesteal", 15, "%")], "Frenzy: faster attacks, lifesteal and cleaving blows.", kind="buff")),
    "seed_mend": ("Seed Mend", "buff", "nature", [0.4, 0.9, 0.35], [L("motes", "body", style="leaf", count=6, speed=0.6), L("glyph", "overhead", style="plus", speed=0.5)], None,
                  "buff.regeneration.start", M("magic", [("HP Regen", 0, "")], "A healing seed is about to bloom.", kind="buff")),
    "grove_renewal": ("Grove Renewal", "aura", "nature", [0.35, 0.85, 0.35], [L("motes", "body", style="leaf", count=10, speed=0.5), L("ring", "ground", style="runes", count=6, speed=0.3)], None,
                      "buff.regeneration.start", M("magic", [("DEF", 15, "%")], "In the healing grove: health each second, less damage taken.", kind="buff")),
    "spirit_tether": ("Spirit Tether", "buff", "spirit", [0.5, 0.85, 1.0], [L("tether", "link", style="beam"), L("motes", "body", style="glint", count=8, speed=0.8)], None,
                      "buff.blessing.start", M("magic", [("HP Regen", 0, "")], "Tethered to a whisp: healing each second while in range.", kind="buff")),
    "kindred_link": ("Kindred Constellation", "buff", "spirit", [0.7, 0.8, 1.0], [L("tether", "link", style="beam"), L("glyph", "overhead", style="stars", speed=0.5)], None,
                     "buff.blessing.start", M("magic", [("HP Regen", 0, "")], "Linked: a heal pulse every 2 seconds.", kind="buff")),
    "evergrove_trail": ("Evergrove Trail", "buff", "nature", [0.5, 0.9, 0.45], [L("ripple", "ground", style="chevron_up", speed=1.4), L("motes", "body", style="leaf", count=6, speed=1.2)], None,
                        "npc.rune.start", M("magic", [("Move", 30, "%")], "On the grove path: moving faster.", kind="buff")),
    "steady_gait": ("Steady Gait", "passive", "nature", [0.55, 0.85, 0.5], [L("motes", "hands", style="leaf", count=4, speed=0.8)], None,
                    "buff.blessing.start", M("magic", [("Healing", 30, "%")], "Your next heal is stronger.", kind="buff")),
    "spring_march": ("Spring March", "aura", "nature", [0.45, 0.95, 0.5], [L("ripple", "ground", style="chevron_up", speed=1.2), L("motes", "overhead", style="leaf", count=6, speed=1.0), L("glyph", "overhead", style="leaf", speed=0.5)], None,
                     "buff.regeneration.start", M("magic", [("Move", 15, "%")], "In the grove aura: healing and faster movement.", kind="buff")),
    "beacon_of_return": ("Beacon of Return", "aura", "holy", [1.0, 0.9, 0.5], [L("tower", "ground", size=0.6), L("ripple", "ground", style="chevron_up", speed=1.0)], None,
                         "buff.blessing.start", M("magic", [("Move", 25, "%"), ("HP Regen", 1, "%")], "At the beacon: faster movement and regeneration.", kind="buff")),
    "sunrise_vigil": ("Sunrise Vigil", "buff", "holy", [1.0, 0.85, 0.45], [L("halo", "overhead", count=8, speed=0.6), L("motes", "body", style="glint", count=10, speed=0.6)], None,
                      "buff.blessing.start", M("magic", [("HP Regen", 0, "")], "Sunrise: healing each second.", kind="buff")),
    "kit_burning": ("Burning", "debuff", "fire", [1.0, 0.45, 0.1], [L("flames", "body", count=4, speed=1.6), L("glyph", "overhead", style="ember", speed=0.8)], None,
                    "debuff.poisoned.start", M("magic", [("HP Regen", 0, "")], "On fire: damage each second.")),
    "axe_frenzy": ("Axe Frenzy", "stance", "blood", [0.9, 0.2, 0.15], [L("weapon", "weapon", speed=1.8), L("swirl", "body", count=2, speed=2.0)],
                   {"swipe": "blood", "onHit": "splash"}, "stance.blood_frenzy.start", M("physical", [("Move", -50, "%")], "Committed to a flurry of axe blows.", kind="stance")),
    "tumbling_mend": ("Tumbling Mend", "buff", "holy", [0.6, 1.0, 0.6], [L("motes", "body", style="plus", count=8, speed=0.6), L("ring", "ground", style="runes", count=4, speed=0.4)], None,
                      "buff.regeneration.start", M("magic", [("HP Regen", 0, "")], "Healing over time after a roll.", kind="buff")),
}
# Buffs with an attack modifier also expose a hit cue (existing pack-backed cues).
HIT_SOUNDS = {"bear_colossus": "buff.war_cry.start", "worldstone_bruiser": "buff.blood_rage.hit", "ether_furnace": "buff.frost_weapon.hit",
              "blood_oath_banner": "buff.blessing.hit", "dragon_form": "stance.blood_frenzy.hit", "red_moon": "buff.blood_rage.hit", "axe_frenzy": "stance.blood_frenzy.hit"}
BUFF_MODIFIER_ROWS = {bid: dict(row[7], name=row[0]) for bid, row in KIT_BUFFS.items()}


def merge_buff_visuals(root):
    path = root / "Content/Data/BuffVisuals.json"
    text = path.read_text(encoding="utf-8")
    data = json.loads(text)
    lines = []
    for bid, (name, kind, school, primary, layers, attack, sound, _mod) in KIT_BUFFS.items():
        if bid in data["buffs"]:
            continue
        secondary = [round(c * 0.4, 2) for c in primary]
        core = [round(min(1.0, c * 0.5 + 0.5), 2) for c in primary]
        row = {"name": name, "kind": kind, "school": school, "source": "kits-complete: " + name + " (CireKitSkills)",
               "palette": {"primary": primary, "secondary": secondary, "core": core}, "priority": 62 if kind != "passive" else 30,
               "layers": layers, "lifecycle": {"burstSeconds": 0.4, "fadeSeconds": 0.35}, "sound": {"start": sound, "end": "buff.expire"}}
        if bid in HIT_SOUNDS:
            row["sound"]["hit"] = HIT_SOUNDS[bid]
        if attack:
            row["attack"] = attack
        lines.append(f'    "{bid}": ' + json.dumps(row, ensure_ascii=False))
    if lines:
        # One row per line like the rest of the file: append inside "buffs" (the last object).
        end = text.rstrip()
        assert end.endswith("}"), "unexpected BuffVisuals.json layout"
        end = end[:-1].rstrip()
        assert end.endswith("}"), "unexpected BuffVisuals.json layout"
        end = end[:-1].rstrip()
        text = end + ",\n" + ",\n".join(lines) + "\n  }\n}\n"
        json.loads(text)
        path.write_text(text, encoding="utf-8")
    return len(lines)


if __name__ == "__main__":
    print("buff visual rows added:", merge_buff_visuals(Path(__file__).resolve().parent.parent))
