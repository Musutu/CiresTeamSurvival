"""Build Content/Data/FabVFX.json from the Niagara systems in the locally installed Fab VFX packs.

Scans <main checkout>/Content/<PackFolder>/**/NS_*.uasset (plus FX_/P_ names that look like systems),
classifies each by school (fire, frost, storm, shadow, holy, life, poison, arcane, earth, nature, tide,
blood, spirit, void, steel) and role (cast, projectile, impact, area, aura) from its path tokens, and writes
ordered candidate lists. Runtime (CireFabVFX) uses the first candidate that exists and loads as a
UNiagaraSystem, so emitters or a wrong guess simply fall through to the next one or to the procedural art.

Hand edits survive: an entry with "locked": true is never rewritten.

fab-coverage: Tools/FabAbilityVFXTable.py adds "abilities.<id>.<role>" (one signature system per champion ability and
role, tried before the school set) and one state overlay per BuffVisuals effect. Kakky FX Variety Pack (FXVarietyPack)
ships Cascade P_ky_* systems; CireFabVFX spawns those as well as Niagara.

Usage (any Python 3):
  python Tools/MapFabVFX.py            # write Content/Data/FabVFX.json
  python Tools/MapFabVFX.py --report   # print what it found, write nothing
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "Content" / "Data" / "FabVFX.json"
MANIFEST = REPO / "Art" / "Fab" / "PurchasedPacks.json"

SCHOOLS = [  # order matters: first match wins for a token
    ("void", r"void|abyss|portal_dark"),
    ("shadow", r"shadow|dark|curse|necro|death|evil|demon|soul_?drain|umbra"),
    ("holy", r"holy|divine|angel|golden|gold|sun|light(?!ning)|sacred|bless|radiant"),
    ("life", r"heal|life|regen|restor|renew"),
    ("fire", r"fire|flame|burn|ember|lava|magma|meteor|inferno|pyro"),
    ("frost", r"ice|frost|snow|freez|cold|blizzard|glacier|crystal_ice"),
    ("storm", r"lightning|thunder|electric|storm|spark|shock|air|wind|tornado|volt"),
    ("poison", r"poison|toxic|acid|venom|plague|gas"),
    ("nature", r"nature|vine|leaf|leaves|plant|wood|thorn|root|flower|druid|grow"),
    ("earth", r"earth|rock|stone|sand|ground|boulder|dust|quake|mud"),
    ("tide", r"water|wave|splash|bubble|tide|aqua|rain"),
    ("blood", r"blood|gore|bleed"),
    ("spirit", r"spirit|ghost|wisp|ethereal|phantom"),
    ("arcane", r"arcane|magic|mana|rune|mystic|astral|star|cosmic|purple"),
]
ROLES = [
    ("impact", r"hit|impact|explo|blast|burst|splat|crash|land"),
    ("projectile", r"projectile|bolt|ball|missile|arrow|orb|bullet|shot|spear|lance|dart|shard"),
    ("aura", r"aura|buff|debuff|state|shield|barrier|armor|armour|body|loop|status|curse_on|stun|freeze_on|burning"),
    ("area", r"aoe|area|zone|field|circle|pool|nova|rain|storm_area|ground|wall|tornado|meteor_rain|pillar"),
    ("cast", r"cast|charge|muzzle|spawn|summon|hand|channel|beam|breath|slash|swing"),
]
# Pack preference inside a slot (listed first = preferred).
PACK_ORDER = ["BigPack", "Big_Pack", "MagicEffects", "ShadowMagic", "Shadow", "Earth", "Nature", "Vine", "State", "Blood"]


def git_main() -> Path:
    out = subprocess.run(["git", "worktree", "list", "--porcelain"], cwd=REPO, capture_output=True, text=True, check=True).stdout
    return Path(out.splitlines()[0].split(" ", 1)[1].strip())


def pack_folders(main: Path) -> list[Path]:
    content = main / "Content"
    tracked = set(subprocess.run(["git", "ls-files", "Content"], cwd=main, capture_output=True, text=True).stdout.split("\n"))
    tracked_dirs = {t.split("/")[1] for t in tracked if t.count("/") >= 2}
    skip = tracked_dirs | {"Collections", "Developers", "Fab", "FabDerived"}
    return [p for p in sorted(content.iterdir()) if p.is_dir() and p.name not in skip]


def classify(rel: str):
    tokens = re.sub(r"([a-z])([A-Z])", r"\1_\2", rel).lower()
    school = next((s for s, rx in SCHOOLS if re.search(rx, tokens)), None)
    role = next((r for r, rx in ROLES if re.search(rx, Path(rel).stem.lower() + "_" + tokens.rsplit("/", 1)[-1])), None)
    if role is None:
        role = next((r for r, rx in ROLES if re.search(rx, tokens)), None)
    return school, role


def pack_rank(path: str) -> int:
    for i, key in enumerate(PACK_ORDER):
        if key.lower() in path.lower():
            return i
    return len(PACK_ORDER)


def scan(main: Path):
    found = []
    for pack in pack_folders(main):
        for f in pack.rglob("*.uasset"):
            stem = f.stem
            if not re.match(r"(NS|FX|P|VFX|Niagara)_", stem, re.I):
                continue
            if re.match(r"(NE|NFX_Emitter|M|MI|T|SM|SK)_", stem):
                continue
            rel = f.relative_to(main / "Content").with_suffix("").as_posix()
            obj = "/Game/%s.%s" % (rel, stem)
            school, role = classify(rel)
            found.append({"path": obj, "rel": rel, "school": school, "role": role})
    return found


# Curated picks by exact system name (Lord Enot "Big Pack Magic Effects" / "Shadow Magic" naming:
# NS_<Element>_Magic_<Type>). Tried before the keyword heuristic; first existing stem wins per slot.
CURATED = {
    "fire":   {"projectile": ["Fire_Magic_Projectile", "Fire_Magic_Orb"], "impact": ["Fire_Magic_Hit", "Fire_Magic_Explosion"],
               "cast": ["Fire_Magic_Muzzle"], "area": ["Fire_Magic_AOE", "Fire_Magic_Circle"], "aura": ["Fire_Magic_Aura"]},
    "frost":  {"projectile": ["Ice_Magic_Projectile", "Ice_Magic_Spear"], "impact": ["Ice_Magic_Hit", "Ice_Magic_Splash"],
               "cast": ["Ice_Magic_Muzzle"], "area": ["Ice_Magic_Circle1", "Ice_Magic_Snowstorm"], "aura": ["Ice_Magic_Aura"]},
    "storm":  {"projectile": ["Lightning_Magic_Projectile1", "Lightning_Magic_Orb"], "impact": ["Lightning_Magic_Tunder", "Lightning_Magic_Shockwave"],
               "cast": ["Lightning_Magic_Blink1"], "area": ["Lightning_Magic_Area", "Lightning_Magic_Tunder_Area"], "aura": ["Lightning_Magic_Aura1"]},
    "shadow": {"projectile": ["Shadow_Magic_Projectile1", "Shadow_Magic_Orb1"], "impact": ["Shadow_Magic_Hit1", "Shadow_Magic_Explosion1"],
               "cast": ["Shadow_Magic_Blink1"], "area": ["Shadow_Magic_Area1", "Shadow_Magic_Circle1"], "aura": ["Shadow_Magic_Aura1"]},
    "void":   {"projectile": ["Dark_Magic_Projectile1", "Dark_Magic_Orb"], "impact": ["Dark_Magic_Hit", "Dark_Flame_Burst"],
               "cast": ["Dark_Magic_Blink1"], "area": ["Dark_Magic_AOE", "Dark_Magic_Circle"], "aura": ["Dark_Magic_Aura"]},
    "holy":   {"projectile": ["Light_Magic_Projectile1", "Light_Magic_Orb2"], "impact": ["Light_Magic_Hit1", "Light_Magic_Explosion1"],
               "cast": ["Light_Magic_Top", "Light_Magic_Blink1"], "area": ["Light_Magic_AOE1", "Light_Magic_Circle"], "aura": ["Light_Magic_Aura"]},
    "life":   {"cast": ["Light_Magic_Heal"], "impact": ["Light_Magic_Heal_Hit"], "area": ["Light_Magic_Top_Area", "Light_Magic_Circle"],
               "aura": ["Light_Magic_Buff"], "projectile": ["Light_Magic_Orb2"]},
    "poison": {"projectile": ["Posion_Magic_Projectile1", "Posion_Magic_Orb"], "impact": ["Posion_Magic_Hit", "Posion_Magic_Explosion1"],
               "cast": ["Posion_Magic_Target"], "area": ["Posion_Magic_Area1", "Posion_Magic_AreaWave"], "aura": ["Posion_Magic_Aura"]},
    "earth":  {"projectile": ["Earth_Magic_Projectile", "Earth_Magic_Stone1"], "impact": ["Earth_Magic_Hit", "Earth_Magic_Splash"],
               "cast": ["Earth_Magic_Muzzle"], "area": ["Earth_Magic_Circle1", "Earth_Magic_Shockwave"], "aura": ["Earth_Magic_Aura"]},
    "tide":   {"projectile": ["Water_Magic_Projectile1", "Water_Magic_Orb"], "impact": ["Water_Magic_Hit", "Water_Magic_Splash1"],
               "cast": ["Water_Magic_Muzzle"], "area": ["Water_Magic_Area1", "Water_Magic_Shockwave"], "aura": ["Water_Magic_Aura"]},
    "blood":  {"projectile": ["Blood_Magic_Projectile1", "Blood_Magic_Orb"], "impact": ["Blood_Magic_Hit", "Blood_Magic_Explo"],
               "cast": ["Blood_Magic_Muzzle"], "area": ["Blood_Magic_Area1"], "aura": ["Blood_Magic_Aura"]},
    "arcane": {"projectile": ["Air_Magic_Projectile1", "Air_Magic_Orb"], "impact": ["Air_Magic_Hit1", "Air_Magic_Splash"],
               "cast": ["Air_Magic_Muzzle1"], "area": ["Air_Magic_AOE"], "aura": ["Air_Magic_Aura"]},
    "spirit": {"projectile": ["Air_Magic_Air_Ball"], "impact": ["Air_Magic_Hit2"], "cast": ["Air_Magic_Muzzle2"], "aura": ["Air_Magic_Buff"]},
}
# Dedicated packs lead their school (tried first, Lord Enot stays as the fallback candidate).
DEDICATED = {
    "earth": {"projectile": ["Earth_Spells_Projectile1"], "impact": ["Earth_Spells_Hit1"], "cast": ["Earth_Spells_Attack_Up"],
              "area": ["Earth_Spells_Circle"], "aura": ["Earth_Spells_Aura"]},
    "nature": {"projectile": ["Ribbon_Nature"], "impact": ["Explosion_Small_Nature"], "cast": ["Explosion_Cast_Nature"],
               "area": ["AreaBuff"], "aura": ["Aura_Nature"]},
    "life": {"area": ["AreaBuff"]},
    # Physical hits: restrained realistic blood (low intensity) - dark-fantasy, not splatter.
    # fab-coverage: steel also gets a crisp white wind slash on the caster and a wind arrow for thrown/shot weapons,
    # so physical skills without their own signature are not bare procedural (one steel slot existed before).
    "steel": {"impact": ["Slash_Low", "BloodBurst_Low"], "cast": ["Air_Magic_Slash1"], "projectile": ["Air_Magic_Arrow1"]},
    "blood": {"impact": ["BloodBurst_Med"]},
}
# Exact status effects (BuffVisuals ids) -> State VFX Niagara loops.
CURATED_STATES = {
    "npc_rooted": "State_VFX_Root1", "stunned": "Stun1", "interrupted": "Stun1", "silenced": "State_VFX_Silence1",
    "npc_silenced": "State_VFX_Silence1", "slowed": "State_VFX_Slow1", "frost_bind": "State_VFX_Freeze1",
    "poisoned": "State_VFX_Poison1", "npc_spores": "State_VFX_Poison1", "npc_dragonfire": "State_VFX_Burn1",
    "healing_cut": "State_VFX_Cursed1", "heal_cut_done": "State_VFX_Cursed1", "bounty_mark": "State_VFX_Cursed1",
    "witch_mark": "State_VFX_Cursed1", "npc_profane": "State_VFX_Cursed1", "npc_mind": "State_VFX_Charm1",
    "npc_ink": "State_VFX_Blind1", "regeneration": "State_VFX_Heal1", "mana_restore": "State_VFX_Mana1",
    "npc_aether": "State_VFX_Shock1", "overcharge": "State_VFX_Shock1", "npc_feral": "State_VFX_Bleed1",
}
# Per-effect-kind state overlays (BuffVisuals kind.school): buffs use <Element>_Buff, debuffs <Element>_Debuff.
CURATED_BUFFS = {
    "buff.fire": ["Fire_Magic_Buff"], "buff.frost": ["Ice_Magic_Buff"], "buff.storm": ["Lightning_Magic_Buff1"],
    "buff.shadow": ["Shadow_Magic_Buff1"], "buff.void": ["Dark_Magic_Buff"], "buff.holy": ["Light_Magic_Buff"],
    "buff.life": ["Light_Magic_Buff"], "buff.poison": ["Posion_Magic_Buff"], "buff.earth": ["Earth_Magic_Buff"],
    "buff.tide": ["Water_Magic_Buff"], "buff.blood": ["Blood_Magic_Buff"], "buff.arcane": ["Air_Magic_Buff"],
    "debuff.shadow": ["Dark_Magic_Debuff"], "debuff.void": ["Dark_Magic_Debuff"], "debuff.poison": ["Posion_Magic_Debuff"],
    "debuff.blood": ["Blood_Magic_Debuff"], "debuff.frost": ["Ice_Magic_Frozen"], "debuff.fire": ["Dark_Magic_DOT"],
}


def build(found, existing):
    by_stem = {}
    for f in found:
        by_stem.setdefault(f["path"].rsplit(".", 1)[1].replace("NS_", "", 1), f["path"])
    curated = {}
    for school, roles in DEDICATED.items():
        for role, stems in roles.items():
            paths = [by_stem[x] for x in stems if x in by_stem]
            if paths:
                curated[(school, role)] = paths
    for school, roles in CURATED.items():
        for role, stems in roles.items():
            paths = [by_stem[x] for x in stems if x in by_stem]
            if paths:
                curated[(school, role)] = curated.get((school, role), []) + [p for p in paths if p not in curated.get((school, role), [])]
    slots = defaultdict(list)
    for f in found:
        if f["school"] and f["role"]:
            slots[(f["school"], f["role"])].append(f["path"])
    schools = {}
    for (school, role), paths in sorted(slots.items()):
        paths = sorted(set(paths), key=lambda p: (pack_rank(p), len(p), p))[:4]
        schools.setdefault(school, {})[role] = {"paths": paths, "scale": 1.0}
    role_scale = {"impact": 1.35, "cast": 1.0, "projectile": 1.0, "area": 1.0, "aura": 1.0}  # tuned on the gameplay camera
    for (school, role), paths in curated.items():
        schools.setdefault(school, {})[role] = {"paths": paths, "scale": role_scale.get(role, 1.0)}
    # Physical hits: blood on flesh reads best in the dark-fantasy tone.
    if "steel" in schools and "impact" in schools["steel"]:
        schools["steel"]["impact"]["scale"] = 0.9  # restrained: a weapon hit, not a gore spray
    if ("steel", "impact") not in curated:
        schools.pop("steel", None)  # never glowing blood-magic on plain weapon hits
    # Heuristic slots for schools the curated table covers are dropped: curated picks are reviewed.
    for school in list(schools):
        if school in CURATED or school in DEDICATED:
            schools[school] = {r: e for r, e in schools[school].items() if (school, r) in curated}
    # Heals borrow holy/nature art when there is no dedicated life set.
    for role in ("cast", "impact", "aura", "area"):
        for donor in ("holy", "nature"):
            if role in schools.get(donor, {}):
                schools.setdefault("life", {}).setdefault(role, dict(schools[donor][role]))
                break
    buffs = {}
    for school, roles in schools.items():
        if "aura" in roles:
            buffs["buff." + school] = dict(roles["aura"])
            buffs["debuff." + school] = dict(roles["aura"])
    for key, stem in CURATED_STATES.items():
        if stem in by_stem:
            buffs[key] = {"paths": [by_stem[stem]], "scale": 1.0}
    for key, stems in CURATED_BUFFS.items():
        paths = [by_stem[s] for s in stems if s in by_stem]
        if paths:
            buffs[key] = {"paths": paths, "scale": 1.0}
    # Keep locked hand edits.
    for school, roles in (existing.get("schools") or {}).items():
        for role, entry in roles.items():
            if isinstance(entry, dict) and entry.get("locked"):
                schools.setdefault(school, {})[role] = entry
    for key, entry in (existing.get("buffs") or {}).items():
        if isinstance(entry, dict) and entry.get("locked"):
            buffs[key] = entry
    return schools, buffs


def build_abilities(found, existing):
    """fab-coverage: per-ability signatures and per-buff overlays from Tools/FabAbilityVFXTable.py."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("FabAbilityVFXTable", str(Path(__file__).with_name("FabAbilityVFXTable.py")))
    table = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(table)
    by_stem = {}
    for f in found:
        by_stem.setdefault(f["path"].rsplit(".", 1)[1].replace("NS_", "", 1), f["path"])
    missing = []

    def entry(value, role):
        stem, scale = (value if isinstance(value, tuple) else (value, None))
        if stem not in by_stem:
            missing.append(stem)
            return None
        path = by_stem[stem]
        if scale is None:
            scale = table.ROLE_SCALE.get(role, 1.0) * (table.CASCADE_SCALE if stem.startswith("P_ky_") else 1.0)
        return {"paths": [path], "scale": round(scale, 3)}
    abilities = {}
    for ability, roles in table.ABILITY_VFX.items():
        row = {}
        for key, value in roles.items():
            e = entry(value, table.ROLE[key])
            if e:
                row[table.ROLE[key]] = e
        if row:
            abilities[ability] = row
    buffs = {}
    for buff, value in table.BUFF_VFX.items():
        e = entry(value, "aura")
        if e:
            buffs[buff] = e
    for ability, roles in (existing.get("abilities") or {}).items():
        for role, e in roles.items():
            if isinstance(e, dict) and e.get("locked"):
                abilities.setdefault(ability, {})[role] = e
    return abilities, buffs, sorted(set(missing))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--report", action="store_true")
    args = ap.parse_args()
    main_tree = git_main()
    found = scan(main_tree)
    existing = json.loads(OUT.read_text(encoding="utf-8")) if OUT.exists() else {}
    schools, buffs = build(found, existing)
    abilities, ability_buffs, missing_stems = build_abilities(found, existing)
    for key, e in ability_buffs.items():
        if not (isinstance(buffs.get(key), dict) and buffs[key].get("locked")):
            buffs[key] = e
    print("ability signatures: %d abilities, %d role slots; buff overlays: %d" % (
        len(abilities), sum(len(r) for r in abilities.values()), len(buffs)))
    for stem in missing_stems:
        print("  missing stem (pack not installed or renamed): " + stem)
    unclassified = [f["rel"] for f in found if not (f["school"] and f["role"])]
    print("systems found: %d, classified: %d, slots: %d, buff rows: %d" % (
        len(found), len(found) - len(unclassified), sum(len(r) for r in schools.values()), len(buffs)))
    for school in sorted(schools):
        print("  %-7s %s" % (school, ", ".join("%s(%d)" % (r, len(e["paths"])) for r, e in sorted(schools[school].items()))))
    if args.report:
        for u in unclassified[:60]:
            print("  ? " + u)
        return 0
    data = {
        "schemaVersion": 1,
        "notes": existing.get("notes", ""),
        "schools": schools,
        "buffs": buffs,
        "abilities": abilities,
    }
    OUT.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    print("wrote " + str(OUT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
