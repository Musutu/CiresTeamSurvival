"""Layout wiring probe (layout-wiring): waves spawned from two monster spawns march down three paths in both realms
and all arrive; then one unit per realm is kited past its leash and must walk back to its path and march on.
See Docs/MapLayout.md ("What the game reads", "Leash").

    python Tools/RunLayoutProbe.py [--town | --procedural] [--timeout 1500]

--town (default when the Medieval Kingdom pack is installed) runs it on the pack town (-CireTown); the town takes
3-5 minutes to stream both realms and build the navmesh. Only child processes created by this runner are terminated.
Nothing is built or imported. Writes Saved/LayoutProbe/<stamp>/{probe.log, probe.txt, report.json}.
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
    parser.add_argument("--town", action="store_true", help="run on the Medieval Kingdom town (-CireTown)")
    parser.add_argument("--procedural", action="store_true", help="run on the procedural town")
    parser.add_argument("--timeout", type=int, default=1500)
    parser.add_argument("--render", action="store_true", help="render (default: -nullrhi)")
    args = parser.parse_args()
    town = args.town or (not args.procedural and (ROOT / "Content/CastleTown/Levels/Persistant/PL_CastleTown.umap").exists())
    folder = ROOT / "Saved/LayoutProbe" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True, exist_ok=False)
    log = folder / "probe.log"
    command = [str(EDITOR), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game",
               *([] if args.render else ["-nullrhi"]), "-CireLayoutProbe", "-CireTown" if town else "-CireProcedural",
               "-benchmark", "-fps=30", f"-CireLayoutProbeSummary={folder / 'probe.txt'}",
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
                if re.search(r"CIRE_(LAYOUT_PROBE_|LEASH_(RETURN|RESUMED|RESCUE)|LAYOUT_ACTIVE|TOWN_FRAME)", line)]
    passed = code == 0 and not failure and not errors and "CIRE_LAYOUT_PROBE_PASS" in text
    report = dict(passed=passed, town=town, exitCode=code, failure=failure or None, seconds=round(time.monotonic() - started, 1),
                  log=str(log), errors=errors, evidence=evidence)
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"layout probe ({'town' if town else 'procedural'}): {'PASS' if passed else 'FAIL'} ({report['seconds']} s)")
    for line in evidence + errors:
        print("  " + line)
    if failure:
        print("  " + failure)
    print(f"Report: {folder / 'report.json'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
