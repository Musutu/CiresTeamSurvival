"""Boss size + unit spacing probe (bosses-spacing). A real match world (the Medieval Kingdom town with Eric's MapLayout.json
when installed, else the Citadel):
  1. crowd before/after: 16 melee monsters engage one hero with the legacy spacing, then with Content/Data/UnitSpacing.json;
     the probe measures overlapping body pairs and the mean nearest-neighbour gap (after must overlap less, spread more);
  2. every outdoor world boss is drawn boss.outdoorBoss (5) x its marker size with a Large-nav-agent capsule, stands on the navmesh and can path to the spawn;
  3. a wave boss (boss.waveBoss, default 1x) marches the lane road through the town for 45 s without stalling;
  4. --shots (renders): the marching giant and a world boss with the raid-boss bar, in all 4 HUD themes (PNG).

    python Tools/RunBossSpacingProbe.py [--shots] [--citadel] [--timeout 1800]

Writes Saved/BossSpacing/<stamp>/{probe.log, probe.txt, report.json, *.png}. Only child processes created here are terminated.
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
FAILURE = re.compile(r"CIRE_\S*(?:CHECK_FAIL|PROBE_FAIL)|Fatal error:|Assertion failed:")
EDITOR = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--shots", action="store_true", help="render and capture the boss + raid bar in every HUD theme")
    parser.add_argument("--citadel", action="store_true", help="skip the town (fast)")
    args = parser.parse_args()
    town = not args.citadel and (ROOT / "Content/CastleTown/Levels/Persistant/PL_CastleTown.umap").exists()
    folder = ROOT / "Saved/BossSpacing" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    folder.mkdir(parents=True, exist_ok=False)
    log = folder / "probe.log"
    command = [str(EDITOR), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game",
               *(["-windowed", "-ResX=1920", "-ResY=1080"] if args.shots else ["-nullrhi"]),
               "-CireBossSpacingProbe", *(["-CireBossSpacingShots"] if args.shots else []),
               *(["-CireUseMapLayout", "-CireTown"] if town else []), f"-CireBossSpacingDir={folder}",
               "-benchmark", "-fps=30", "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-CireNoReplay", f"-abslog={log}"]
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
    evidence = [line.split("Display: ", 1)[-1] for line in text.splitlines() if "CIRE_BOSS_SPACING_PROBE" in line or "CIRE_UNIT_SPACING " in line]
    passed = code == 0 and not failure and not errors and "CIRE_BOSS_SPACING_PROBE_PASS" in text
    shots = sorted(str(p) for p in folder.glob("*.png"))
    report = dict(passed=passed, town=town, exitCode=code, failure=failure or None, seconds=round(time.monotonic() - started, 1),
                  log=str(log), errors=errors, evidence=evidence, shots=shots)
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"boss spacing probe ({'town' if town else 'citadel'}): {'PASS' if passed else 'FAIL'} ({report['seconds']} s)")
    for line in evidence + errors:
        print("  " + line)
    if failure:
        print("  " + failure)
    print(f"Report: {folder / 'report.json'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
