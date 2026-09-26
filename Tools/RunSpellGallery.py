"""Render the actual native spell models and exact ground boundaries offscreen.

Requires a successful current CiresTeamSurvivalEditor build. Stops only its own
bounded child, validates the explicit fixture marker plus PNG dimensions, and
writes a report. It does not modify a level or player save.
"""
from datetime import datetime, timezone
import argparse
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time


# AutoSDK is off on this machine, so every editor boot otherwise runs "Build.bat -Mode=ValidatePlatforms"
# and blocks on Build.bat's machine-wide lock file while any other worktree compiles. Probes only target Win64.
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    """Kill the child's whole process tree so a Build.bat spawned by the editor cannot outlive it."""
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--options", action="store_true", help="Render sixteen Options, combat-HUD, developer and roster views")
    parser.add_argument("--fab-ground", action="store_true",
                        help="telegraphs: Fab ground-effect overlays on circle zones: stock vendor look, fitted + dimmed, other candidates (measured)")
    parser.add_argument("--timeout", type=int, default=85, help="process bound in seconds (a cold asset registry after a rebuild can need more)")
    args = parser.parse_args()
    kind = "Options" if args.options else "Spell"
    expected = 16 if args.options else (3 if args.fab_ground else 7)
    root = Path(__file__).resolve().parent.parent
    folder = root / f"Saved/{kind}GalleryChecks" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    log, console = folder / "gallery.log", folder / "console.log"
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(root / "CiresTeamSurvival.uproject"),
               "/Game/Maps/Citadel", "-game", f"-Cire{kind}Gallery", "-RenderOffscreen", "-ForceRes",
               "-ResX=1920", "-ResY=1080", "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding",
               "-ExecCmds=t.MaxFPS 60", f"-abslog={log}"] + (["-CireFabGroundGallery"] if args.fab_ground else [])
    started = time.monotonic()
    failure = ""
    with console.open("wb") as output:
        child = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), env=EDITOR_ENV)
        try:
            code = child.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            kill_tree(child)
            child.terminate()
            try:
                code = child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                code = child.wait(timeout=5)
            failure = f"Gallery exceeded {args.timeout}-second bound"
    contents = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    match = re.search(rf"CIRE_{kind.upper()}_GALLERY_PASS captures={expected} directory=(.+)", contents)
    smoke_marker = "CIRE_OPTIONS_SETTINGS_PASS" if args.options else "CIRE_SPELL_PRESENTATION_PASS"
    errors = [line for line in contents.splitlines() if re.search(r"CIRE_\S*(?:FAIL|ERROR|BLOCKER)|Fatal error:|Assertion failed:|Ensure condition failed:", line)]
    passed = code == 0 and match is not None and smoke_marker in contents and not errors
    if args.options:
        passed = passed and "CIRE_DEVELOPER_RUNTIME_PASS" in contents
    captures = []
    if match:
        directory = Path(match.group(1).strip())
        for path in sorted(directory.glob("*.png")):
            with path.open("rb") as stream:
                header = stream.read(24)
            valid = header[:8] == b"\x89PNG\r\n\x1a\n" and len(header) == 24
            size = struct.unpack(">II", header[16:24]) if valid else (0, 0)
            passed = passed and size == (1920, 1080) and path.stat().st_size > 10000
            captures.append(dict(path=str(path), width=size[0], height=size[1], bytes=path.stat().st_size))
    passed = passed and len(captures) == expected
    report = dict(passed=bool(passed), exitCode=code, seconds=round(time.monotonic()-started, 2),
                  failure=failure, errors=errors, log=str(log), captures=captures, visualReviewAccepted=False)
    (folder / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
