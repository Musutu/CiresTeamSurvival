"""Cycle every video preset in the REAL windowed game and fail on any crash or render ensure.

Launches a visible, windowed standalone game (Citadel, -CireTripoChampions; no -RenderOffscreen) with
-CireVideoCycle. The native fixture (Source/CiresTeamSurvival/CireVideoCycle.cpp) keeps the Options >
Video page open and applies Low, Medium, High, Epic and Cinematic at 1600x900 and 1920x1080, render
scale 50/75 %, 2560x1440 + Cinematic at once (Eric's crash) and borderless, through the same deferred
CireVideo queue as the Options buttons: first on champion select (live 3D preview), then again in the
match. After every change it saves a screenshot and the raw champion render, and scans all primitives
for NaN / zero-scale / out-of-float-range transforms.

PASS needs: exit code 0, CIRE_VIDEO_CYCLE_PASS, every CIRE_VIDEO_CYCLE_CHECK passing, and none of the
engine failures from the crash reports in the log (the distance-field "precision loss" / "non-invertible
matrix" ensures, any other ensure, fatal error or access violation). Only the child process started here
is terminated. Captures: Saved/VideoCycle/<stamp>/.
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
ENGINE_FAILURES = [
    ("ensure", re.compile(r"Ensure condition failed")),
    ("df_precision", re.compile(r"precision loss while converting matrix")),
    ("non_invertible", re.compile(r"non-invertible matrix|InverseFast")),
    ("fatal", re.compile(r"Fatal error|Unhandled Exception|EXCEPTION_ACCESS_VIOLATION|Assertion failed")),
    ("render_sanity", re.compile(r"CIRE_RENDER_SANITY_BAD|CIRE_RENDER_SANITY_REPAIRED|found NaN in Bounds")),
    ("apply_during_draw", re.compile(r"CIRE_VIDEO_APPLY_DURING_DRAW")),
]


def run_persist(a) -> int:
    """The "save and reload" flow: launch 1 keeps new video settings and quits; launch 2 starts exactly like
    Play.cmd (no -windowed/-ResX/-ResY) and must come up with them. Launch 2 then restores the defaults."""
    folder = ROOT / "Saved/VideoCycle" / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + "_persist")
    folder.mkdir(parents=True)
    results = {}
    for mode in ("set", "check"):
        log = folder / f"{mode}.log"
        cmd = [str(a.editor), str(a.project.resolve()), "/Game/Maps/Citadel", "-game", "-CireTripoChampions", f"-CireVideoPersist={mode}",
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", f"-abslog={log}", *a.extra]
        with (folder / f"{mode}.console.log").open("wb") as out:
            child = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT, env={**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"})
            try:
                code = child.wait(timeout=a.timeout)
            except subprocess.TimeoutExpired:
                subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
                code = -1
        text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
        line = re.search(r"CIRE_VIDEO_PERSIST_(?:PASS|FAIL|SET) [^\r\n]*", text)
        results[mode] = {"exitCode": code, "line": line.group(0) if line else None}
        print(mode, code, results[mode]["line"])
    passed = results["check"]["exitCode"] == 0 and bool(results["check"]["line"]) and "CIRE_VIDEO_PERSIST_PASS" in results["check"]["line"]
    (folder / "report.json").write_text(json.dumps({"passed": passed, **results}, indent=1), encoding="utf-8")
    print("video_persist: PASS" if passed else "video_persist: FAIL", folder)
    return 0 if passed else 1


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    p.add_argument("--project", type=Path, default=ROOT / "CiresTeamSurvival.uproject")
    p.add_argument("--timeout", type=float, default=1200)
    p.add_argument("--fullscreen", action="store_true", help="Also switch to exclusive fullscreen once (takes over the display)")
    p.add_argument("--tag", default="")
    p.add_argument("--extra", action="append", default=[], help="Extra engine argument")
    p.add_argument("--persist", action="store_true", help="Instead: keep settings, quit, relaunch like Play.cmd and verify they stuck")
    a = p.parse_args()
    if a.persist:
        return run_persist(a)
    folder = ROOT / "Saved/VideoCycle" / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ") + (f"_{a.tag}" if a.tag else ""))
    folder.mkdir(parents=True)
    log = folder / "game.log"
    cmd = [str(a.editor), str(a.project.resolve()), "/Game/Maps/Citadel", "-game", "-CireVideoCycle", "-CireTripoChampions",
           "-windowed", "-ResX=1600", "-ResY=900", "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding",
           f"-abslog={log}", *a.extra]
    if a.tag:
        cmd.append(f"-CireVideoCycleTag={a.tag}")
    if a.fullscreen:
        cmd.append("-CireVideoCycleFullscreen")
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
    checks = [{"pass": m.group(1) == "PASS", "name": m.group(2).strip()} for m in re.finditer(r"CIRE_VIDEO_CYCLE_CHECK_(PASS|FAIL) (.*)", text)]
    summary = re.search(r"CIRE_VIDEO_CYCLE_(PASS|FAIL) [^\n]*", text)
    failures = {name: len(rx.findall(text)) for name, rx in ENGINE_FAILURES}
    # The fixture deliberately requests one apply inside the HUD draw to prove it is deferred.
    if "CIRE_VIDEO_CYCLE_CHECK_PASS apply inside a draw is deferred" in text:
        failures["apply_during_draw"] = max(0, failures["apply_during_draw"] - 1)
    failures = {k: v for k, v in failures.items() if v}
    figures = [m.group(0) for m in re.finditer(r"CIRE_VIDEO_CYCLE_FIGURE [^\n]*", text)]
    shots = re.findall(r"CIRE_VIDEO_CYCLE_SHOT \S+ file=([^\n]+)", text)
    passed = (code == 0 and bool(summary) and summary.group(1) == "PASS" and bool(checks) and all(c["pass"] for c in checks)
              and not failures)
    report = {"passed": bool(passed), "exitCode": code, "seconds": round(time.monotonic() - started, 1),
              "summary": summary.group(0) if summary else None, "engineFailures": failures, "checks": checks,
              "figures": figures, "shots": shots, "log": str(log)}
    (folder / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    for c in checks:
        if not c["pass"]:
            print("FAIL " + c["name"])
    print(f"checks: {sum(c['pass'] for c in checks)}/{len(checks)} pass; shots: {len(shots)}; engine failures: {failures or 'none'}")
    for f in figures:
        print(f)
    print(report["summary"] or "no cycle summary (startup stall? retry)")
    print(("video_cycle: PASS" if passed else "video_cycle: FAIL"), "report:", folder / "report.json")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
