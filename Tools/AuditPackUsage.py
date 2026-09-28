"""Pack usage audit (pack-usage): how much of each purchased Fab pack the game references, per sub-kit.

Eric's rule: after integrating a pack, count the referenced assets per pack / sub-kit and report before -> after.
"Referenced" = the object path appears in committed data (Content/Data/*.json, Art/Audio/FabAudioMap.json,
Art/Fab/*.json) or in Source/. Assets are counted against what is installed in the main checkout:
  VFX packs    every NS_* / P_ky_* system (demo / blueprint folders excluded)
  audio packs  every SoundWave (WAVs) - the packs' own SoundCues are not used (CireAudio picks and pitches its own)
  other packs  every .uasset (animation / creature / prop packs; listed for completeness, not this pass's scope)

  python Tools/AuditPackUsage.py                       # current data -> Docs/PackUsage.md auto block + stdout table
  python Tools/AuditPackUsage.py --ref main            # the same count on another git ref's data (the "before" column)
  python Tools/AuditPackUsage.py --before main         # before (ref) -> after (working tree) table into Docs/PackUsage.md
  python Tools/AuditPackUsage.py --unused Shadow_Magic # list what a pack still leaves on the shelf
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
from collections import OrderedDict, defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DOC = REPO / "Docs" / "PackUsage.md"
DATA_GLOBS = ["Content/Data/*.json", "Content/Data/**/*.json", "Art/Audio/*.json", "Art/Fab/*.json", "Source/CiresTeamSurvival/*.cpp", "Source/CiresTeamSurvival/*.h"]
# Inventory rows are never "usage" (they list the whole pack): excluded from the reference scan.
INVENTORY_FILES = {"Art/Fab/FabVFXInventory.json", "Art/Fab/FabCatalog.json"}

VFX_PACKS = ["Big_Pack_Magic_VFX", "Shadow_Magic", "Earth_Spells", "Forest_VFX", "State_VFX", "FXVarietyPack", "RealisticBlood"]
AUDIO_PACKS = ["Fantasy_UI_SFX_Pack", "Magic_Spell_SFX_Pack_Vol1", "Combat_Sounds_-_Lite", "Professional_Gunshots", "ShieldBlocksDeflects"]
OTHER_PACKS = ["GDHBundle", "MaleLocomotionSet", "Gun_and_Sword", "CrossbowPackAnim", "ROG_Creatures", "QuadrapedCreatures", "UndeadPack", "Polyphoria",
               "Medieval_Weapons", "Medieval_Weapons_VOL2", "CastleTown", "Monster"]


def main_checkout() -> Path:
    out = subprocess.run(["git", "worktree", "list", "--porcelain"], cwd=REPO, capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith("worktree "):
            return Path(line[9:].strip())
    return REPO


CONTENT = main_checkout() / "Content"


def subkit_of(pack: str, rel: str) -> str:
    parts = rel.split("/")
    if pack == "Big_Pack_Magic_VFX":
        return parts[2] if len(parts) > 3 else "root"
    if pack == "Fantasy_UI_SFX_Pack":
        return parts[2] if len(parts) > 3 else "root"
    if pack in ("Magic_Spell_SFX_Pack_Vol1", "Combat_Sounds_-_Lite", "Professional_Gunshots", "ShieldBlocksDeflects"):
        return parts[1] if len(parts) > 2 else "root"
    if pack == "RealisticBlood":
        return parts[1] if len(parts) > 2 else "root"
    return parts[1] if len(parts) > 2 and pack in OTHER_PACKS else "all"


def installed_assets(pack: str) -> dict[str, str]:
    """object path -> sub-kit for the countable assets of a pack."""
    root = CONTENT / pack
    out = {}
    if not root.is_dir():
        return out
    for f in root.rglob("*.uasset"):
        rel = f.relative_to(CONTENT).with_suffix("").as_posix()
        if "/Demo/" in rel or "/_Blueprints/" in rel or "/_LevelSequence/" in rel or "/_Maps/" in rel:
            continue
        stem = f.stem
        if pack in VFX_PACKS:
            if not re.match(r"(NS|P_ky)_", stem) or re.match(r"(NE|NFX)_", stem):
                continue
        elif pack in AUDIO_PACKS:
            if "/CUEs/" in rel or "/Cues/" in rel or stem.startswith(("SC_", "Cue_")) or "_Cue" in stem:
                continue
        out["/Game/" + rel] = subkit_of(pack, rel)
    return out


def referenced_paths(ref: str | None) -> set[str]:
    """Every /Game/<Pack>/... path mentioned in committed data or code (object path or package path)."""
    texts = []
    if ref:
        files = subprocess.run(["git", "ls-tree", "-r", "--name-only", ref], cwd=REPO, capture_output=True, text=True).stdout.split("\n")
        for f in files:
            if f in INVENTORY_FILES:
                continue
            if any(Path(f).match(g.replace("**/", "")) for g in DATA_GLOBS) and (f.startswith("Content/Data") or f.startswith("Art/") or f.startswith("Source/")):
                texts.append(subprocess.run(["git", "show", f"{ref}:{f}"], cwd=REPO, capture_output=True, text=True, encoding="utf-8", errors="replace").stdout)
    else:
        for g in DATA_GLOBS:
            for f in REPO.glob(g):
                if f.relative_to(REPO).as_posix() in INVENTORY_FILES:
                    continue
                texts.append(f.read_text(encoding="utf-8", errors="replace"))
    found = set()
    for t in texts:
        for m in re.finditer(r"/Game/(?:%s)/[A-Za-z0-9_\-/. ]+?(?=[\"'\)\],\s]|$)" % "|".join(re.escape(p) for p in VFX_PACKS + AUDIO_PACKS + OTHER_PACKS), t):
            path = m.group(0).rstrip(".")
            found.add(path.split(".", 1)[0] if "." in path.rsplit("/", 1)[-1] else path)  # package path
    return found


def audit(ref: str | None):
    refs = referenced_paths(ref)
    rows = OrderedDict()
    for pack in VFX_PACKS + AUDIO_PACKS + OTHER_PACKS:
        assets = installed_assets(pack)
        if not assets:
            continue
        kits = defaultdict(lambda: [0, 0, []])
        for path, kit in assets.items():
            kits[kit][1] += 1
            if path in refs:
                kits[kit][0] += 1
            else:
                kits[kit][2].append(path.rsplit("/", 1)[-1])
        rows[pack] = OrderedDict(sorted(kits.items()))
    return rows


def pct(used, total):
    return "%d/%d (%d%%)" % (used, total, round(100.0 * used / total)) if total else "-"


def table(before, after):
    lines = ["| Pack | Sub-kit | Before | After |", "|---|---|---|---|"]
    for pack, kits in after.items():
        tu = sum(v[1] for v in kits.values())
        bu = sum(v[0] for v in before.get(pack, {}).values())
        au = sum(v[0] for v in kits.values())
        lines.append("| **%s** | all | **%s** | **%s** |" % (pack, pct(bu, tu) if pack in before else "-", pct(au, tu)))
        if len(kits) > 1:
            for kit, (used, total, _) in kits.items():
                b = before.get(pack, {}).get(kit, [0, total, []])[0]
                lines.append("| | %s | %s | %s |" % (kit, pct(b, total) if pack in before else "-", pct(used, total)))
    return "\n".join(lines)


def write_doc(block: str):
    start, end = "<!-- AUTO:usage -->", "<!-- /AUTO:usage -->"
    text = DOC.read_text(encoding="utf-8") if DOC.exists() else "# Pack usage\n\n%s\n%s\n" % (start, end)
    if start not in text:
        text += "\n%s\n%s\n" % (start, end)
    head, rest = text.split(start, 1)
    _, tail = rest.split(end, 1)
    DOC.write_text(head + start + "\n" + block + "\n" + end + tail, encoding="utf-8", newline="\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ref", help="count this git ref's data instead of the working tree")
    ap.add_argument("--before", help="git ref for the Before column (the working tree is After); writes Docs/PackUsage.md")
    ap.add_argument("--unused", help="list the unreferenced assets of this pack folder")
    ap.add_argument("--all-packs", action="store_true", help="also count the animation / creature / prop packs (thousands of assets, not this pass's scope)")
    args = ap.parse_args()
    scope = VFX_PACKS + AUDIO_PACKS + (OTHER_PACKS if args.all_packs else [])
    after = OrderedDict((k, v) for k, v in audit(args.ref).items() if k in scope)
    before = OrderedDict((k, v) for k, v in audit(args.before).items() if k in scope) if args.before else {}
    out = table(before, after)
    print(out)
    if args.unused:
        for kit, (_, _, left) in after.get(args.unused, {}).items():
            print("%s/%s (%d): %s" % (args.unused, kit, len(left), " ".join(sorted(left))))
    if args.before:
        write_doc(out)
        print("wrote " + str(DOC.relative_to(REPO)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
