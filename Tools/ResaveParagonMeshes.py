"""Resave the Paragon skeletal meshes once so the editor stops rebuilding them on every load (champ-select-perf).

Why: in UE 5.8 every Paragon hero mesh logs "The derived data key is different after the build. Save the asset to
avoid rebuilding it everytime the editor load it." and rebuilds its render data (LOD reduction) on its FIRST load in
every editor session: 5-7 s frozen on the game thread per hero (champion-select hover, and again in a match when a
Paragon hero spawns). Measured on Terra (2026-09-28): 6.5 s rebuild before the resave, 1.1 s plain load after it.

This rewrites local, Epic-licensed files (Content/Paragon*, junctioned from F:/CiresTeamSurvival-ParagonStore): they are
never committed. Close EVERY editor / game instance that uses the packs first (all worktrees share the store).

Usage (from the project root):
    python Tools/ResaveParagonMeshes.py --dry-run       # list the meshes
    python Tools/ResaveParagonMeshes.py                 # resave them (runs the editor's Python commandlet)
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def mesh_packages() -> list[str]:
    data = json.loads((ROOT / "Content/Data/ParagonChampions.json").read_text(encoding="utf-8"))
    out: list[str] = []
    for row in data.get("bindings", []):
        for path in [row.get("mesh", "")] + [p.get("mesh", "") for p in row.get("parts", []) if isinstance(p, dict)]:
            if path.startswith("/Game/Paragon"):
                package = path.split(".")[0]
                if package not in out:
                    out.append(package)
    for hero in data.get("heroes", []):
        for skin in hero.get("skins", []):
            package = skin.get("mesh", "").split(".")[0]
            if package.startswith("/Game/Paragon") and package not in out:
                out.append(package)
    return out


SCRIPT = r'''
import unreal, json
packages = json.loads(open(r"{list}", encoding="utf-8").read())
saved = failed = 0
for package in packages:
    asset = unreal.EditorAssetLibrary.load_asset(package)
    if asset is None:
        unreal.log_warning("CIRE_RESAVE_MISSING " + package); failed += 1; continue
    if unreal.EditorAssetLibrary.save_asset(package, only_if_is_dirty=False):
        saved += 1
    else:
        unreal.log_warning("CIRE_RESAVE_FAIL " + package); failed += 1
unreal.log("CIRE_RESAVE_DONE saved=%d failed=%d" % (saved, failed))
'''


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    packages = [p for p in mesh_packages() if (ROOT / "Content" / (p[len("/Game/"):] + ".uasset")).exists()]
    print(f"{len(packages)} Paragon skeletal mesh packages installed")
    if args.dry_run:
        for package in packages:
            print("  " + package)
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        listing = Path(tmp) / "packages.json"
        listing.write_text(json.dumps(packages), encoding="utf-8")
        script = Path(tmp) / "resave.py"
        script.write_text(SCRIPT.replace("{list}", str(listing)), encoding="utf-8")
        log = ROOT / "Saved/ResaveParagonMeshes.log"
        env = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}
        code = subprocess.call([str(args.editor), str(ROOT / "CiresTeamSurvival.uproject"), "-run=pythonscript", f"-script={script}",
                                "-unattended", "-nullrhi", "-nosplash", "-nop4", f"-abslog={log}"], env=env)
        text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
        done = [line for line in text.splitlines() if "CIRE_RESAVE_DONE" in line]
        print(done[-1] if done else "no completion marker", f"(log: {log})")
        return 0 if code == 0 and done and "failed=0" in done[-1] else 1


if __name__ == "__main__":
    sys.exit(main())
