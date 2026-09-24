"""Render the WoW-style interface gallery (frames, tooltips, threat, SCT, level-up, UI scale).

Launches the game offscreen at 1920x1080 with -CireWowUIGallery, waits for the native
fixture (CireOptionsGallery.cpp, namespace CireWowUIGallery) and validates its PNGs.
Captures land in Saved/WowUIGallery/<utc>/; this report in Saved/WowUIGalleryChecks/<utc>/.
Visual review of the PNGs is still required; a pass only proves the native checks.
"""
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import struct
import subprocess
import time

STAGES = 15


def main():
    root = Path(__file__).resolve().parent.parent
    directory = root / "Saved/WowUIGalleryChecks" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    directory.mkdir(parents=True)
    log = directory / "gallery.log"
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(root / "CiresTeamSurvival.uproject"),
               "/Game/Maps/Citadel", "-game", "-CireWowUIGallery", "-RenderOffscreen", "-ForceRes",
               "-ResX=1920", "-ResY=1080", "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding",
               "-ExecCmds=t.MaxFPS 60", f"-abslog={log}"]
    failures, started = [], time.monotonic()
    with (directory / "console.log").open("wb") as output:
        child = subprocess.Popen(command, cwd=root, stdout=output, stderr=subprocess.STDOUT,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            code = child.wait(timeout=180)
        except subprocess.TimeoutExpired:
            child.terminate()
            try:
                code = child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                code = child.wait(timeout=5)
            failures.append("WoW UI gallery exceeded its 180-second process bound")
    contents = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    match = re.search(r"CIRE_WOWUI_GALLERY_PASS captures=(\d+) checks=(\d+) directory=(.+)", contents)
    failures += [line for line in contents.splitlines()
                 if re.search(r"CIRE_\S*(?:FAIL|ERROR|BLOCKER)|Fatal error:|Assertion failed:|Ensure condition failed:", line)]
    if code != 0 or not match:
        failures.append("Native WoW UI gallery did not finish successfully")
    captures = []
    if match:
        capture_dir = Path(match.group(3).strip())
        for path in sorted(capture_dir.glob("*.png")):
            with path.open("rb") as stream:
                header = stream.read(24)
            valid = len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n"
            size = struct.unpack(">II", header[16:24]) if valid else (0, 0)
            if size != (1920, 1080) or path.stat().st_size <= 10000:
                failures.append("Invalid rendered image: " + path.name)
            captures.append(str(path))
        if len(captures) != STAGES:
            failures.append(f"Expected {STAGES} captures, found {len(captures)}")
    report = dict(passed=not failures, exitCode=code, seconds=round(time.monotonic() - started, 2),
                  checks=int(match.group(2)) if match else 0, errors=failures, log=str(log),
                  captures=captures, visualReviewRequired=True)
    (directory / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
