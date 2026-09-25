"""Render the level-up skill offer at 1920x1080 (offscreen) and verify the captures.

Launches the game with -CireSkillOfferGallery. The native fixture (CireSkillOfferHUD.cpp)
stages a normal offer with a hovered card (DPS+Support hybrid), an offer with
ultimates (tank), the final passive-only offer (support), the deferred reminder
and the pick animation, saving PNGs under Saved/SkillOfferGallery/<stamp>. Only
the process started here is stopped. Structural checks are not visual approval.
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
FAILURE = re.compile(r"CIRE_SKILL_OFFER\S*FAIL|Fatal error:|Assertion failed:")
EXPECTED = ["01_normal_offer_hover", "02_ultimate_offer", "03_passive_only_offer", "04_deferred_reminder", "05_pick_animation", "06_opening_offer_tank", "07_opening_offer_support"]


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=ROOT / "CiresTeamSurvival.uproject")
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--mannequin", action="store_true", help="Omit -CireTripoChampions (fallback bodies)")
    parser.add_argument("--timeout", type=int, default=260)
    args = parser.parse_args()
    root = args.project.resolve().parent
    folder = root / "Saved/SkillOfferGalleryChecks" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    log = folder / "gallery.log"
    command = [str(args.editor), str(args.project.resolve()), "/Game/Maps/Citadel", "-game", "-CireSkillOfferGallery",
               "-RenderOffscreen", "-ForceRes", "-ResX=1920", "-ResY=1080", "-unattended", "-nosplash", "-nosound",
               "-nop4", "-NoLiveCoding", "-ExecCmds=t.MaxFPS 60", f"-abslog={log}"]
    if not args.mannequin:
        command.insert(5, "-CireTripoChampions")
    started = time.monotonic(); failure = ""
    with (folder / "console.log").open("wb") as stream:
        child = subprocess.Popen(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT,
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
            failure = f"Skill offer gallery exceeded {args.timeout} seconds"
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    errors = [line for line in text.splitlines() if FAILURE.search(line)]
    match = re.search(r"CIRE_SKILL_OFFER_GALLERY_PASS captures=(\d+) directory=(.+)", text)
    captures = []
    if match:
        directory = Path(match.group(2).strip())
        for name in EXPECTED:
            path = directory / (name + ".png")
            if not path.is_file():
                errors.append("Missing capture " + path.name); continue
            size = png_size(path)
            if size != (1920, 1080) or path.stat().st_size < 50000:
                errors.append(f"Unexpected capture {path.name}: {size} {path.stat().st_size} bytes")
            captures.append(dict(path=str(path), width=size[0], height=size[1], bytes=path.stat().st_size))
    passed = code == 0 and match is not None and not errors and not failure
    report = dict(passed=passed, exitCode=code, seconds=round(time.monotonic() - started, 2), failure=failure,
                  errors=errors, log=str(log), captures=captures, visualReviewRequired=True)
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
