"""Render the champion-select screen at several resolutions (offscreen) and verify it.

For every resolution the game launches with -CireDraftGallery -CireTripoChampions.
The native fixture walks nine states (browse, hover, selected, hybrid, abilities tab,
teammate-locked + lore tab, search, low timer, locked in), waits for the live 3D preview to settle
and saves one PNG per state under Saved/DraftGallery/<stamp>_<WxH>. For each capture
it also writes <shot>.layout.json: every text run and card with the box it must stay
inside. The fixture fails a shot (CIRE_DRAFT_LAYOUT_FAIL) when a card leaves the roster
panel, text overflows its box or the safe area, the title is clipped, or text is
smaller than the readability floor (9.4 logical units; card names 10, i.e. 15 px at
1080p). This runner only stops the processes it starts. Structural checks are not
visual approval: review the PNGs.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
FAILURE = re.compile(r"CIRE_DRAFT\S*FAIL|Fatal error:|Assertion failed:")
EXPECTED = ["01_browse_all", "02_tank_hover_knight", "03_tank_selected_knight", "04_dps_selected_hybrid_wizard",
            "05_support_abilities_hover_keeper", "06_teammate_locked_dryad_lore", "07_search_golem", "08_timer_low_behemoth",
            "09_locked_in_knight"]
# 16:9 (1080p, launcher default, small window), Eric's 1755x1336 window, 21:9 ultrawide, 4:3.
RESOLUTIONS = [(1920, 1080), (1600, 900), (1280, 720), (1755, 1336), (2560, 1080), (1440, 1080)]


# AutoSDK is off on this machine, so every editor boot otherwise runs "Build.bat -Mode=ValidatePlatforms"
# and blocks on Build.bat's machine-wide lock file while any other worktree compiles. Probes only target Win64.
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    """Kill the child's whole process tree so a Build.bat spawned by the editor cannot outlive it."""
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


def png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as stream:
        header = stream.read(24)
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n":
        return (0, 0)
    return struct.unpack(">II", header[16:24])


def expected_shots(args) -> list[str]:
    if args.champions:
        ids = [i for i in args.champions.split(",") if i]
        return [f"{2 * n + 1:02d}_selected_{i}" for n, i in enumerate(ids)] + [f"{2 * n + 2:02d}_abilities_{i}" for n, i in enumerate(ids)]
    return EXPECTED[:max(1, min(args.shots, len(EXPECTED)))]


def run_one(args, width: int, height: int, folder: Path) -> dict:
    tag = f"{width}x{height}"
    log = folder / f"gallery_{tag}.log"
    command = [str(args.editor), str(args.project.resolve()), "/Game/Maps/Citadel", "-game", "-CireDraftGallery",
               "-RenderOffscreen", "-ForceRes", f"-ResX={width}", f"-ResY={height}", "-windowed", "-unattended",
               "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-ExecCmds=t.MaxFPS 60",
               f"-CireDraftGalleryTag={tag}", f"-abslog={log}"]
    if args.champions:
        # new-champions: each listed champion selected (overview) and on its abilities tab instead of the fixed states.
        command.append(f"-CireDraftGalleryChampions={args.champions}")
    elif args.shots < len(EXPECTED):
        command.append(f"-CireDraftGalleryShots={args.shots}")
    if not args.mannequin:
        command.insert(5, "-CireTripoChampions")
    started = time.monotonic(); failure = ""
    with (folder / f"console_{tag}.log").open("wb") as stream:
        child = subprocess.Popen(command, cwd=args.project.resolve().parent, stdout=stream, stderr=subprocess.STDOUT,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), env=EDITOR_ENV)
        try:
            code = child.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            kill_tree(child)
            child.terminate()
            try:
                code = child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill(); code = child.wait(timeout=10)
            failure = f"Draft gallery exceeded {args.timeout} seconds"
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    errors = [line.strip() for line in text.splitlines() if FAILURE.search(line)]
    layout = [line.split("LogCireDraft: Display: ")[-1].strip() for line in text.splitlines() if "CIRE_DRAFT_LAYOUT_" in line]
    cutout = [line.split("LogCireDraft: Display: ")[-1].strip() for line in text.splitlines() if "CIRE_DRAFT_CUTOUT" in line]
    match = re.search(r"CIRE_DRAFT_GALLERY_PASS captures=(\d+) layout_failures=(\d+) directory=(.+)", text)
    captures = []
    if match:
        directory = Path(match.group(3).strip())
        for name in expected_shots(args):
            path = directory / (name + ".png")
            if not path.is_file():
                errors.append("Missing capture " + path.name); continue
            size = png_size(path)
            if size != (width, height) or path.stat().st_size < 50000:
                errors.append(f"Unexpected capture {path.name}: {size} {path.stat().st_size} bytes")
            report = directory / (name + ".layout.json")
            issues = []
            if report.is_file():
                raw = report.read_bytes()
                text = raw.decode("utf-16") if raw[:2] in (bytes([0xFF, 0xFE]), bytes([0xFE, 0xFF])) else raw.decode("utf-8-sig")
                issues = json.loads(text).get("issues", [])
            else:
                errors.append("Missing layout report " + report.name)
            captures.append(dict(path=str(path), width=size[0], height=size[1], layoutIssues=issues))
    passed = code == 0 and match is not None and not errors and not failure
    return dict(resolution=tag, passed=passed, exitCode=code, seconds=round(time.monotonic() - started, 2),
                failure=failure, errors=errors, layout=layout, cutout=cutout, log=str(log), captures=captures)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=ROOT / "CiresTeamSurvival.uproject")
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--mannequin", action="store_true", help="Omit -CireTripoChampions (fallback bodies)")
    parser.add_argument("--timeout", type=int, default=300, help="Seconds per resolution")
    parser.add_argument("--shots", type=int, default=len(EXPECTED), help="Capture only the first N states (iteration)")
    parser.add_argument("--champions", default="", help="Comma-separated profile ids: review these champions instead of the fixed states")
    parser.add_argument("--res", action="append", default=[], help="WIDTHxHEIGHT; repeat to pick resolutions (default: all six)")
    args = parser.parse_args()
    resolutions = [tuple(int(v) for v in r.lower().split("x")) for r in args.res] or RESOLUTIONS
    folder = args.project.resolve().parent / "Saved/DraftGalleryChecks" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    runs = []
    for (w, h) in resolutions:
        # A launch that never reaches the draft screen (engine start-up stalls while other
        # builds hold the UBT mutex, or an early crash) is retried; a layout failure is not.
        for attempt in range(3):
            run = run_one(args, w, h, folder)
            started = any("CIRE_DRAFT_GALLERY_SHOT" in line for line in
                          Path(run["log"]).read_text(encoding="utf-8", errors="replace").splitlines()) if Path(run["log"]).exists() else False
            run["attempt"] = attempt + 1
            if run["passed"] or started:
                break
        runs.append(run)
    passed = all(run["passed"] for run in runs)
    report = dict(passed=passed, runs=runs, visualReviewRequired=True)
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for run in runs:
        print(f"{run['resolution']:>10}  {'PASS' if run['passed'] else 'FAIL'}  {run['seconds']:>6}s  "
              f"captures={len(run['captures'])}  errors={len(run['errors'])}")
        for line in run["errors"][:6]:
            print("            ", line[:220])
        for line in run["cutout"][:1]:
            print("            ", line)
    print(f"report: {folder / 'report.json'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
