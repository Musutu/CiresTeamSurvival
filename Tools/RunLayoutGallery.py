"""Map layout editor gallery (dev-route-tools): boots the clean edit mode (-CireRouteEdit via -CireLayoutGallery),
drives the real editor through a scripted authoring session and captures seven views under Saved/LayoutGallery:
the map view with every setter, Replace on a pack, the walk view placing a pack, the vendor group, validation per
team, the preview walking both realms and the packs applied live. No data files are written. See Docs/MapLayout.md.

Only child processes created by this runner are terminated. Nothing is built or imported.

    python Tools/RunLayoutGallery.py [--timeout 600]
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
FAILURE = re.compile(r"CIRE_\S*(?:FAIL|ERROR)|Fatal error:|Assertion failed:|Ensure condition failed:")
EDITOR = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--timeout", type=int, default=600)
    args = parser.parse_args()
    folder = ROOT / "Saved/LayoutGalleryRuns" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True, exist_ok=False)
    log = folder / "gallery.log"
    command = [str(EDITOR), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-CireLayoutGallery",
               "-RenderOffscreen", "-ForceRes", "-ResX=1920", "-ResY=1080", "-windowed", "-ExecCmds=t.MaxFPS 60",
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-CireNoReplay", f"-abslog={log}"]
    started = time.monotonic()
    failure = ""
    with (folder / "gallery.console.log").open("wb") as output:
        child = subprocess.Popen(command, cwd=ROOT, stdout=output, stderr=subprocess.STDOUT,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0, env=EDITOR_ENV)
        try:
            code = child.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            kill_tree(child)
            code = -1
            failure = f"exceeded {args.timeout} s"
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    errors = [line for line in text.splitlines() if FAILURE.search(line)]
    evidence = [line.split("Display: ", 1)[-1] for line in text.splitlines() if "CIRE_LAYOUT_GALLERY" in line or "CIRE_ROUTE_EDIT_MODE" in line]
    captures = []
    match = re.search(r"CIRE_LAYOUT_GALLERY_DONE captures=\d+ dir=(.+)", text)
    if match:
        for path in sorted(Path(match.group(1).strip()).glob("*.png")):
            with path.open("rb") as stream:
                header = stream.read(24)
            size = struct.unpack(">II", header[16:24]) if header[:8] == b"\x89PNG\r\n\x1a\n" else (0, 0)
            captures.append(dict(path=str(path), width=size[0], height=size[1], bytes=path.stat().st_size))
    passed = code == 0 and not failure and not errors and len(captures) == 7 and all(c["bytes"] > 10000 for c in captures)
    report = dict(passed=passed, exitCode=code, failure=failure or None, seconds=round(time.monotonic() - started, 1),
                  log=str(log), errors=errors, evidence=evidence, captures=captures)
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"layout gallery: {'PASS' if passed else 'FAIL'} ({report['seconds']} s)")
    for line in evidence + errors:
        print("  " + line)
    for capture in captures:
        print(f"  {capture['path']} {capture['width']}x{capture['height']}")
    print(f"Report: {folder / 'report.json'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
