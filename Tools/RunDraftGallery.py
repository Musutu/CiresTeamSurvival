"""Render the champion draft screen at 1920x1080 (offscreen) and verify the captures.

Launches the game with -CireDraftGallery -CireTripoChampions. The native fixture
hovers a tank, a DPS and a support champion (plus a hybrid selection, a large
tank and a small spirit), waits for the live 3D preview to settle, and saves one
PNG per state under Saved/DraftGallery/<stamp>. This runner only stops the
process it starts. Structural checks are not visual approval: review the PNGs.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
FAILURE = re.compile(r"CIRE_DRAFT\S*FAIL|Fatal error:|Assertion failed:")
EXPECTED = ["01_tank_hover_knight", "02_dps_hover_ranger", "03_support_hover_keeper",
            "04_hybrid_select_wizard", "05_tank_hover_behemoth", "06_support_hover_whisp"]


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
    parser.add_argument("--shots", type=int, default=len(EXPECTED), help="Capture only the first N states (iteration)")
    args = parser.parse_args()
    root = args.project.resolve().parent
    folder = root / "Saved/DraftGalleryChecks" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    log = folder / "gallery.log"
    command = [str(args.editor), str(args.project.resolve()), "/Game/Maps/Citadel", "-game", "-CireDraftGallery",
               "-RenderOffscreen", "-ForceRes", "-ResX=1920", "-ResY=1080", "-unattended", "-nosplash", "-nosound",
               "-nop4", "-NoLiveCoding", "-ExecCmds=t.MaxFPS 60", f"-abslog={log}"]
    if args.shots < len(EXPECTED):
        command.append(f"-CireDraftGalleryShots={args.shots}")
    if not args.mannequin:
        command.insert(5, "-CireTripoChampions")
    started = time.monotonic(); failure = ""
    with (folder / "console.log").open("wb") as stream:
        child = subprocess.Popen(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            code = child.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            child.terminate()
            try:
                code = child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill(); code = child.wait(timeout=10)
            failure = f"Draft gallery exceeded {args.timeout} seconds"
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    errors = [line for line in text.splitlines() if FAILURE.search(line)]
    match = re.search(r"CIRE_DRAFT_GALLERY_PASS captures=(\d+) directory=(.+)", text)
    captures = []
    if match:
        directory = Path(match.group(2).strip())
        for name in EXPECTED[:max(1, min(args.shots, len(EXPECTED)))]:
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
