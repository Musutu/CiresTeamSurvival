"""Outdoor boss probe (outdoor-bosses): Eric's own MapLayout.json on the Medieval Kingdom town, in BOTH realms, trimmed to
its Play Bounds exactly like a match (-CireUseMapLayout). Every Boss and Challenge Pack marker must project onto the navmesh
the way the layout editor's VALIDATE checks it (and VALIDATE must raise nothing about them), every boss lair's leash area
must be walkable and reachable from the player spawn, one different world boss must stand on every Boss marker (the same
boss in both realms), and a kill must pay the boss bounty and bring the boss back after its respawn time.
See Docs/OutdoorBosses.md.

    python Tools/RunOutdoorBossProbe.py [--timeout 1800]

Needs the Medieval Kingdom pack (Content/CastleTown). The town takes several minutes to stream both realms and build the
navmesh (the first run after a bounds change rebuilds it, later runs use Saved/NavCache). Only child processes created by
this runner are terminated. Nothing is built or imported. Writes Saved/OutdoorBossProbe/<stamp>/{probe.log, probe.txt, report.json}.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
FAILURE = re.compile(r"CIRE_\S*(?:FAIL|ERROR)|Fatal error:|Assertion failed:|Ensure condition failed:")
EDITOR = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
# AutoSDK is off on this machine: skip the ValidatePlatforms Build.bat that otherwise blocks on other worktrees' builds.
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--respawn", type=int, default=5, help="respawn seconds for the kill check (-CireOutdoorBossRespawn)")
    parser.add_argument("--render", action="store_true", help="render (default: -nullrhi)")
    args = parser.parse_args()
    town = (ROOT / "Content/CastleTown/Levels/Persistant/PL_CastleTown.umap").exists()
    if not town:
        print("outdoor boss probe: the Medieval Kingdom pack (Content/CastleTown) is not installed")
        return 1
    folder = ROOT / "Saved/OutdoorBossProbe" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True, exist_ok=False)
    log = folder / "probe.log"
    command = [str(EDITOR), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game",
               *([] if args.render else ["-nullrhi"]), "-CireOutdoorBossProbe", "-CireUseMapLayout", "-CireTown",
               f"-CireOutdoorBossRespawn={args.respawn}", "-benchmark", "-fps=30", f"-CireOutdoorBossProbeSummary={folder / 'probe.txt'}",
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-CireNoReplay", f"-abslog={log}"]
    started = time.monotonic()
    failure = ""
    with (folder / "probe.console.log").open("wb") as output:
        child = subprocess.Popen(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT, env=EDITOR_ENV,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0)
        try:
            code = child.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            kill_tree(child)
            try:
                code = child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill()
                code = child.wait(timeout=10)
            failure = f"the probe exceeded its {args.timeout}-second bound"
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    errors = [line for line in text.splitlines() if FAILURE.search(line)]
    evidence = [line.split("Display: ", 1)[-1] for line in text.splitlines()
                if re.search(r"CIRE_(OUTDOOR_BOSS_PROBE_|OUTDOOR_BOSS_SPAWN|OUTDOOR_BOSSES |NAV_READY|LAYOUT_ACTIVE|LAYOUT_REJECTED)", line)]
    passed = code == 0 and not failure and not errors and "CIRE_OUTDOOR_BOSS_PROBE_PASS" in text
    report = dict(passed=passed, town=town, exitCode=code, failure=failure or None, seconds=round(time.monotonic() - started, 1),
                  log=str(log), errors=errors, evidence=evidence)
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"outdoor boss probe (town): {'PASS' if passed else 'FAIL'} ({report['seconds']} s)")
    for line in evidence + errors:
        print("  " + line)
    if failure:
        print("  " + failure)
    print(f"Report: {folder / 'report.json'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
