"""Measure champion-select hover switching (champ-select-perf).

Launches the game at champion select with -CireDraftHoverProbe -CireTripoChampions (the Play.cmd body set) and lets
the native probe (Source/CiresTeamSurvival/CireDraftHoverProbe.cpp) hover a fixed list of heroes like a mouse would:

  browse   every 3rd roster hero, 700 ms each (authored and Paragon heroes, shared and unique backgrounds)
  scrub    the heroes between them, 60 ms each (a fast sweep across the grid)
  revisit  the first ten browse heroes again (cache hits)

For every switch it logs the longest frame in the switch window (the stall), frames over the hitch threshold (50 ms),
game-thread milliseconds spent loading the background, portraits and 3D body, and the time until the details and the
live figure show the hovered hero. The runner prints the per-phase summary and writes report.json next to the logs.
Only processes started here are stopped. No screenshots are taken.
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
FAILURE = re.compile(r"CIRE_DRAFT_HOVER\S*FAIL|Fatal error:|Assertion failed:")
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}
SUMMARY = re.compile(r"CIRE_DRAFT_HOVER_SUMMARY phase=(\w+) (.*)")


def kill_tree(child) -> None:
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


def run(args, folder: Path, attempt: int) -> dict:
    width, height = (int(v) for v in args.res.lower().split("x"))
    log = folder / f"hover_{args.tag or 'run'}_{attempt}.log"
    command = [str(args.editor), str(args.project.resolve()), "/Game/Maps/Citadel", "-game", "-CireTripoChampions",
               "-CireDraftHoverProbe", "-RenderOffscreen", "-ForceRes", f"-ResX={width}", f"-ResY={height}", "-windowed",
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-ExecCmds=t.MaxFPS 60",
               f"-abslog={log}"]
    if args.tag:
        command.append(f"-CireDraftHoverTag={args.tag}")
    if args.count:
        command.append(f"-CireDraftHoverCount={args.count}")
    if args.budget:
        command.append(f"-CireDraftHoverBudgetMs={args.budget}")
    command.extend(args.extra)
    started = time.monotonic(); failure = ""
    child = subprocess.Popen(command, cwd=args.project.resolve().parent, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), env=EDITOR_ENV)
    try:
        code = child.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        kill_tree(child)
        try:
            code = child.wait(timeout=10)
        except subprocess.TimeoutExpired:
            child.kill(); code = child.wait(timeout=10)
        failure = f"hover probe exceeded {args.timeout} seconds"
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    errors = [line.strip() for line in text.splitlines() if FAILURE.search(line)]
    summary = {}
    for line in text.splitlines():
        match = SUMMARY.search(line)
        if match:
            summary[match.group(1)] = {k: float(v) for k, v in (kv.split("=") for kv in match.group(2).split())}
    directory = re.search(r"CIRE_DRAFT_HOVER_(?:PASS|FAIL) switches=\d+ directory=(.+)", text)
    passed = code == 0 and "CIRE_DRAFT_HOVER_PASS" in text and not errors and not failure
    return dict(passed=passed, exitCode=code, seconds=round(time.monotonic() - started, 1), failure=failure, errors=errors,
                summary=summary, log=str(log), directory=directory.group(1).strip() if directory else None,
                began="CIRE_DRAFT_HOVER_BEGIN" in text)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--project", type=Path, default=ROOT / "CiresTeamSurvival.uproject")
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--res", default="1920x1080")
    parser.add_argument("--tag", default="", help="label for the output folder (e.g. before / after)")
    parser.add_argument("--count", type=int, default=0, help="browse switches (default 30)")
    parser.add_argument("--budget", type=float, default=0, help="fail when the browse/revisit p95 stall exceeds this (ms)")
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--extra", action="append", default=[])
    args = parser.parse_args()
    folder = args.project.resolve().parent / "Saved/DraftHoverChecks" / (datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + (f"_{args.tag}" if args.tag else ""))
    folder.mkdir(parents=True)
    result = {}
    for attempt in range(1, 3):
        result = run(args, folder, attempt)
        result["attempt"] = attempt
        if result["passed"] or result["began"]:
            break
    (folder / "report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"{'PASS' if result['passed'] else 'FAIL'}  {result['seconds']}s  log={result['log']}")
    for phase, values in result["summary"].items():
        keys = ("stall_avg_ms", "stall_p95_ms", "stall_max_ms", "hitches", "body_avg_ms", "bg_avg_ms", "splash_avg_ms", "ready_avg_ms", "not_ready")
        print(f"  {phase:8} " + "  ".join(f"{k}={values.get(k, 0):g}" for k in keys))
    for line in result["errors"][:6]:
        print("  ", line[:220])
    print(f"report: {folder / 'report.json'}")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
