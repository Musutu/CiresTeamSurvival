"""Capture the actual HUD tooltip at six scales/anchors in an isolated process."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import struct
import subprocess
import time

CASES = [("cursor_small_edge", 0, .6), ("cursor_large_edge", 0, 1.4),
         ("fixed_small_edge", 1, .6), ("fixed_large_edge", 1, 1.4),
         ("radial_default_edge", 2, .8), ("radial_large_edge", 2, 1.4)]


def validate_manifest(manifest, directory):
    failures = []
    captures = manifest.get("captures", [])
    if manifest.get("passed") is not True or len(captures) != len(CASES):
        failures.append("Native fixture did not pass all six captures")
    for row, (name, mode, scale) in zip(captures, CASES):
        if row.get("id") != name or row.get("mode") != mode or abs(row.get("scale", 0)-scale) > .001:
            failures.append("Capture order or mode/scale does not match " + name)
        rect, view = row.get("rect", {}), row.get("logicalViewport", {})
        if (rect.get("x", -1) < 3.5 or rect.get("y", -1) < 3.5 or rect.get("w", 0) <= 0
                or rect.get("h", 0) <= 0 or rect.get("x", 0)+rect.get("w", 0) > view.get("x", 0)-3.5
                or rect.get("y", 0)+rect.get("h", 0) > view.get("y", 0)-3.5):
            failures.append("Tooltip escapes logical viewport: " + name)
        if row.get("bodyLines", 0) < 3 or row.get("bodyFontSize", 0) <= 0:
            failures.append("Long description did not wrap: " + name)
        path = directory / (name + ".png")
        if not path.is_file():
            failures.append("Missing capture: " + name)
            continue
        with path.open("rb") as stream:
            header = stream.read(24)
        valid = len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n"
        size = struct.unpack(">II", header[16:24]) if valid else (0, 0)
        if size != (1920, 1080) or path.stat().st_size <= 10000:
            failures.append("Invalid rendered image: " + name)
        row["file"] = str(path)
    if len(captures) == len(CASES):
        for low, high in ((0, 1), (2, 3), (4, 5)):
            a, b = captures[low], captures[high]
            if (b.get("bodyFontSize", 0) <= a.get("bodyFontSize", 0)
                    or b.get("rect", {}).get("w", 0) <= a.get("rect", {}).get("w", 0)
                    or b.get("rect", {}).get("h", 0) <= a.get("rect", {}).get("h", 0)):
                failures.append("Higher scale did not increase font and rectangle: " + CASES[high][0])
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", action="store_true", help="Print the six cases without launching Unreal or writing files")
    args = parser.parse_args()
    if args.plan:
        print(json.dumps([dict(id=n, mode=m, scale=s) for n, m, s in CASES], indent=2))
        return 0
    root = Path(__file__).resolve().parent.parent
    directory = root / "Saved/TooltipGalleryChecks" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    directory.mkdir(parents=True)
    log = directory / "gallery.log"
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(root / "CiresTeamSurvival.uproject"),
               "/Game/Maps/Citadel", "-game", "-CireTooltipGallery", "-RenderOffscreen", "-ForceRes",
               "-ResX=1920", "-ResY=1080", "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding",
               "-ExecCmds=t.MaxFPS 60", f"-abslog={log}"]
    failures, started = [], time.monotonic()
    with (directory / "console.log").open("wb") as output:
        child = subprocess.Popen(command, cwd=root, stdout=output, stderr=subprocess.STDOUT,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            code = child.wait(timeout=90)
        except subprocess.TimeoutExpired:
            child.terminate()
            try:
                code = child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill(); code = child.wait(timeout=5)
            failures.append("Tooltip gallery exceeded its 90-second process bound")
    contents = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    match = re.search(r"CIRE_TOOLTIP_GALLERY_PASS captures=6 checks=(\d+) directory=(.+)", contents)
    failures += [line for line in contents.splitlines()
                 if re.search(r"CIRE_\S*(?:FAIL|ERROR|BLOCKER)|Fatal error:|Assertion failed:|Ensure condition failed:", line)]
    if code != 0 or not match:
        failures.append("Native tooltip gallery did not finish successfully")
    manifest = {}
    if match:
        capture_dir = Path(match.group(2).strip())
        try:
            manifest = json.loads((capture_dir / "manifest.json").read_text(encoding="utf-8-sig"))
            failures += validate_manifest(manifest, capture_dir)
        except (OSError, ValueError, TypeError, KeyError) as error:
            failures.append("Cannot validate native gallery: " + str(error))
    report = dict(passed=not failures, exitCode=code, seconds=round(time.monotonic()-started, 2),
                  checks=int(match.group(1)) if match else 0, errors=failures, log=str(log),
                  captures=manifest.get("captures", []), visualReviewAccepted=False, visualReviewRequired=True)
    (directory / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
