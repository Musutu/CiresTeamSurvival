"""Town performance probe (town-perf): load-to-playable seconds, navmesh build and frame stats for the Medieval Kingdom
town in both realms (10 bot champions, 48 monsters). See Docs/CastleTown.md "Performance".

Runs the editor binaries in -game, windowed at the shipped first-launch size (1600x900) and the default quality
preset, exactly like Play.cmd. The probe itself is -CireTown -CireTownPerfProbe (Source/CiresTeamSurvival/CireTownPerf.*).

    python Tools/RunTownPerf.py [--repeats 2] [--profile-gpu] [--trace] [--extra "-ExecCmds=..."]
    python Tools/RunTownPerf.py --bounds auto [--no-trim]     (town-trim: A/B the Play Bounds trim)

--bounds FILE|auto trims the town to a Play Bounds polygon (-CireTownTrimBounds): a MapLayout.json (its playBounds marker)
or {"points": [[x, y], ...]}. "auto" snapshots the main checkout's Content/Data/MapLayout.json (read only): its Play
Bounds when Eric drew them, else the hull of all his markers plus 30 m. --no-trim loads the whole town with the same
polygon on the command line (the "before" of an A/B pair).

Prints each run's CIRE_TOWN_PERF_* lines and the machine load (other Unreal processes) so noisy numbers can be spotted.
Only child processes created by this runner are terminated.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
GAME = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe")
ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}
MAIN_LAYOUT = Path("F:/CiresTeamSurvival/Content/Data/MapLayout.json")  # Eric's live layout: READ ONLY


def hull(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    pts = sorted(set(points))
    if len(pts) < 3:
        return pts
    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lower: list = []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    upper: list = []
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def grow(poly: list[tuple[float, float]], by: float) -> list[tuple[float, float]]:
    # Push every corner of a convex, counter-clockwise hull outward along its bisector (rounded offset, good enough).
    import math
    out = []
    n = len(poly)
    for i, p in enumerate(poly):
        a, b = poly[i - 1], poly[(i + 1) % n]
        def normal(u, v):
            dx, dy = v[0] - u[0], v[1] - u[1]
            length = math.hypot(dx, dy) or 1.0
            return dy / length, -dx / length
        n1, n2 = normal(a, p), normal(p, b)
        bx, by_ = n1[0] + n2[0], n1[1] + n2[1]
        length = math.hypot(bx, by_) or 1.0
        scale = by / max(0.3, (bx / length) * n1[0] + (by_ / length) * n1[1])
        out.append((round(p[0] + bx / length * scale, 1), round(p[1] + by_ / length * scale, 1)))
    return out


def bounds_file(spec: str, folder: Path) -> Path:
    """The polygon file for -CireTownTrimBounds (auto: a snapshot of Eric's layout, never the live file)."""
    if spec != "auto":
        return Path(spec).resolve()
    layout = json.loads(MAIN_LAYOUT.read_text(encoding="utf-8"))
    markers = layout.get("markers", [])
    drawn = [m for m in markers if m.get("type") == "playBounds" and len(m.get("points", [])) >= 3]
    if drawn:
        points, source = [tuple(p) for p in drawn[0]["points"]], "Eric's Play Bounds"
    else:
        spots = []
        for m in markers:
            spots += [tuple(p) for p in m.get("points", [])]
            if "x" in m:
                spots.append((m["x"], m["y"]))
            for part in ("sign", "stall"):
                if isinstance(m.get(part), dict):
                    spots.append((m[part]["x"], m[part]["y"]))
        points, source = grow(hull(spots), 3000.0), "hull of every marker + 30 m"
    target = folder / "bounds.json"
    target.write_text(json.dumps({"source": source, "from": str(MAIN_LAYOUT), "points": [list(p) for p in points]}, indent=1) + "\n", encoding="utf-8")
    print(f"bounds: {source}, {len(points)} corners -> {target}")
    return target


def other_unreal() -> list[str]:
    if os.name != "nt":
        return []
    out = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True, check=False).stdout
    names = []
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if parts and re.match(r"(UnrealEditor|cl\.exe|link\.exe|ShaderCompileWorker)", parts[0], re.I):
            names.append(parts[0])
    counts: dict[str, int] = {}
    for n in names:
        counts[n] = counts.get(n, 0) + 1
    return [f"{k}x{v}" for k, v in sorted(counts.items())]


def kill_tree(child) -> None:
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def run_once(folder: Path, index: int, args) -> dict:
    log = folder / f"run{index}.log"
    command = [str(GAME), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-windowed", "-ResX=1600", "-ResY=900",
               "-CireProcedural" if args.procedural else "-CireTown", "-unattended", "-nosplash", "-nop4", "-NoLiveCoding", "-CireNoReplay", f"-abslog={log}"]
    if not args.no_fight:
        command.append("-CireTownPerfProbe")
    if args.doors:
        command.append("-CireTownDoorProbe")
    if args.profile_gpu:
        command.append("-CireTownPerfProfileGPU")
    if args.load_only:
        command.append("-CireTownPerfLoadOnly")
    if args.trace:
        command += ["-trace=cpu,gpu,frame,bookmark,loadtime,region", f"-tracefile={folder / f'run{index}.utrace'}"]
    if args.bounds_file:
        command.append(f"-CireTownTrimBounds={args.bounds_file}")
    if args.no_trim:
        command.append("-CireNoTownTrim")
    command += args.extra
    load = other_unreal()
    started = time.monotonic()
    child = subprocess.Popen(command, cwd=ROOT, env=ENV)
    try:
        code = child.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        kill_tree(child)
        code = -1
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    lines = [l.split("Display: ", 1)[-1] for l in text.splitlines()
             if re.search(r"CIRE_TOWN_PERF|CIRE_TOWN_REALMS_LOADED|CIRE_NAV_READY|CIRE_NAV_CACHE|CIRE_NAV_REBUILT|CIRE_TOWN_LIGHTING|CIRE_TOWN_NESTED|CIRE_TOWN_REALM_PREPARED|CIRE_TOWN_VIEW|CIRE_TOWN_SCENE|CIRE_TOWN_DOOR|CIRE_TOWN_INTERIOR|CIRE_TOWN_TRIM", l)]
    external = sum(1 for l in text.splitlines() if "Failed to load Actor for External Actor Package" in l)
    return dict(run=index, exit=code, seconds=round(time.monotonic() - started, 1), machine_load=load, external_actor_failures=external,
                passed="CIRE_TOWN_PERF_PASS" in text or ((args.load_only or args.no_fight) and code == 0), lines=lines, log=str(log))


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--repeats", type=int, default=1)
    p.add_argument("--timeout", type=int, default=1200)
    p.add_argument("--profile-gpu", action="store_true")
    p.add_argument("--trace", action="store_true")
    p.add_argument("--load-only", action="store_true", help="exit once playable (load-time iterations)")
    p.add_argument("--doors", action="store_true", help="door walkability + interior shots (-CireTownDoorProbe) before the fight")
    p.add_argument("--procedural", action="store_true", help="the old procedural town (reference for the same fight)")
    p.add_argument("--no-fight", action="store_true", help="drop -CireTownPerfProbe (with --doors: doors and shots only)")
    p.add_argument("--label", default="")
    p.add_argument("--bounds", default="", help="town-trim: Play Bounds polygon file, or 'auto' (see above)")
    p.add_argument("--no-trim", action="store_true", help="town-trim: load the whole town (the 'before' of an A/B pair)")
    p.add_argument("--extra", nargs="*", default=[])
    args = p.parse_args()
    folder = ROOT / "Saved/TownPerfRuns" / (datetime.now().strftime("%Y%m%d-%H%M%S") + (f"-{args.label}" if args.label else ""))
    folder.mkdir(parents=True, exist_ok=True)
    args.bounds_file = bounds_file(args.bounds, folder) if args.bounds else None
    results = []
    for i in range(args.repeats):
        r = run_once(folder, i, args)
        results.append(r)
        print(f"run {i}: exit={r['exit']} {r['seconds']} s load={','.join(r['machine_load']) or 'idle'} external_actor_failures={r['external_actor_failures']}")
        for line in r["lines"]:
            print("  " + line)
    (folder / "report.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(f"Report: {folder / 'report.json'}")
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
