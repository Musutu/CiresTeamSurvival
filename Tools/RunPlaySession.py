"""Run the simulated WoW casting/camera play session (-CirePlaySession) and report its checks.

Launches a standalone offscreen game on the Citadel map. The native probe (CirePlaySession.cpp) drives
real inputs through PlayerInput: ability keys, WASD/QE, RMB steering with raw mouse deltas, LMB clicks
and drags. It checks smart cast, target retention, ground aim through movement and steering,
clean-click confirm/cancel, stop-to-cast, "Can't cast while moving", movement cancelling a cast, and
summons with no target. Only the child process started here is terminated.
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


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    p.add_argument("--project", type=Path, default=ROOT / "CiresTeamSurvival.uproject")
    p.add_argument("--timeout", type=float, default=240)
    a = p.parse_args()
    folder = ROOT / "Saved/PlaySession" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    log = folder / "session.log"
    cmd = [str(a.editor), str(a.project.resolve()), "/Game/Maps/Citadel", "-game", "-CirePlaySession", "-CireTripoChampions",
           "-RenderOffscreen", "-ForceRes", "-ResX=1280", "-ResY=720", "-unattended", "-nosplash", "-nosound", "-nop4",
           "-NoLiveCoding", "-ExecCmds=t.MaxFPS 60", f"-abslog={log}"]
    started = time.monotonic()
    with (folder / "console.log").open("wb") as out:
        # UE_SKIP_UBT_SDK_SETUP avoids the editor's ValidatePlatforms Build.bat, which blocks on the
        # machine-wide Build.bat lock while other worktrees compile.
        child = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env={**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"})
        try:
            code = child.wait(timeout=a.timeout)
        except subprocess.TimeoutExpired:
            if os.name == "nt":  # kill the whole tree we started
                subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
            child.kill()
            child.wait(timeout=10)
            code = -1
    text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
    checks = [{"pass": m.group(1) == "PASS", "name": m.group(2).strip()} for m in re.finditer(r"CIRE_PLAY_CHECK_(PASS|FAIL) (.*)", text)]
    summary = re.search(r"CIRE_PLAY_SESSION_(PASS|FAIL)[^\n]*", text)
    passed = code == 0 and bool(summary) and summary.group(1) == "PASS" and all(c["pass"] for c in checks) and checks
    report = {"passed": bool(passed), "exitCode": code, "seconds": round(time.monotonic() - started, 1),
              "summary": summary.group(0) if summary else None, "checks": checks, "log": str(log)}
    (folder / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    for c in checks:
        print(("PASS " if c["pass"] else "FAIL ") + c["name"])
    print(report["summary"] or "no session summary (startup stall? retry)")
    print("Report:", folder / "report.json")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
