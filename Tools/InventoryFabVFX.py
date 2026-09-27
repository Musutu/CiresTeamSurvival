"""Inventory of every Niagara / Cascade system in the purchased Fab VFX packs (pack-usage).

Scans <main checkout>/Content/<pack> for NS_* / P_ky_* systems and writes Art/Fab/FabVFXInventory.json: one row per
system with its pack, sub-kit (element folder), a role/school guess from the name, and the reviewed fields that drive
the mapping (Tools/FabAbilityVFXTable.py picks by rating, Eric's rule: quality first, reuse the good systems with a
recolour rather than falling back to a weaker one):

  quality   A (hero-grade: use it, reuse it with tints) | B (solid, fits its slot) | C (filler: only when nothing better fits)
  look      one line describing what it draws (from the catalogue sheets, Tools/RunFabVFXCatalog.py)
  reach     measured XY reach (cm at scale 1) from the catalogue run (CIRE_FAB_CATALOG_REACH)
  colors    exposed colour user parameters (Lord Enot "Color_<part>" and friends) - empty = not recolourable

Hand-edited fields (quality, look, tags) are kept across runs; only paths, colors and reach are refreshed.
Only object paths are stored: the packs are licensed and never committed.

  python Tools/InventoryFabVFX.py                    # rescan, keep reviewed fields
  python Tools/InventoryFabVFX.py --catalog DIR      # also fold in reach/colors from a RunFabVFXCatalog gallery.log
  python Tools/InventoryFabVFX.py --summary          # counts per pack and quality
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
from collections import Counter, OrderedDict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "Art" / "Fab" / "FabVFXInventory.json"
PACKS = OrderedDict([
    ("Big_Pack_Magic_VFX", "Big Pack Magic Effects (Lord Enot)"),
    ("Shadow_Magic", "Shadow Magic (Lord Enot)"),
    ("Earth_Spells", "49 Earth Spells (Lord Enot)"),
    ("Forest_VFX", "Forest / Nature VFX"),
    ("State_VFX", "State VFX (status loops)"),
    ("FXVarietyPack", "Kakky FX Variety Pack (Cascade)"),
    ("RealisticBlood", "Realistic Blood"),
])
ROLE_GUESS = [
    ("impact", r"hit|impact|explo|burst|splash|splat|crash|land|splatter"),
    ("projectile", r"projectile|bolt|ball|missile|arrow|orb|bullet|shot|spear|lance|dart|shard|grenade|ribbon"),
    ("aura", r"aura|buff|debuff|state|shield|barrier|armor|armour|loop|status|frozen|dot|hide|wing"),
    ("area", r"aoe|area|zone|field|circle|pool|nova|rain|ground|wall|tornado|meteor|pillar|arena|storm|line|cone|spike|flow|wave|top|beam|laser|ray"),
    ("cast", r"cast|charge|muzzle|spawn|summon|hand|channel|breath|slash|swing|blink|dash|attack|target|marker|up|step"),
]
SCHOOL_GUESS = [
    ("shadow", r"shadow"), ("void", r"dark_"), ("holy", r"light_"), ("fire", r"fire"), ("frost", r"ice_"), ("storm", r"lightning|thunder|shot"),
    ("poison", r"posion|poison"), ("earth", r"earth"), ("tide", r"water|aqua"), ("blood", r"blood|artery|amputat|brain|dripping|slash_|stab_|bullethit|splatter"),
    ("arcane", r"air_|magiccircle|shootingstar|laser"), ("nature", r"nature|forest|vine|wood|leaf|heal"), ("state", r"state_vfx|stun"),
]


def main_checkout() -> Path:
    out = subprocess.run(["git", "worktree", "list", "--porcelain"], cwd=REPO, capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith("worktree "):
            return Path(line[9:].strip())
    return REPO


def guess(rx_table, text):
    text = text.lower()
    return next((k for k, rx in rx_table if re.search(rx, text)), "")


def scan(main: Path):
    rows = []
    for pack in PACKS:
        root = main / "Content" / pack
        if not root.is_dir():
            continue
        for f in sorted(root.rglob("*.uasset")):
            stem = f.stem
            if not re.match(r"(NS|P_ky)_", stem) or re.match(r"(NE|NFX)_", stem):
                continue
            if "/Demo/" in f.as_posix() or "/_Blueprints/" in f.as_posix():
                continue
            rel = f.relative_to(main / "Content").with_suffix("").as_posix()
            parts = f.relative_to(root).parts
            subkit = parts[1] if pack == "Big_Pack_Magic_VFX" and len(parts) > 2 else (parts[0] if len(parts) > 1 else pack)
            name = stem.replace("NS_", "", 1)
            rows.append(OrderedDict([
                ("stem", name), ("path", "/Game/%s.%s" % (rel, stem)), ("pack", pack), ("subkit", subkit),
                ("kind", "cascade" if stem.startswith("P_") else "niagara"),
                ("role", guess(ROLE_GUESS, name)), ("school", guess(SCHOOL_GUESS, rel)),
                ("base", name.endswith("_Base")),
            ]))
    return rows


def parse_catalog(directory: Path):
    """CIRE_FAB_CATALOG_ITEM index=.. path=.. colors=a|b  and  CIRE_FAB_CATALOG_REACH index=.. reach=.."""
    log = directory / "gallery.log" if directory.is_dir() else directory
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    by_index, out = {}, {}
    for m in re.finditer(r"CIRE_FAB_CATALOG_ITEM index=(\d+) path=(\S+) colors=(\S+)", text):
        by_index[int(m.group(1))] = m.group(2)
        out[m.group(2)] = {"colors": [] if m.group(3) in ("-", "cascade") else m.group(3).split("|")}
    for m in re.finditer(r"CIRE_FAB_CATALOG_REACH index=(\d+) reach=(\d+)", text):
        path = by_index.get(int(m.group(1)))
        if path:
            out.setdefault(path, {})["reach"] = int(m.group(2))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--catalog", type=Path, help="RunFabVFXCatalog capture directory (or its gallery.log) to fold in reach/colors")
    ap.add_argument("--summary", action="store_true")
    args = ap.parse_args()
    previous = json.loads(OUT.read_text(encoding="utf-8")) if OUT.exists() else {}
    prev_rows = {r["path"]: r for r in previous.get("systems", [])}
    rows = scan(main_checkout())
    measured = parse_catalog(args.catalog) if args.catalog else {}
    for r in rows:
        old = prev_rows.get(r["path"], {})
        for key in ("quality", "look", "tags"):
            if key in old:
                r[key] = old[key]
        r.setdefault("quality", "")
        r.setdefault("look", "")
        for key in ("colors", "reach"):
            if key in measured.get(r["path"], {}):
                r[key] = measured[r["path"]][key]
            elif key in old:
                r[key] = old[key]
    data = OrderedDict([
        ("_comment", "Fab VFX inventory written by Tools/InventoryFabVFX.py (paths only; the packs stay local). quality/look/tags are "
                     "reviewed by hand from the catalogue sheets and drive Tools/FabAbilityVFXTable.py; colors/reach come from the catalogue run."),
        ("packs", OrderedDict((k, {"name": v, "systems": sum(1 for r in rows if r["pack"] == k)}) for k, v in PACKS.items())),
        ("systems", rows),
    ])
    if not args.summary:
        OUT.parent.mkdir(parents=True, exist_ok=True)
        OUT.write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
        print("wrote %s (%d systems)" % (OUT.relative_to(REPO), len(rows)))
    for pack in PACKS:
        q = Counter(r["quality"] or "?" for r in rows if r["pack"] == pack)
        print("%-20s %4d  %s" % (pack, sum(q.values()), " ".join("%s:%d" % kv for kv in sorted(q.items()))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
