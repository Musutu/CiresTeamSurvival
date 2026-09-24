"""Render every themed arena: an overview, a gameplay-camera shot and a vista, 1920x1080 each.

Usage: python Tools/RunArenaGallery.py [--only sunlit_fields,black_shore] [--fallback]
Writes Saved/ArenaGallery/<stamp>/*.png and Saved/ArenaGalleryChecks/<stamp>/report.json
(frame time per shot included; the capture runs offscreen, so treat it as indicative).
"""
from datetime import datetime, timezone
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import time


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", default="")
    parser.add_argument("--fallback", action="store_true", help="also render the legacy fallback court")
    parser.add_argument("--verbose", action="store_true", help="log every resolved arena slot")
    parser.add_argument("--skip", default="", help="development: slot ids to leave out")
    parser.add_argument("--view", default="", help="development: extra close-up, arena-local 'x,y,z,tx,ty,tz'")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    folder = root / "Saved/ArenaGalleryChecks" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    log = folder / "gallery.log"
    flag = f"-CireArenaGallery={args.only.replace(',', '+')}" if args.only else "-CireArenaGallery"
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(root / "CiresTeamSurvival.uproject"),
               "/Game/Maps/Citadel", "-game", flag, "-RenderOffscreen", "-ForceRes", "-ResX=1920", "-ResY=1080",
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-CireNoReplay", "-ExecCmds=t.MaxFPS 60", f"-abslog={log}"]
    if args.fallback:
        command.append("-CireArenaGalleryFallback")
    if args.view:
        command.append("-CireArenaGalleryView=" + args.view.replace(",", "_"))
    if args.skip:
        command.append(f"-CireArenaSkip={args.skip}")
    if args.verbose:
        command.append("-LogCmds=LogCireArenas Verbose")
    started = time.monotonic()
    failure = ""
    with (folder / "console.log").open("wb") as output:
        child = subprocess.Popen(command, cwd=root, stdout=output, stderr=subprocess.STDOUT,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            code = child.wait(timeout=1200)
        except subprocess.TimeoutExpired:
            child.terminate()
            try:
                code = child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                code = child.wait(timeout=5)
            failure = "Arena gallery exceeded its 1200-second process bound"
    contents = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    match = re.search(r"CIRE_ARENA_GALLERY_PASS captures=(\d+) arenas=(\d+) directory=(.+)", contents)
    errors = [line for line in contents.splitlines() if re.search(r"CIRE_\S*(?:FAIL|ERROR)|Fatal error:|Assertion failed:|Ensure condition failed:|Failed to compile Material", line)]
    passed = code == 0 and match is not None and not failure and not errors
    shots = {m.group(2): float(m.group(3)) for m in re.finditer(r"CIRE_ARENA_GALLERY_SHOT arena=(\S+) file=(.+?) frame_ms=([\d.]+)", contents)}
    captures = []
    if match:
        for path in sorted(Path(match.group(3).strip()).glob("*.png")):
            with path.open("rb") as stream:
                header = stream.read(24)
            valid = len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n"
            size = struct.unpack(">II", header[16:24]) if valid else (0, 0)
            passed = passed and size == (1920, 1080) and path.stat().st_size > 10000
            captures.append(dict(path=str(path), width=size[0], height=size[1], bytes=path.stat().st_size, frameMs=shots.get(str(path))))
        passed = passed and len(captures) == int(match.group(1))
    report = dict(passed=bool(passed), exitCode=code, seconds=round(time.monotonic() - started, 2), failure=failure, errors=errors,
                  log=str(log), captures=captures)
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
